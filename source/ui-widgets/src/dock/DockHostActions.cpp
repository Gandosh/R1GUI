// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: DockHost's editing commands (close, open, activate, float, lock, pin, collapse, tab
//   navigation, close groups, reset), the tab context menu, keyboard focus movement between regions
//   and panels, and the small intents the views report (tab activation, region press, strip
//   double click).
// Why: these are the operations the command layer binds to menu items and shortcuts; each one is a
//   single model call followed by the host's synchronisation, so a refused call changes nothing.
// Invariants: a command works on the model through DockLayout only; every successful command ends
//   in afterModelChanged (so views, contents, windows and the observer follow); lastError() carries
//   the reason of a refusal.
// Callers: the application and tests (public methods), DockTabStrip (intents), MenuController
//   (context menu items).
#include <algorithm>

#include "r1ui/core/events/TreeQueries.h"
#include "r1ui/widgets/dock/DockHost.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace layout = core::layout;
using core::tree::WidgetId;

namespace {

// The stack node a panel sits in.
const dock::Node* stackNodeOf(const dock::DockLayout& model, dock::PanelId panel) {
  const std::optional<dock::PanelSlot> slot = model.locate(panel);
  if (!slot) return nullptr;
  for (const dock::Area& area : model.areas()) {
    if (area.id != slot->area || !area.root) continue;
    const dock::Node* node = &*area.root;
    for (uint32_t index : slot->path) node = &node->children[index];
    return node;
  }
  return nullptr;
}

const dock::Node* nodeAtPath(const dock::DockLayout& model, uint32_t areaId, const dock::Path& path) {
  for (const dock::Area& area : model.areas()) {
    if (area.id != areaId || !area.root) continue;
    const dock::Node* node = &*area.root;
    for (uint32_t index : path) {
      if (node->kind != dock::Node::Kind::Split || index >= node->children.size()) return nullptr;
      node = &node->children[index];
    }
    return node;
  }
  return nullptr;
}

// First focusable widget in document order below (and including) `root`.
WidgetId firstFocusable(UiContext& ui, WidgetId root) {
  std::vector<WidgetId> pending{root};
  while (!pending.empty()) {
    const WidgetId w = pending.back();
    pending.pop_back();
    if (core::events::isFocusable(ui.tree(), w)) return w;
    std::vector<WidgetId> children;
    for (WidgetId c = ui.tree().firstChild(w); c.valid(); c = ui.tree().nextSibling(c)) children.push_back(c);
    for (auto it = children.rbegin(); it != children.rend(); ++it) pending.push_back(*it);
  }
  return {};
}

}  // namespace

bool DockHost::commitOp(const dock::Status& status) {
  if (!status) {
    lastError_ = status.error;
    return false;
  }
  return true;
}

// ---- tab intents ----------------------------------------------------------------------------

void DockHost::tabActivated(dock::PanelId panel) { activatePanel(panel); }

bool DockHost::activatePanel(dock::PanelId panel) {
  const std::optional<dock::PanelSlot> slot = layout_->locate(panel);
  if (!slot) return false;
  const bool wasFront = slot->front;
  const bool wasActive = activePanel_ == panel;
  if (!wasFront && !commitOp(layout_->activateTab(panel))) return false;
  setActivePanel(panel);
  if (!wasFront) {
    afterModelChanged(DockChange::Active);
  } else if (!wasActive) {
    notify(DockChange::Active);
  }
  return true;
}

void DockHost::regionPressed(dock::PanelId frontPanel) {
  if (frontPanel != 0 && frontPanel != activePanel_) {
    setActivePanel(frontPanel);
    notify(DockChange::Active);
  }
}

void DockHost::stripBackgroundDoubleClick(DockTabStrip& strip) {
  const FloatId window = strip.window();
  if (window == kMainWindow || !backend_.describe().maximize) return;
  const AreaRec* rec = areaRecForWindow(window);
  if (rec == nullptr) return;
  backend_.setMaximized(window, !backend_.isMaximized(window));
  if (const std::optional<dock::Rect> rect = backend_.contentRect(window)) layout_->setAreaRect(rec->area, *rect);
  afterModelChanged(DockChange::Window, true);
}

// ---- commands -------------------------------------------------------------------------------

bool DockHost::closePanel(dock::PanelId panel) {
  const dock::Node* stack = stackNodeOf(*layout_, panel);
  if (stack == nullptr) return false;
  const std::vector<dock::PanelId> sameStack = stack->tabs;
  if (!commitOp(layout_->closePanel(panel))) return false;
  if (activePanel_ == panel) {
    for (dock::PanelId other : sameStack) {
      if (other != panel && layout_->isDocked(other)) {
        setActivePanel(frontOfStackHolding(other));
        break;
      }
    }
  }
  afterModelChanged(DockChange::Arrangement);
  return true;
}

bool DockHost::openPanel(dock::PanelId panel) {
  dock::OpenOptions options;
  options.floatBounds = mainRect();
  options.floatWhenUnplaced = options_.floatWhenUnplaced;
  const dock::OpenOutcome out = layout_->openPanel(panel, options);
  if (!commitOp(out.status)) return false;
  setActivePanel(panel);
  afterModelChanged(out.alreadyOpen ? DockChange::Active : DockChange::Arrangement);
  return true;
}

bool DockHost::closeActiveTab() { return activePanel_ != 0 && closePanel(activePanel_); }

bool DockHost::nextTab() {
  const dock::Node* stack = stackNodeOf(*layout_, activePanel_);
  if (stack == nullptr || stack->tabs.size() < 2) return false;
  const auto at = std::find(stack->tabs.begin(), stack->tabs.end(), activePanel_) - stack->tabs.begin();
  return activatePanel(stack->tabs[(static_cast<size_t>(at) + 1) % stack->tabs.size()]);
}

bool DockHost::previousTab() {
  const dock::Node* stack = stackNodeOf(*layout_, activePanel_);
  if (stack == nullptr || stack->tabs.size() < 2) return false;
  const size_t at = static_cast<size_t>(std::find(stack->tabs.begin(), stack->tabs.end(), activePanel_) - stack->tabs.begin());
  return activatePanel(stack->tabs[(at + stack->tabs.size() - 1) % stack->tabs.size()]);
}

dock::Rect DockHost::contentRectForFloat(const dock::Rect& ghost) const {
  const FrameInsets frame = backend_.describe().frame;
  return {ghost.x + frame.left, ghost.y + frame.top, ghost.w, ghost.h};
}

dock::DropZone DockHost::floatZoneNear(dock::PanelId panel) const {
  dock::DragQuery query;
  query.panel = panel;
  query.pointer = {24.0, 24.0};
  for (const dock::StackLayout& s : result_.stacks) {
    for (const dock::TabLayout& t : s.tabs) {
      if (t.panel == panel) query.pointer = {s.bounds.x + 24.0, s.bounds.y + 24.0};
    }
  }
  dock::DropZone zone = layout_->floatZone(result_, query);
  zone.preview = contentRectForFloat(zone.preview);
  return zone;
}

bool DockHost::floatPanel(dock::PanelId panel) {
  if (!layout_->isDocked(panel)) return false;
  if (!commitOp(layout_->dock(panel, floatZoneNear(panel)))) return false;
  setActivePanel(panel);
  afterModelChanged(DockChange::Arrangement);
  return true;
}

bool DockHost::floatActiveTab() { return activePanel_ != 0 && floatPanel(activePanel_); }

bool DockHost::moveStackToNewWindow(dock::PanelId panel) {
  const dock::Node* stack = stackNodeOf(*layout_, panel);
  if (stack == nullptr) return false;
  const std::vector<dock::PanelId> tabs = stack->tabs;
  dock::PanelId seed = 0;
  for (dock::PanelId t : tabs) {
    const dock::PanelInfo* info = layout_->panel(t);
    if (info != nullptr && !info->locked && info->canFloat) {
      seed = t;
      break;
    }
  }
  if (seed == 0) {
    lastError_ = "none of the tabs can leave its region";
    return false;
  }
  if (!commitOp(layout_->dock(seed, floatZoneNear(seed)))) return false;
  for (dock::PanelId t : tabs) {
    if (t == seed) continue;
    const dock::PanelInfo* info = layout_->panel(t);
    const std::optional<dock::PanelSlot> target = layout_->locate(seed);
    if (info == nullptr || info->locked || !target) continue;  // locked tabs stay where they are
    dock::DropZone zone;
    zone.kind = dock::DropKind::JoinStack;
    zone.area = target->area;
    zone.stackPanel = seed;
    zone.index = target->tabCount;
    layout_->dock(t, zone);
  }
  setActivePanel(panel);
  afterModelChanged(DockChange::Arrangement);
  return true;
}

// Closes `victims` one by one, skipping any that refuse and continuing past them (rule 40), then
// synchronises once. `survivor` becomes the active panel when the active one went.
size_t DockHost::closeGroup(const std::vector<dock::PanelId>& victims, dock::PanelId survivor) {
  size_t closed = 0;
  for (dock::PanelId t : victims) {
    const dock::PanelInfo* info = layout_->panel(t);
    const bool kindOk = options_.closeGroupsAnyKind ||
                        (info != nullptr && (info->kind == dock::PanelKind::Document || info->kind == dock::PanelKind::ApplicationPage));
    if (kindOk && layout_->closePanel(t)) ++closed;
  }
  if (closed > 0) {
    if (!layout_->isDocked(activePanel_)) setActivePanel(survivor);
    afterModelChanged(DockChange::Arrangement);
  }
  return closed;
}

size_t DockHost::closeOthers(dock::PanelId keep) {
  const dock::Node* stack = stackNodeOf(*layout_, keep);
  if (stack == nullptr) return 0;
  std::vector<dock::PanelId> victims;
  for (dock::PanelId t : stack->tabs) {
    if (t != keep) victims.push_back(t);
  }
  return closeGroup(victims, keep);
}

size_t DockHost::closeToLeft(dock::PanelId of) {
  const dock::Node* stack = stackNodeOf(*layout_, of);
  if (stack == nullptr) return 0;
  const auto at = static_cast<size_t>(std::find(stack->tabs.begin(), stack->tabs.end(), of) - stack->tabs.begin());
  return closeGroup(std::vector<dock::PanelId>(stack->tabs.begin(), stack->tabs.begin() + static_cast<std::ptrdiff_t>(at)), of);
}

size_t DockHost::closeToRight(dock::PanelId of) {
  const dock::Node* stack = stackNodeOf(*layout_, of);
  if (stack == nullptr) return 0;
  const auto at = static_cast<size_t>(std::find(stack->tabs.begin(), stack->tabs.end(), of) - stack->tabs.begin());
  return closeGroup(std::vector<dock::PanelId>(stack->tabs.begin() + static_cast<std::ptrdiff_t>(at) + 1, stack->tabs.end()), of);
}

bool DockHost::setPanelLocked(dock::PanelId panel, bool locked) {
  if (!commitOp(layout_->setPanelLocked(panel, locked))) return false;
  relayout();
  notify(DockChange::Lock);
  return true;
}

bool DockHost::togglePinned(dock::PanelId panel) {
  const std::optional<dock::PanelSlot> slot = layout_->locate(panel);
  if (!slot) return false;
  const dock::Node* node = nodeAtPath(*layout_, slot->area, slot->path);
  if (node == nullptr || !commitOp(layout_->setPinned(slot->area, slot->path, !node->pinned, mainRect()))) return false;
  afterModelChanged(DockChange::Arrangement);
  return true;
}

bool DockHost::toggleCollapsed(dock::PanelId panel) {
  const std::optional<dock::PanelSlot> slot = layout_->locate(panel);
  if (!slot) return false;
  const dock::Node* node = nodeAtPath(*layout_, slot->area, slot->path);
  if (node == nullptr || !commitOp(layout_->setCollapsed(slot->area, slot->path, !node->collapsed))) return false;
  afterModelChanged(DockChange::Arrangement);
  return true;
}

bool DockHost::resetLayout() {
  if (!defaultLayout_) {
    lastError_ = "no default layout was supplied";
    return false;
  }
  if (!commitOp(setLayout(defaultLayout_()))) return false;
  notify(DockChange::Arrangement);
  return true;
}

// ---- keyboard focus ---------------------------------------------------------------------------

void DockHost::focusPanelOfStrip(DockTabStrip& strip) {
  const dock::PanelId panel = strip.frontPanel();
  const WidgetId content = contentOf(panel);
  if (!content.valid()) return;
  const WidgetId target = firstFocusable(strip.ui(), content);
  if (target.valid()) strip.ui().focusWidget(target, core::events::FocusReason::Keyboard);
}

bool DockHost::focusActivePanel() {
  const WidgetId content = contentOf(activePanel_);
  if (!content.valid()) return false;
  const auto it = contents_.find(activePanel_);
  if (it == contents_.end() || it->second.ui == nullptr) return false;
  const WidgetId target = firstFocusable(*it->second.ui, content);
  return target.valid() && it->second.ui->focusWidget(target, core::events::FocusReason::Keyboard);
}

bool DockHost::focusNextRegion(bool backwards) {
  struct Candidate {
    UiContext* ui;
    WidgetId strip;
    dock::PanelId front;
  };
  std::vector<Candidate> order;
  for (const dock::Area& area : layout_->areas()) {
    const AreaRec* rec = areaRecFor(area.id);
    DockAreaView* view = rec != nullptr && rec->ui != nullptr ? rec->ui->objectAs<DockAreaView>(rec->view) : nullptr;
    if (view == nullptr) continue;
    for (const DockAreaView::Unit& u : view->units()) {
      if (DockTabStrip* strip = view->stripOf(u)) {
        if (strip->tabCount() > 0) order.push_back({rec->ui, u.strip, strip->frontPanel()});
      }
    }
  }
  if (order.empty()) return false;
  size_t current = order.size();
  for (size_t i = 0; i < order.size(); ++i) {
    if (order[i].front == activePanel_) current = i;
  }
  const size_t next = current == order.size() ? 0 : (backwards ? (current + order.size() - 1) % order.size() : (current + 1) % order.size());
  const Candidate target = order[next];
  activatePanel(target.front);
  return target.ui->focusWidget(target.strip, core::events::FocusReason::Keyboard);
}

// ---- context menu -----------------------------------------------------------------------------

MenuController& DockHost::menuFor(UiContext& context) {
  for (auto& entry : menus_) {
    if (entry.first == &context) return *entry.second;
  }
  menus_.emplace_back(&context, std::make_unique<MenuController>(context));
  return *menus_.back().second;
}

void DockHost::closeMenus() {
  for (auto& entry : menus_) entry.second->close();
}

void DockHost::tabContextMenu(DockTabStrip& strip, dock::PanelId panel, double x, double y) {
  if (panel == 0) panel = strip.frontPanel();
  const dock::PanelInfo* info = layout_->panel(panel);
  const dock::Node* stack = stackNodeOf(*layout_, panel);
  const std::optional<dock::PanelSlot> slot = layout_->locate(panel);
  if (info == nullptr || stack == nullptr || !slot) return;

  const auto closable = [&](dock::PanelId t) {
    const dock::PanelInfo* p = layout_->panel(t);
    if (p == nullptr || !p->canClose || p->locked) return false;
    return options_.closeGroupsAnyKind || p->kind == dock::PanelKind::Document || p->kind == dock::PanelKind::ApplicationPage;
  };
  const size_t at = static_cast<size_t>(std::find(stack->tabs.begin(), stack->tabs.end(), panel) - stack->tabs.begin());
  bool others = false, left = false, right = false;
  for (size_t i = 0; i < stack->tabs.size(); ++i) {
    if (i == at || !closable(stack->tabs[i])) continue;
    others = true;
    (i < at ? left : right) = true;
  }
  const dock::Node* node = nodeAtPath(*layout_, slot->area, slot->path);
  const bool nested = !slot->path.empty();
  const bool canLeave = info->canFloat && !info->locked;

  MenuSpec spec;
  const auto add = [&](std::string id, std::string label, bool enabled, std::string shortcut = {}) {
    MenuItemSpec item = menuAction(std::move(id), std::move(label), std::move(shortcut));
    item.enabled = enabled;
    spec.items.push_back(std::move(item));
  };
  add("close", "Close", info->canClose && !info->locked, "Ctrl+W");
  add("closeOthers", "Close Others", others && stack->tabs.size() >= 2);
  add("closeLeft", "Close to the Left", left);
  add("closeRight", "Close to the Right", right);
  spec.items.push_back(menuSeparator());
  add("float", "Float", canLeave);
  add("newWindow", "Move to New Window", canLeave);
  spec.items.push_back(menuSeparator());
  add("lock", info->locked ? "Unlock Tab" : "Lock Tab", true);
  add("pin", node != nullptr && node->pinned ? "Unpin Region" : "Pin Region", nested);
  add("collapse", node != nullptr && node->collapsed ? "Expand Region" : "Collapse Region", nested);

  const std::shared_ptr<bool> alive = alive_;
  spec.onCommand = [this, alive, panel](const MenuItemSpec& item) {
    if (!*alive) return;
    if (item.id == "close") closePanel(panel);
    else if (item.id == "closeOthers") closeOthers(panel);
    else if (item.id == "closeLeft") closeToLeft(panel);
    else if (item.id == "closeRight") closeToRight(panel);
    else if (item.id == "float") floatPanel(panel);
    else if (item.id == "newWindow") moveStackToNewWindow(panel);
    else if (item.id == "lock") {
      const dock::PanelInfo* p = layout_->panel(panel);
      if (p != nullptr) setPanelLocked(panel, !p->locked);
    } else if (item.id == "pin") togglePinned(panel);
    else if (item.id == "collapse") toggleCollapsed(panel);
  };
  menuFor(strip.ui()).openContextMenu(std::move(spec), x, y);
}

}  // namespace r1ui::widgets

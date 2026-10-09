// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: DockHost's life cycle and model flow: construction, attach/detach, replacing the layout,
//   keeping area views, floating windows, panel contents and the active panel in step with the
//   model after every change, and the floating backend listener.
// Invariants: the model has this one writer; after every model change the order is: create
//   missing windows and views, lay out and mount contents, destroy contents of closed panels,
//   destroy obsolete windows (so no content is destroyed with a window that still holds an open
//   panel), enforce the stacking order, then notify. Content records never point into a destroyed
//   context: windows are only destroyed after their records are dropped or their contents parked.
//   Callbacks into the application (factory, observer) are made with no iterator or reference held.
// Callers: UiContext (lifecycle, layout), the application, DockHost{Actions,Drag}.cpp.
#include "r1ui/widgets/dock/DockHost.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace layout = core::layout;
using core::tree::WidgetId;

namespace {

// Parking place for panel contents while their body is rebuilt: never shown, never laid out.
class DockHolding : public WidgetObject {
 public:
  const char* typeName() const override { return "DockHolding"; }
  void onAttached() override {
    style().position = layout::Position::Absolute;
    style().display = layout::Display::None;
  }
};

bool sameRect(const dock::Rect& a, const dock::Rect& b) { return a == b; }

// The stack nodes of a subtree in layout order (iterative, so depth does not matter).
std::vector<const dock::Node*> stacksOf(const dock::Node& root) {
  std::vector<const dock::Node*> stacks;
  std::vector<const dock::Node*> pending{&root};
  while (!pending.empty()) {
    const dock::Node* n = pending.back();
    pending.pop_back();
    if (n->kind == dock::Node::Kind::Stack) {
      stacks.push_back(n);
    } else {
      for (auto it = n->children.rbegin(); it != n->children.rend(); ++it) pending.push_back(&*it);
    }
  }
  return stacks;
}

std::vector<dock::PanelId> panelsOfArea(const dock::DockLayout& layout, uint32_t area) {
  std::vector<dock::PanelId> panels;
  for (const dock::Area& a : layout.areas()) {
    if (a.id != area || !a.root) continue;
    for (const dock::Node* stack : stacksOf(*a.root)) panels.insert(panels.end(), stack->tabs.begin(), stack->tabs.end());
  }
  return panels;
}

// The front tab of the first stack of the first non-empty area (main first), or 0.
dock::PanelId firstFront(const dock::DockLayout& layout) {
  for (const dock::Area& a : layout.areas()) {
    if (!a.root) continue;
    const dock::Node* stack = stacksOf(*a.root).front();
    return stack->tabs[stack->active];
  }
  return 0;
}

// Closes a panel even if it is locked (its window is gone or cannot be shown) and keeps its lock flag.
void closeIgnoringLock(dock::DockLayout& layout, dock::PanelId panel) {
  const dock::PanelInfo* info = layout.panel(panel);
  if (info == nullptr) return;
  const bool wasLocked = info->locked;
  layout.setPanelLocked(panel, false);
  layout.closePanel(panel);
  layout.setPanelLocked(panel, wasLocked);
}

}  // namespace

// ---- construction ---------------------------------------------------------------------------

DockHost::DockHost(PanelRegistry& registry, IFloatingBackend& backend, DockHostOptions options)
    : registry_(registry), backend_(backend), options_(std::move(options)) {
  dock::DockLayoutResult created = dock::DockLayout::create(registry_.infos(), options_.config);
  if (!created.ok()) {
    lastError_ = "invalid dock configuration: " + created.error;
    options_.config = {};
    created = dock::DockLayout::create(registry_.infos(), options_.config);
  }
  if (!created.ok()) created = dock::DockLayout::create({}, options_.config);
  layout_ = std::move(*created.layout);
  registryRevision_ = registry_.revision();
}

DockHost::~DockHost() { *alive_ = false; }

void DockHost::onAttached() {
  layout::Style& s = style();
  s.flexGrow = 1.0;
  s.flexShrink = 1.0;
  s.minWidth = layout::Length::px(0);
  s.minHeight = layout::Length::px(0);
  node().flags.clipsChildren = false;
  setWantsLayoutCallback(true);
  holding_ = ui().create<DockHolding>(id()).id();
  backend_.setListener(this);
  backend_.setMainContent(ui(), id());
  detached_ = false;
  createWindowsAndViews(false);
  relayout();
}

void DockHost::onDetached() {
  detached_ = true;
  *alive_ = false;
  cancelHoverTimer();
  if (drag_.active) endDragState();
  closeMenus();
  teardownViews();
  backend_.setListener(nullptr);
  backend_.setMainContent(ui(), {});
}

void DockHost::teardownViews() {
  for (const AreaRec& rec : areas_) {
    if (rec.window != kMainWindow) backend_.destroyWindow(rec.window);
  }
  for (const FloatId w : obsolete_) backend_.destroyWindow(w);
  obsolete_.clear();
  contents_.clear();
  areas_.clear();
}

void DockHost::onLayout() {
  const dock::Rect now = mainRect();
  if (sameRect(now, lastMain_)) return;
  lastMain_ = now;
  relayout();
  captureWindowStates();
}

dock::Rect DockHost::mainRect() const { return backend_.mainContentRect(); }

// ---- helpers --------------------------------------------------------------------------------

DockHost::AreaRec* DockHost::areaRecFor(uint32_t area) {
  for (AreaRec& rec : areas_) {
    if (rec.area == area) return &rec;
  }
  return nullptr;
}

const DockHost::AreaRec* DockHost::areaRecForWindow(FloatId window) const {
  for (const AreaRec& rec : areas_) {
    if (rec.window == window) return &rec;
  }
  return nullptr;
}

DockAreaView* DockHost::areaView(uint32_t area) const {
  for (const AreaRec& rec : areas_) {
    if (rec.area == area && rec.ui != nullptr) return rec.ui->objectAs<DockAreaView>(rec.view);
  }
  return nullptr;
}

std::optional<FloatId> DockHost::windowOfArea(uint32_t area) const {
  for (const AreaRec& rec : areas_) {
    if (rec.area == area) return rec.window;
  }
  return std::nullopt;
}

DockTabStrip* DockHost::stripOf(dock::PanelId panel) const {
  for (const AreaRec& rec : areas_) {
    DockAreaView* view = rec.ui != nullptr ? rec.ui->objectAs<DockAreaView>(rec.view) : nullptr;
    if (view == nullptr) continue;
    if (const DockAreaView::Unit* u = view->unitHolding(panel)) return view->stripOf(*u);
  }
  return nullptr;
}

WidgetId DockHost::contentOf(dock::PanelId panel) const {
  const auto it = contents_.find(panel);
  return it == contents_.end() ? WidgetId{} : it->second.widget;
}

void DockHost::notify(DockChange kind) {
  if (!onChanged_) return;
  const auto callback = onChanged_;
  callback(kind);
}

DockTabInfo DockHost::tabInfo(dock::PanelId panel) const {
  DockTabInfo info;
  info.panel = panel;
  if (const PanelDescriptor* d = registry_.find(panel)) {
    info.title = d->title;
    info.icon = d->icon;
  }
  if (const dock::PanelInfo* p = layout_->panel(panel)) {
    info.locked = p->locked;
    info.closable = p->canClose && !p->locked;
  }
  return info;
}

void DockHost::setActivePanel(dock::PanelId panel) {
  if (activePanel_ == panel) return;
  activePanel_ = panel;
  for (const AreaRec& rec : areas_) {
    if (DockAreaView* view = rec.ui != nullptr ? rec.ui->objectAs<DockAreaView>(rec.view) : nullptr) {
      for (const DockAreaView::Unit& u : view->units()) {
        if (DockTabStrip* strip = view->stripOf(u)) strip->requestPaint();
      }
    }
  }
}

dock::PanelId DockHost::frontOfStackHolding(dock::PanelId panel) const {
  const std::optional<dock::PanelSlot> slot = layout_->locate(panel);
  if (!slot) return 0;
  const dock::Area* area = nullptr;
  for (const dock::Area& a : layout_->areas()) {
    if (a.id == slot->area) area = &a;
  }
  if (area == nullptr || !area->root) return 0;
  const dock::Node* node = &*area->root;
  for (uint32_t index : slot->path) node = &node->children[index];
  return node->tabs.empty() ? 0 : node->tabs[node->active];
}

// ---- model flow -----------------------------------------------------------------------------

void DockHost::captureWindowStates() {
  if (!layout_) return;
  const dock::Rect main = mainRect();
  std::vector<std::pair<uint32_t, dock::WindowState>> updates;
  for (const dock::Area& area : layout_->areas()) {
    dock::WindowState state = area.window;
    if (area.id == dock::kMainAreaId) {
      if (main.w > 0.0 && main.h > 0.0) {
        state.hasRect = true;
        state.rect = main;
      }
    } else if (const AreaRec* rec = areaRecFor(area.id); rec != nullptr) {
      if (const std::optional<FloatContent> content = backend_.content(rec->window)) state.dpiScale = content->scale;
      state.maximized = backend_.isMaximized(rec->window);
      state.monitor = backend_.monitorAt({area.rect.x + area.rect.w / 2.0, area.rect.y + area.rect.h / 2.0});
    }
    if (!(state == area.window)) updates.emplace_back(area.id, state);
  }
  for (const auto& [id, state] : updates) layout_->setWindowState(id, state);
}

void DockHost::createWindowsAndViews(bool fromBackend) {
  std::vector<uint32_t> failed;
  for (const dock::Area& area : layout_->areas()) {
    if (area.id == dock::kMainAreaId) {
      if (areaRecFor(dock::kMainAreaId) == nullptr) {
        AreaRec rec;
        rec.area = dock::kMainAreaId;
        rec.window = kMainWindow;
        rec.ui = &ui();
        rec.view = ui().create<DockAreaView>(id(), static_cast<IDockInteraction&>(*this), dock::kMainAreaId, kMainWindow).id();
        areas_.push_back(rec);
      }
      continue;
    }
    AreaRec* rec = areaRecFor(area.id);
    if (rec == nullptr) {
      FloatRequest request;
      for (const dock::Node* stack : stacksOf(*area.root)) {
        for (dock::PanelId p : stack->tabs) request.panels.push_back(p);
        if (request.active == 0) request.active = stack->tabs[stack->active];
      }
      request.contentRect = area.rect;
      request.minContentSize = {options_.config.minFloatSize, options_.config.minFloatSize};
      for (dock::PanelId p : request.panels) {
        if (const PanelDescriptor* d = registry_.find(p)) {
          request.minContentSize.x = std::max(request.minContentSize.x, d->minSize.x);
          request.minContentSize.y = std::max(request.minContentSize.y, d->minSize.y);
        }
      }
      if (const PanelDescriptor* d = registry_.find(request.active)) request.title = d->title;
      const FloatCreateResult created = backend_.createWindow(request);
      const std::optional<FloatContent> content = created.ok ? backend_.content(created.id) : std::nullopt;
      if (!created.ok || !content || !content->valid()) {
        lastError_ = created.ok ? "the floating window has no content area" : created.error;
        if (created.ok) backend_.destroyWindow(created.id);
        failed.push_back(area.id);
        continue;
      }
      AreaRec fresh;
      fresh.area = area.id;
      fresh.window = created.id;
      fresh.ui = content->ui;
      fresh.view = content->ui->create<DockAreaView>(content->parent, static_cast<IDockInteraction&>(*this), area.id, created.id).id();
      areas_.push_back(fresh);
      rec = &areas_.back();
      if (const std::optional<dock::Rect> actual = backend_.contentRect(created.id); actual && !(*actual == area.rect)) {
        layout_->setAreaRect(area.id, *actual);  // the backend limited it; the model follows
      }
    } else if (!fromBackend) {
      const std::optional<dock::Rect> current = backend_.contentRect(rec->window);
      if (!current || !(*current == area.rect)) {
        backend_.setContentRect(rec->window, area.rect);
        if (const std::optional<dock::Rect> actual = backend_.contentRect(rec->window); actual && !(*actual == area.rect)) {
          layout_->setAreaRect(area.id, *actual);
        }
      }
    }
  }
  // Windows of areas that no longer exist are destroyed after the contents moved out (see header).
  for (auto it = areas_.begin(); it != areas_.end();) {
    bool exists = false;
    for (const dock::Area& area : layout_->areas()) exists = exists || area.id == it->area;
    if (exists) {
      ++it;
    } else {
      if (it->window != kMainWindow) obsolete_.push_back(it->window);
      for (auto c = contents_.begin(); c != contents_.end();) {
        if (c->second.ui == it->ui && it->ui != &ui()) {
          c = contents_.erase(c);  // lives in the window's own context and goes with it
        } else {
          ++c;
        }
      }
      it = areas_.erase(it);
    }
  }
  if (!failed.empty()) {
    // The backend could not show these windows: fold their panels back into closed memory.
    for (uint32_t area : failed) {
      for (dock::PanelId p : panelsOfArea(*layout_, area)) closeIgnoringLock(*layout_, p);
    }
  }
}

void DockHost::destroyObsoleteWindows() {
  std::vector<FloatId> windows;
  windows.swap(obsolete_);
  for (FloatId w : windows) backend_.destroyWindow(w);
}

void DockHost::enforceStacking() {
  std::vector<FloatId> want;
  for (const dock::Area& area : layout_->areas()) {
    if (area.id == dock::kMainAreaId) continue;
    if (const AreaRec* rec = areaRecFor(area.id)) want.push_back(rec->window);
  }
  if (backend_.stacking() == want) return;
  for (FloatId w : want) backend_.bringToFront(w);
}

void DockHost::relayout() {
  if (inRelayout_ || detached_) return;
  inRelayout_ = true;
  result_ = layout_->computeLayout(mainRect());
  const std::vector<AreaRec> snapshot = areas_;
  for (const AreaRec& rec : snapshot) {
    if (DockAreaView* view = rec.ui != nullptr ? rec.ui->objectAs<DockAreaView>(rec.view) : nullptr) view->apply(result_);
  }
  inRelayout_ = false;
}

void DockHost::pruneContents() {
  for (auto it = contents_.begin(); it != contents_.end();) {
    if (layout_->isDocked(it->first)) {
      ++it;
      continue;
    }
    if (it->second.ui != nullptr && it->second.widget.valid() && it->second.ui->alive(it->second.widget)) it->second.ui->destroy(it->second.widget);
    it = contents_.erase(it);
  }
}

void DockHost::afterModelChanged(DockChange kind, bool fromBackend, bool notifyObserver) {
  const std::shared_ptr<bool> alive = alive_;
  createWindowsAndViews(fromBackend);
  relayout();
  pruneContents();
  destroyObsoleteWindows();
  enforceStacking();
  if (!layout_->isDocked(activePanel_)) {
    activePanel_ = firstFront(*layout_);
    for (const AreaRec& rec : areas_) {
      if (DockAreaView* view = rec.ui != nullptr ? rec.ui->objectAs<DockAreaView>(rec.view) : nullptr) view->requestPaint();
    }
  }
  if (kind == DockChange::Window || kind == DockChange::Arrangement) captureWindowStates();
  if (notifyObserver && *alive) notify(kind);
}

// ---- layout replacement ------------------------------------------------------------------------

dock::Status DockHost::setLayout(dock::DockLayout layout) {
  std::vector<dock::PanelId> known;
  for (const PanelDescriptor& d : registry_.all()) known.push_back(d.id);
  for (const dock::PanelInfo& p : layout.panels()) {
    if (std::find(known.begin(), known.end(), p.id) == known.end()) {
      return dock::Status::failure("the layout refers to panel " + std::to_string(p.id) + " which is not registered");
    }
  }
  if (known.size() != layout.panels().size()) return dock::Status::failure("the layout was built for a different set of panels");

  if (drag_.active) endDragState();
  if (resize_.active) resize_ = {};
  closeMenus();
  // Park every content that lives in the host's own context, then rebuild all floating windows.
  for (auto& [panel, rec] : contents_) {
    if (rec.ui == &ui() && rec.widget.valid() && ui().alive(rec.widget)) ui().reparent(rec.widget, holding_);
  }
  for (auto it = areas_.begin(); it != areas_.end();) {
    if (it->window == kMainWindow) {
      ++it;
      continue;
    }
    obsolete_.push_back(it->window);
    for (auto c = contents_.begin(); c != contents_.end();) {
      c = (c->second.ui == it->ui && it->ui != &ui()) ? contents_.erase(c) : std::next(c);
    }
    it = areas_.erase(it);
  }
  layout_ = std::move(layout);
  registryRevision_ = registry_.revision();
  afterModelChanged(DockChange::Arrangement, false, false);
  return dock::Status::success();
}

dock::Status DockHost::refreshPanels() {
  const std::string text = layout_->toJson();
  dock::LoadResult loaded = dock::DockLayout::fromJson(text, registry_.infos(), options_.config);
  if (!loaded.ok()) {
    // A layout with nothing docked cannot be re-read (spec 04 rule 14); rebuild an empty one.
    dock::DockLayoutResult fresh = dock::DockLayout::create(registry_.infos(), options_.config);
    if (!fresh.ok()) return dock::Status::failure(fresh.error);
    return setLayout(std::move(*fresh.layout));
  }
  return setLayout(std::move(*loaded.layout));
}

void DockHost::setMainWindowState(const dock::WindowState& state) {
  dock::WindowState merged = state;
  const dock::Rect main = mainRect();
  if (main.w > 0.0 && main.h > 0.0) {
    merged.hasRect = true;
    merged.rect = main;
  }
  layout_->setWindowState(dock::kMainAreaId, merged);
}

// ---- contents -------------------------------------------------------------------------------

void DockHost::mountContent(dock::PanelId panel, UiContext& context, WidgetId body, bool visible) {
  auto it = contents_.find(panel);
  if (it != contents_.end() && it->second.widget.valid() && !it->second.ui->alive(it->second.widget)) {
    contents_.erase(it);  // lost with a body or a window
    it = contents_.end();
  }
  const PanelDescriptor* desc = registry_.find(panel);
  const auto create = [&]() -> WidgetId {
    if (desc == nullptr || !desc->factory) return {};
    try {
      return desc->factory(context, body);
    } catch (const std::exception& ex) {
      lastError_ = "creating the content of panel " + std::to_string(panel) + " failed: " + ex.what();
      return {};
    }
  };
  if (it == contents_.end()) {
    if (!visible) return;
    contents_[panel] = {&context, create()};
    it = contents_.find(panel);
  } else if (it->second.ui != &context) {
    if (!visible) return;
    if (it->second.widget.valid() && it->second.ui->alive(it->second.widget)) it->second.ui->destroy(it->second.widget);
    it->second = {&context, create()};
  } else if (it->second.widget.valid() && context.tree().parent(it->second.widget) != body) {
    context.reparent(it->second.widget, body);
  }
  const WidgetId widget = it->second.widget;
  if (!widget.valid()) return;
  if (WidgetObject* object = context.object(widget)) {
    layout::Style& s = object->style();
    const layout::Style before = s;
    s.width = layout::Length::percent(100);
    s.height = layout::Length::percent(100);
    s.flexShrink = 0.0;
    s.display = visible ? layout::Display::Flex : layout::Display::None;
    if (!(before.width == s.width && before.height == s.height && before.display == s.display && before.flexShrink == s.flexShrink)) {
      object->requestLayout();
    }
  }
}

void DockHost::releaseBody(UiContext& context, WidgetId body) {
  if (&context != &ui() || !holding_.valid()) return;
  for (auto& [panel, rec] : contents_) {
    if (rec.ui == &context && rec.widget.valid() && context.alive(rec.widget) && context.tree().parent(rec.widget) == body) {
      context.reparent(rec.widget, holding_);
    }
  }
}

// ---- IFloatingListener ----------------------------------------------------------------------------

void DockHost::onFloatMoved(FloatId window, const dock::Rect& contentRect) {
  const AreaRec* rec = areaRecForWindow(window);
  if (rec == nullptr) return;
  if (!layout_->setAreaRect(rec->area, contentRect)) return;
  afterModelChanged(DockChange::Window, true);
}

void DockHost::onFloatCloseRequested(FloatId window) {
  const AreaRec* rec = areaRecForWindow(window);
  if (rec != nullptr) closeWindowOfArea(rec->area);
}

bool DockHost::closeWindowOfArea(uint32_t area) {
  const std::vector<dock::PanelId> panels = panelsOfArea(*layout_, area);
  // Spec 03 rule 43: every tab is asked; if one refuses nothing is closed.
  for (dock::PanelId p : panels) {
    const dock::PanelInfo* info = layout_->panel(p);
    if (info == nullptr || !info->canClose || info->locked) {
      lastError_ = "panel " + std::to_string(p) + " cannot be closed, so the window stays open";
      return false;
    }
  }
  bool any = false;
  for (dock::PanelId p : panels) any = layout_->closePanel(p).ok || any;
  if (any) afterModelChanged(DockChange::Arrangement);
  return any;
}

void DockHost::onFloatActivated(FloatId window) {
  const AreaRec* rec = areaRecForWindow(window);
  if (rec == nullptr || drag_.active) return;
  const uint32_t area = rec->area;
  const bool onTop = !layout_->areas().empty() && layout_->areas().back().id == area;
  if (!onTop) layout_->raiseArea(area);
  for (const dock::Area& a : layout_->areas()) {
    if (a.id == area && a.root) {
      const dock::Node* n = &*a.root;
      while (n->kind == dock::Node::Kind::Split) n = &n->children.front();
      setActivePanel(n->tabs[n->active]);
    }
  }
  if (!onTop) afterModelChanged(DockChange::Window, true);
}

void DockHost::onFloatLost(FloatId window) {
  const AreaRec* rec = areaRecForWindow(window);
  if (rec == nullptr) return;
  const uint32_t area = rec->area;
  UiContext* lostUi = rec->ui;
  // The window is already gone: forget it and everything that lived in its context.
  for (auto c = contents_.begin(); c != contents_.end();) {
    c = (c->second.ui == lostUi && lostUi != &ui()) ? contents_.erase(c) : std::next(c);
  }
  areas_.erase(std::remove_if(areas_.begin(), areas_.end(), [&](const AreaRec& r) { return r.area == area; }), areas_.end());
  for (dock::PanelId p : panelsOfArea(*layout_, area)) closeIgnoringLock(*layout_, p);  // a lock cannot keep a window that is gone
  afterModelChanged(DockChange::Arrangement, true);
}

void DockHost::onFloatScaleChanged(FloatId window, double scale) {
  const AreaRec* rec = areaRecForWindow(window);
  if (rec == nullptr || !(scale > 0.0)) return;
  for (const dock::Area& a : layout_->areas()) {
    if (a.id != rec->area) continue;
    dock::WindowState state = a.window;
    state.dpiScale = scale;
    layout_->setWindowState(a.id, state);
    break;
  }
  notify(DockChange::Window);
}

void DockHost::onFloatMaximizedChanged(FloatId window, bool maximized) {
  const AreaRec* rec = areaRecForWindow(window);
  if (rec == nullptr) return;
  for (const dock::Area& a : layout_->areas()) {
    if (a.id != rec->area) continue;
    dock::WindowState state = a.window;
    state.maximized = maximized;
    layout_->setWindowState(a.id, state);
    break;
  }
  notify(DockChange::Window);
}

}  // namespace r1ui::widgets

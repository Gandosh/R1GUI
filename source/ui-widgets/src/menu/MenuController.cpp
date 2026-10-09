// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of MenuController.h: the MenuStack shared state (levels, pending submenu
//   request, commands) that listens to every MenuPanel of the stack.
// Invariants: levels_ is ordered bottom (root) to top; every level's overlay is open; a level's
//   parentIndex is the row of the level below that opened it; at most one pending request (timer)
//   exists and it is cancelled by leaving the row, reaching a submenu, closing a level or opening a
//   menu; callbacks into application code (commands, onClosed) run with no internal state half-edited.
// Callers: MenuController (thin handle), MenuPanel (listener calls).
#include "r1ui/widgets/menu/MenuController.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "r1ui/widgets/menu/MenuPanel.h"
#include "r1ui/widgets/popover/OverlayWatch.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

using core::tree::WidgetId;
namespace events = core::events;

namespace {
// Submenus keep 5 px from the window edge (the measured submenu ends 5 px above the window bottom).
constexpr double kSubmenuWindowMargin = 5.0;
}  // namespace

class MenuStack final : public MenuPanelListener, public std::enable_shared_from_this<MenuStack> {
 public:
  explicit MenuStack(UiContext& ui) : ui_(&ui) {}

  // ---- controller API ----
  bool open(MenuSpec spec, const MenuOpenOptions& options);
  void closeAll(DismissReason reason) { closeFrom(0, reason); }
  // The controller handle is gone: no application callback may run any more (they capture the owner).
  // The menu itself stays until it is dismissed; activating a row then only closes it.
  void release() {
    released_ = true;
    rootClosed_ = nullptr;
    rootNavigation_ = nullptr;
    command_ = nullptr;
  }
  bool isOpen() const { return !levels_.empty(); }
  int levelCount() const { return static_cast<int>(levels_.size()); }
  WidgetId panelAt(int level) const { return inRange(level) ? levels_[static_cast<size_t>(level)].panel : WidgetId{}; }
  WidgetId hostAt(int level) const { return inRange(level) ? levels_[static_cast<size_t>(level)].host : WidgetId{}; }
  OverlayId overlayAt(int level) const { return inRange(level) ? levels_[static_cast<size_t>(level)].overlay : OverlayId{}; }
  MenuTiming timing_;
  std::function<bool(bool)> rootNavigation_;

  // ---- MenuPanelListener ----
  void panelItemActivated(MenuPanel& panel, int index, bool fromKeyboard) override;
  void panelItemEntered(MenuPanel& panel, int index) override;
  void panelItemLeft(MenuPanel& panel, int index) override;
  void panelPointerEntered(MenuPanel&) override { cancelPending(); }
  void panelPointerMoved(MenuPanel&, double x) override { lastMoveX_ = x; }
  bool panelSubmenuOpenFor(const MenuPanel& panel, int index) const override;
  bool panelNavigationKey(MenuPanel& panel, bool open) override;
  void panelDismissRequested(MenuPanel&) override { closeAll(DismissReason::Programmatic); }

 private:
  struct Level {
    OverlayId overlay;
    WidgetId host;
    WidgetId panel;
    int parentIndex = -1;
    uint64_t openedAtMs = 0;
  };

  bool inRange(int level) const { return level >= 0 && static_cast<size_t>(level) < levels_.size(); }
  int levelOfPanel(const MenuPanel& panel) const;
  bool openLevel(OverlayOptions options, std::vector<MenuItemSpec> items, double minWidth, int parentIndex, bool highlightFirst);
  void openSubmenu(int level, int index, bool keyboard);
  void closeFrom(size_t from, DismissReason reason);
  void levelClosed(uint32_t overlayId);
  void cancelPending();
  void schedule(uint64_t delayMs, std::function<void()> action);
  void runCommand(MenuPanel& panel, int index);

  UiContext* ui_;
  std::vector<Level> levels_;
  std::function<void(const MenuItemSpec&)> command_;
  std::function<void()> rootClosed_;
  double submenuGap_ = 0.0;
  std::string shadow_;
  MenuLook look_;
  UiContext::TimerId pending_ = 0;
  double lastMoveX_ = 0.0;
  bool released_ = false;
};

// ---- opening ------------------------------------------------------------------------------------

bool MenuStack::open(MenuSpec spec, const MenuOpenOptions& o) {
  if (spec.items.empty()) return false;
  const std::shared_ptr<MenuStack> self = shared_from_this();
  closeAll(DismissReason::Replaced);
  command_ = std::move(spec.onCommand);
  look_ = spec.look;
  rootClosed_ = o.onClosed;
  submenuGap_ = std::isfinite(o.submenuGap) ? std::max(0.0, o.submenuGap) : 0.0;
  shadow_ = o.shadow;
  OverlayOptions oo;
  oo.anchor = o.anchor;
  oo.placement = o.placement;
  oo.gap = o.gap;
  oo.windowMargin = o.windowMargin;
  oo.matchAnchorWidth = o.matchAnchorWidth;
  oo.shadow = o.shadow;
  oo.anchorWidget = o.anchorWidget;
  oo.surface = OverlaySurface::Menu;
  oo.escapeFirst = true;
  oo.closeAllOnEscape = true;
  oo.dismissOnWindowDeactivate = true;
  oo.focusOnOpen = true;
  if (!openLevel(std::move(oo), std::move(spec.items), spec.minWidth, -1, o.highlightFirst)) {
    command_ = nullptr;
    rootClosed_ = nullptr;
    return false;
  }
  return true;
}

bool MenuStack::openLevel(OverlayOptions options, std::vector<MenuItemSpec> items, double minWidth, int parentIndex, bool highlightFirst) {
  // The overlay id is only known after open(); the close callback finds its level through this box.
  const auto idBox = std::make_shared<uint32_t>(0);
  options.onClosed = [weak = weak_from_this(), idBox](DismissReason) {
    if (const std::shared_ptr<MenuStack> stack = weak.lock()) stack->levelClosed(*idBox);
  };
  const OverlayHandle handle = ui_->overlays().open(options);
  if (!handle.valid()) return false;
  *idBox = handle.id.value;
  WidgetId panelId;
  try {
    panelId = ui_->create<MenuPanel>(handle.host, std::move(items), minWidth, look_, shared_from_this()).id();
  } catch (const std::exception&) {
    ui_->overlays().close(handle.id, DismissReason::Programmatic);
    return false;
  }
  // A root menu belongs to its anchor widget: it closes when that widget goes away or is hidden.
  if (parentIndex < 0 && options.anchorWidget.valid()) {
    try {
      ui_->create<OverlayWatch>(handle.host, handle.id, WidgetId{}, options.anchorWidget);
    } catch (const std::exception&) {
      ui_->overlays().close(handle.id, DismissReason::Programmatic);
      return false;
    }
  }
  levels_.push_back(Level{handle.id, handle.host, panelId, parentIndex, ui_->now()});
  if (highlightFirst) {
    if (MenuPanel* panel = ui_->objectAs<MenuPanel>(panelId)) panel->highlightFirst();
  }
  return true;
}

void MenuStack::openSubmenu(int level, int index, bool keyboard) {
  if (!inRange(level)) return;
  const std::shared_ptr<MenuStack> self = shared_from_this();
  const size_t above = static_cast<size_t>(level) + 1;
  if (above < levels_.size() && levels_[above].parentIndex == index) {
    // Already open: a keyboard request moves focus into it.
    if (keyboard) ui_->router().focus(levels_[above].panel, events::FocusReason::Keyboard);
    return;
  }
  closeFrom(above, DismissReason::Programmatic);
  if (!inRange(level)) return;
  MenuPanel* parent = ui_->objectAs<MenuPanel>(levels_[static_cast<size_t>(level)].panel);
  if (parent == nullptr || index < 0 || index >= parent->itemCount()) return;
  const MenuItemSpec& row = parent->item(index);
  if (row.kind != MenuItemKind::Submenu || !row.enabled || row.children.empty()) return;
  const core::layout::Rect r = ui_->absRect(parent->itemWidget(index));
  OverlayOptions oo;
  oo.anchor = r;  // the submenu's top edge is level with the row that opened it (measured in the File menu)
  oo.placement = Placement::RightStart;
  oo.gap = submenuGap_;
  oo.windowMargin = kSubmenuWindowMargin;
  oo.shadow = shadow_;
  oo.surface = OverlaySurface::Menu;
  oo.escapeFirst = true;
  oo.closeAllOnEscape = true;
  oo.dismissOnWindowDeactivate = true;
  oo.focusOnOpen = keyboard;
  oo.restoreFocus = keyboard;  // a hover-opened submenu never took focus, so closing it must not move it
  std::vector<MenuItemSpec> children = row.children;
  if (levels_.size() >= static_cast<size_t>(kMaxMenuDepth)) return;
  openLevel(std::move(oo), std::move(children), look_.submenuMinWidth, index, keyboard);
}

// ---- closing ------------------------------------------------------------------------------------

void MenuStack::closeFrom(size_t from, DismissReason reason) {
  if (from >= levels_.size()) return;
  const std::shared_ptr<MenuStack> self = shared_from_this();
  cancelPending();
  while (levels_.size() > from) {
    const OverlayId id = levels_.back().overlay;
    ui_->overlays().close(id, reason);  // levelClosed() removes the level
    if (!levels_.empty() && levels_.back().overlay == id) levels_.pop_back();  // the overlay was already gone
  }
}

void MenuStack::levelClosed(uint32_t overlayId) {
  const auto it = std::find_if(levels_.begin(), levels_.end(), [&](const Level& l) { return l.overlay.value == overlayId; });
  if (it == levels_.end()) return;
  const std::shared_ptr<MenuStack> self = shared_from_this();
  const size_t index = static_cast<size_t>(it - levels_.begin());
  std::vector<OverlayId> deeper;
  for (size_t i = index + 1; i < levels_.size(); ++i) deeper.push_back(levels_[i].overlay);
  levels_.erase(it, levels_.end());
  cancelPending();
  // A level that closes takes the submenus above it along (they would be orphans).
  for (size_t i = deeper.size(); i-- > 0;) ui_->overlays().close(deeper[i], DismissReason::Programmatic);
  if (index == 0) {
    command_ = nullptr;
    const std::function<void()> callback = std::move(rootClosed_);
    rootClosed_ = nullptr;
    if (callback) callback();
  }
}

// ---- pending request (timers) -------------------------------------------------------------------

void MenuStack::cancelPending() {
  if (pending_ != 0) ui_->cancelTimer(pending_);
  pending_ = 0;
}

void MenuStack::schedule(uint64_t delayMs, std::function<void()> action) {
  cancelPending();
  if (delayMs == 0) {
    action();
    return;
  }
  pending_ = ui_->setTimer(delayMs, [weak = weak_from_this(), action = std::move(action)]() {
    if (const std::shared_ptr<MenuStack> stack = weak.lock()) {
      stack->pending_ = 0;
      action();
    }
  });
}

// ---- listener -----------------------------------------------------------------------------------

int MenuStack::levelOfPanel(const MenuPanel& panel) const {
  for (size_t i = 0; i < levels_.size(); ++i) {
    if (levels_[i].panel == panel.id()) return static_cast<int>(i);
  }
  return -1;
}

bool MenuStack::panelSubmenuOpenFor(const MenuPanel& panel, int index) const {
  const int level = levelOfPanel(panel);
  return level >= 0 && static_cast<size_t>(level) + 1 < levels_.size() && levels_[static_cast<size_t>(level) + 1].parentIndex == index;
}

void MenuStack::panelItemEntered(MenuPanel& panel, int index) {
  const int level = levelOfPanel(panel);
  if (level < 0) return;
  cancelPending();
  const size_t above = static_cast<size_t>(level) + 1;
  const bool submenuRow = panel.item(index).kind == MenuItemKind::Submenu;
  const bool haveOpen = above < levels_.size();
  if (haveOpen && submenuRow && levels_[above].parentIndex == index) return;  // its own submenu is open already

  // Is the pointer moving toward the open submenu (zero movement counts as toward)?
  bool delayed = false;
  if (haveOpen) {
    const core::layout::Rect here = ui_->absRect(levels_[static_cast<size_t>(level)].host);
    const core::layout::Rect there = ui_->absRect(levels_[above].host);
    const double side = there.x >= here.x ? 1.0 : -1.0;
    const bool toward = (ui_->pointerX() - lastMoveX_) * side >= 0.0;
    const bool old = ui_->now() - levels_[above].openedAtMs >= timing_.submenuMinLifetimeMs;
    delayed = toward && old;
  }
  lastMoveX_ = ui_->pointerX();
  const WidgetId panelId = panel.id();
  const auto stillThere = [this, panelId]() -> int {
    for (size_t i = 0; i < levels_.size(); ++i) {
      if (levels_[i].panel == panelId) return static_cast<int>(i);
    }
    return -1;
  };
  if (submenuRow) {
    const uint64_t delay = haveOpen ? (delayed ? timing_.submenuReplaceDelayMs : 0) : timing_.submenuOpenDelayMs;
    schedule(delay, [this, stillThere, index]() {
      const int l = stillThere();
      if (l >= 0) openSubmenu(l, index, false);
    });
  } else if (haveOpen) {
    schedule(delayed ? timing_.submenuReplaceDelayMs : 0, [this, stillThere]() {
      const int l = stillThere();
      if (l >= 0) closeFrom(static_cast<size_t>(l) + 1, DismissReason::Programmatic);
    });
  }
}

void MenuStack::panelItemLeft(MenuPanel&, int) { cancelPending(); }

bool MenuStack::panelNavigationKey(MenuPanel& panel, bool open) {
  const int level = levelOfPanel(panel);
  if (level < 0) return false;
  const std::shared_ptr<MenuStack> self = shared_from_this();
  if (open) {
    const int row = panel.highlighted();
    if (row >= 0 && panel.item(row).kind == MenuItemKind::Submenu && panel.item(row).enabled) {
      openSubmenu(level, row, true);
      return true;
    }
    return level == 0 && rootNavigation_ && rootNavigation_(true);
  }
  if (level > 0) {
    const WidgetId parentPanel = levels_[static_cast<size_t>(level) - 1].panel;
    closeFrom(static_cast<size_t>(level), DismissReason::Programmatic);
    ui_->router().focus(parentPanel, events::FocusReason::Keyboard);
    return true;
  }
  return rootNavigation_ && rootNavigation_(false);
}

void MenuStack::panelItemActivated(MenuPanel& panel, int index, bool fromKeyboard) {
  const int level = levelOfPanel(panel);
  if (level < 0) return;
  const MenuItemKind kind = panel.item(index).kind;
  if (kind == MenuItemKind::Submenu) {
    cancelPending();
    openSubmenu(level, index, fromKeyboard);
    return;
  }
  runCommand(panel, index);
}

void MenuStack::runCommand(MenuPanel& panel, int index) {
  const std::shared_ptr<MenuStack> self = shared_from_this();
  cancelPending();
  const MenuItemSpec result = panel.applyActivationState(index);  // checks toggle before the callback sees them
  const std::function<void(const MenuItemSpec&)> handler = released_ ? nullptr : (result.onActivate ? result.onActivate : command_);
  // The stack closes first (focus goes back to where it was before the menu), then the command runs:
  // a command that opens a dialog saves a focus target that still exists, and one that moves focus
  // itself wins over the restore.
  if (!result.keepOpen) closeAll(DismissReason::Programmatic);
  if (handler) handler(result);
}

// ---- MenuController handle ----------------------------------------------------------------------

MenuController::MenuController(UiContext& ui) : stack_(std::make_shared<MenuStack>(ui)) {}

MenuController::~MenuController() { stack_->release(); }

bool MenuController::open(MenuSpec spec, const MenuOpenOptions& options) { return stack_->open(std::move(spec), options); }

bool MenuController::openContextMenu(MenuSpec spec, double x, double y, std::function<void()> onClosed) {
  if (!std::isfinite(x) || !std::isfinite(y)) return false;
  MenuOpenOptions o;
  o.anchor = {static_cast<int32_t>(std::lround(std::clamp(x, -1e6, 1e6))), static_cast<int32_t>(std::lround(std::clamp(y, -1e6, 1e6))), 0, 0};
  o.placement = Placement::Manual;
  o.shadow = "overlay";
  spec.look.arrowGlyph = true;
  o.windowMargin = 0.0;  // the reference menu sits flush against the window edge (spec 10 rule 48: clamped to the work area)
  o.onClosed = std::move(onClosed);
  return stack_->open(std::move(spec), o);
}

void MenuController::close() { stack_->closeAll(DismissReason::Programmatic); }
bool MenuController::isOpen() const { return stack_->isOpen(); }
int MenuController::levelCount() const { return stack_->levelCount(); }
WidgetId MenuController::panelAt(int level) const { return stack_->panelAt(level); }
WidgetId MenuController::hostAt(int level) const { return stack_->hostAt(level); }
OverlayId MenuController::overlayAt(int level) const { return stack_->overlayAt(level); }
MenuTiming MenuController::timing() const { return stack_->timing_; }
void MenuController::setTiming(const MenuTiming& timing) { stack_->timing_ = timing; }
void MenuController::setRootNavigationHandler(std::function<bool(bool)> handler) { stack_->rootNavigation_ = std::move(handler); }

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: MenuController, the open menu stack of one UiContext: opening a menu (dropdown under an
//   anchor, context menu at a point) as an overlay, opening and closing submenus with the timings of
//   spec 10 (open at once, replace at once while sweeping, 0.5 s delay while moving toward an open
//   submenu), keyboard-driven submenu navigation, activation of rows (run the command, close the
//   whole stack) and dismissal bookkeeping.
// Why: which levels exist, when a submenu opens or closes and what a click does are properties of
//   the stack, not of one panel; keeping them in one object lets a menu bar, a context menu and a
//   test drive the same behaviour. The overlay layer supplies placement, outside-press and Escape
//   dismissal (Escape closes the whole stack), focus restore and window-deactivation closing.
// Callers: MenuBar, application code (context menus, dropdown buttons), tests. Calls: OverlayManager,
//   MenuPanel (as its listener), UiContext timers.
// Submenu rules (spec 10 rules 29 to 36): entering a submenu row opens its submenu at once when no
//   submenu of that level is open. When another submenu is open and has been for less than
//   minLifetimeMs, or the pointer is not moving toward it, the switch is immediate; otherwise it waits
//   replaceDelayMs and is cancelled when the pointer leaves the row or reaches the submenu. "Toward"
//   means the horizontal move agrees with the side the submenu is on, a zero move counting as toward.
//   Entering a plain row closes an open submenu by the same rule. There is no diagonal safe zone.
// Lifetime: the controller is a handle on shared state. Destroying it does NOT touch the UI (it may
//   run while the context is being destroyed): call close() first (widgets that own a controller
//   do it in onDetached). A menu left open by a destroyed controller stays open and inert until it
//   is dismissed. Commands run before the stack closes; a command may close or destroy the
//   controller; the controller then only finishes closing.
// Boundaries: specs are sanitised by MenuPanel (text limits, item and depth caps); a failure to
//   create widgets (tree limits) makes open() return false and leaves nothing open.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "r1ui/core/layout/Geometry.h"
#include "r1ui/core/tree/WidgetId.h"
#include "r1ui/widgets/menu/MenuModel.h"
#include "r1ui/widgets/overlay/OverlayManager.h"

namespace r1ui::widgets {

class UiContext;
class MenuStack;

struct MenuTiming {
  uint64_t submenuOpenDelayMs = 0;       // nothing open: open at once
  uint64_t submenuReplaceDelayMs = 500;  // switching away from an old submenu while moving toward it
  uint64_t submenuMinLifetimeMs = 500;   // a submenu younger than this is replaced at once
};

struct MenuOpenOptions {
  core::layout::Rect anchor;                         // logical window px; zero size = a point
  Placement placement = Placement::BelowStart;
  double gap = 0.0;
  double windowMargin = 4.0;
  double submenuGap = 0.0;                           // between a row and its submenu (menu bar menus use 4)
  bool matchAnchorWidth = false;
  std::string shadow;                                // shadow token for every level (context menus: "overlay"); empty = the menu default
  core::tree::WidgetId anchorWidget;                 // a press inside it only closes the menu (toggle)
  bool highlightFirst = false;                       // keyboard-opened: highlight the first row
  std::function<void()> onClosed;                    // after the whole stack is gone
};

class MenuController {
 public:
  explicit MenuController(UiContext& ui);
  ~MenuController();
  MenuController(const MenuController&) = delete;
  MenuController& operator=(const MenuController&) = delete;

  // Opens `spec` as the root of a new stack, replacing an open one (its onClosed runs first). False
  // when the menu has no rows or the widgets could not be created.
  bool open(MenuSpec spec, const MenuOpenOptions& options);
  // Context menu at a point (window logical px): placed there and pushed inside the window (no
  // margin: it may touch the window edge, like the reference) with the overlay shadow.
  bool openContextMenu(MenuSpec spec, double x, double y, std::function<void()> onClosed = {});
  // Closes the whole stack (Programmatic); a no-op when nothing is open.
  void close();

  bool isOpen() const;
  int levelCount() const;
  core::tree::WidgetId panelAt(int level) const;
  core::tree::WidgetId hostAt(int level) const;
  OverlayId overlayAt(int level) const;

  MenuTiming timing() const;
  void setTiming(const MenuTiming& timing);

  // Root level: Right on a row without a submenu (open = true) or Left (false) is offered here
  // first (a menu bar switches to the neighbouring menu). Return true when used.
  void setRootNavigationHandler(std::function<bool(bool right)> handler);

 private:
  std::shared_ptr<MenuStack> stack_;
};

}  // namespace r1ui::widgets

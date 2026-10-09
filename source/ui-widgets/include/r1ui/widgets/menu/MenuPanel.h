// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: MenuPanel, the content of one menu level (one popup of a menu stack): the rows built from a
//   list of MenuItemSpec, the single highlight shared by pointer and keyboard, keyboard navigation
//   (Up, Down, Home, End without wrapping, Enter and Space, Right and Left for submenus, type-ahead),
//   scrolling of tall menus (wheel, scroll into view, a thin scroll bar) and the pointer reports the
//   controller needs for the submenu timers.
// Why: the highlight, the keyboard handling and the scrolling are properties of one open menu level
//   regardless of who opened it (menu bar, context menu, submenu), so they live in one widget; the
//   stack logic (which levels exist, timers, dismissal) stays in MenuController, which the panel only
//   talks to through MenuPanelListener.
// Callers: MenuController creates one per level as a child of an overlay host. Calls: MenuItems.h.
// Layout: the panel is a focusable, clipping column inside the overlay host with a 1 px margin (the
//   host paints a 1 px border that has no layout effect); its single child, the list, holds the rows
//   and is shifted up by the scroll offset through a negative top margin. The host limits the
//   height (80% of the window); the panel shrinks to it and scrolls.
// Keyboard (spec 10 rules 44 to 47, owner decisions D20): Up and Down move to the previous or next
//   selectable row and stop at the ends; a missing highlight starts at the first (Down) or last (Up)
//   row; Home and End jump; Enter and Space activate; Right opens a submenu (or is offered to the
//   listener for menu-bar navigation); Left closes the level (offered likewise); Tab closes the
//   stack; printable characters jump to the next row whose label starts with the typed prefix
//   (case-insensitive, the prefix resets after 1 s).
// Lifetime: the panel shares ownership of its listener (so a controller that was destroyed first
//   leaves a harmless, inert listener behind) and releases it when it is detached; the panel never calls the
//   listener while it is being destroyed. Rows are addressed by index; indices stay valid for the
//   life of the panel (a menu is rebuilt, not edited in place).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "r1ui/widgets/menu/MenuItems.h"
#include "r1ui/widgets/menu/MenuModel.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class MenuPanel;

class MenuPanelListener {
 public:
  virtual ~MenuPanelListener() = default;
  // A row was activated by click, Enter or Space. Submenu rows open their submenu.
  virtual void panelItemActivated(MenuPanel& panel, int index, bool fromKeyboard) = 0;
  // The pointer entered / left a selectable row (the panel already moved the highlight).
  virtual void panelItemEntered(MenuPanel& panel, int index) = 0;
  virtual void panelItemLeft(MenuPanel& panel, int index) = 0;
  // The pointer entered the panel itself (it moved into this menu level) or moved inside it.
  virtual void panelPointerEntered(MenuPanel& panel) = 0;
  virtual void panelPointerMoved(MenuPanel& panel, double x) = 0;
  // True when the submenu of row `index` is open (the row keeps its highlight, spec 10 rule 25).
  virtual bool panelSubmenuOpenFor(const MenuPanel& panel, int index) const = 0;
  // Right (open = true) or Left (open = false). Returns true when the listener used the key.
  virtual bool panelNavigationKey(MenuPanel& panel, bool open) = 0;
  // Tab: the stack closes.
  virtual void panelDismissRequested(MenuPanel& panel) = 0;
};

// The column that holds the rows (shifted by the scroll offset).
class MenuList final : public WidgetObject {
 public:
  const char* typeName() const override { return "MenuList"; }
  void onAttached() override;
};

class MenuPanel final : public WidgetObject {
 public:
  MenuPanel(std::vector<MenuItemSpec> items, double minWidth, const MenuLook& look, std::shared_ptr<MenuPanelListener> listener);
  const char* typeName() const override { return "MenuPanel"; }
  void onAttached() override;
  void onDetached() override { listener_.reset(); }
  void paintOver(PaintContext& ctx) override;
  void onKeyDown(Event& e) override;
  bool wantsTextInput() const override { return true; }
  void onTextInput(Event& e) override;
  void onPointerWheel(Event& e) override;
  void onPointerEnter(Event& e) override;
  void onPointerMove(Event& e) override;
  std::string_view accessibleName() const override { return "Menu"; }

  // ---- rows ----
  int itemCount() const { return static_cast<int>(items_.size()); }
  const MenuItemSpec& item(int index) const { return items_[static_cast<size_t>(index)]; }
  core::tree::WidgetId itemWidget(int index) const;
  // False for separators, headings, disabled rows and rows hidden because nothing follows a heading.
  bool selectable(int index) const;
  bool truncated() const { return truncated_; }

  // ---- highlight ----
  int highlighted() const { return highlighted_; }
  // Moves the highlight (-1 clears it); false when `index` is not selectable. Scrolls it into view.
  bool setHighlight(int index, bool scrollIntoView = true);
  // delta -1 / +1: the previous / next selectable row, no wrap; false when there is none.
  bool moveHighlight(int delta);
  bool highlightFirst();
  bool highlightLast();

  // ---- reports from the rows ----
  void itemPointerMoved(int index);
  void itemPointerLeft(int index);
  void itemActivated(int index, bool fromKeyboard);

  // Applies the state change of a Check or Radio activation to the data and the rows and returns the
  // updated spec (other kinds return the unchanged spec).
  MenuItemSpec applyActivationState(int index);

  // Updates the live state of row `index` (label, shortcut, tooltip, enabled, checked; texts are
  // sanitised) while the menu is open; kind, icon and structure never change. A row that became
  // disabled loses the highlight. False for a bad index or a row without a widget.
  bool refreshItem(int index, const MenuItemSpec& live);

  // ---- scrolling ----
  double scrollOffset() const { return scroll_; }
  double maxScroll() const;
  void scrollTo(double offset);

  void setListener(std::shared_ptr<MenuPanelListener> listener) { listener_ = std::move(listener); }

 private:
  int nextSelectable(int from, int delta) const;
  void ensureVisible(int index);
  void jumpToPrefix();

  std::vector<MenuItemSpec> items_;
  std::vector<core::tree::WidgetId> widgets_;
  std::vector<bool> shown_;
  double minWidth_;
  MenuLook look_;
  std::shared_ptr<MenuPanelListener> listener_;
  core::tree::WidgetId list_;
  int highlighted_ = -1;
  double scroll_ = 0.0;
  bool truncated_ = false;
  int pointerItem_ = -1;   // the row the last real pointer movement was over
  std::string prefix_;
  uint64_t prefixAtMs_ = 0;
};

}  // namespace r1ui::widgets

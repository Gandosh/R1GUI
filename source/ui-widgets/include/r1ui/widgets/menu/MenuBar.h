// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: MenuBar (a row of menu titles) and MenuBarItem (one title): click opens the menu under the
//   title, hovering another title while a menu is open switches to it at once (spec 10 rule 20),
//   a second click on the open title closes it, Left and Right move between titles (also while a
//   menu is open), Down, Enter and Space open the menu from the keyboard.
// Why: the menu bar is the one menu surface with its own state (which title is open); the rest of the
//   menu behaviour (stack, timers, keys) is MenuController's, so the bar only decides when to open
//   and when to switch.
// Callers: application shells, the gallery. Calls: MenuController (owned), MenuItems style rows.
// Look (docs/spec/widgets.md 4.3): titles 24 px high, padding 8 x 4, radius 4, 12 px `muted` text;
//   hover and the open title use the `hover` fill with `surface` text (style rows menubar.item).
//   Menus open 5 px below the title, 1 px left of it (the measured File menu position), with a 4 px
//   gap before submenus and a 208 px minimum width.
// Keyboard: Left and Right between titles do not wrap (like menu rows). Tab order: every title is a
//   tab stop; focus shows the ring only for keyboard focus.
// Lifetime: the bar closes its open menu when it is destroyed (onDetached); items hold only the bar's
//   id. Titles and menus are sanitised through the model's text limits.
#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/menu/MenuController.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class MenuBarItem final : public WidgetObject {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows();

  MenuBarItem(std::string title, core::tree::WidgetId bar, int index);
  const char* typeName() const override { return "MenuBarItem"; }
  void onAttached() override;
  core::layout::MeasureResult measure(const core::layout::MeasureInput& input) override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  uint8_t styleState() const override;
  Cursor cursor() const override { return Cursor::Default; }
  void onPointerEnter(Event& e) override;
  void onClick(Event& e) override;
  void onKeyDown(Event& e) override;
  std::string_view accessibleName() const override { return title_; }
  const std::string& title() const { return title_; }

 private:
  std::string title_;
  core::tree::WidgetId bar_;
  int index_;
};

class MenuBar final : public WidgetObject {
 public:
  explicit MenuBar() = default;
  const char* typeName() const override { return "MenuBar"; }
  void onAttached() override;
  void onDetached() override;

  // Adds a title with its menu; returns its index, or -1 when the title is empty after sanitising.
  int addMenu(std::string title, MenuSpec menu);
  int menuCount() const { return static_cast<int>(menus_.size()); }
  core::tree::WidgetId itemWidget(int index) const;
  // Opens the menu of `index` (replacing an open one); false for a bad index or when it has no rows.
  bool openMenu(int index, bool fromKeyboard = false);
  void closeMenu();
  int openIndex() const { return open_; }
  MenuController& controller() { return *controller_; }
  // Called with a copy of a title's spec right before its menu opens, so the caller can refresh what
  // changed since addMenu (enabled, checked and shortcut state of command-driven menus).
  void setBeforeOpen(std::function<void(int index, MenuSpec&)> hook) { beforeOpen_ = std::move(hook); }

  // From the items.
  void itemHovered(int index);
  void itemClicked(int index);
  void itemKey(int index, Event& e);

 private:
  struct Entry {
    std::string title;
    MenuSpec spec;
    core::tree::WidgetId widget;
  };
  void setOpen(int index);
  bool moveTo(int index, bool openMenuToo);

  std::vector<Entry> menus_;
  std::unique_ptr<MenuController> controller_;
  std::function<void(int, MenuSpec&)> beforeOpen_;
  int open_ = -1;
};

}  // namespace r1ui::widgets

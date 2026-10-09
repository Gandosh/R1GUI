// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the row widgets of a menu: MenuItemWidget (icon, label, optional second line, right-aligned
//   shortcut text, submenu chevron, check or radio indicator), MenuSeparatorWidget (1 px line with
//   4 px margins) and MenuHeadingWidget (small muted section label), plus their style rows.
// Why: a row is the one visual unit shared by menu bars, dropdowns, context menus and submenus;
//   the panel (MenuPanel) decides which row is highlighted, the row only paints and reports pointer
//   events, so keyboard and pointer highlighting stay one state.
// Callers: MenuPanel creates them from a MenuItemSpec. Calls: PaintContext, the style sheet rows
//   menu.item*, menu.separator, menu.heading registered by styleRows().
// Measured look (docs/spec/widgets.md 2.9): item 28 px high (12 px text, line 16, padding 6 x 8,
//   radius 6), label then at least 24 px gap then the shortcut (11 px muted); a leading icon is
//   12 px with 8 px gap (MenuLook); highlight fill `hover`; disabled text `muted` at 50% alpha, no hover fill;
//   component tone colours text `component` with a 12% `component` highlight; a submenu row is
//   32 px high when its arrow is the 14 px character (context menus; the chevron icon of menu bar menus is 12 px and keeps 28); a check or radio indicator sits at the right
//   (14 px check icon, 6 px dot).
// Highlight: `highlighted` is set by the panel (pointer hover and keyboard share it); styleState()
//   maps it to the hover bit. The row never highlights itself from the Router hover state.
// Boundaries: texts are sanitised by the spec builders; the row measures and paints the strings it
//   was given and shortens the label with an ellipsis when its box is narrower than the text.
#pragma once

#include <span>
#include <string>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/menu/MenuModel.h"
#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

// Pixel metrics of a menu row (logical px); see the header comment for their source.
struct MenuRowMetrics {
  static constexpr double kMinGap = 24.0;
  static constexpr double kChevronCell = 12.0;        // chevron icon of a submenu row (menu bar menus)
  static constexpr double kArrowGlyphSize = 14.0;     // font size of the arrow character (context menus)
  static constexpr double kArrowGlyphLine = 20.0;     // its line height: makes a submenu row 32 px high
  static constexpr double kCheckSize = 14.0;
  static constexpr double kDescriptionLine = 13.333;
  static constexpr double kSeparatorMargin = 4.0;
  static constexpr double kSeparatorHeight = 1.0;
};

class MenuItemWidget final : public WidgetObject {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows();

  // `owner` is the MenuPanel widget that receives pointer reports; `index` is the row's index there.
  MenuItemWidget(const MenuItemSpec& spec, core::tree::WidgetId owner, int index, const MenuLook& look = {});
  const char* typeName() const override { return "MenuItem"; }
  void onAttached() override;
  core::layout::MeasureResult measure(const core::layout::MeasureInput& input) override;
  uint8_t styleState() const override;
  void paint(PaintContext& ctx) override;
  Cursor cursor() const override { return enabled() ? Cursor::Pointer : Cursor::Default; }
  // The highlight follows real pointer movement, not hover changes caused by scrolling or layout
  // under a resting pointer (keyboard navigation must not be stolen by a stationary mouse).
  void onPointerMove(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onClick(Event& e) override;
  std::string_view accessibleName() const override;

  const MenuItemSpec& spec() const { return spec_; }
  void setChecked(bool checked);
  bool highlighted() const { return highlighted_; }
  void setHighlighted(bool on);
  // Style row names for this item's tone (tests).
  const char* itemKey() const;

 private:
  const char* shortcutKey() const;
  const char* iconKey() const;
  double rightCellWidth() const;  // chevron, check indicator or nothing

  MenuItemSpec spec_;
  core::tree::WidgetId owner_;
  int index_;
  MenuLook look_;
  bool highlighted_ = false;
};

class MenuSeparatorWidget final : public WidgetObject {
 public:
  const char* typeName() const override { return "MenuSeparator"; }
  void onAttached() override;
  core::layout::MeasureResult measure(const core::layout::MeasureInput& input) override;
  void paint(PaintContext& ctx) override;
};

class MenuHeadingWidget final : public WidgetObject {
 public:
  explicit MenuHeadingWidget(std::string label) : label_(std::move(label)) {}
  const char* typeName() const override { return "MenuHeading"; }
  void onAttached() override;
  core::layout::MeasureResult measure(const core::layout::MeasureInput& input) override;
  void paint(PaintContext& ctx) override;
  std::string_view accessibleName() const override { return label_; }

 private:
  std::string label_;
};

}  // namespace r1ui::widgets

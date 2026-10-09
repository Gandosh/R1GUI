// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: visual oracle for TabBar against the document tab bar captures (screen-tab-bar-two-documents
//   and the widget crops cut from it) in both themes: the whole bar with an inactive and an active
//   tab and the new-tab button, the inactive tab idle / hovered (hover reveals the close button), the
//   active tab idle / hovered and the new-tab button idle / hovered. The widget crops are 6 px wider
//   than the widget on the sides where it has neighbours, so each comparison cuts the tab area out of
//   the crop file and shifts the bar so that the compared tab sits at the origin.
// Callers: CTest (tabbar gpu: renders offscreen on a Vulkan device, no window).
#include "../g3support/RegionCompare.h"
#include "r1ui/widgets/tabbar/TabBar.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;
using r1ui::theme::ThemeId;

enum class Where { None, InactiveTab, ActiveTab, NewButton };

// The bar of the capture: "Untitled" (inactive) and "Untitled" (active), 1440 wide, shifted left by
// `shiftX` so that the part under test starts at the origin; the pointer is put over `hover`.
r1test::visual::Build makeBar(int shiftX, Where hover) {
  return [shiftX, hover](UiContext& ui, WidgetId parent) {
    TabBar& bar = ui.create<TabBar>(parent);
    bar.style().width = layout::Length::px(1440);
    bar.style().margin[layout::kLeft] = layout::Length::px(-shiftX);
    bar.addTab(1, "Untitled");
    bar.addTab(2, "Untitled");
    bar.setActiveTab(2);
    if (hover != Where::None) {
      ui.frame();
      layout::Rect target;
      switch (hover) {
        case Where::InactiveTab: target = bar.tabRect(1); break;
        case Where::ActiveTab: target = bar.tabRect(2); break;
        case Where::NewButton: target = bar.newButtonRect(); break;
        case Where::None: break;
      }
      ui.pointerMove(target.x + 30, target.y + 5);
    }
    return bar.id();
  };
}

}  // namespace

int main() {
  using r1test::visual::RegionSpec;
  for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
    // The whole bar: x 0..360, 36 px high (the bar's bottom border is its last row).
    R1_EXPECT_MATCHES_REGION(makeBar(0, Where::None), (RegionSpec{.reference = "screen-tab-bar-two-documents", .x = 0, .y = 0, .w = 360, .h = 36, .theme = theme, .profile = "text", .background = "canvas", .tag = "tabbar-two-documents", .luminance = true}));
    // The inactive tab is the first one: its crop starts at x 0 (no neighbour on the left).
    R1_EXPECT_MATCHES_REGION(makeBar(0, Where::None), (RegionSpec{.reference = "widget-tab-bar-tab-inactive-idle", .x = 0, .y = 0, .w = 109, .h = 35, .theme = theme, .profile = "text", .background = "canvas", .tag = "tabbar-inactive-idle", .luminance = true}));
    R1_EXPECT_MATCHES_REGION(makeBar(0, Where::InactiveTab), (RegionSpec{.reference = "widget-tab-bar-tab-inactive-hover", .x = 0, .y = 0, .w = 109, .h = 35, .theme = theme, .profile = "text", .background = "canvas", .tag = "tabbar-inactive-hover", .luminance = true}));
    // The active tab starts 6 px into its crop (the neighbour's edge is on the left).
    R1_EXPECT_MATCHES_REGION(makeBar(109, Where::None), (RegionSpec{.reference = "widget-tab-bar-tab-active-idle", .x = 6, .y = 0, .w = 109, .h = 35, .theme = theme, .profile = "text", .background = "canvas", .tag = "tabbar-active-idle", .luminance = true}));
    R1_EXPECT_MATCHES_REGION(makeBar(109, Where::ActiveTab), (RegionSpec{.reference = "widget-tab-bar-tab-active-hover", .x = 6, .y = 0, .w = 109, .h = 35, .theme = theme, .profile = "text", .background = "canvas", .tag = "tabbar-active-hover", .luminance = true}));
    // The new-tab button: its crop starts 6 px before it, and the button is 36 x 36.
    R1_EXPECT_MATCHES_REGION(makeBar(212, Where::None), (RegionSpec{.reference = "widget-tab-bar-new-idle", .x = 0, .y = 0, .w = 48, .h = 35, .theme = theme, .profile = "text", .background = "canvas", .tag = "tabbar-new-idle", .luminance = true}));
    R1_EXPECT_MATCHES_REGION(makeBar(212, Where::NewButton), (RegionSpec{.reference = "widget-tab-bar-new-hover", .x = 0, .y = 0, .w = 48, .h = 35, .theme = theme, .profile = "text", .background = "canvas", .tag = "tabbar-new-hover", .luminance = true}));
  }
  R1_EXPECT(r1ui::widgets::testing::validationMessageCount() == 0);  // the Debug tree runs the validation layers
  return r1test::finish();
}

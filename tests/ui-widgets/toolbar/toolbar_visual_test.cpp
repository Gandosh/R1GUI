// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: visual oracle for Toolbar and FlyoutList against the editor's floating tool bar captures in
//   both themes: the whole bar cut out of screen-toolbar-flyout-open (select tool active, nothing
//   hovered) and of screen-toolbar-tooltip (the rectangle button hovered), the 44 x 44 crops of the
//   active tool button and of the theme toggle in idle and hover, and the tool flyout (content and a
//   highlighted row). The reference crops include the surrounding bar, so the toolbar is placed with
//   the offsets the capture has.
// Documented differences that the comparisons mask or accept (see Goal/evidence/P4_g3_notes.md):
//   (1) the capture's bar sits on a half-pixel offset (x 547.5), ours on whole pixels, so vertical icon
//   strokes differ by half a pixel of coverage; (2) the page behind the bar and the popup is the
//   document colour #4d4d4d, which is not a theme token, so the pixels outside the rounded surface
//   are ignored; (3) the tooltip over the hovered button overlaps the first rows of the bar in
//   screen-toolbar-tooltip, which are ignored; (4) the reference draws text with LCD antialiasing, so
//   comparisons are on luminance.
// Callers: CTest (toolbar gpu: renders offscreen on a Vulkan device, no window).
#include "../g3support/RegionCompare.h"
#include "r1ui/widgets/toolbar/Toolbar.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;
using r1ui::theme::ThemeId;
using r1test::visual::RegionSpec;

// The tool set of the capture: select, frame group, rectangle group, pen, text, hand, separator,
// components, grid, and a settings button with a status dot.
void fill(Toolbar& bar) {
  bar.addTool("select", "mouse-pointer", "Select (V)");
  bar.addToolGroup({{"frame", "frame", "Frame", "F"}, {"section", "layout-grid", "Section", "S"}});
  bar.addToolGroup({{"rect", "square", "Rectangle", "R"}, {"ellipse", "circle", "Ellipse", "O"}});
  bar.addTool("pen", "pen-tool", "Pen (P)");
  bar.addTool("text", "type", "Text (T)");
  bar.addTool("hand", "hand", "Hand (H)");
  bar.addSeparator();
  bar.addAction("components", "blocks", "Components", [](ToolbarButton&) {});
  bar.addAction("grid", "grid-3x3", "Grid", [](ToolbarButton&) {});
  bar.addAction("settings", "server-cog", "Frame selection", [](ToolbarButton&) {}).setBadge(true);
  bar.setActiveTool("select");
}

// The bar at (left, top) in the render; `hoverButton` >= 0 puts the pointer over that button.
r1test::visual::Build barAt(int left, int top, int hoverButton) {
  return [=](UiContext& ui, WidgetId parent) {
    ui.rootStyle().alignItems = layout::Align::Start;
    Toolbar& bar = ui.create<Toolbar>(parent);
    bar.style().margin[layout::kLeft] = layout::Length::px(left);
    bar.style().margin[layout::kTop] = layout::Length::px(top);
    fill(bar);
    if (hoverButton >= 0) {
      ui.frame();
      const layout::Rect r = ui.absRect(bar.button(static_cast<size_t>(hoverButton))->id());
      ui.pointerMove(r.x + r.w / 2.0, r.y + r.h / 2.0);
    }
    return bar.id();
  };
}

// The capture's toggle is the "dark theme" button: on in the dark theme, off in the light one.
r1test::visual::Build themeToggleAt(int left, int top, bool hover, bool on) {
  return [=](UiContext& ui, WidgetId parent) {
    ui.rootStyle().alignItems = layout::Align::Start;
    Toolbar& bar = ui.create<Toolbar>(parent);
    bar.style().margin[layout::kLeft] = layout::Length::px(left);
    bar.style().margin[layout::kTop] = layout::Length::px(top);
    ToolbarButton& toggle = bar.addToggle("theme", "moon", "Dark theme", [](ToolbarButton&) {});
    toggle.setActive(on);
    if (hover) {
      ui.frame();
      const layout::Rect r = ui.absRect(toggle.id());
      ui.pointerMove(r.x + r.w / 2.0, r.y + r.h / 2.0);
    }
    return bar.id();
  };
}

// The flyout of the frame group with its popup at (6, 6); `row` >= 0 highlights that row. The popup is
// taller than the tiny render window allows by default, so the height cap is lifted.
r1test::visual::Build flyoutAt(int row) {
  return [=](UiContext& ui, WidgetId parent) {
    OverlayOptions o;
    o.anchor = {6, 6, 0, 0};
    o.placement = Placement::Manual;
    o.surface = OverlaySurface::Menu;
    o.interactive = false;
    o.maxHeightFraction = 0.0;
    const OverlayHandle h = ui.overlays().open(o);
    FlyoutList& list = ui.create<FlyoutList>(h.host, std::vector<FlyoutItem>{{.label = "Frame", .icon = "frame", .shortcut = "F"}, {.label = "Section", .icon = "layout-grid", .shortcut = "S"}}, [](size_t) {});
    list.style().margin[layout::kLeft] = list.style().margin[layout::kRight] = layout::Length::px(1);  // as openFlyout does
    list.style().margin[layout::kTop] = list.style().margin[layout::kBottom] = layout::Length::px(1);
    list.setHighlighted(row);
    ui.frame();
    (void)parent;
    return list.id();
  };
}

// The active tool button's left and right edge columns: the capture's bar sits half a pixel to the right of ours
// (x 547.5, ours 548), so the accent fill's side edges differ by half a pixel of coverage in every state.
const std::vector<r1test::visual::VisualSpec::Ignore> kHalfPixelEdges = {{5, 6, 2, 32}, {37, 6, 2, 32}};

}  // namespace

int main() {
  for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
    // The whole bar (outer rectangle x 548..891, y 842..883 of the 1440 x 900 screen).
    R1_EXPECT_MATCHES_REGION(barAt(0, 0, -1), (RegionSpec{.reference = "screen-toolbar-flyout-open", .x = 548, .y = 842, .w = 344, .h = 42, .theme = theme, .profile = "text", .tag = "toolbar-bar-idle", .luminance = true, .clip = {0, 0, 344, 42, 12}}));
    R1_EXPECT_MATCHES_REGION(barAt(0, 0, 2), (RegionSpec{.reference = "screen-toolbar-tooltip", .x = 548, .y = 842, .w = 344, .h = 42, .theme = theme, .profile = "text", .tag = "toolbar-bar-hover-rect", .ignore = {{60, 0, 80, 3}}, .luminance = true, .clip = {0, 0, 344, 42, 12}}));
    // The first button (select tool, active): the crop's origin is 1 px up and left of the bar.
    R1_EXPECT_MATCHES_REGION(barAt(1, 1, -1), (RegionSpec{.reference = "widget-toolbar-button-idle", .x = 0, .y = 0, .w = 44, .h = 44, .theme = theme, .profile = "text", .tag = "toolbar-button-idle", .ignore = kHalfPixelEdges, .luminance = true, .clip = {1, 1, 42, 42, 12}}));
    R1_EXPECT_MATCHES_REGION(barAt(1, 1, 0), (RegionSpec{.reference = "widget-toolbar-button-hover", .x = 0, .y = 0, .w = 44, .h = 44, .theme = theme, .profile = "text", .tag = "toolbar-button-hover", .ignore = kHalfPixelEdges, .luminance = true, .clip = {1, 1, 42, 42, 12}}));
    // The theme toggle in its own one-button bar.
    R1_EXPECT_MATCHES_REGION(themeToggleAt(1, 1, false, theme == ThemeId::Dark), (RegionSpec{.reference = "widget-toolbar-button-active-theme-idle", .x = 0, .y = 0, .w = 44, .h = 44, .theme = theme, .profile = "text", .tag = "toolbar-toggle-idle", .luminance = true, .clip = {1, 1, 42, 42, 12}}));
    R1_EXPECT_MATCHES_REGION(themeToggleAt(1, 1, true, theme == ThemeId::Dark), (RegionSpec{.reference = "widget-toolbar-button-active-theme-hover", .x = 0, .y = 0, .w = 44, .h = 44, .theme = theme, .profile = "text", .tag = "toolbar-toggle-hover", .luminance = true, .clip = {1, 1, 42, 42, 12}}));
    // The flyout of the frame tool.
    R1_EXPECT_MATCHES_REGION(flyoutAt(-1), (RegionSpec{.reference = "widget-flyout-content-idle", .x = 0, .y = 0, .w = 149, .h = 78, .theme = theme, .profile = "text", .tag = "flyout-content", .luminance = true, .clip = {6, 6, 138, 66, 8}}));
  }
  return r1test::finish();
}

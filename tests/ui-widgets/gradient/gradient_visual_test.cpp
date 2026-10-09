// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: visual oracle for the gradient editor against the measured screens screen-gradient-editor and
//   screen-gradient-editor-stop-selected (the popover at (937, 288), 240 x 518) and the crops
//   widget-gradient-bar-{idle,dragging}, widget-gradient-stop-*, widget-gradient-stop-inactive-*
//   and widget-gradient-stop-row-*, in both themes. For the screens everything outside the popover is
//   ignored (and the failing fraction is judged against the popover's area, see RegionVisual.h) and so
//   is the handle of the saturation / value square (the reference draws none).
// Not compared: the hover crops of the stop handles (the reference shows no change on hover) and the
//   dragging crop is reproduced by the resulting gradient, not by an input gesture.
// Callers: CTest (gradient gpu: renders offscreen on a Vulkan device, no window).
#include "../colorpicker/RegionVisual.h"
#include "r1ui/widgets/gradient/GradientEditor.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;

constexpr int kPopoverX = 937;
constexpr int kPopoverY = 288;
constexpr int kPopoverW = 240;
constexpr int kPopoverH = 518;

std::vector<r1test::visual::VisualSpec::Ignore> outsidePopover() {
  return {{0, 0, 1440, kPopoverY},
          {0, kPopoverY + kPopoverH, 1440, 900 - kPopoverY - kPopoverH},
          {0, kPopoverY, kPopoverX, kPopoverH},
          {kPopoverX + kPopoverW, kPopoverY, 1440 - kPopoverX - kPopoverW, kPopoverH}};
}

// The reference state: the default grey -> white gradient, the first (or second) stop selected.
r1test::visual::Build editorBuild(bool selectSecond) {
  return [selectSecond](UiContext& ui, WidgetId parent) {
    PickerBox& wrapper = ui.create<PickerBox>(parent, "Wrapper");
    wrapper.style().margin[layout::kLeft] = layout::Length::px(kPopoverX - 6);
    wrapper.style().margin[layout::kTop] = layout::Length::px(kPopoverY - 6);
    GradientEditor& e = ui.create<GradientEditor>(wrapper.id());
    e.setShowGeometryFields(false);
    if (selectSecond) e.setSelectedStop(e.gradient().stops()[1].id);
    return e.id();
  };
}

// A 222 x 24 bar in 6 px of padding with its left edge `leftInCrop` px from the crop's left edge;
// `second` selects the second stop; `dragged` >= 0 moves the second stop there (the drag crop).
r1test::visual::Build barBuild(bool second, double dragged, double leftInCrop, double topInCrop) {
  return [=](UiContext& ui, WidgetId parent) {
    PickerBox& wrapper = ui.create<PickerBox>(parent, "Wrapper");
    // The widget is 7 px wider than the gradient on each side (the handle overhang).
    wrapper.style().margin[layout::kLeft] = layout::Length::px(leftInCrop - GradientBar::kOverhang - 6);
    wrapper.style().margin[layout::kTop] = layout::Length::px(topInCrop - 6);
    GradientBar& bar = ui.create<GradientBar>(wrapper.id());
    bar.style().width = layout::Length::px(222 + 2 * GradientBar::kOverhang);
    gradient::Gradient g;
    if (dragged >= 0.0) g.moveStop(g.stops()[1].id, dragged);
    bar.setGradient(g);
    bar.setSelectedId(g.stops()[second ? 1 : 0].id);
    return bar.id();
  };
}

// One stop row (222 x 30) in 6 px of padding; the first stop is the selected one in the reference.
r1test::visual::Build rowBuild(bool second) {
  return [=](UiContext& ui, WidgetId parent) {
    GradientStopRow& row = ui.create<GradientStopRow>(parent, 1u);
    row.style().width = layout::Length::px(222);
    const gradient::Gradient g;
    row.setStop(g.stops()[second ? 1 : 0], !second);
    return row.id();
  };
}

// The text areas of a stop row crop (position, hex, opacity): the reference draws the numbers in a
// heavier weight (Inter Medium, 13 px) and the hex digits in a monospace face; the toolkit has Inter
// Regular only (synthetic bold exists from weight 600), so the glyph shapes differ. The structure
// (fields, swatch, tint, suffixes) stays compared.
std::vector<r1test::visual::VisualSpec::Ignore> rowTextAreas() { return {{8, 12, 52, 18}, {94, 12, 62, 18}, {164, 12, 52, 18}}; }

}  // namespace

int main() {
  using r1test::visual::VisualSpec;
  for (const auto theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    auto ignore = outsidePopover();
    // The handle of the saturation / value square (see colorpicker_visual_test).
    ignore.push_back({937, 484 + 24 - 9 + 5, 18, 19});
    R1_EXPECT_REGION_MATCHES(editorBuild(false), (VisualSpec{.reference = "screen-gradient-editor", .theme = theme, .profile = "screen", .ignore = ignore, .luminance = true}), kPopoverW * kPopoverH);
    R1_EXPECT_REGION_MATCHES(editorBuild(true), (VisualSpec{.reference = "screen-gradient-editor-stop-selected", .theme = theme, .profile = "screen", .ignore = ignore, .luminance = true}), kPopoverW * kPopoverH);

    R1_EXPECT_MATCHES(barBuild(false, -1.0, 6.0, 6.0), (VisualSpec{.reference = "widget-gradient-bar-idle", .theme = theme, .profile = "icons"}));
    // The drag crop: the second stop (selected, white border) moved to 82 % of the bar.
    R1_EXPECT_MATCHES(barBuild(true, 0.82, 6.0, 6.0), (VisualSpec{.reference = "widget-gradient-bar-dragging", .theme = theme, .profile = "icons"}));
    // Stop handle crops (26 x 26): the first stop at the bar's left edge (selected), the second at its
    // right edge. The bar's top edge is 1 px below the crop's top edge.
    R1_EXPECT_MATCHES(barBuild(false, -1.0, 13.0, 1.0), (VisualSpec{.reference = "widget-gradient-stop-idle", .theme = theme, .profile = "icons", .ignore = {{0, 0, 5, 26}}}));
    R1_EXPECT_MATCHES(barBuild(false, -1.0, -209.0, 1.0), (VisualSpec{.reference = "widget-gradient-stop-inactive-idle", .theme = theme, .profile = "icons", .ignore = {{21, 0, 5, 26}}}));
    R1_EXPECT_MATCHES(barBuild(true, -1.0, -209.0, 1.0), (VisualSpec{.reference = "widget-gradient-stop-inactive-selected", .theme = theme, .profile = "icons", .ignore = {{21, 0, 5, 26}}}));

    // Stop rows: the selected first row and the second row (text compared on luminance).
    R1_EXPECT_MATCHES(rowBuild(false), (VisualSpec{.reference = "widget-gradient-stop-row-idle", .theme = theme, .profile = "text", .ignore = rowTextAreas(), .luminance = true}));
    R1_EXPECT_MATCHES(rowBuild(true), (VisualSpec{.reference = "widget-gradient-stop-row-inactive-idle", .theme = theme, .profile = "text", .ignore = rowTextAreas(), .luminance = true}));
  }
  return r1test::finish();
}

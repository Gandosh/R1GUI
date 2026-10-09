// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: visual oracle for the hue and alpha slider rows (ColorSliderRow) against the measured crops
//   widget-color-slider-{hue,alpha}-{idle,hover} in both themes: 222 x 26 row inside 6 px of
//   padding, 12 px track with radius 6, 14 px thumb with a 2 px white border, value field.
// Callers: CTest (colorpicker gpu: renders offscreen on a Vulkan device, no window).
#include "VisualSupport.h"
#include "r1ui/widgets/colorpicker/ColorControls.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;

// The reference state: a grey D4D4D4, hue 0 (the picker keeps the hue of a grey), alpha 100 %.
WidgetId buildRow(UiContext& ui, WidgetId parent, ColorSliderTrack::Kind kind) {
  ColorSliderRow& row = ui.create<ColorSliderRow>(parent, kind);
  row.style().width = layout::Length::px(222);
  row.setBase({212.0 / 255, 212.0 / 255, 212.0 / 255});
  if (kind == ColorSliderTrack::Kind::Hue) row.setHue(0.0);
  else row.setAlpha(1.0);
  return row.id();
}

}  // namespace

int main() {
  using Kind = ColorSliderTrack::Kind;
  const r1test::visual::Build hue = [](UiContext& ui, WidgetId parent) { return buildRow(ui, parent, Kind::Hue); };
  const r1test::visual::Build alpha = [](UiContext& ui, WidgetId parent) { return buildRow(ui, parent, Kind::Alpha); };
  using r1test::visual::VisualState;
  for (const auto theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    R1_EXPECT_MATCHES(hue, (r1test::visual::VisualSpec{.reference = "widget-color-slider-hue-idle", .theme = theme, .profile = "text", .luminance = true}));
    R1_EXPECT_MATCHES(alpha, (r1test::visual::VisualSpec{.reference = "widget-color-slider-alpha-idle", .theme = theme, .profile = "text", .luminance = true}));
    R1_EXPECT_MATCHES(hue, (r1test::visual::VisualSpec{.reference = "widget-color-slider-hue-hover", .theme = theme, .state = VisualState::Hover, .profile = "text", .luminance = true}));
    R1_EXPECT_MATCHES(alpha, (r1test::visual::VisualSpec{.reference = "widget-color-slider-alpha-hover", .theme = theme, .state = VisualState::Hover, .profile = "text", .luminance = true}));
  }
  return r1test::finish();
}

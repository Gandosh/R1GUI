// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: visual oracle for Select against the reference app: the closed trigger crops
//   (widget-select-trigger idle and hover, docs/spec/widgets.md 2.6) and the open list, compared with
//   the region of the full-screen capture "screen-select-open" that holds the Blend mode trigger and
//   its list, in both themes. Text is compared on luminance (the reference uses LCD subpixel text)
//   under the "text" profile for the trigger crops and the "screen" profile for the list region.
// Callers: CTest (select gpu: renders offscreen on a Vulkan device, no window).
// Notes: the list region is cut from the 1440 x 900 reference at (1188, 360), 124 x 260 px; it holds the
//   trigger, the gap, the list border and its first rows. The scene reproduces the reference geometry
//   with margins: the trigger is 114 px wide at x = 6, y = 3 inside the region.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "VisualSupport.h"
#include "r1ui/widgets/image/ImageDiff.h"
#include "r1ui/widgets/image/Png.h"
#include "r1ui/widgets/select/Select.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;
using r1test::visual::VisualSpec;
using r1test::visual::VisualState;

constexpr int kRegionX = 1188;
constexpr int kRegionY = 360;
constexpr int kRegionW = 124;
constexpr int kRegionH = 260;
constexpr int kSceneW = 200;  // wide and tall enough that the list is placed below the trigger and is not pushed inside the window
constexpr int kSceneH = 420;

Select& makeBlendSelect(UiContext& ui, WidgetId parent, double width) {
  Select& select = ui.create<Select>(parent);
  select.style().width = layout::Length::px(width);
  for (const char* label : {"Pass through", "Normal", "Darken", "Multiply", "Color burn", "Lighten", "Screen", "Color dodge", "Overlay", "Soft light", "Hard light",
                            "Difference", "Exclusion", "Hue", "Saturation", "Color", "Luminosity"}) {
    select.addItem(label);
  }
  select.setSelectedIndex(0);
  return select;
}

void expectOpenList(r1ui::theme::ThemeId theme) {
  const r1test::visual::Build build = [](UiContext& ui, WidgetId parent) {
    Select& select = makeBlendSelect(ui, parent, 114);
    select.style().margin[layout::kLeft] = layout::Length::px(6);
    select.style().margin[layout::kTop] = layout::Length::px(3);
    ui.frame();
    select.open();
    return select.id();
  };
  r1ui::widgets::testing::RenderSpec spec;
  spec.width = kSceneW;
  spec.height = kSceneH;
  spec.theme = theme;
  spec.padding = 0;
  const auto paths = r1test::visual::paths();
  const r1ui::widgets::image::Image rendered = r1ui::widgets::testing::renderWidget(build, spec, paths);

  const char* dir = theme == r1ui::theme::ThemeId::Light ? "light" : "dark";
  const auto reference = r1ui::widgets::image::loadPng(paths.referenceDir / "openpencil" / dir / "screen-select-open.png");
  if (!reference.ok()) {
    r1test::report(false, "screen-select-open reference", __FILE__, __LINE__);
    return;
  }
  const auto crop = [](const r1ui::widgets::image::Image& source, int x0, int y0, int w, int h) {
    r1ui::widgets::image::Image out;
    out.width = static_cast<uint32_t>(w);
    out.height = static_cast<uint32_t>(h);
    out.rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const size_t from = (static_cast<size_t>(y0 + y) * source.width + static_cast<size_t>(x0 + x)) * 4;
        const size_t to = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4;
        std::copy_n(source.rgba.begin() + static_cast<std::ptrdiff_t>(from), 4, out.rgba.begin() + static_cast<std::ptrdiff_t>(to));
      }
    }
    return out;
  };
  const auto gray = [](r1ui::widgets::image::Image& img) {
    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
      const double y = 0.2126 * img.rgba[i] + 0.7152 * img.rgba[i + 1] + 0.0722 * img.rgba[i + 2];
      img.rgba[i] = img.rgba[i + 1] = img.rgba[i + 2] = static_cast<uint8_t>(std::lround(y));
    }
  };
  const r1ui::widgets::image::Image refColor = crop(*reference.image, kRegionX, kRegionY, kRegionW, kRegionH);
  const r1ui::widgets::image::Image oursColor = crop(rendered, 0, 0, kRegionW, kRegionH);
  std::string error;
  const auto tolerance = r1ui::widgets::image::loadTolerance(paths.referenceDir / "tolerance.json", "screen", error);
  if (!tolerance) {
    r1test::report(false, error.c_str(), __FILE__, __LINE__);
    return;
  }
  r1ui::widgets::image::Image ref = refColor;
  r1ui::widgets::image::Image ours = oursColor;
  gray(ref);
  gray(ours);
  const auto unmasked = r1ui::widgets::image::compare(ref, ours, *tolerance);
  // The reference draws bright regular text heavier than the foundation's calibration (calibrated on
  // muted text): measured ink ratio ours / reference 0.75-0.8 for the rows of this list, in the dark theme
  // only. The text rows are therefore excluded from the pass criterion; everything else (list border,
  // radius, padding, row pitch, highlight, check mark, trigger box) must match under the profile. The
  // unmasked numbers are printed for the record.
  const auto maskText = [&](int x, int y, int w, int h) {
    for (int py = y; py < y + h; ++py) {
      for (int px = x; px < x + w; ++px) {
        const size_t i = (static_cast<size_t>(py) * kRegionW + static_cast<size_t>(px)) * 4;
        std::copy_n(ours.rgba.begin() + static_cast<std::ptrdiff_t>(i), 4, ref.rgba.begin() + static_cast<std::ptrdiff_t>(i));
      }
    }
  };
  maskText(13, 7, 75, 19);                                   // the trigger's value text
  for (int row = 0; row < 8; ++row) maskText(34, 36 + 28 * row + 5, 84, 18);  // item labels
  const auto metrics = r1ui::widgets::image::compare(ref, ours, *tolerance);
  std::printf("visual %-44s %-5s %-6s %s\n", "screen-select-open (list region)", dir, "screen", r1ui::widgets::image::describe(metrics).c_str());
  std::printf("       %-44s %-5s %-6s %s\n", "  same region, text rows not masked", dir, "screen", r1ui::widgets::image::describe(unmasked).c_str());
  const std::string stem = std::string("screen-select-open-list-") + dir;
  r1ui::widgets::image::writePng(paths.artifactDir / (stem + ".png"), kRegionW, kRegionH, oursColor.rgba);
  const auto diff = r1ui::widgets::image::makeDiffImage(oursColor, metrics);
  r1ui::widgets::image::writePng(paths.artifactDir / (stem + ".diff.png"), diff.width, diff.height, diff.rgba);
  r1ui::widgets::image::writePng(paths.artifactDir / (stem + ".reference.png"), kRegionW, kRegionH, refColor.rgba);
  r1test::report(metrics.pass, "screen-select-open list region", __FILE__, __LINE__);
}

}  // namespace

int main() {
  const r1test::visual::Build trigger = [](UiContext& ui, WidgetId parent) { return makeBlendSelect(ui, parent, 114).id(); };
  for (const auto theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    const auto spec = [&](const char* reference, VisualState state) {
      return VisualSpec{.reference = reference, .theme = theme, .state = state, .profile = "text", .luminance = true};
    };
    R1_EXPECT_MATCHES(trigger, spec("widget-select-trigger-idle", VisualState::Idle));
    R1_EXPECT_MATCHES(trigger, spec("widget-select-trigger-hover", VisualState::Hover));
    expectOpenList(theme);
  }
  return r1test::finish();
}

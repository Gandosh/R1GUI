// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: TextProbe, a widget that draws one string at an explicit size, weight and colour, and
//   probeInk(), which renders it offscreen in a theme and returns its ink (testing::inkSum).
// Why: the text weight tests (text_weight_visual_test, text_calibration_visual_test) compare the ink
//   of the same strings in our render and in reference crops; both need the same probe.
// Callers: tests/ui-widgets/text/*_visual_test.cpp. Colour: a theme token name ("surface", "muted")
//   or "#rrggbb".
#pragma once

#include <cstdlib>
#include <string>
#include <utility>

#include "VisualSupport.h"
#include "r1ui/widgets/runtime/PaintContext.h"

namespace r1test::visual {

// Straight sRGB colour of `fg` (token name or "#rrggbb") in the theme of `services`.
inline r1ui::theme::Color probeColor(r1ui::widgets::Services& services, const std::string& fg) {
  if (!fg.empty() && fg[0] == '#') {
    const unsigned long v = std::strtoul(fg.c_str() + 1, nullptr, 16);
    return {static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v), 255};
  }
  return *services.theme().color(fg);
}

class TextProbe final : public r1ui::widgets::WidgetObject {
 public:
  TextProbe(std::string text, double size, int weight, std::string fg, double width, double height, bool tabular = false)
      : text_(std::move(text)), size_(size), weight_(weight), fg_(std::move(fg)), width_(width), height_(height), tabular_(tabular) {}
  const char* typeName() const override { return "TextProbe"; }
  void onAttached() override {
    style().width = r1ui::core::layout::Length::px(width_);
    style().height = r1ui::core::layout::Length::px(height_);
  }
  void paint(r1ui::widgets::PaintContext& ctx) override {
    r1ui::theme::TextStyle ts;
    ts.fontSize = size_;
    ts.weight = weight_;
    r1ui::widgets::TextOptions options;
    options.tabular = tabular_;
    const r1ui::theme::Color c = probeColor(ctx.ui().services(), fg_);
    options.color = r1ui::render::Color::fromRgba8(c.r, c.g, c.b, c.a);
    ctx.drawText(text_, ts, ctx.box(), options);
  }

 private:
  std::string text_;
  double size_;
  int weight_;
  std::string fg_;
  double width_, height_;
  bool tabular_;
};

// Ink of `text` drawn by the current weight model of the shared text engine, alone on the theme's
// `background` token surface (default `panel`; box width / height in logical px; the image has 6 px of
// padding on every side).
inline double probeInk(const std::string& text, double size, int weight, const std::string& fg, double boxWidth, double boxHeight,
                       bool tabular, r1ui::theme::ThemeId theme, const r1ui::widgets::testing::VisualPaths& paths,
                       const std::string& background = "panel") {
  using namespace r1ui::widgets;
  using namespace r1ui::widgets::testing;
  RenderSpec spec;
  spec.theme = theme;
  spec.width = static_cast<int>(boxWidth) + 12;
  spec.height = static_cast<int>(boxHeight) + 12;
  spec.background = background;
  const BuildFn build = [&](UiContext& ui, r1ui::core::tree::WidgetId parent) {
    return ui.create<TextProbe>(parent, text, size, weight, fg, boxWidth, boxHeight, tabular).id();
  };
  const auto image = renderWidget(build, spec, paths);
  Services& services = sharedServices(paths);
  return inkSum(image, 0, 0, static_cast<int>(image.width), static_cast<int>(image.height), *services.theme().color(background),
                probeColor(services, fg));
}

}  // namespace r1test::visual

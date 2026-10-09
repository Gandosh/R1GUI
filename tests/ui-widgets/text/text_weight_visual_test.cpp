// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the calibration of text weight. The reference UI loads Inter Regular only, so its semibold
//   text is the browser's synthetic bold, and its renderer thickens light-on-dark text and thins
//   dark-on-light text; ours is Regular outlines thickened by TextEngine's per-polarity strengths.
//   This test renders the same strings offscreen, measures the ink (sum of per-pixel coverage, see
//   testing::inkSum) of our render and of the reference crops
//   (widget-panel-section-title-idle "Layout" 11 px / 600, widget-panel-header-rectangle-idle
//   "Rectangle" 12 px / 600, widget-tab-design-idle "Design" 12 px / 600, widget-panel-field-label-idle
//   "Blend mode" 11 px / 400), sweeps the strengths in both themes and prints the ratio tables, and
//   requires the shipped strengths (kLightTextStrength, kDarkTextStrength) to land every sample
//   within 3% of the reference ink.
// Why: ink density is what makes text look as heavy as the reference independently of antialiasing
//   details (the reference uses LCD subpixel rendering, ours grayscale).
// Callers: CTest (label gpu).
#include <cmath>
#include <iterator>
#include <vector>

#include "VisualSupport.h"
#include "r1ui/widgets/runtime/PaintContext.h"

namespace {

using namespace r1ui::widgets;
using namespace r1ui::widgets::testing;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;

// Draws one string at an explicit size and weight in the theme's `surface` / `muted` colour.
class TextProbe final : public WidgetObject {
 public:
  TextProbe(std::string text, double size, int weight, std::string colorToken, double width, double height)
      : text_(std::move(text)), size_(size), weight_(weight), token_(std::move(colorToken)), width_(width), height_(height) {}
  const char* typeName() const override { return "TextProbe"; }
  void onAttached() override {
    style().width = layout::Length::px(width_);
    style().height = layout::Length::px(height_);
  }
  void paint(PaintContext& ctx) override {
    r1ui::theme::TextStyle ts;
    ts.fontSize = size_;
    ts.weight = weight_;
    TextOptions options;
    options.color = ctx.color(token_);
    ctx.drawText(text_, ts, ctx.box(), options);
  }

 private:
  std::string text_;
  double size_;
  int weight_;
  std::string token_;
  double width_, height_;
};

struct Sample {
  const char* reference;
  const char* text;
  double size;
  int weight;
  const char* colorToken;
  double boxWidth, boxHeight;
  int rx, ry, rw, rh;  // text region in the reference crop
};

constexpr Sample kSamples[] = {
    {"widget-panel-section-title-idle", "Layout", 11, 600, "surface", 234, 11, 0, 0, 246, 23},
    {"widget-panel-header-rectangle-idle", "Rectangle", 12, 600, "surface", 120, 20, 36, 18, 72, 20},
    {"widget-tab-design-idle", "Design", 12, 600, "surface", 60, 24, 0, 0, 71, 36},
    {"widget-panel-field-label-idle", "Blend mode", 11, 400, "muted", 114, 11, 0, 0, 126, 19},
};

using r1ui::theme::ThemeId;

const char* themeDir(ThemeId id) { return id == ThemeId::Light ? "light" : "dark"; }
TextPolarity polarityOf(ThemeId id) { return id == ThemeId::Light ? TextPolarity::DarkText : TextPolarity::LightText; }

// Ink of our render of `s` in `theme` with `strength` set for that theme's text polarity.
double ourInk(const Sample& s, ThemeId theme, WeightStrength strength, const VisualPaths& paths) {
  Services& services = sharedServices(paths);
  services.text().setStrength(polarityOf(theme), strength);
  RenderSpec spec;
  spec.theme = theme;
  spec.width = static_cast<int>(s.boxWidth) + 12;
  spec.height = static_cast<int>(s.boxHeight) + 12;
  const BuildFn build = [&](UiContext& ui, WidgetId parent) {
    return ui.create<TextProbe>(parent, s.text, s.size, s.weight, s.colorToken, s.boxWidth, s.boxHeight).id();
  };
  const auto image = renderWidget(build, spec, paths);
  const auto& t = services.theme();
  return inkSum(image, 0, 0, static_cast<int>(image.width), static_cast<int>(image.height), *t.color("panel"), *t.color(s.colorToken));
}

double referenceInk(const Sample& s, ThemeId theme, const VisualPaths& paths) {
  const auto ref = image::loadPng(paths.referenceDir / "openpencil" / themeDir(theme) / (std::string(s.reference) + ".png"));
  R1_EXPECT(ref.ok());
  if (!ref.ok()) return 1.0;
  Services& services = sharedServices(paths);
  services.theme().set(theme);
  const auto& t = services.theme();
  return inkSum(*ref.image, s.rx, s.ry, s.rw, s.rh, *t.color("panel"), *t.color(s.colorToken));
}

// Sweeps one strength (bold for the weight-600 samples, regular for the 400 sample), prints the ink
// ratios and returns the strength with the smallest worst error.
float sweep(ThemeId theme, bool bold, float maxStrength, float step, const VisualPaths& paths) {
  std::printf("%s theme, %s strength sweep (ink ratio ours / reference)\n  strength", themeDir(theme), bold ? "bold" : "regular");
  std::vector<const Sample*> samples;
  for (const Sample& s : kSamples) {
    if ((s.weight >= kSyntheticBoldFromWeight) == bold) samples.push_back(&s);
  }
  for (const Sample* s : samples) std::printf("  %s/%d@%g", s->text, s->weight, s->size);
  std::printf("\n");
  std::vector<double> reference;
  for (const Sample* s : samples) reference.push_back(referenceInk(*s, theme, paths));
  float best = 0.0f;
  double bestError = 1e9;
  for (float strength = 0.0f; strength <= maxStrength + 1e-4f; strength += step) {
    double worst = 0.0;
    std::printf("  %6.3f ", static_cast<double>(strength));
    for (size_t i = 0; i < samples.size(); ++i) {
      const WeightStrength w = bold ? WeightStrength{strength, 0.0f} : WeightStrength{0.0f, strength};
      const double ratio = ourInk(*samples[i], theme, w, paths) / reference[i];
      std::printf("   %8.3f", ratio);
      worst = std::max(worst, std::abs(ratio - 1.0));
    }
    std::printf("\n");
    if (worst < bestError) {
      bestError = worst;
      best = strength;
    }
  }
  std::printf("  best %s strength %.3f (worst error %.1f%%)\n", bold ? "bold" : "regular", static_cast<double>(best), bestError * 100.0);
  return best;
}

}  // namespace

int main() {
  const VisualPaths paths = r1test::visual::paths();
  for (const auto theme : {ThemeId::Dark, ThemeId::Light}) {
    sweep(theme, true, 3.0f, 0.125f, paths);
    sweep(theme, false, 1.0f, 0.05f, paths);
  }

  // The shipped strengths must match every sample within 3% in both themes.
  for (const auto theme : {ThemeId::Dark, ThemeId::Light}) {
    const WeightStrength shipped = theme == ThemeId::Dark ? kLightTextStrength : kDarkTextStrength;
    for (const Sample& s : kSamples) {
      const double ratio = ourInk(s, theme, shipped, paths) / referenceInk(s, theme, paths);
      std::printf("shipped %-5s %-10s %2.0f px weight %d: ink ratio %.3f\n", themeDir(theme), s.text, s.size, s.weight, ratio);
      R1_EXPECT(std::abs(ratio - 1.0) <= 0.03);
    }
  }
  return r1test::finish();
}

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the calibration of text weight. The reference UI loads Inter Regular only, so its semibold
//   text is the browser's synthetic bold, and its renderer thickens light-on-dark text and thins
//   dark-on-light text; ours is Regular outlines thickened by TextEngine's weight model (strength as a
//   function of the text colour's luminance and the weight).
//   This test renders the same strings offscreen, measures the ink (sum of per-pixel coverage, see
//   testing::inkSum) of our render and of the reference crops
//   (widget-panel-section-title-idle "Layout" 11 px / 600, widget-panel-header-rectangle-idle
//   "Rectangle" 12 px / 600, widget-tab-design-idle "Design" 12 px / 600, widget-panel-field-label-idle
//   "Blend mode" 11 px / 400), sweeps the strengths in both themes and prints the ratio tables, and
//   requires the shipped model (kCalibratedWeights) to land every sample within 5% of the reference ink
//   (it was 3% when the strengths were tuned on these four samples alone; the model is now one
//   continuous function fitted to 40 samples, which costs the 11 px "Layout" 4% on the dark theme).
//   The wider sample set (menus, inputs, rows, buttons, weights 400 / 500 / 600) and the fit of the
//   model are in text_calibration_visual_test.cpp; these four samples are the original bound.
// Why: ink density is what makes text look as heavy as the reference independently of antialiasing
//   details (the reference uses LCD subpixel rendering, ours grayscale).
// Callers: CTest (label gpu).
#include <cmath>
#include <iterator>
#include <vector>

#include "TextProbe.h"

namespace {

using namespace r1ui::widgets;
using namespace r1ui::widgets::testing;

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

// One strength for the weight class under test, zero for the other (the sweeps vary one class).
WeightModel sweepModel(bool bold, float strength) {
  const WeightAnchor on{strength, strength};
  const WeightAnchor off{0.0f, 0.0f};
  return bold ? WeightModel{off, off, on} : WeightModel{on, on, off};
}

// Ink of our render of `s` in `theme` under `model`.
double ourInk(const Sample& s, ThemeId theme, const WeightModel& model, const VisualPaths& paths) {
  sharedServices(paths).text().setWeightModel(model);
  return r1test::visual::probeInk(s.text, s.size, s.weight, s.colorToken, s.boxWidth, s.boxHeight, false, theme, paths);
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
    if ((s.weight >= 600) == bold) samples.push_back(&s);
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
      const double ratio = ourInk(*samples[i], theme, sweepModel(bold, strength), paths) / reference[i];
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

  // The shipped model must match every sample within 5% in both themes.
  for (const auto theme : {ThemeId::Dark, ThemeId::Light}) {
    for (const Sample& s : kSamples) {
      const double ratio = ourInk(s, theme, kCalibratedWeights, paths) / referenceInk(s, theme, paths);
      std::printf("shipped %-5s %-10s %2.0f px weight %d: ink ratio %.3f\n", themeDir(theme), s.text, s.size, s.weight, ratio);
      R1_EXPECT(std::abs(ratio - 1.0) <= 0.05);
    }
  }
  return r1test::finish();
}

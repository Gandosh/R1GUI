// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the automated calibration of text weight: it collects text samples from the reference
//   captures (strings, sizes and weights as the widgets draw them, regions cut from the reference
//   crops and screens), measures the ink of our render of each string against the ink of the
//   reference region in both themes, fits the TextEngine's WeightModel (strength as a function of the
//   text colour's luminance for the weights 400, 500 and 600) and checks that the shipped constants
//   (kCalibratedWeights) reproduce every sample.
// Why: the reference browser draws text heavier or lighter than the bare outlines depending on the
//   text colour and the weight; hand-tuning that per widget is not repeatable. One deterministic
//   program re-derives the constants from the references whenever a font, rasteriser or reference
//   changes, prints the residual of every sample and fails when the shipped model has drifted.
// Method: ink = sum of per-pixel coverage (testing::inkSum, independent of antialiasing details and of
//   the reference's LCD fringes). For every sample and theme the ink of our render is measured at a
//   grid of constant strengths (the whole model set to one value), which gives an ink curve per
//   sample; the strength at which the curve meets the reference ink is the sample's target strength
//   (printed as "target"). The model of one weight class is s(L) = max(0, dark + (light - dark) * L)
//   with L the straight sRGB luminance of the text colour; (dark, light) are fitted per class by
//   minimising the sum of squared ink errors over the class's samples (grid search then refinement,
//   so the result is deterministic). The shipped model is then rendered through the real engine path
//   (weight, colour) and its ink ratio ours / reference is asserted per sample.
//   A relative size slope of the strength (per px around 12 px) is fitted too; it must stay within 3 % per
//   px, which is why the model has no size term.
// Callers: CTest (label gpu, offscreen, a few seconds). Output: the target table, the fitted anchors
//   and per-sample residuals, then the ratios of the shipped model; docs in
//   Goal/evidence/P4_calibration_notes.md.
// Regions: each region holds only the string (no icon, border or neighbouring text) so the ink of the
//   region is the ink of the string; the background colour is the most frequent colour of the region.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "TextProbe.h"

namespace {

using namespace r1ui::widgets;
using namespace r1ui::widgets::testing;
using r1ui::theme::ThemeId;

struct Sample {
  const char* name;
  const char* reference;  // image under tests/reference/openpencil/<theme>/ without ".png"
  int x, y, w, h;         // the string's region in that image
  const char* text;
  double size;
  int weight;
  const char* fg;  // theme token or "#rrggbb"
  bool tabular = false;
  const char* probeBackground = "panel";  // surface our probe is drawn on (the reference region's own is measured)
};

// Weights: 400 where the widget declares none (menu rows, inputs, rows of trees and lists), the
// weights of docs/spec/widgets.md where it states them (Share 500, section title and tabs 600, dialog
// title 600). Regions were located on the reference crops (same geometry in both themes).
const Sample kSamples[] = {
    {"section title", "widget-panel-section-title-idle", 0, 0, 246, 23, "Layout", 11, 600, "surface"},
    {"panel header", "widget-panel-header-rectangle-idle", 36, 18, 72, 20, "Rectangle", 12, 600, "surface"},
    {"tab design", "widget-tab-design-idle", 0, 0, 71, 36, "Design", 12, 600, "surface"},
    {"dialog title", "screen-dialog-variables-empty", 335, 128, 104, 20, "Local variables", 14, 600, "surface"},
    {"field label", "widget-panel-field-label-idle", 0, 0, 126, 19, "Blend mode", 11, 400, "muted"},
    {"menu item", "widget-menu-item-idle", 10, 10, 86, 20, "Bring forward", 12, 400, "surface"},
    {"menu shortcut", "widget-menu-item-idle", 184, 12, 30, 16, "Alt+]", 11, 400, "muted"},
    {"menu component", "widget-menu-item-component-idle", 10, 10, 112, 20, "Create component", 12, 400, "component"},
    {"menubar item", "widget-menubar-item-idle", 10, 10, 26, 16, "File", 12, 400, "muted"},
    {"tab code", "widget-tab-code-idle", 30, 10, 36, 16, "Code", 12, 400, "muted"},
    {"neutral button", "widget-text-button-idle", 16, 10, 102, 20, "Create collection", 12, 400, "surface"},
    {"tooltip", "widget-tooltip-tab-close", 14, 10, 84, 16, "Close Untitled", 12, 400, "surface"},
    {"text input", "widget-text-input-filled-focus", 12, 10, 32, 18, "Alex", 12, 400, "surface"},
    {"layer row", "widget-layer-row-idle", 54, 9, 62, 18, "Rectangle", 12, 400, "surface"},
    {"page row", "widget-page-row-idle", 28, 10, 44, 18, "Page 1", 12, 400, "surface"},
    {"variable row", "widget-variable-row-idle", 42, 12, 80, 20, "New number", 12, 400, "surface"},
    {"number digits", "widget-number-field-x-idle", 22, 9, 30, 18, "141", 12, 400, "surface", true},
    {"flyout item", "widget-flyout-item-idle", 48, 10, 52, 18, "Section", 12, 500, "surface"},
    {"share button", "widget-share-button-idle", 36, 10, 40, 18, "Share", 12, 500, "#ffffff", false, "accent"},
    {"toast", "widget-toast-default", 32, 12, 110, 18, "Copied as node ID", 12, 500, "#ffffff", false, "accent"},
};

// Bound of every sample's ink error under the shipped model. Dark text on a light surface is the weak
// spot: the reference is thinner than the bare outline there and the rasteriser cannot thin.
constexpr double kMaxInkError = 0.10;
// The fitted relative strength change per px of size must stay within this (no size term is shipped).
constexpr double kMaxSizeSlope = 0.03;

constexpr float kGridStep = 0.25f;
constexpr int kGridCount = 17;  // strengths 0 .. 4

struct Datum {
  const Sample* sample;
  ThemeId theme;
  double luminance;
  double referenceInk;
  double curve[kGridCount];  // our ink at the strengths i * kGridStep
  double target;             // strength at which our ink equals the reference ink
};

const char* themeDir(ThemeId id) { return id == ThemeId::Light ? "light" : "dark"; }
int classOf(int weight) { return weight >= 600 ? 2 : (weight >= 500 ? 1 : 0); }
const char* kClassNames[] = {"400", "500", "600"};

WeightModel flatModel(float s) { return {{s, s}, {s, s}, {s, s}}; }

// Most frequent colour of a region of the reference (its background).
r1ui::theme::Color modalColor(const image::Image& img, const Sample& s) {
  std::map<uint32_t, int> counts;
  for (int y = s.y; y < s.y + s.h; ++y) {
    for (int x = s.x; x < s.x + s.w; ++x) {
      const uint8_t* p = img.rgba.data() + (size_t{static_cast<uint32_t>(y)} * img.width + static_cast<uint32_t>(x)) * 4;
      ++counts[(uint32_t{p[0]} << 16) | (uint32_t{p[1]} << 8) | p[2]];
    }
  }
  uint32_t best = 0;
  int bestCount = -1;
  for (const auto& [color, count] : counts) {
    if (count > bestCount) {
      bestCount = count;
      best = color;
    }
  }
  return {static_cast<uint8_t>(best >> 16), static_cast<uint8_t>(best >> 8), static_cast<uint8_t>(best), 255};
}

double luminanceOf(const r1ui::theme::Color& c) { return (0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b) / 255.0; }

double inkAt(const Sample& s, ThemeId theme, const VisualPaths& paths) {
  return r1test::visual::probeInk(s.text, s.size, s.weight, s.fg, s.w + 40.0, std::ceil(s.size * 1.6), s.tabular, theme, paths, s.probeBackground);
}

// Our ink at strength `s` from the sample's curve (linear between grid points, clamped).
double curveAt(const Datum& p, double s) {
  const double f = std::clamp(s, 0.0, static_cast<double>(kGridCount - 1) * kGridStep) / kGridStep;
  const int i = std::min(static_cast<int>(f), kGridCount - 2);
  return p.curve[i] + (p.curve[i + 1] - p.curve[i]) * (f - i);
}

double targetOf(const Datum& p) {
  if (p.referenceInk <= p.curve[0]) return 0.0;
  for (int i = 0; i + 1 < kGridCount; ++i) {
    if (p.referenceInk <= p.curve[i + 1]) {
      const double span = p.curve[i + 1] - p.curve[i];
      return (i + (span > 0.0 ? (p.referenceInk - p.curve[i]) / span : 0.0)) * kGridStep;
    }
  }
  return (kGridCount - 1) * kGridStep;
}

// The engine's strength for a sample under one class's anchors (the same function the shipped model is
// evaluated with, so fit and product cannot disagree), times the size factor being tested: the fit
// looks for a relative change of the strength per px of size around 12 px; the shipped model has none.
double strengthOf(const WeightAnchor& a, float sizePerPx, const Datum& p) {
  return emboldenStrength({a, a, a}, static_cast<float>(p.luminance), 400) * std::max(0.0, 1.0 + static_cast<double>(sizePerPx) * (12.0 - p.sample->size));
}

// Sum of squared ink errors of one weight class under anchors `a`.
double classError(const std::vector<const Datum*>& points, const WeightAnchor& a, float sizePerPx) {
  double sum = 0.0;
  for (const Datum* p : points) {
    const double e = curveAt(*p, strengthOf(a, sizePerPx, *p)) / p->referenceInk - 1.0;
    sum += e * e;
  }
  return sum;
}

// Deterministic fit of one class for a given size slope: coarse grid over both anchors, then a finer
// grid around the best. Returns the anchors; `error` receives the class's squared error.
WeightAnchor fitClass(const std::vector<const Datum*>& points, float sizePerPx, double& error) {
  WeightAnchor best{0.0f, 0.0f};
  double bestError = 1e18;
  const auto scan = [&](float d0, float d1, float l0, float l1, float step) {
    WeightAnchor around = best;
    for (float d = d0; d <= d1 + 1e-6f; d += step) {
      for (float l = l0; l <= l1 + 1e-6f; l += step) {
        const double e = classError(points, {d, l}, sizePerPx);
        if (e < bestError - 1e-12) {
          bestError = e;
          around = {d, l};
        }
      }
    }
    best = around;
  };
  scan(-2.0f, 4.0f, -1.0f, 4.0f, 0.05f);
  scan(best.dark - 0.06f, best.dark + 0.06f, best.light - 0.06f, best.light + 0.06f, 0.005f);
  error = bestError;
  return best;
}

struct Fit {
  WeightAnchor anchors[3];
  float sizePerPx = 0.0f;
  double error = 1e18;
};

// Fits all three classes for every size slope on a grid and keeps the best total error.
Fit fitAll(const std::vector<Datum>& points) {
  std::vector<const Datum*> members[3];
  for (const Datum& p : points) members[classOf(p.sample->weight)].push_back(&p);
  Fit best;
  for (float slope = -0.05f; slope <= 0.30f + 1e-6f; slope += 0.01f) {
    Fit f;
    f.sizePerPx = slope;
    f.error = 0.0;
    for (int c = 0; c < 3; ++c) {
      double e = 0.0;
      f.anchors[c] = fitClass(members[c], slope, e);
      f.error += e;
    }
    if (f.error < best.error) best = f;
  }
  return best;
}

}  // namespace

int main() {
  const VisualPaths paths = r1test::visual::paths();
  Services& services = sharedServices(paths);
  std::vector<Datum> points;
  points.reserve(std::size(kSamples) * 2);

  // 1. Measure: reference ink and our ink curve of every sample in both themes.
  for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
    std::map<std::string, image::DecodeResult> cache;
    for (const Sample& s : kSamples) {
      auto found = cache.find(s.reference);
      if (found == cache.end()) found = cache.emplace(s.reference, image::loadPng(paths.referenceDir / "openpencil" / themeDir(theme) / (std::string(s.reference) + ".png"))).first;
      const image::DecodeResult& ref = found->second;
      R1_EXPECT(ref.ok());
      if (!ref.ok()) continue;
      R1_EXPECT(s.x >= 0 && s.y >= 0 && s.x + s.w <= static_cast<int>(ref.image->width) && s.y + s.h <= static_cast<int>(ref.image->height));
      Datum p{};
      p.sample = &s;
      p.theme = theme;
      services.theme().set(theme);
      const r1ui::theme::Color fg = r1test::visual::probeColor(services, s.fg);
      p.luminance = luminanceOf(fg);
      p.referenceInk = inkSum(*ref.image, s.x, s.y, s.w, s.h, modalColor(*ref.image, s), fg);
      R1_EXPECT(p.referenceInk > 1.0);
      for (int i = 0; i < kGridCount; ++i) {
        services.text().setWeightModel(flatModel(static_cast<float>(i) * kGridStep));
        p.curve[i] = inkAt(s, theme, paths);
      }
      p.target = targetOf(p);
      points.push_back(p);
    }
  }

  std::printf("%-15s %-5s %4s %4s %6s %9s %9s %8s\n", "sample", "theme", "size", "wght", "lum", "ref ink", "ink s=0", "target");
  for (const Datum& p : points) {
    std::printf("%-15s %-5s %4.0f %4d %6.3f %9.2f %9.2f %8.3f\n", p.sample->name, themeDir(p.theme), p.sample->size, p.sample->weight, p.luminance,
                p.referenceInk, p.curve[0], p.target);
  }

  // 2. Fit one (dark, light) pair per weight class and one size slope, and print every residual.
  const Fit fit = fitAll(points);
  std::printf("\nfitted size slope %+.3f per px below 12 px (samples at 11, 12 and 14 px; not shipped, see TextEngine.h)\n", static_cast<double>(fit.sizePerPx));
  for (int c = 0; c < 3; ++c) {
    double worst = 0.0;
    double sumSquares = 0.0;
    size_t count = 0;
    std::printf("\nweight class %s: fitted strength at luminance 0 = %.3f, at luminance 1 = %.3f\n", kClassNames[c],
                static_cast<double>(fit.anchors[c].dark), static_cast<double>(fit.anchors[c].light));
    for (const Datum& p : points) {
      if (classOf(p.sample->weight) != c) continue;
      const double s = strengthOf(fit.anchors[c], fit.sizePerPx, p);
      const double residual = curveAt(p, s) / p.referenceInk - 1.0;
      worst = std::max(worst, std::abs(residual));
      sumSquares += residual * residual;
      ++count;
      std::printf("  %-15s %-5s size %4.0f lum %5.3f: model strength %.3f, target %.3f, ink residual %+6.2f%%\n", p.sample->name, themeDir(p.theme),
                  p.sample->size, p.luminance, s, p.target, residual * 100.0);
    }
    R1_EXPECT(count >= 4);
    std::printf("  class %s (%zu samples): rms residual %.2f%%, worst %.2f%%\n", kClassNames[c], count, std::sqrt(sumSquares / static_cast<double>(count)) * 100.0,
                worst * 100.0);
  }

  // 3. The shipped constants reproduce the fit and every sample, through the real engine path.
  const WeightModel shipped = kCalibratedWeights;
  const WeightAnchor shippedAnchors[3] = {shipped.regular, shipped.medium, shipped.bold};
  for (int c = 0; c < 3; ++c) {
    R1_EXPECT_NEAR(static_cast<double>(shippedAnchors[c].dark), static_cast<double>(fit.anchors[c].dark), 0.06);
    R1_EXPECT_NEAR(static_cast<double>(shippedAnchors[c].light), static_cast<double>(fit.anchors[c].light), 0.06);
  }
  R1_EXPECT_NEAR(static_cast<double>(fit.sizePerPx), 0.0, kMaxSizeSlope);  // size independent: no size term is shipped
  services.text().setWeightModel(shipped);
  std::printf("\nshipped model: 400 (%.2f, %.2f)  500 (%.2f, %.2f)  600 (%.2f, %.2f)\n", static_cast<double>(shipped.regular.dark),
              static_cast<double>(shipped.regular.light), static_cast<double>(shipped.medium.dark), static_cast<double>(shipped.medium.light),
              static_cast<double>(shipped.bold.dark), static_cast<double>(shipped.bold.light));
  double worst = 0.0;
  for (const Datum& p : points) {
    const double ratio = inkAt(*p.sample, p.theme, paths) / p.referenceInk;
    worst = std::max(worst, std::abs(ratio - 1.0));
    std::printf("shipped %-15s %-5s %2.0f px weight %d: ink ratio %.3f\n", p.sample->name, themeDir(p.theme), p.sample->size, p.sample->weight, ratio);
    R1_EXPECT(std::abs(ratio - 1.0) <= kMaxInkError);
  }
  std::printf("worst shipped ink error %.1f%% over %zu samples\n", worst * 100.0, points.size());
  return r1test::finish();
}

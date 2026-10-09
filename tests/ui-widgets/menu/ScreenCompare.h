// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the visual-test helper for popups that live at a place on a full window: renders a scene on a
//   1440 x 900 canvas the way the reference screens were captured (neutral canvas, the white page
//   rectangle), crops the same region as a reference crop and compares it under a tolerance profile.
// Why: the harness renders a single widget at the size of its crop; menus, tooltips, dialogs and
//   toasts are overlays whose reference crops include the surface around them (shadow, backdrop) at
//   their screen position, so the whole screen is rendered and only the crop is compared. Shared by
//   the menu, tooltip, popover, dialog and toast visual tests of this widget group.
// Callers: tests/ui-widgets/{menu,tooltip,popover,dialog,toast}/*_visual_test.cpp (label gpu).
// Output: one line per case with the failing fraction, the maximum and the mean difference (same
//   format as the harness), and under R1UI_ARTIFACT_DIR: <reference>-<theme>.png (our crop),
//   .diff.png (failing pixels in red) and .compare.png (reference | ours | diff side by side).
// Backdrop: the screens were captured over a #4d4d4d canvas with a white 520 x 370 page at (330, 150)
//   (measured from screen-context-menu); Backdrop paints exactly that so shadows and the pixels
//   around a popup compare like for like. `page = false` leaves only the canvas.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "VisualSupport.h"
#include "r1ui/widgets/image/ImageDiff.h"

namespace r1test::screen {

using namespace r1ui::widgets;
using namespace r1ui::widgets::testing;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;

inline constexpr int kScreenW = 1440;
inline constexpr int kScreenH = 900;

// Full-window backdrop: the canvas colour and (optionally) the white page of the reference screens.
class Backdrop final : public WidgetObject {
 public:
  // `token` (a theme colour) replaces the canvas grey when not empty.
  explicit Backdrop(bool page = true, std::string token = {}) : page_(page), token_(std::move(token)) {}
  const char* typeName() const override { return "Backdrop"; }
  void onAttached() override {
    layout::Style& s = style();
    s.position = layout::Position::Absolute;
    for (int e = 0; e < 4; ++e) s.inset[e] = layout::Length::px(0);
    node().flags.hitTestTransparent = true;
    node().layer = -1;  // absolute widgets paint above in-flow ones of the same layer: go below them all
  }
  void paint(PaintContext& ctx) override {
    ctx.painter().fillRect(ctx.box(), token_.empty() ? r1ui::render::Color{77.0f / 255.0f, 77.0f / 255.0f, 77.0f / 255.0f, 1.0f} : ctx.color(token_));
    if (page_) ctx.painter().fillRect(ctx.toPhysical(330, 150, 520, 370), {1.0f, 1.0f, 1.0f, 1.0f});
  }

 private:
  bool page_;
  std::string token_;
};

struct Ignore {
  int x = 0, y = 0, w = 0, h = 0;  // crop pixels
};

struct Case {
  std::string reference;            // file name without ".png"
  double clipX = 0.0;               // top-left of the crop on the screen (manifest clip, may be fractional)
  double clipY = 0.0;
  r1ui::theme::ThemeId theme = r1ui::theme::ThemeId::Dark;
  std::string profile = "screen";
  bool luminance = false;
  VisualState state = VisualState::Idle;
  bool page = true;
  std::string canvasToken;          // theme colour for the backdrop instead of the canvas grey
  std::vector<Ignore> ignore;
  // When cropW > 0 the reference file is a whole screen: this region (at the clip origin) is cut from it.
  int cropW = 0;
  int cropH = 0;
  // When not empty only these crop regions are compared (everything else counts as matching): used for
  // screens whose surroundings (side panels, rulers) are not part of the widget under test.
  std::vector<Ignore> only;
  float scale = 1.0f;
};

struct Outcome {
  bool ok = false;
  image::DiffMetrics metrics;
  std::string error;
};

// The widget list is built by `build` as children of the root (it may also open overlays); the id it
// returns is the widget the state applies to (hover goes to its centre).
inline Outcome compareCrop(const BuildFn& build, const Case& c) {
  Outcome out;
  const VisualPaths p = r1test::visual::paths();
  const char* themeDir = c.theme == r1ui::theme::ThemeId::Light ? "light" : "dark";
  const auto loaded = image::loadPng(p.referenceDir / "openpencil" / themeDir / (c.reference + ".png"));
  if (!loaded.ok()) {
    out.error = loaded.error;
    return out;
  }
  std::string error;
  const auto tolerance = image::loadTolerance(p.referenceDir / "tolerance.json", c.profile, error);
  if (!tolerance) {
    out.error = error;
    return out;
  }
  RenderSpec spec;
  spec.width = kScreenW;
  spec.height = kScreenH;
  spec.theme = c.theme;
  spec.state = c.state;
  spec.scale = c.scale;
  spec.padding = 0;
  spec.background = "panel";
  const bool page = c.page;
  const std::string token = c.canvasToken;
  const BuildFn withBackdrop = [&build, page, token](UiContext& ui, WidgetId parent) {
    ui.create<Backdrop>(parent, page, token);
    return build(ui, parent);
  };
  const image::Image screenImage = renderWidget(withBackdrop, spec, p);

  // Crop at the rounded clip origin (the capture rounds a fractional clip the same way).
  const int x0 = static_cast<int>(std::lround(c.clipX));
  const int y0 = static_cast<int>(std::lround(c.clipY));
  image::Image cut;
  if (c.cropW > 0 && c.cropH > 0) {
    cut.width = static_cast<uint32_t>(c.cropW);
    cut.height = static_cast<uint32_t>(c.cropH);
    cut.rgba.assign(size_t{cut.width} * cut.height * 4, 0);
    for (int y = 0; y < c.cropH; ++y) {
      for (int x = 0; x < c.cropW; ++x) {
        const int sx = x0 + x;
        const int sy = y0 + y;
        if (sx < 0 || sy < 0 || sx >= static_cast<int>(loaded.image->width) || sy >= static_cast<int>(loaded.image->height)) continue;
        std::copy_n(loaded.image->rgba.begin() + static_cast<std::ptrdiff_t>((size_t{static_cast<uint32_t>(sy)} * loaded.image->width + static_cast<uint32_t>(sx)) * 4), 4,
                    cut.rgba.begin() + static_cast<std::ptrdiff_t>((size_t{static_cast<uint32_t>(y)} * cut.width + static_cast<uint32_t>(x)) * 4));
      }
    }
  }
  const image::Image& reference = cut.width != 0 ? cut : *loaded.image;
  image::Image ours;
  ours.width = reference.width;
  ours.height = reference.height;
  ours.rgba.assign(size_t{reference.width} * reference.height * 4, 0);
  for (uint32_t y = 0; y < reference.height; ++y) {
    for (uint32_t x = 0; x < reference.width; ++x) {
      const int sx = x0 + static_cast<int>(x);
      const int sy = y0 + static_cast<int>(y);
      if (sx < 0 || sy < 0 || sx >= static_cast<int>(screenImage.width) || sy >= static_cast<int>(screenImage.height)) continue;
      const uint8_t* s = screenImage.rgba.data() + (size_t{static_cast<uint32_t>(sy)} * screenImage.width + static_cast<uint32_t>(sx)) * 4;
      std::copy_n(s, 4, ours.rgba.begin() + static_cast<std::ptrdiff_t>((size_t{y} * ours.width + x) * 4));
    }
  }
  image::Image ref = reference;
  if (!c.only.empty()) {
    for (uint32_t y = 0; y < ref.height; ++y) {
      for (uint32_t x = 0; x < ref.width; ++x) {
        bool inside = false;
        for (const Ignore& r : c.only) {
          if (static_cast<int>(x) >= r.x && static_cast<int>(x) < r.x + r.w && static_cast<int>(y) >= r.y && static_cast<int>(y) < r.y + r.h) inside = true;
        }
        if (inside) continue;
        const size_t i = (size_t{y} * ref.width + x) * 4;
        std::copy_n(ours.rgba.begin() + static_cast<std::ptrdiff_t>(i), 4, ref.rgba.begin() + static_cast<std::ptrdiff_t>(i));
      }
    }
  }
  for (const Ignore& ig : c.ignore) {
    for (int y = std::max(0, ig.y); y < std::min<int>(ig.y + ig.h, static_cast<int>(ref.height)); ++y) {
      for (int x = std::max(0, ig.x); x < std::min<int>(ig.x + ig.w, static_cast<int>(ref.width)); ++x) {
        const size_t i = (size_t{static_cast<uint32_t>(y)} * ref.width + static_cast<uint32_t>(x)) * 4;
        std::copy_n(ours.rgba.begin() + static_cast<std::ptrdiff_t>(i), 4, ref.rgba.begin() + static_cast<std::ptrdiff_t>(i));
      }
    }
  }
  image::Image refCmp = ref;
  image::Image oursCmp = ours;
  if (c.luminance) {
    const auto gray = [](image::Image& img) {
      for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        const double y = 0.2126 * img.rgba[i] + 0.7152 * img.rgba[i + 1] + 0.0722 * img.rgba[i + 2];
        img.rgba[i] = img.rgba[i + 1] = img.rgba[i + 2] = static_cast<uint8_t>(std::lround(y));
      }
    };
    gray(refCmp);
    gray(oursCmp);
  }
  out.metrics = image::compare(refCmp, oursCmp, *tolerance);
  out.ok = out.metrics.error.empty() && out.metrics.pass;

  // Artifacts: our crop, the diff and a reference | ours | diff strip for the eye.
  const std::string name = c.reference + (c.theme == r1ui::theme::ThemeId::Light ? "-light" : "-dark");
  image::writePng(p.artifactDir / (name + ".png"), ours.width, ours.height, ours.rgba);
  if (out.metrics.error.empty()) {
    const image::Image diff = image::makeDiffImage(ours, out.metrics);
    if (out.metrics.failingPixels != 0) image::writePng(p.artifactDir / (name + ".diff.png"), diff.width, diff.height, diff.rgba);
    const uint32_t w = ours.width;
    const uint32_t h = ours.height;
    std::vector<uint8_t> strip(size_t{w} * 3 * h * 4, 0);
    const image::Image* parts[3] = {&ref, &ours, &diff};
    for (uint32_t part = 0; part < 3; ++part) {
      for (uint32_t y = 0; y < h; ++y) {
        std::copy_n(parts[part]->rgba.begin() + static_cast<std::ptrdiff_t>(size_t{y} * w * 4), size_t{w} * 4,
                    strip.begin() + static_cast<std::ptrdiff_t>((size_t{y} * w * 3 + size_t{part} * w) * 4));
      }
    }
    image::writePng(p.artifactDir / (name + ".compare.png"), w * 3, h, strip);
  }
  return out;
}

// One-liner for tests: prints the result line and counts a failure when the crop does not match.
inline bool expectCrop(const BuildFn& build, const Case& c, const char* file, int line) {
  const Outcome o = compareCrop(build, c);
  const char* theme = c.theme == r1ui::theme::ThemeId::Light ? "light" : "dark";
  const std::string summary = !o.error.empty() ? o.error : (!o.metrics.error.empty() ? o.metrics.error : image::describe(o.metrics));
  std::printf("visual %-44s %-5s %-6s %s\n", c.reference.c_str(), theme, c.profile.c_str(), summary.c_str());
  std::fflush(stdout);
  r1test::report(o.ok, c.reference.c_str(), file, line);
  return o.ok;
}

}  // namespace r1test::screen

#define R1_EXPECT_CROP(build, c) ::r1test::screen::expectCrop((build), (c), __FILE__, __LINE__)

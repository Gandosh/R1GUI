// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: R1_EXPECT_MATCHES_REGION, the visual comparison of a widget against a rectangle cut out of a
//   larger reference image (a full 1440 x 900 screen of tests/reference/openpencil), for widgets whose
//   exact context exists only in a screen capture (a toolbar, a whole tab bar, a property section).
// Why: the harness compares whole reference files; screens are far larger than the widget, so the
//   crop is taken here and the rest of the pipeline (render, tolerance profile, luminance, ignore
//   masks, PNG and diff output, metrics line) is the same as VisualSupport.h.
// Callers: the visual tests of the section, scroll, tabbar, toolbar and tree folders (group g3).
// Failure behavior: a missing reference or a crop outside the image is reported as a failed
//   expectation with the reason, never thrown.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../support/VisualSupport.h"
#include "r1ui/widgets/image/ImageDiff.h"
#include "r1ui/widgets/image/Png.h"

namespace r1test::visual {

struct RegionSpec {
  std::string reference;  // file name of the big image without ".png", e.g. "screen-rectangle-selected"
  int x = 0, y = 0, w = 0, h = 0;  // the region in image pixels
  r1ui::theme::ThemeId theme = r1ui::theme::ThemeId::Dark;
  VisualState state = VisualState::Idle;
  std::string profile = "default";
  int padding = 0;
  std::string background = "panel";
  std::string tag;  // artifact name; defaults to the reference name
  std::vector<VisualSpec::Ignore> ignore;
  bool luminance = false;
};

inline bool expectMatchesRegion(const BuildFn& build, const RegionSpec& spec, const char* file, int line) {
  namespace image = r1ui::widgets::image;
  const bool light = spec.theme == r1ui::theme::ThemeId::Light;
  const auto p = paths();
  const std::string name = (spec.tag.empty() ? spec.reference : spec.tag) + (light ? "-light" : "");
  const auto fail = [&](const std::string& why) {
    std::printf("visual %-44s %-5s %-6s %s\n", name.c_str(), light ? "light" : "dark", spec.profile.c_str(), why.c_str());
    report(false, name.c_str(), file, line);
    return false;
  };
  const image::DecodeResult big = image::loadPng(p.referenceDir / "openpencil" / (light ? "light" : "dark") / (spec.reference + ".png"));
  if (!big.ok()) return fail(big.error);
  if (spec.x < 0 || spec.y < 0 || spec.w <= 0 || spec.h <= 0 || spec.x + spec.w > static_cast<int>(big.image->width) || spec.y + spec.h > static_cast<int>(big.image->height)) {
    return fail("region outside the reference image");
  }
  std::string error;
  const auto tolerance = image::loadTolerance(p.referenceDir / "tolerance.json", spec.profile, error);
  if (!tolerance) return fail(error);

  image::Image ref;
  ref.width = static_cast<uint32_t>(spec.w);
  ref.height = static_cast<uint32_t>(spec.h);
  ref.rgba.resize(static_cast<size_t>(spec.w) * static_cast<size_t>(spec.h) * 4);
  for (int row = 0; row < spec.h; ++row) {
    const size_t src = (static_cast<size_t>(spec.y + row) * big.image->width + static_cast<size_t>(spec.x)) * 4;
    std::copy_n(big.image->rgba.begin() + static_cast<std::ptrdiff_t>(src), static_cast<size_t>(spec.w) * 4, ref.rgba.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(row) * static_cast<size_t>(spec.w) * 4));
  }

  r1ui::widgets::testing::RenderSpec render;
  render.width = spec.w;
  render.height = spec.h;
  render.theme = spec.theme;
  render.state = spec.state;
  render.padding = spec.padding;
  render.background = spec.background;
  image::Image candidate = r1ui::widgets::testing::renderWidget(build, render, p);

  for (const VisualSpec::Ignore& ig : spec.ignore) {
    for (int py = std::max(0, ig.y); py < std::min(ig.y + ig.h, spec.h); ++py) {
      for (int px = std::max(0, ig.x); px < std::min(ig.x + ig.w, spec.w); ++px) {
        const size_t i = (static_cast<size_t>(py) * ref.width + static_cast<size_t>(px)) * 4;
        std::copy_n(candidate.rgba.begin() + static_cast<std::ptrdiff_t>(i), 4, ref.rgba.begin() + static_cast<std::ptrdiff_t>(i));
      }
    }
  }
  image::Image refCmp = ref;
  image::Image candCmp = candidate;
  if (spec.luminance) {
    const auto gray = [](image::Image& img) {
      for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        const double yv = 0.2126 * img.rgba[i] + 0.7152 * img.rgba[i + 1] + 0.0722 * img.rgba[i + 2];
        img.rgba[i] = img.rgba[i + 1] = img.rgba[i + 2] = static_cast<uint8_t>(std::lround(yv));
      }
    };
    gray(refCmp);
    gray(candCmp);
  }
  const image::DiffMetrics metrics = image::compare(refCmp, candCmp, *tolerance);
  image::writePng(p.artifactDir / (name + ".png"), candidate.width, candidate.height, candidate.rgba);
  image::writePng(p.artifactDir / (name + ".ref.png"), ref.width, ref.height, ref.rgba);
  const bool pass = metrics.error.empty() && metrics.pass;
  if (metrics.error.empty() && metrics.failingPixels != 0) {
    const image::Image diff = image::makeDiffImage(candidate, metrics);
    image::writePng(p.artifactDir / (name + ".diff.png"), diff.width, diff.height, diff.rgba);
  }
  std::printf("visual %-44s %-5s %-6s %s\n", name.c_str(), light ? "light" : "dark", spec.profile.c_str(), metrics.error.empty() ? image::describe(metrics).c_str() : metrics.error.c_str());
  report(pass, name.c_str(), file, line);
  return pass;
}

}  // namespace r1test::visual

#define R1_EXPECT_MATCHES_REGION(build, spec) ::r1test::visual::expectMatchesRegion((build), (spec), __FILE__, __LINE__)

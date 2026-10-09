// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: a visual-test helper for crops of a full-screen reference where only a rectangle (a popover) is
//   compared and the rest is ignored: it renders and compares exactly like R1_EXPECT_MATCHES but
//   judges the failing-pixel fraction against the AREA OF THE RECTANGLE instead of the whole 1440 x 900
//   screen, so ignoring the rest of the screen cannot hide failures.
// Why: the harness divides by every pixel of the reference; for a 240 x 363 popover in a 1440 x 900
//   screen that makes the profile ten times more lenient than it reads.
// Callers: the colour picker and gradient editor visual tests (group g5).
#pragma once

#include <cstdio>
#include <string>

#include "VisualSupport.h"

namespace r1test::visual {

// `maxFraction` is the allowed share of failing pixels inside `areaPixels` (the screen profile's 0.03).
inline bool expectRegionMatches(const BuildFn& build, const VisualSpec& spec, long long areaPixels, double maxFraction, const char* file, int line) {
  const auto result = r1ui::widgets::testing::compareWithReference(build, spec, paths());
  const double fraction = areaPixels > 0 ? static_cast<double>(result.metrics.failingPixels) / static_cast<double>(areaPixels) : 1.0;
  const bool ok = result.error.empty() && result.metrics.error.empty() && fraction <= maxFraction;
  std::printf("visual %-44s %-5s %-6s region %lld px: failing %zu (%.3f%% of region) max %d mean %.3f %s\n", spec.reference.c_str(),
              spec.theme == r1ui::theme::ThemeId::Light ? "light" : "dark", spec.profile.c_str(), areaPixels, result.metrics.failingPixels, fraction * 100.0,
              result.metrics.maxChannelDiff, result.metrics.meanDiff, ok ? "PASS" : "FAIL");
  if (!result.error.empty()) std::fprintf(stderr, "  %s\n", result.error.c_str());
  if (!ok) std::fprintf(stderr, "  render: %s\n  diff:   %s\n", result.renderPath.string().c_str(), result.diffPath.string().c_str());
  report(ok, spec.reference.c_str(), file, line);
  return ok;
}

}  // namespace r1test::visual

#define R1_EXPECT_REGION_MATCHES(build, spec, area, maxFraction) \
  ::r1test::visual::expectRegionMatches((build), (spec), (area), (maxFraction), __FILE__, __LINE__)

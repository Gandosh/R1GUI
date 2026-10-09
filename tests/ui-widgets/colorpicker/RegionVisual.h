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

// The allowed share of failing pixels inside `areaPixels` is the profile's maxFailingFraction (read from
// the tolerance file the harness uses, R1UI_TOLERANCE_FILE or tests/reference/tolerance.json).
inline bool expectRegionMatches(const BuildFn& build, const VisualSpec& spec, long long areaPixels, const char* file, int line) {
  const auto result = r1ui::widgets::testing::compareWithReference(build, spec, paths());
  std::string toleranceError;
  const auto tolerance = r1ui::widgets::image::loadTolerance(r1ui::widgets::image::toleranceFilePath(paths().referenceDir), spec.profile, toleranceError);
  const double maxFraction = tolerance ? tolerance->maxFailingFraction : 0.0;
  const double fraction = areaPixels > 0 ? static_cast<double>(result.metrics.failingPixels) / static_cast<double>(areaPixels) : 1.0;
  const bool ok = tolerance && result.error.empty() && result.metrics.error.empty() && fraction <= maxFraction;
  std::printf("visual %-44s %-5s %-6s region %lld px: failing %zu (%.3f%% of region) max %d mean %.3f %s%s\n", spec.reference.c_str(),
              spec.theme == r1ui::theme::ThemeId::Light ? "light" : "dark", spec.profile.c_str(), areaPixels, result.metrics.failingPixels, fraction * 100.0,
              result.metrics.maxChannelDiff, result.metrics.meanDiff, ok ? "PASS" : "FAIL",
              r1ui::widgets::image::sweepSuffix(result.metrics, static_cast<double>(areaPixels)).c_str());
  if (!tolerance) std::fprintf(stderr, "  %s\n", toleranceError.c_str());
  if (!result.error.empty()) std::fprintf(stderr, "  %s\n", result.error.c_str());
  if (!ok) std::fprintf(stderr, "  render: %s\n  diff:   %s\n", result.renderPath.string().c_str(), result.diffPath.string().c_str());
  report(ok, spec.reference.c_str(), file, line);
  return ok;
}

}  // namespace r1test::visual

#define R1_EXPECT_REGION_MATCHES(build, spec, area) ::r1test::visual::expectRegionMatches((build), (spec), (area), __FILE__, __LINE__)

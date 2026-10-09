// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the in-process port of tools/spec/imgdiff.py: pixel comparison of a candidate render with a
//   reference image under a named tolerance profile from tests/reference/tolerance.json.
// Why: visual tests must be one-liners that run in CTest without Python; the algorithm and profile
//   semantics are identical to the script, so numbers from either tool agree.
// Method: images must have the same size. A pixel FAILS when any of R, G, B differs by more than
//   `channelTolerance` (0..255; alpha is ignored like the script). The comparison PASSES when the
//   fraction of failing pixels is at most `maxFailingFraction`. Metrics also report the maximum and
//   mean absolute channel difference (mean over all pixels of the per-pixel maximum channel
//   difference) and the bounding box of failing pixels.
// Callers: the visual harness (testing::expectMatchesReference), icon and text calibration tests.
// Failure behavior: a size mismatch or an unknown profile is reported in `error`, never thrown.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/widgets/image/Png.h"

namespace r1ui::widgets::image {

struct Tolerance {
  int channelTolerance = 8;
  double maxFailingFraction = 0.002;
};

struct DiffMetrics {
  uint32_t width = 0;
  uint32_t height = 0;
  size_t failingPixels = 0;
  double failingFraction = 0.0;
  int maxChannelDiff = 0;
  double meanDiff = 0.0;
  int boundsMinX = -1;  // -1 when nothing fails
  int boundsMinY = -1;
  int boundsMaxX = -1;
  int boundsMaxY = -1;
  // Pixels by their largest channel difference (index 0..255): lets a report say how the failing
  // fraction would change under any other channelTolerance without comparing again.
  std::array<uint32_t, 256> histogram{};
  bool pass = false;
  std::string error;               // non-empty when the images could not be compared
  std::vector<uint8_t> failMask;   // width * height, 1 = failing pixel
};

// The tolerance file the visual tests read: the environment variable R1UI_TOLERANCE_FILE when it is
// set and non-empty (to run the suite against a proposed profile set without editing the committed
// one), else `referenceDir`/tolerance.json.
std::filesystem::path toleranceFilePath(const std::filesystem::path& referenceDir);

// Reads `profile` ("default" or a name under "profiles") from a tolerance.json file. nullopt (and
// `error` set) when the file is unreadable, malformed or lacks the profile.
std::optional<Tolerance> loadTolerance(const std::filesystem::path& toleranceFile, std::string_view profile, std::string& error);

DiffMetrics compare(const Image& reference, const Image& candidate, const Tolerance& tolerance);

// Candidate dimmed to a third with failing pixels in red, like `imgdiff.py --diff`.
Image makeDiffImage(const Image& candidate, const DiffMetrics& metrics);

// With R1UI_DIFF_SWEEP set (to anything): " sweep t:percent ..." giving the share of failing pixels,
// relative to `areaPixels`, at channel tolerances 0, 8, 16, ... 128 (what the share would be under any
// other channelTolerance); the tolerance proposal tooling parses it. Empty when the variable is unset.
std::string sweepSuffix(const DiffMetrics& metrics, double areaPixels);

// One-line summary for test output, followed by sweepSuffix over the whole image.
std::string describe(const DiffMetrics& metrics);

}  // namespace r1ui::widgets::image

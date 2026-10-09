// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: a small SVG subset parser and rasteriser for stroke icons (the Lucide set under
//   assets/icons/lucide): it turns one SVG file into 8-bit coverage at a requested pixel size.
// Why: the preview draws icons as tinted coverage quads (docs/spec/icons.md); the full rasteriser
//   tool is a later slice, this is the smallest implementation that renders the icons the panel uses.
// Callers: IconSet (preview), tests/preview. Calls: nothing outside the standard library.
// Supported subset: <svg viewBox stroke-width>, <path d> (M L H V C S Q T A Z, absolute and
//   relative), <circle>, <ellipse>, <rect (with rx/ry)>, <line>, <polyline>, <polygon>. Strokes
//   are round-capped and round-joined (distance to the flattened outline), elements with
//   fill="currentColor" are filled (even-odd). Anything else (unknown element, bad number, missing
//   attribute, too many elements or points) rejects the whole file with a message; nothing is
//   drawn partially.
// Limits: the file is at most kMaxSvgBytes, kMaxSvgElements elements, kMaxSvgPoints flattened
//   points; the output side is 4..kMaxIconPixels.
// Anti-aliasing: 4x4 supersampling per pixel. Pixel centres follow the browser convention (the
//   viewBox maps to [0, size] and pixel i covers [i, i+1]).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace preview {

inline constexpr size_t kMaxSvgBytes = 64 * 1024;
inline constexpr size_t kMaxSvgElements = 64;
inline constexpr size_t kMaxSvgPoints = 20000;
inline constexpr int kMinIconPixels = 4;
inline constexpr int kMaxIconPixels = 256;

struct SvgPoint {
  float x = 0.0f;
  float y = 0.0f;
};

// One flattened outline in viewBox units.
struct SvgOutline {
  std::vector<SvgPoint> points;
  bool closed = false;
  bool filled = false;  // fill="currentColor": interior is covered as well as the stroke
  bool stroked = true;  // false for filled shapes that declare stroke="none"
};

struct SvgIcon {
  float viewBox = 24.0f;
  float strokeWidth = 2.0f;
  std::vector<SvgOutline> outlines;
};

struct SvgParseResult {
  bool ok = false;
  SvgIcon icon;
  std::string error;  // set when !ok
};

SvgParseResult parseSvg(std::string_view text);

// size x size bytes, row 0 at the top, 255 = full coverage. Throws std::invalid_argument for a
// size outside [kMinIconPixels, kMaxIconPixels].
std::vector<uint8_t> rasterizeIcon(const SvgIcon& icon, int size);

}  // namespace preview

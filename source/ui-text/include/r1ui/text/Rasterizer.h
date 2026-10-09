// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: glyph rasterization (FreeType) into 8-bit grayscale coverage bitmaps.
// Why: the renderer samples glyph coverage from an atlas; this produces the pixels, and the
//   bearings that place them, deterministically.
// Callers: GlyphAtlas (cache misses), the dump tool, tests.
// Rendering model (matches the reference browser's unhinted, subpixel-positioned grayscale AA):
//   - no hinting at all (FT_LOAD_NO_HINTING); the outline is scaled from design units, so shapes
//     and spacing are identical at every subpixel position;
//   - the pen's fractional x is applied to the outline before scan conversion, quantized to
//     kSubpixelBins (4) steps of 1/4 px; y is always whole pixels;
//   - coverage is exact-area 8-bit (FreeType's smooth rasterizer), alpha only, no LCD filtering;
//   - synthetic bold (emboldenPx > 0) offsets the outline by emboldenPx in total (about half on
//     each side) before rendering and leaves advances untouched, as the reference UI does.
// Determinism: same font, glyph and parameters give a bit-identical bitmap on every call.
// Threading: touches FreeType face state; UI thread only.
#pragma once

#include <cstdint>
#include <vector>

#include "r1ui/text/Font.h"
#include "r1ui/text/Result.h"

namespace r1ui::text {

inline constexpr int kSubpixelBins = 4;

struct RasterParams {
  float pixelSize = 12.0f;   // valid range per isValidPixelSize; sizes are used at 1/64 px resolution
  int subpixelBin = 0;       // 0..3, fractional pen position in quarter pixels
  float emboldenPx = 0.0f;   // 0..pixelSize; total outline growth in pixels
};

struct GlyphBitmap {
  int width = 0;             // 0 for glyphs with no ink (space)
  int height = 0;
  int left = 0;              // x of the first column relative to the integer pen position
  int top = 0;               // y of the first row above the baseline (positive = up)
  std::vector<std::uint8_t> coverage;  // width * height bytes, row-major, no padding
};

// Errors: InvalidArgument (size, bin, embolden or glyph id out of range), GlyphTooLarge
// (bitmap above 4096 px in either direction), Internal (FreeType failure).
Result<GlyphBitmap> rasterizeGlyph(const Font& font, std::uint32_t glyphId, const RasterParams& params);

}  // namespace r1ui::text

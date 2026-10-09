// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: turning a shaped run plus a pen position into textured quads ready for a renderer.
// Why: the renderer should not know about subpixel bins, bearings or atlas lookups; it receives
//   rectangles in pixels and UVs into the atlas image.
// Callers: ui-render's text drawing, widgets, tests.
// Positioning: the pen advances by the unhinted shaped advances. Each glyph's x is rounded to
//   the nearest quarter pixel; the whole part moves the quad, the quarter part selects the
//   pre-shifted atlas bitmap (so text is subpixel positioned without resampling). With
//   snapBaselineY (default) the baseline is rounded to a whole pixel, which keeps horizontal
//   stems crisp like the reference browser; y offsets from shaping are applied before rounding.
// Output: quads are appended to `out` in glyph order; ink-less glyphs (spaces) produce no quad.
// Frame rule: call atlas.beginFrame() once per frame before building quads; a quad list is valid
//   until the next beginFrame().
// Failure behavior: a glyph that cannot be placed (atlas full, too large) is skipped and
//   counted; building continues so one bad glyph does not blank a whole label.
#pragma once

#include <cstddef>
#include <vector>

#include "r1ui/text/Font.h"
#include "r1ui/text/GlyphAtlas.h"
#include "r1ui/text/Result.h"
#include "r1ui/text/Shaper.h"

namespace r1ui::text {

struct GlyphQuad {
  float x = 0;  // destination rectangle, pixels
  float y = 0;
  float w = 0;
  float h = 0;
  float u0 = 0;  // atlas UVs (0..1)
  float v0 = 0;
  float u1 = 0;
  float v1 = 0;
};

struct QuadParams {
  float pixelSize = 12.0f;
  float emboldenPx = 0.0f;
  bool snapBaselineY = true;
};

struct QuadStats {
  std::size_t quads = 0;
  std::size_t skipped = 0;            // glyphs dropped because the atlas could not hold them
  bool atlasFull = false;             // at least one skip was LookupStatus::AtlasFull
};

// `penX` is where the run starts and `baselineY` the baseline, both in pixels. Errors:
// InvalidArgument for a non-finite or absurd (|v| > 1e7) position or invalid size/embolden.
Result<QuadStats> buildGlyphQuads(GlyphAtlas& atlas, const Font& font, const ShapedRun& run,
                                  const QuadParams& params, float penX, float baselineY,
                                  std::vector<GlyphQuad>& out);

}  // namespace r1ui::text

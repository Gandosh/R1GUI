// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: buildGlyphQuads (see r1ui/text/GlyphQuads.h for the positioning rules).
// Invariants: on success `out` only grows; quad coordinates are finite.
#include "r1ui/text/GlyphQuads.h"

#include <cmath>
#include <new>

namespace r1ui::text {

namespace {

constexpr float kMaxCoordinate = 1.0e7f;

// Rounds a pen x to quarter pixels and splits it into whole pixels and a bin 0..3.
void splitSubpixel(double x, double& whole, int& bin) {
  const double quarters = std::floor(x * kSubpixelBins + 0.5);
  const double wholeQuarters = std::floor(quarters / kSubpixelBins);
  whole = wholeQuarters;
  bin = static_cast<int>(quarters - wholeQuarters * kSubpixelBins);
}

}  // namespace

Result<QuadStats> buildGlyphQuads(GlyphAtlas& atlas, const Font& font, const ShapedRun& run,
                                  const QuadParams& params, float penX, float baselineY,
                                  std::vector<GlyphQuad>& out) {
  if (!isValidPixelSize(params.pixelSize) || !std::isfinite(params.emboldenPx) || params.emboldenPx < 0.0f ||
      params.emboldenPx > params.pixelSize) {
    return makeError(ErrorCode::InvalidArgument, "invalid size or embolden");
  }
  if (!std::isfinite(penX) || !std::isfinite(baselineY) || std::abs(penX) > kMaxCoordinate ||
      std::abs(baselineY) > kMaxCoordinate) {
    return makeError(ErrorCode::InvalidArgument, "pen position must be finite and within 1e7 px");
  }

  QuadStats stats;
  const std::size_t startSize = out.size();
  try {
    out.reserve(out.size() + run.glyphs.size());
    double pen = penX;
    for (const ShapedGlyph& g : run.glyphs) {
      double whole = 0;
      int bin = 0;
      splitSubpixel(pen + static_cast<double>(g.xOffset), whole, bin);
      pen += static_cast<double>(g.xAdvance);

      RasterParams rp;
      rp.pixelSize = params.pixelSize;
      rp.subpixelBin = bin;
      rp.emboldenPx = params.emboldenPx;
      const AtlasLookup hit = atlas.get(font, g.glyphId, rp);
      if (hit.status != LookupStatus::Ok) {
        ++stats.skipped;
        if (hit.status == LookupStatus::AtlasFull) stats.atlasFull = true;
        continue;
      }
      if (!hit.glyph.visible) continue;

      double baseline = static_cast<double>(baselineY) - static_cast<double>(g.yOffset);
      if (params.snapBaselineY) baseline = std::floor(baseline + 0.5);
      GlyphQuad q;
      q.x = static_cast<float>(whole + hit.glyph.left);
      q.y = static_cast<float>(baseline - hit.glyph.top);
      q.w = static_cast<float>(hit.glyph.w);
      q.h = static_cast<float>(hit.glyph.h);
      q.u0 = hit.glyph.u0;
      q.v0 = hit.glyph.v0;
      q.u1 = hit.glyph.u1;
      q.v1 = hit.glyph.v1;
      out.push_back(q);
      ++stats.quads;
    }
  } catch (const std::bad_alloc&) {
    out.resize(startSize);
    return makeError(ErrorCode::Internal, "out of memory building quads");
  }
  return stats;
}

}  // namespace r1ui::text

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: rasterizeGlyph (see r1ui/text/Rasterizer.h for the rendering model).
// Order of operations: load unhinted outline -> embolden -> translate by the subpixel bin ->
//   render with the smooth rasterizer. Embolden before translate so the bin shifts the final shape.
// Invariants: the returned bitmap is tightly packed; a glyph with an empty outline yields a 0x0
//   bitmap and Ok status; FreeType's glyph slot is left reusable after every path.
#include "r1ui/text/Rasterizer.h"

#include <cmath>
#include <cstring>

#include "FontImpl.h"
#include FT_OUTLINE_H
#include "r1ui/core/CheckedCast.h"

namespace r1ui::text {

namespace {

constexpr int kMaxBitmapDimension = 4096;

Error rasterError(ErrorCode code, const char* what) { return makeError(code, what); }

}  // namespace

Result<GlyphBitmap> rasterizeGlyph(const Font& font, std::uint32_t glyphId, const RasterParams& params) {
  if (!isValidPixelSize(params.pixelSize)) {
    return rasterError(ErrorCode::InvalidArgument, "pixel size must be finite and within range");
  }
  if (params.subpixelBin < 0 || params.subpixelBin >= kSubpixelBins) {
    return rasterError(ErrorCode::InvalidArgument, "subpixel bin must be 0..3");
  }
  if (!std::isfinite(params.emboldenPx) || params.emboldenPx < 0.0f || params.emboldenPx > params.pixelSize) {
    return rasterError(ErrorCode::InvalidArgument, "embolden must be within 0..pixelSize");
  }
  if (glyphId >= font.glyphCount()) {
    return rasterError(ErrorCode::InvalidArgument, "glyph id out of range");
  }

  Font::Impl& impl = font.impl();
  if (!impl.setPixelSize(params.pixelSize)) {
    return rasterError(ErrorCode::Internal, "FreeType rejected the pixel size");
  }
  if (FT_Load_Glyph(impl.face, glyphId, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0) {
    return rasterError(ErrorCode::Internal, "FreeType could not load the glyph");
  }
  FT_GlyphSlot slot = impl.face->glyph;
  if (slot->format != FT_GLYPH_FORMAT_OUTLINE) {
    return rasterError(ErrorCode::Internal, "glyph is not an outline");
  }

  GlyphBitmap bitmap;
  if (slot->outline.n_points == 0) return bitmap;

  if (params.emboldenPx > 0.0f) {
    const FT_Pos strength = static_cast<FT_Pos>(std::lround(params.emboldenPx * 64.0f));
    if (strength > 0 && FT_Outline_Embolden(&slot->outline, strength) != 0) {
      return rasterError(ErrorCode::Internal, "FreeType could not embolden the outline");
    }
  }
  if (params.subpixelBin != 0) {
    FT_Outline_Translate(&slot->outline, params.subpixelBin * (64 / kSubpixelBins), 0);
  }
  if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) {
    return rasterError(ErrorCode::Internal, "FreeType could not render the glyph");
  }

  const FT_Bitmap& src = slot->bitmap;
  if (src.pixel_mode != FT_PIXEL_MODE_GRAY && src.width != 0 && src.rows != 0) {
    return rasterError(ErrorCode::Internal, "unexpected bitmap pixel mode");
  }
  if (src.width > kMaxBitmapDimension || src.rows > kMaxBitmapDimension) {
    return rasterError(ErrorCode::GlyphTooLarge, "glyph bitmap exceeds 4096 px");
  }
  bitmap.width = r1ui::core::checkedCast<int>(src.width);
  bitmap.height = r1ui::core::checkedCast<int>(src.rows);
  bitmap.left = slot->bitmap_left;
  bitmap.top = slot->bitmap_top;
  bitmap.coverage.resize(static_cast<std::size_t>(bitmap.width) * static_cast<std::size_t>(bitmap.height));
  for (int row = 0; row < bitmap.height; ++row) {
    // pitch may be negative (bottom-up bitmaps); FreeType's smooth renderer returns top-down.
    const unsigned char* line = src.buffer + static_cast<std::ptrdiff_t>(row) * src.pitch;
    std::memcpy(bitmap.coverage.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(bitmap.width),
                line, static_cast<std::size_t>(bitmap.width));
  }
  return bitmap;
}

}  // namespace r1ui::text

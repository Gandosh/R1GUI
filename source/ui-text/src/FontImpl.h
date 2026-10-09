// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the private FreeType/HarfBuzz state behind r1ui::text::Font, shared by Font.cpp,
//   Shaper.cpp and Rasterizer.cpp.
// Why: keeps ft2build.h and hb.h out of public headers so consumers (ui-render, widgets) never
//   depend on the third-party include paths.
// Lifetime: Font::Impl destroys HarfBuzz objects first, then the FreeType face, then (last, by
//   member order) releases its share of the FreeType library.
#pragma once

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "r1ui/text/Font.h"

namespace r1ui::text {

// Owns the FT_Library; shared by the FontLibrary and all Fonts created from it.
struct FtLibrary {
  FT_Library library = nullptr;
  FtLibrary() = default;
  FtLibrary(const FtLibrary&) = delete;
  FtLibrary& operator=(const FtLibrary&) = delete;
  ~FtLibrary() {
    if (library != nullptr) FT_Done_FreeType(library);
  }
};

struct Font::Impl {
  std::shared_ptr<FtLibrary> ft;                      // declared first: destroyed last
  std::shared_ptr<const std::vector<std::uint8_t>> bytes;  // backing store for both libraries
  FT_Face face = nullptr;
  hb_blob_t* blob = nullptr;
  hb_face_t* hbFace = nullptr;
  hb_font_t* hbFont = nullptr;  // scale = units per em, so positions are in font units
  FT_F26Dot6 currentSize = 0;   // last size given to FreeType, to skip redundant resets

  Impl() = default;
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  ~Impl() {
    if (hbFont != nullptr) hb_font_destroy(hbFont);
    if (hbFace != nullptr) hb_face_destroy(hbFace);
    if (blob != nullptr) hb_blob_destroy(blob);
    if (face != nullptr) FT_Done_Face(face);
  }

  // Sets the FreeType character size in 26.6 pixels (1/64 px resolution). Returns false on error.
  bool setPixelSize(float pixelSize) {
    const FT_F26Dot6 size = static_cast<FT_F26Dot6>(pixelSize * 64.0f + 0.5f);
    if (size == currentSize) return true;
    if (FT_Set_Char_Size(face, 0, size, 72, 72) != 0) return false;
    currentSize = size;
    return true;
  }
};

}  // namespace r1ui::text

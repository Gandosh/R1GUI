// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: font loading (TrueType/OpenType from memory or file), per-size metrics, and the
//   family/weight resolution that decides which face and how much synthetic bold to use.
// Why: one validated entry point turns untrusted font bytes into a Font that the shaper
//   (HarfBuzz) and rasterizer (FreeType) can use without further checks.
// Callers: Shaper, Rasterizer, GlyphAtlas, GlyphQuads, widgets, tests, the dump tool.
// Threading: FontLibrary and every Font created from it are UI-thread objects; FreeType face
//   state is mutated while rasterizing, so concurrent use needs external locking.
// Lifetime: a Font keeps its byte buffer, its FreeType face and its HarfBuzz face alive; it also
//   keeps the FreeType library alive, so Fonts may outlive the FontLibrary object.
// Failure behavior: loaders return Result errors (never throw across this boundary); a rejected
//   load leaves no state behind.
// Limits: font files above kMaxFontFileBytes are rejected; units-per-em must be 16..16384;
//   pixel sizes accepted by size-taking APIs are finite and within [kMinPixelSize, kMaxPixelSize].
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/text/Result.h"

namespace r1ui::text {

inline constexpr std::size_t kMaxFontFileBytes = std::size_t{32} * 1024 * 1024;
inline constexpr float kMinPixelSize = 1.0f;
inline constexpr float kMaxPixelSize = 1024.0f;

// Vertical metrics at one pixel size, all in pixels. descent is positive (distance below the
// baseline). naturalLineHeight = ascent + descent + lineGap. The UI normally uses the fixed line
// heights from the design tokens instead; baselineInBox places a baseline inside such a box the
// way CSS half-leading does.
struct FontMetrics {
  float ascent = 0;
  float descent = 0;
  float lineGap = 0;
  float xHeight = 0;
  float capHeight = 0;
  float naturalLineHeight = 0;

  // Distance from the top of a line box of `boxHeight` to the baseline.
  float baselineInBox(float boxHeight) const { return (boxHeight - (ascent + descent)) * 0.5f + ascent; }
};

// True when `size` is finite and inside the supported range.
bool isValidPixelSize(float size);

class Font {
 public:
  struct Impl;  // FreeType/HarfBuzz state; defined privately in src/FontImpl.h

  ~Font();
  Font(const Font&) = delete;
  Font& operator=(const Font&) = delete;

  // Unique for the process lifetime and never reused; used as part of glyph cache keys.
  std::uint32_t id() const { return id_; }
  int weight() const { return weight_; }
  int unitsPerEm() const { return unitsPerEm_; }
  std::uint32_t glyphCount() const { return glyphCount_; }

  // Metrics at `pixelSize` (hhea ascender/descender/lineGap, OS/2 x-height and cap height).
  // Precondition: isValidPixelSize(pixelSize).
  FontMetrics metricsAt(float pixelSize) const;

  Impl& impl() const { return *impl_; }

 private:
  friend class FontLibrary;
  Font() = default;

  std::unique_ptr<Impl> impl_;
  std::uint32_t id_ = 0;
  int weight_ = 400;
  int unitsPerEm_ = 0;
  std::uint32_t glyphCount_ = 0;
};

using FontHandle = std::shared_ptr<Font>;

class FontLibrary {
 public:
  FontLibrary();
  ~FontLibrary();
  FontLibrary(const FontLibrary&) = delete;
  FontLibrary& operator=(const FontLibrary&) = delete;

  // False when the FreeType library could not be initialized; loads then fail with Internal.
  bool ready() const;

  // Copies `data` and loads the first face. `weight` (1..1000) labels the face for family
  // resolution; it does not change the outlines.
  Result<FontHandle> loadFromMemory(std::span<const std::uint8_t> data, int weight);

  // Reads a regular file (no directories, no devices) of at most kMaxFontFileBytes.
  Result<FontHandle> loadFromFile(std::string_view utf8Path, int weight);

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

// How heavier weights are produced.
enum class BoldMode {
  Synthetic,  // Regular outlines thickened by an outline offset, like the reference UI
  RealFaces   // nearest real face of the family, no thickening
};

struct ResolvedFace {
  FontHandle font;
  float emboldenPx = 0;  // outline growth in pixels for the rasterizer; 0 = none
};

// Skia-style fake bold strength: size * k where k falls linearly from 1/24 (size <= 9) to 1/32
// (size >= 36). Returns 0 for invalid sizes.
float defaultEmboldenPx(float pixelSize);

// A set of faces of one family, keyed by weight.
class FontFamily {
 public:
  // Adds a face; a second face with the same weight replaces the first. Weight must be 1..1000.
  Status addFace(FontHandle font);

  // Synthetic: weights below 500 use the closest face at or below 400 (Regular), weights
  // 500..1000 use the Regular face with defaultEmboldenPx (or `emboldenOverride` when >= 0).
  // RealFaces: the face whose weight is closest to `weight`, never emboldened.
  // Returns a null font when the family is empty or the size is invalid.
  ResolvedFace resolve(int weight, float pixelSize, BoldMode mode, float emboldenOverride = -1.0f) const;

 private:
  std::vector<FontHandle> faces_;
};

}  // namespace r1ui::text

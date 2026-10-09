// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: text shaping (HarfBuzz) into positioned glyph runs, plus width measurement and
//   truncate-with-ellipsis built on it.
// Why: widgets need exact text widths for layout and glyph positions for drawing; both must
//   come from the same shaper so what is measured is what is drawn.
// Callers: GlyphQuads (drawing), TextEditor/CaretMap (caret and hit-testing), widgets (layout).
// Input policy: UTF-8 in. Invalid sequences are replaced by U+FFFD before shaping and
//   ShapedRun::sanitizedText then holds the text that cluster offsets refer to; for valid input
//   cluster offsets refer to the caller's own string. Text longer than kMaxShapeBytes is rejected
//   with InputTooLarge (user text is never silently cut). NUL and other control characters are
//   shaped like any other code point (they map to the font's .notdef or are hidden by the font).
// Output units: pixels (float). Advances come from the font's unhinted design units scaled to the
//   requested size, with HarfBuzz default features on (kerning, ligatures, contextual alternates).
// Scripts: the text is split into runs of one script (Common/Inherited characters join the
//   surrounding run) and each run is shaped with its natural direction. Right-to-left runs are
//   shaped correctly but there is no bidi reordering of mixed-direction text and no font
//   fallback in v1: those scripts must not crash and render with the font's own coverage.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/text/Font.h"
#include "r1ui/text/Result.h"

namespace r1ui::text {

inline constexpr std::size_t kMaxShapeBytes = std::size_t{1} * 1024 * 1024;

struct ShapedGlyph {
  std::uint32_t glyphId = 0;   // font glyph index (0 = .notdef)
  std::uint32_t cluster = 0;   // byte offset of the first source byte this glyph represents
  float xAdvance = 0;          // pixels
  float xOffset = 0;           // pixels, added to the pen position before drawing
  float yOffset = 0;           // pixels, positive moves the glyph up
  bool rtl = false;            // glyph belongs to a right-to-left run
};

struct ShapedRun {
  std::vector<ShapedGlyph> glyphs;  // visual order within each script run, runs in logical order
  float width = 0;                  // sum of advances, pixels
  float pixelSize = 0;
  std::string sanitizedText;        // non-empty only when the input had to be repaired
  bool wasSanitized = false;
};

struct ShapeOptions {
  bool kerning = true;
  bool ligatures = true;
};

// Shapes `utf8` at `pixelSize`. Errors: InvalidArgument (bad size), InputTooLarge, Internal.
Result<ShapedRun> shapeText(const Font& font, float pixelSize, std::string_view utf8,
                            const ShapeOptions& options = {});

// Width in pixels of `utf8` at `pixelSize` (same rules and errors as shapeText).
Result<float> measureWidth(const Font& font, float pixelSize, std::string_view utf8,
                           const ShapeOptions& options = {});

struct EllipsisResult {
  std::string text;      // the (possibly shortened) text including the ellipsis when truncated
  float width = 0;       // measured width of `text`
  bool truncated = false;
};

// Fits `utf8` into `maxWidth`: returns it unchanged when it fits, otherwise the longest prefix
// that ends on a grapheme boundary followed by U+2026 whose measured width is <= maxWidth. When
// even the ellipsis alone does not fit, the result is the bare ellipsis (truncated = true, width
// larger than maxWidth); the caller may clip. Invalid UTF-8 is repaired first (the returned text
// is then derived from the repaired string).
Result<EllipsisResult> truncateWithEllipsis(const Font& font, float pixelSize, std::string_view utf8,
                                            float maxWidth, const ShapeOptions& options = {});

}  // namespace r1ui::text

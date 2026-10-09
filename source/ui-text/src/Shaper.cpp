// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: HarfBuzz shaping, script itemization, measurement and ellipsis truncation.
// Why: see r1ui/text/Shaper.h. This file is the only HarfBuzz shaping call site.
// Invariants: every cluster value in the output is a byte offset into the text the run refers
//   to (original when valid, run.sanitizedText otherwise); glyph positions are scaled from font
//   units (hb font scale == units per em) so no hinting or integer pixel rounding is involved.
#include "r1ui/text/Shaper.h"

#include <hb.h>

#include <climits>
#include <cmath>
#include <memory>
#include <new>

#include "FontImpl.h"
#include "r1ui/core/CheckedCast.h"
#include "r1ui/text/CaretMap.h"
#include "r1ui/text/Grapheme.h"
#include "r1ui/text/Utf8.h"

namespace r1ui::text {

namespace {

struct BufferDeleter {
  void operator()(hb_buffer_t* buffer) const { hb_buffer_destroy(buffer); }
};
using Buffer = std::unique_ptr<hb_buffer_t, BufferDeleter>;

struct ScriptRun {
  std::size_t begin = 0;
  std::size_t end = 0;
  hb_script_t script = HB_SCRIPT_COMMON;
};

constexpr char kEllipsis[] = "\xE2\x80\xA6";  // U+2026
constexpr int kMaxEllipsisRefits = 64;

Error shapeError(ErrorCode code, const char* what) { return makeError(code, what); }

// Splits `text` into runs of a single script. Common/Inherited code points (spaces, digits,
// punctuation, combining marks) join the run they appear in; a leading stretch of them takes the
// script of the first real letter. Text with no real script stays one Common run.
std::vector<ScriptRun> itemize(std::string_view text) {
  std::vector<ScriptRun> runs;
  hb_unicode_funcs_t* funcs = hb_unicode_funcs_get_default();
  ScriptRun current;
  bool open = false;
  bool resolved = false;  // current.script is a real script
  for (std::size_t pos = 0; pos < text.size();) {
    const DecodedCodePoint d = decodeUtf8(text, pos);
    const hb_script_t s = hb_unicode_script(funcs, d.codePoint);
    const bool neutral = s == HB_SCRIPT_COMMON || s == HB_SCRIPT_INHERITED || s == HB_SCRIPT_UNKNOWN;
    if (!open) {
      current = ScriptRun{pos, pos, neutral ? HB_SCRIPT_COMMON : s};
      resolved = !neutral;
      open = true;
    } else if (!neutral) {
      if (!resolved) {
        current.script = s;
        resolved = true;
      } else if (s != current.script) {
        runs.push_back(current);
        current = ScriptRun{pos, pos, s};
      }
    }
    pos += d.length;
    current.end = pos;
  }
  if (open) runs.push_back(current);
  return runs;
}

std::vector<hb_feature_t> buildFeatures(const ShapeOptions& options) {
  std::vector<hb_feature_t> features;
  const auto off = [&](char a, char b, char c, char d) {
    features.push_back(hb_feature_t{HB_TAG(a, b, c, d), 0, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END});
  };
  if (!options.kerning) off('k', 'e', 'r', 'n');
  if (!options.ligatures) {
    off('l', 'i', 'g', 'a');
    off('c', 'l', 'i', 'g');
  }
  return features;
}

}  // namespace

Result<ShapedRun> shapeText(const Font& font, float pixelSize, std::string_view utf8,
                            const ShapeOptions& options) {
  if (!isValidPixelSize(pixelSize)) {
    return shapeError(ErrorCode::InvalidArgument, "pixel size must be finite and within range");
  }
  if (utf8.size() > kMaxShapeBytes) {
    return shapeError(ErrorCode::InputTooLarge, "text exceeds the shaping size limit");
  }

  ShapedRun run;
  run.pixelSize = pixelSize;
  std::string_view text = utf8;
  try {
    if (!isValidUtf8(utf8)) {
      run.sanitizedText = sanitizeUtf8(utf8);
      run.wasSanitized = true;
      text = run.sanitizedText;
    }
    if (text.empty()) return run;

    const std::vector<ScriptRun> scriptRuns = itemize(text);
    const std::vector<hb_feature_t> features = buildFeatures(options);
    Buffer buffer(hb_buffer_create());
    if (!hb_buffer_allocation_successful(buffer.get())) {
      return shapeError(ErrorCode::Internal, "HarfBuzz buffer allocation failed");
    }
    const hb_language_t language = hb_language_from_string("en", -1);
    const int textLength = r1ui::core::checkedCast<int>(text.size());
    const double scale = static_cast<double>(pixelSize) / static_cast<double>(font.unitsPerEm());
    double width = 0.0;

    for (const ScriptRun& sr : scriptRuns) {
      hb_buffer_clear_contents(buffer.get());
      hb_buffer_add_utf8(buffer.get(), text.data(), textLength,
                         r1ui::core::checkedCast<unsigned int>(sr.begin),
                         r1ui::core::checkedCast<int>(sr.end - sr.begin));
      const hb_direction_t direction = hb_script_get_horizontal_direction(sr.script);
      hb_buffer_set_direction(buffer.get(), direction == HB_DIRECTION_RTL ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
      hb_buffer_set_script(buffer.get(), sr.script);
      hb_buffer_set_language(buffer.get(), language);
      hb_shape(font.impl().hbFont, buffer.get(), features.empty() ? nullptr : features.data(),
               r1ui::core::checkedCast<unsigned int>(features.size()));
      if (!hb_buffer_allocation_successful(buffer.get())) {
        return shapeError(ErrorCode::Internal, "HarfBuzz shaping ran out of memory");
      }

      unsigned int count = 0;
      const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer.get(), &count);
      const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer.get(), &count);
      const bool rtl = hb_buffer_get_direction(buffer.get()) == HB_DIRECTION_RTL;
      run.glyphs.reserve(run.glyphs.size() + count);
      for (unsigned int i = 0; i < count; ++i) {
        ShapedGlyph g;
        g.glyphId = infos[i].codepoint;
        g.cluster = infos[i].cluster;
        g.xAdvance = static_cast<float>(static_cast<double>(positions[i].x_advance) * scale);
        g.xOffset = static_cast<float>(static_cast<double>(positions[i].x_offset) * scale);
        g.yOffset = static_cast<float>(static_cast<double>(positions[i].y_offset) * scale);
        g.rtl = rtl;
        width += static_cast<double>(positions[i].x_advance) * scale;
        run.glyphs.push_back(g);
      }
    }
    run.width = static_cast<float>(width);
  } catch (const std::bad_alloc&) {
    return shapeError(ErrorCode::Internal, "out of memory while shaping");
  } catch (const std::range_error&) {
    return shapeError(ErrorCode::InputTooLarge, "text does not fit the shaper's integer ranges");
  }
  return run;
}

Result<float> measureWidth(const Font& font, float pixelSize, std::string_view utf8,
                           const ShapeOptions& options) {
  Result<ShapedRun> run = shapeText(font, pixelSize, utf8, options);
  if (!run.ok()) return run.error();
  return run.value().width;
}

Result<EllipsisResult> truncateWithEllipsis(const Font& font, float pixelSize, std::string_view utf8,
                                            float maxWidth, const ShapeOptions& options) {
  if (std::isnan(maxWidth)) {
    return shapeError(ErrorCode::InvalidArgument, "maxWidth is NaN");
  }
  Result<ShapedRun> full = shapeText(font, pixelSize, utf8, options);
  if (!full.ok()) return full.error();
  const std::string_view text = full.value().wasSanitized ? std::string_view(full.value().sanitizedText) : utf8;

  EllipsisResult out;
  if (full.value().width <= maxWidth) {
    out.text.assign(text);
    out.width = full.value().width;
    return out;
  }
  out.truncated = true;

  // Start from the longest prefix whose stop lies within the budget left after the ellipsis,
  // then re-measure prefix + ellipsis together (kerning and ligatures can shift the estimate)
  // and back off one grapheme at a time. The loop is bounded; on exhaustion only the ellipsis
  // is returned, which is always a valid (if short) answer.
  Result<float> ellipsisWidth = measureWidth(font, pixelSize, kEllipsis, options);
  if (!ellipsisWidth.ok()) return ellipsisWidth.error();
  const float budget = maxWidth - ellipsisWidth.value();
  std::size_t candidate = 0;
  if (budget > 0.0f) {
    const CaretMap map = CaretMap::build(full.value(), text);
    for (const CaretMap::Stop& stop : map.stops()) {
      if (stop.x <= budget && stop.offset > candidate) candidate = stop.offset;
    }
  }
  for (int attempt = 0; candidate > 0 && attempt < kMaxEllipsisRefits; ++attempt) {
    std::string probe(text.substr(0, candidate));
    probe += kEllipsis;
    Result<float> w = measureWidth(font, pixelSize, probe, options);
    if (!w.ok()) return w.error();
    if (w.value() <= maxWidth) {
      out.text = std::move(probe);
      out.width = w.value();
      return out;
    }
    candidate = prevGraphemeBoundary(text, candidate);
  }
  out.text = kEllipsis;
  out.width = ellipsisWidth.value();
  return out;
}

}  // namespace r1ui::text

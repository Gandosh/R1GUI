// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: grapheme and word segmentation (see r1ui/text/Grapheme.h for the covered rule set).
// Why: boundary decisions are a pure function of the text and the offset (isGraphemeBoundary),
//   and next/prev are defined by walking code points until that function says "boundary", so the
//   forward and backward directions can never disagree.
// Dependencies: HarfBuzz's built-in Unicode data for general categories (no extra tables).
// Bounds: the left-context scans for regional-indicator parity and ZWJ emoji sequences look back
//   at most kMaxRiLookback / kMaxExtendLookback code points so a hostile string cannot make
//   boundary queries quadratic; beyond those limits the answer is still deterministic.
#include "r1ui/text/Grapheme.h"

#include <hb.h>

#include <algorithm>

#include "r1ui/text/Utf8.h"

namespace r1ui::text {

namespace {

constexpr std::size_t kMaxRiLookback = 1024;
constexpr std::size_t kMaxExtendLookback = 256;

enum class Gb : unsigned char {
  Other, Cr, Lf, Control, Extend, Zwj, RegionalIndicator, SpacingMark,
  HangulL, HangulV, HangulT, HangulLv, HangulLvt, ExtPict
};

struct Range {
  char32_t lo;
  char32_t hi;
};

// Approximation of Extended_Pictographic: the assigned emoji blocks plus the legacy symbols
// that render as emoji. Regional indicators and skin-tone modifiers are classified first.
constexpr Range kExtPict[] = {
    {0x00A9, 0x00A9}, {0x00AE, 0x00AE}, {0x203C, 0x203C}, {0x2049, 0x2049}, {0x2122, 0x2122},
    {0x2139, 0x2139}, {0x2194, 0x2199}, {0x21A9, 0x21AA}, {0x231A, 0x231B}, {0x2328, 0x2328},
    {0x2388, 0x2388}, {0x23CF, 0x23CF}, {0x23E9, 0x23F3}, {0x23F8, 0x23FA}, {0x24C2, 0x24C2},
    {0x25AA, 0x25AB}, {0x25B6, 0x25B6}, {0x25C0, 0x25C0}, {0x25FB, 0x25FE}, {0x2600, 0x27BF},
    {0x2934, 0x2935}, {0x2B05, 0x2B07}, {0x2B1B, 0x2B1C}, {0x2B50, 0x2B50}, {0x2B55, 0x2B55},
    {0x3030, 0x3030}, {0x303D, 0x303D}, {0x3297, 0x3297}, {0x3299, 0x3299}, {0x1F000, 0x1FAFF},
    {0x1FC00, 0x1FFFD}};

bool inRange(char32_t cp, char32_t lo, char32_t hi) { return cp >= lo && cp <= hi; }

hb_unicode_general_category_t category(char32_t cp) {
  return hb_unicode_general_category(hb_unicode_funcs_get_default(), cp);
}

Gb classify(char32_t cp) {
  if (cp == 0x0D) return Gb::Cr;
  if (cp == 0x0A) return Gb::Lf;
  if (cp < 0x20 || inRange(cp, 0x7F, 0x9F) || cp == 0x2028 || cp == 0x2029) return Gb::Control;
  if (cp < 0x300) return cp == 0xA9 || cp == 0xAE ? Gb::ExtPict : Gb::Other;
  if (inRange(cp, 0x1F1E6, 0x1F1FF)) return Gb::RegionalIndicator;
  if (cp == 0x200D) return Gb::Zwj;
  if (cp == 0x200C || cp == 0xFF9E || cp == 0xFF9F || inRange(cp, 0x1F3FB, 0x1F3FF) ||
      inRange(cp, 0xE0020, 0xE007F)) {
    return Gb::Extend;
  }
  if (inRange(cp, 0x1100, 0x115F) || inRange(cp, 0xA960, 0xA97C)) return Gb::HangulL;
  if (inRange(cp, 0x1160, 0x11A7) || inRange(cp, 0xD7B0, 0xD7C6)) return Gb::HangulV;
  if (inRange(cp, 0x11A8, 0x11FF) || inRange(cp, 0xD7CB, 0xD7FB)) return Gb::HangulT;
  if (inRange(cp, 0xAC00, 0xD7A3)) return (cp - 0xAC00) % 28 == 0 ? Gb::HangulLv : Gb::HangulLvt;
  switch (category(cp)) {
    case HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK:
    case HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK:
      return Gb::Extend;
    case HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK:
      return Gb::SpacingMark;
    case HB_UNICODE_GENERAL_CATEGORY_CONTROL:
    case HB_UNICODE_GENERAL_CATEGORY_FORMAT:
    case HB_UNICODE_GENERAL_CATEGORY_LINE_SEPARATOR:
    case HB_UNICODE_GENERAL_CATEGORY_PARAGRAPH_SEPARATOR:
      return Gb::Control;
    default:
      break;
  }
  for (const Range& r : kExtPict) {
    if (inRange(cp, r.lo, r.hi)) return Gb::ExtPict;
  }
  return Gb::Other;
}

// Start of the code point that ends at `pos` (pos > 0). Falls back to a one-byte step when the
// bytes before `pos` are not a well-formed sequence so that backward walks always progress.
std::size_t prevCodePointStart(std::string_view text, std::size_t pos) {
  std::size_t start = pos - 1;
  for (int i = 0; i < 3 && start > 0 && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80; ++i) {
    --start;
  }
  const DecodedCodePoint d = decodeUtf8(text, start);
  return start + d.length == pos ? start : pos - 1;
}

// Moves a mid-code-point offset back to the start of its code point.
std::size_t alignDown(std::string_view text, std::size_t offset) {
  if (offset >= text.size()) return text.size();
  std::size_t start = offset;
  for (int i = 0; i < 3 && start > 0 && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80; ++i) {
    --start;
  }
  const DecodedCodePoint d = decodeUtf8(text, start);
  return start + d.length > offset ? start : offset;
}

bool boundaryAt(std::string_view text, std::size_t pos) {
  if (pos == 0 || pos >= text.size()) return true;
  if (alignDown(text, pos) != pos) return false;
  const std::size_t prevStart = prevCodePointStart(text, pos);
  const Gb a = classify(decodeUtf8(text, prevStart).codePoint);
  const Gb b = classify(decodeUtf8(text, pos).codePoint);

  if (a == Gb::Cr && b == Gb::Lf) return false;
  if (a == Gb::Control || a == Gb::Cr || a == Gb::Lf) return true;
  if (b == Gb::Control || b == Gb::Cr || b == Gb::Lf) return true;

  if (a == Gb::HangulL &&
      (b == Gb::HangulL || b == Gb::HangulV || b == Gb::HangulLv || b == Gb::HangulLvt)) {
    return false;
  }
  if ((a == Gb::HangulLv || a == Gb::HangulV) && (b == Gb::HangulV || b == Gb::HangulT)) return false;
  if ((a == Gb::HangulLvt || a == Gb::HangulT) && b == Gb::HangulT) return false;

  if (b == Gb::Extend || b == Gb::Zwj || b == Gb::SpacingMark) return false;

  if (a == Gb::Zwj && b == Gb::ExtPict) {
    std::size_t p = prevStart;
    for (std::size_t i = 0; i < kMaxExtendLookback && p > 0; ++i) {
      p = prevCodePointStart(text, p);
      const Gb c = classify(decodeUtf8(text, p).codePoint);
      if (c == Gb::ExtPict) return false;
      if (c != Gb::Extend) break;
    }
    return true;
  }

  if (a == Gb::RegionalIndicator && b == Gb::RegionalIndicator) {
    std::size_t count = 1;
    std::size_t p = prevStart;
    while (count < kMaxRiLookback && p > 0) {
      p = prevCodePointStart(text, p);
      if (classify(decodeUtf8(text, p).codePoint) != Gb::RegionalIndicator) break;
      ++count;
    }
    return count % 2 == 0;
  }
  return true;
}

enum class WordClass : unsigned char { Space, Word, Punct };

WordClass wordClassOf(char32_t cp) {
  if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0x85 || cp == 0x2028 || cp == 0x2029) {
    return WordClass::Space;
  }
  const Gb g = classify(cp);
  if (g == Gb::ExtPict || g == Gb::RegionalIndicator) return WordClass::Word;
  switch (category(cp)) {
    case HB_UNICODE_GENERAL_CATEGORY_SPACE_SEPARATOR:
      return WordClass::Space;
    case HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER:
    case HB_UNICODE_GENERAL_CATEGORY_LOWERCASE_LETTER:
    case HB_UNICODE_GENERAL_CATEGORY_TITLECASE_LETTER:
    case HB_UNICODE_GENERAL_CATEGORY_MODIFIER_LETTER:
    case HB_UNICODE_GENERAL_CATEGORY_OTHER_LETTER:
    case HB_UNICODE_GENERAL_CATEGORY_DECIMAL_NUMBER:
    case HB_UNICODE_GENERAL_CATEGORY_LETTER_NUMBER:
    case HB_UNICODE_GENERAL_CATEGORY_OTHER_NUMBER:
    case HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK:
    case HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK:
    case HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK:
    case HB_UNICODE_GENERAL_CATEGORY_CONNECT_PUNCTUATION:
      return WordClass::Word;
    default:
      return WordClass::Punct;
  }
}

// Class of the grapheme that starts at `start` (a boundary).
WordClass wordClassAt(std::string_view text, std::size_t start) {
  return wordClassOf(decodeUtf8(text, start).codePoint);
}

}  // namespace

bool isGraphemeBoundary(std::string_view text, std::size_t offset) {
  if (offset > text.size()) return false;
  return boundaryAt(text, offset);
}

std::size_t nextGraphemeBoundary(std::string_view text, std::size_t offset) {
  offset = alignDown(text, offset);
  if (offset >= text.size()) return text.size();
  std::size_t pos = offset + decodeUtf8(text, offset).length;
  while (pos < text.size() && !boundaryAt(text, pos)) pos += decodeUtf8(text, pos).length;
  return pos;
}

std::size_t prevGraphemeBoundary(std::string_view text, std::size_t offset) {
  offset = std::min(offset, text.size());
  std::size_t pos;
  const std::size_t aligned = alignDown(text, offset);
  if (aligned != offset) {
    pos = aligned;  // inside a code point: its start is the first candidate below the offset
  } else if (offset == 0) {
    return 0;
  } else {
    pos = prevCodePointStart(text, offset);
  }
  while (pos > 0 && !boundaryAt(text, pos)) pos = prevCodePointStart(text, pos);
  return pos;
}

std::size_t nextWordBoundary(std::string_view text, std::size_t offset) {
  offset = alignDown(text, offset);
  if (offset >= text.size()) return text.size();
  const WordClass cls = wordClassAt(text, offset);
  std::size_t pos = offset;
  while (pos < text.size() && wordClassAt(text, pos) == cls) pos = nextGraphemeBoundary(text, pos);
  if (cls != WordClass::Space) {
    while (pos < text.size() && wordClassAt(text, pos) == WordClass::Space) {
      pos = nextGraphemeBoundary(text, pos);
    }
  }
  return pos;
}

std::size_t prevWordBoundary(std::string_view text, std::size_t offset) {
  offset = alignDown(text, offset);
  std::size_t pos = offset;
  while (pos > 0) {
    const std::size_t prev = prevGraphemeBoundary(text, pos);
    if (wordClassAt(text, prev) != WordClass::Space) break;
    pos = prev;
  }
  if (pos == 0) return 0;
  const WordClass cls = wordClassAt(text, prevGraphemeBoundary(text, pos));
  while (pos > 0) {
    const std::size_t prev = prevGraphemeBoundary(text, pos);
    if (wordClassAt(text, prev) != cls) break;
    pos = prev;
  }
  return pos;
}

std::pair<std::size_t, std::size_t> wordRangeAt(std::string_view text, std::size_t offset) {
  if (text.empty()) return {0, 0};
  std::size_t start = alignDown(text, offset);
  start = start >= text.size() ? prevGraphemeBoundary(text, text.size())
                               : (boundaryAt(text, start) ? start : prevGraphemeBoundary(text, start));
  const WordClass cls = wordClassAt(text, start);
  std::size_t begin = start;
  while (begin > 0) {
    const std::size_t prev = prevGraphemeBoundary(text, begin);
    if (wordClassAt(text, prev) != cls) break;
    begin = prev;
  }
  std::size_t end = nextGraphemeBoundary(text, start);
  while (end < text.size() && wordClassAt(text, end) == cls) end = nextGraphemeBoundary(text, end);
  return {begin, end};
}

}  // namespace r1ui::text

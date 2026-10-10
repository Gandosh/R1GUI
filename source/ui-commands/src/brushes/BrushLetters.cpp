// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of BrushLetters.h.
// Invariants: decodeUtf8 always advances by at least one byte; foldCodePoint is idempotent (folding a
//   folded code point changes nothing), which the property tests check over the supported ranges.
// Callers: BrushLibraryModel, the brush popup, tests.
#include "r1ui/commands/brushes/BrushLetters.h"

namespace r1ui::commands::brushes {

namespace {

constexpr char32_t kReplacement = 0xFFFD;

bool isContinuation(unsigned char byte) { return (byte & 0xC0u) == 0x80u; }

// Latin-1 letters with accents map to their base letter (the table starts at U+00C0).
constexpr char kLatin1Base[64] = {
    'a', 'a', 'a', 'a', 'a', 'a', 0,   'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',   // C0..CF (C6 AE ligature stays)
    0,   'n', 'o', 'o', 'o', 'o', 'o', 0,   0,   'u', 'u', 'u', 'u', 'y', 0,   0,     // D0..DF (D7 multiplication)
    'a', 'a', 'a', 'a', 'a', 'a', 0,   'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',   // E0..EF
    0,   'n', 'o', 'o', 'o', 'o', 'o', 0,   0,   'u', 'u', 'u', 'u', 'y', 0,   'y'};  // F0..FF (F7 division)

}  // namespace

char32_t decodeUtf8(std::string_view text, size_t& pos) {
  const auto byteAt = [&](size_t i) { return static_cast<unsigned char>(text[i]); };
  const unsigned char lead = byteAt(pos);
  if (lead < 0x80) {
    ++pos;
    return lead;
  }
  size_t length = 0;
  char32_t value = 0;
  char32_t minimum = 0;
  if (lead >= 0xC2 && lead <= 0xDF) {
    length = 2;
    value = lead & 0x1Fu;
    minimum = 0x80;
  } else if (lead >= 0xE0 && lead <= 0xEF) {
    length = 3;
    value = lead & 0x0Fu;
    minimum = 0x800;
  } else if (lead >= 0xF0 && lead <= 0xF4) {
    length = 4;
    value = lead & 0x07u;
    minimum = 0x10000;
  } else {
    ++pos;
    return kReplacement;
  }
  if (pos + length > text.size()) {
    ++pos;
    return kReplacement;
  }
  for (size_t i = 1; i < length; ++i) {
    if (!isContinuation(byteAt(pos + i))) {
      ++pos;
      return kReplacement;
    }
    value = (value << 6) | (byteAt(pos + i) & 0x3Fu);
  }
  if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
    ++pos;
    return kReplacement;
  }
  pos += length;
  return value;
}

void appendUtf8(std::string& out, char32_t cp) {
  if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = kReplacement;
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

char32_t foldCodePoint(char32_t cp) {
  if (cp < 0x80) return cp >= 'A' && cp <= 'Z' ? cp + 32 : cp;
  if (cp >= 0xC0 && cp <= 0xFF) {
    const char base = kLatin1Base[cp - 0xC0];
    if (base != 0) return static_cast<char32_t>(base);
    // Letters without a base letter: lower-case the capital (AE ligature, eth, thorn, o-slash).
    return cp >= 0xC0 && cp <= 0xDE && cp != 0xD7 ? cp + 32 : cp;
  }
  if (cp >= 0x100 && cp <= 0x137) return cp | 1u;                          // pairs: capital even, small odd
  if (cp >= 0x139 && cp <= 0x148) return (cp & 1u) != 0 ? cp + 1 : cp;     // pairs: capital odd, small even
  if (cp >= 0x14A && cp <= 0x177) return cp | 1u;
  if (cp == 0x178) return 'y';
  if (cp >= 0x391 && cp <= 0x3A9 && cp != 0x3A2) return cp + 0x20;        // Greek capitals
  if (cp == 0x3C2) return 0x3C3;                                          // final sigma
  if (cp >= 0x400 && cp <= 0x40F) return cp + 0x50;                       // Cyrillic capitals
  if (cp >= 0x410 && cp <= 0x42F) return cp + 0x20;
  return cp;
}

bool isKeyCharacter(char32_t f) {
  if (f < 0x80) return (f >= 'a' && f <= 'z') || (f >= '0' && f <= '9');
  if (f >= 0xC0 && f <= 0x24F) return f != 0xD7 && f != 0xF7;              // Latin-1 letters, Latin Extended A and B
  if (f >= 0x370 && f <= 0x3FF) return f != 0x37E && f != 0x387;           // Greek (not the question mark or middle dot)
  if (f >= 0x400 && f <= 0x52F) return true;                               // Cyrillic
  if (f >= 0x531 && f <= 0x58F) return true;                               // Armenian
  if (f >= 0x5D0 && f <= 0x5EA) return true;                               // Hebrew letters
  if (f >= 0x620 && f <= 0x64A) return true;                               // Arabic letters
  if (f >= 0x3041 && f <= 0x30FF) return true;                             // kana
  if (f >= 0x3400 && f <= 0x4DBF) return true;                             // CJK extension A
  if (f >= 0x4E00 && f <= 0x9FFF) return true;                             // CJK ideographs
  if (f >= 0xAC00 && f <= 0xD7A3) return true;                             // Hangul syllables
  if (f >= 0xFF21 && f <= 0xFF3A) return true;                             // full-width capitals
  if (f >= 0xFF41 && f <= 0xFF5A) return true;
  return false;
}

std::u32string keyOf(std::string_view text) {
  std::u32string key;
  size_t pos = 0;
  while (pos < text.size() && key.size() < kMaxKeyLength) {
    const char32_t folded = foldCodePoint(decodeUtf8(text, pos));
    if (isKeyCharacter(folded)) key.push_back(folded);
  }
  return key;
}

std::u32string foldText(std::string_view text, size_t maxCodePoints) {
  std::u32string out;
  size_t pos = 0;
  while (pos < text.size() && out.size() < maxCodePoints) out.push_back(foldCodePoint(decodeUtf8(text, pos)));
  return out;
}

std::optional<char32_t> singleKeyLetter(std::string_view text) {
  if (text.empty()) return std::nullopt;
  size_t pos = 0;
  const char32_t folded = foldCodePoint(decodeUtf8(text, pos));
  if (pos != text.size() || !isKeyCharacter(folded)) return std::nullopt;
  return folded;
}

std::string displayKey(std::u32string_view key, size_t length) {
  std::string out;
  for (size_t i = 0; i < key.size() && i < length; ++i) {
    char32_t c = key[i];
    if (c >= 'a' && c <= 'z') c -= 32;
    else if (c >= 0x3B1 && c <= 0x3C9) c -= 0x20;
    else if (c >= 0x430 && c <= 0x44F) c -= 0x20;
    else if (c >= 0x450 && c <= 0x45F) c -= 0x50;
    appendUtf8(out, c);
  }
  return out;
}

}  // namespace r1ui::commands::brushes

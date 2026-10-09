// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of the strict UTF-8 decoder declared in r1ui/text/Utf8.h.
// Invariants: decodeUtf8 always consumes at least one byte and never reads past text.size().
#include "r1ui/text/Utf8.h"

namespace r1ui::text {

// Table-free decoder following the Unicode well-formedness ranges for the second byte, which
// is what rejects overlongs (E0 80.., F0 80..), surrogates (ED A0..) and values > U+10FFFF.
DecodedCodePoint decodeUtf8(std::string_view text, std::size_t pos) {
  const auto at = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
  const unsigned char b0 = at(pos);
  if (b0 < 0x80) return {b0, 1, true};

  std::size_t needed = 0;
  unsigned char lo = 0x80;
  unsigned char hi = 0xBF;
  char32_t cp = 0;
  if (b0 >= 0xC2 && b0 <= 0xDF) {
    needed = 1;
    cp = b0 & 0x1Fu;
  } else if (b0 >= 0xE0 && b0 <= 0xEF) {
    needed = 2;
    cp = b0 & 0x0Fu;
    if (b0 == 0xE0) lo = 0xA0;
    if (b0 == 0xED) hi = 0x9F;
  } else if (b0 >= 0xF0 && b0 <= 0xF4) {
    needed = 3;
    cp = b0 & 0x07u;
    if (b0 == 0xF0) lo = 0x90;
    if (b0 == 0xF4) hi = 0x8F;
  } else {
    return {kReplacementChar, 1, false};
  }

  std::size_t consumed = 1;
  for (std::size_t i = 0; i < needed; ++i) {
    if (pos + consumed >= text.size()) {
      return {kReplacementChar, static_cast<std::uint8_t>(consumed), false};
    }
    const unsigned char b = at(pos + consumed);
    const unsigned char minByte = i == 0 ? lo : 0x80;
    const unsigned char maxByte = i == 0 ? hi : 0xBF;
    if (b < minByte || b > maxByte) {
      return {kReplacementChar, static_cast<std::uint8_t>(consumed), false};
    }
    cp = (cp << 6) | (b & 0x3Fu);
    ++consumed;
  }
  return {cp, static_cast<std::uint8_t>(consumed), true};
}

bool isValidUtf8(std::string_view text) {
  for (std::size_t pos = 0; pos < text.size();) {
    const DecodedCodePoint d = decodeUtf8(text, pos);
    if (!d.valid) return false;
    pos += d.length;
  }
  return true;
}

std::string sanitizeUtf8(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t pos = 0; pos < text.size();) {
    const DecodedCodePoint d = decodeUtf8(text, pos);
    if (d.valid) {
      out.append(text.substr(pos, d.length));
    } else {
      appendUtf8(out, kReplacementChar);
    }
    pos += d.length;
  }
  return out;
}

void appendUtf8(std::string& out, char32_t cp) {
  if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = kReplacementChar;
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

}  // namespace r1ui::text

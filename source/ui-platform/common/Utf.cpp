// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: strict UTF-8 / UTF-16 conversion and WM_CHAR-style code point assembly (see Utf.h).
// Why: one validated implementation for every OS-boundary text path; the OS conversion
//   functions differ in how they treat lone surrogates and overlong forms, so we do not use them.
// Callers: Window backend, clipboard backend, tests. Portable: standard library only.
// Invariants: decoding accepts exactly well-formed UTF-8 (RFC 3629): shortest form, no
//   surrogate code points, maximum U+10FFFF.
#include "r1ui/platform/Utf.h"

#include <cstddef>

namespace r1ui::platform {

namespace {

constexpr char32_t kMaxCodePoint = 0x10FFFF;

bool isSurrogate(char32_t cp) { return cp >= 0xD800 && cp <= 0xDFFF; }
bool isHighSurrogate(char16_t u) { return u >= 0xD800 && u <= 0xDBFF; }
bool isLowSurrogate(char16_t u) { return u >= 0xDC00 && u <= 0xDFFF; }

// Decodes one code point at s[i]. Returns the number of bytes consumed, or 0 if the sequence is
// not well-formed (invalid lead byte, bad continuation, overlong, surrogate, too large, truncated).
size_t decodeOne(std::string_view s, size_t i, char32_t& cp) {
  const auto b0 = static_cast<unsigned char>(s[i]);
  if (b0 < 0x80) {
    cp = b0;
    return 1;
  }
  size_t len = 0;
  char32_t minimum = 0;
  if (b0 >= 0xC2 && b0 <= 0xDF) {
    len = 2;
    cp = b0 & 0x1Fu;
    minimum = 0x80;
  } else if ((b0 & 0xF0u) == 0xE0u) {
    len = 3;
    cp = b0 & 0x0Fu;
    minimum = 0x800;
  } else if (b0 >= 0xF0 && b0 <= 0xF4) {
    len = 4;
    cp = b0 & 0x07u;
    minimum = 0x10000;
  } else {
    return 0;
  }
  if (s.size() - i < len) return 0;
  for (size_t k = 1; k < len; ++k) {
    const auto b = static_cast<unsigned char>(s[i + k]);
    if ((b & 0xC0u) != 0x80u) return 0;
    cp = (cp << 6) | (b & 0x3Fu);
  }
  if (cp < minimum || cp > kMaxCodePoint || isSurrogate(cp)) return 0;
  return len;
}

void appendUtf8(std::string& out, char32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else {
    out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  }
}

char32_t combineSurrogates(char16_t high, char16_t low) {
  return 0x10000u + ((static_cast<char32_t>(high) - 0xD800u) << 10) + (static_cast<char32_t>(low) - 0xDC00u);
}

}  // namespace

bool isValidUtf8(std::string_view utf8) {
  for (size_t i = 0; i < utf8.size();) {
    char32_t cp = 0;
    const size_t n = decodeOne(utf8, i, cp);
    if (n == 0) return false;
    i += n;
  }
  return true;
}

std::string replaceInvalidUtf8(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    char32_t cp = 0;
    const size_t n = decodeOne(text, i, cp);
    if (n == 0) {
      appendUtf8(out, 0xFFFD);
      ++i;
    } else {
      out.append(text.substr(i, n));
      i += n;
    }
  }
  return out;
}

std::optional<std::u16string> utf8ToUtf16(std::string_view utf8) {
  std::u16string out;
  out.reserve(utf8.size());
  for (size_t i = 0; i < utf8.size();) {
    char32_t cp = 0;
    const size_t n = decodeOne(utf8, i, cp);
    if (n == 0) return std::nullopt;
    i += n;
    if (cp < 0x10000) {
      out.push_back(static_cast<char16_t>(cp));
    } else {
      const char32_t v = cp - 0x10000;
      out.push_back(static_cast<char16_t>(0xD800u + (v >> 10)));
      out.push_back(static_cast<char16_t>(0xDC00u + (v & 0x3FFu)));
    }
  }
  return out;
}

std::optional<std::string> utf16ToUtf8(std::u16string_view utf16) {
  std::string out;
  out.reserve(utf16.size());
  for (size_t i = 0; i < utf16.size(); ++i) {
    const char16_t u = utf16[i];
    if (isHighSurrogate(u)) {
      if (i + 1 >= utf16.size() || !isLowSurrogate(utf16[i + 1])) return std::nullopt;
      appendUtf8(out, combineSurrogates(u, utf16[i + 1]));
      ++i;
    } else if (isLowSurrogate(u)) {
      return std::nullopt;
    } else {
      appendUtf8(out, u);
    }
  }
  return out;
}

bool isTextCodePoint(char32_t cp) {
  if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) return false;
  return cp <= kMaxCodePoint && !isSurrogate(cp);
}

std::optional<char32_t> CodePointAssembler::feed(char16_t unit) {
  if (isHighSurrogate(unit)) {
    high_ = unit;  // a second high surrogate replaces an unpaired one
    return std::nullopt;
  }
  if (isLowSurrogate(unit)) {
    if (high_ == 0) return std::nullopt;  // unpaired low surrogate
    const char32_t cp = combineSurrogates(high_, unit);
    high_ = 0;
    return cp;
  }
  high_ = 0;  // a pending high surrogate followed by a BMP unit was unpaired: discard it
  return static_cast<char32_t>(unit);
}

}  // namespace r1ui::platform

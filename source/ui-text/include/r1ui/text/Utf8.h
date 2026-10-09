// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: strict UTF-8 decoding, validation and sanitizing (the single place that decides what
//   "valid text" means for ui-text).
// Why: shaping, grapheme walking and the editor all take bytes from outside the process; they
//   share one decoder so a lone surrogate or overlong form is classified the same everywhere.
// Callers: Shaper, Grapheme, TextEditor, tests.
// Policy: invalid input is never rejected here, it is replaced. Each maximal invalid subpart
//   (Unicode "substitution of maximal subparts") becomes one U+FFFD. Surrogates (U+D800..DFFF),
//   overlong forms and code points above U+10FFFF are invalid. U+0000 is valid UTF-8 and is
//   passed through (callers that cannot accept it, such as the editor, filter it themselves).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace r1ui::text {

inline constexpr char32_t kReplacementChar = 0xFFFD;

struct DecodedCodePoint {
  char32_t codePoint = kReplacementChar;
  std::uint8_t length = 1;  // bytes consumed, always >= 1 so scanning loops terminate
  bool valid = true;        // false: codePoint is U+FFFD standing in for a maximal invalid subpart
};

// Decodes the code point starting at `pos`. Precondition: pos < text.size().
DecodedCodePoint decodeUtf8(std::string_view text, std::size_t pos);

bool isValidUtf8(std::string_view text);

// Returns `text` with every maximal invalid subpart replaced by U+FFFD.
std::string sanitizeUtf8(std::string_view text);

// Appends the UTF-8 encoding of `cp`; surrogates and values above U+10FFFF encode U+FFFD.
void appendUtf8(std::string& out, char32_t cp);

}  // namespace r1ui::text

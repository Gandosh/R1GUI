// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: strict UTF-8 / UTF-16 validation and conversion, and assembly of UTF-16 code units into
//   Unicode code points (WM_CHAR surrogate pairs).
// Why: text crossing the OS boundary (clipboard, titles, typed characters) must be validated,
//   never truncated or passed through as invalid sequences (guard register: Text and Unicode).
// Callers: Window.h implementation, clipboard backend, tests. Portable: no OS headers.
// Failure behavior: conversions return std::nullopt on any invalid input (overlong forms,
//   surrogate code points, values above U+10FFFF, truncated sequences, lone UTF-16 surrogates).
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace r1ui::platform {

bool isValidUtf8(std::string_view utf8);
// Copy of `text` in which every byte that is not part of a well-formed sequence is replaced by
// U+FFFD, so display-only strings (window titles) never fail on bad input.
std::string replaceInvalidUtf8(std::string_view text);
std::optional<std::u16string> utf8ToUtf16(std::string_view utf8);
std::optional<std::string> utf16ToUtf8(std::u16string_view utf16);

// True for code points the toolkit accepts as typed text: valid scalar values that are not
// control characters (C0, DEL, C1).
bool isTextCodePoint(char32_t codePoint);

// Combines UTF-16 units into code points. A high surrogate is held until its low surrogate
// arrives; an unpaired surrogate is discarded and the assembler stays usable.
class CodePointAssembler {
 public:
  // Returns the completed code point, or nullopt when more input is needed or the unit was an
  // unpaired surrogate.
  std::optional<char32_t> feed(char16_t unit);
  void reset() { high_ = 0; }

 private:
  char16_t high_ = 0;
};

}  // namespace r1ui::platform

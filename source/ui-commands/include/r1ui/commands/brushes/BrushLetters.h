// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the text rules of the brush library's quick letters: UTF-8 decoding that never fails, the simple
//   case and accent folding, the definition of a "key character" and the key of a name (the folded
//   letters and digits of the name, in order).
// Why: the library narrows by typing the first letters of a name. Names come from hosts and files and may
//   start with digits, symbols, non-Latin letters, contain invalid UTF-8 or be thousands of characters
//   long; one place defines what a typed letter means so the model, the popup and the tests agree.
// Callers: BrushLibraryModel, the brush popup (to decide which typed characters narrow the list), tests.
// Folding: ASCII letters, the Latin-1 and Latin Extended-A letters, Greek and Cyrillic fold to lower case;
//   Latin-1 letters with accents also lose the accent (e with an acute accent types as e). Other scripts
//   are kept as they are. This is a deliberate approximation of Unicode case folding, not the full table.
// Key characters: ASCII letters and digits and letters or digits of the scripts above plus CJK ideographs,
//   kana and Hangul by block. Spaces, punctuation, symbols, emoji and control characters are skipped, so
//   "Clay Buildup" has the key "claybuildup" and a name made only of symbols has an empty key.
// Failure behavior: nothing throws; invalid UTF-8 decodes to U+FFFD, one byte at a time.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace r1ui::commands::brushes {

// Longest key kept per brush; a name past it is matched on its first kMaxKeyLength characters.
inline constexpr size_t kMaxKeyLength = 64;

// Decodes the code point at `pos` and advances `pos` past it (one byte for an invalid sequence, which
// decodes to U+FFFD). `pos` must be below text.size().
char32_t decodeUtf8(std::string_view text, size_t& pos);
void appendUtf8(std::string& out, char32_t codePoint);

// Lower case, accent-less form of a code point (see the header comment).
char32_t foldCodePoint(char32_t codePoint);
// True for a folded code point that counts as a letter or digit of a key.
bool isKeyCharacter(char32_t folded);

// The folded key characters of `text`, at most kMaxKeyLength of them.
std::u32string keyOf(std::string_view text);
// Folds every code point of `text` (spaces and punctuation stay), at most `maxCodePoints` of them.
std::u32string foldText(std::string_view text, size_t maxCodePoints);
// The folded code point when `text` is exactly one letter or digit (after folding); nullopt otherwise.
std::optional<char32_t> singleKeyLetter(std::string_view text);
// A key, upper-cased for display (the badge on a tile); the inverse of nothing, display only.
std::string displayKey(std::u32string_view key, size_t length);

}  // namespace r1ui::commands::brushes

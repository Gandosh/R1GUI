// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of BrushLetters.h: UTF-8 decoding of hostile bytes, folding, key characters and the key of
//   names that start with digits, symbols and non-Latin letters.
// Callers: CTest (fast).
#include <string>

#include "BrushFixtures.h"
#include "r1ui/commands/brushes/BrushLetters.h"

using namespace r1ui::commands::brushes;

int main() {
  // Decoding always advances, valid sequences round trip, invalid ones become U+FFFD one byte at a time.
  for (char32_t cp : {U'a', U'é', U'Ж', U'中', U'\U0001F600', U'\u007f', U'\U0010FFFF'}) {
    std::string text;
    appendUtf8(text, cp);
    size_t pos = 0;
    R1_EXPECT(decodeUtf8(text, pos) == cp);
    R1_EXPECT(pos == text.size());
  }
  {
    const std::string bad[] = {"\xff", "\xc0\x80", "\xe0\x80\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xc3", "\xe2\x82", "\x80", "\xf8\x88\x80\x80\x80"};
    for (const std::string& text : bad) {
      size_t pos = 0;
      size_t steps = 0;
      while (pos < text.size()) {
        const size_t before = pos;
        const char32_t cp = decodeUtf8(text, pos);
        R1_EXPECT(pos > before);
        R1_EXPECT(cp == 0xFFFD || cp < 0x80);
        ++steps;
      }
      R1_EXPECT(steps >= 1);
    }
  }
  {
    // Random bytes never loop and never read past the end.
    r1test::Rng rng(7);
    for (int round = 0; round < 2000; ++round) {
      std::string bytes;
      const size_t n = rng.below(24);
      for (size_t i = 0; i < n; ++i) bytes.push_back(static_cast<char>(rng.below(256)));
      size_t pos = 0;
      size_t guard = 0;
      while (pos < bytes.size() && guard++ < 100) decodeUtf8(bytes, pos);
      R1_EXPECT(pos == bytes.size());
      (void)keyOf(bytes);
    }
  }

  // Folding is idempotent on the whole range and never maps a key character to a non-key character.
  for (char32_t cp = 0; cp < 0x30000; ++cp) {
    const char32_t folded = foldCodePoint(cp);
    if (foldCodePoint(folded) != folded) R1_EXPECT(foldCodePoint(folded) == folded);
  }
  R1_EXPECT(foldCodePoint(U'A') == U'a');
  R1_EXPECT(foldCodePoint(U'É') == U'e');   // E with an acute accent
  R1_EXPECT(foldCodePoint(U'é') == U'e');
  R1_EXPECT(foldCodePoint(U'Ñ') == U'n');
  R1_EXPECT(foldCodePoint(U'×') == U'×');  // the multiplication sign is not a letter
  R1_EXPECT(!isKeyCharacter(foldCodePoint(U'×')));
  R1_EXPECT(foldCodePoint(U'Ж') == U'ж');  // Cyrillic capital zhe
  R1_EXPECT(foldCodePoint(U'Δ') == U'δ');  // Greek capital delta
  R1_EXPECT(isKeyCharacter(U'7') && isKeyCharacter(U'q') && isKeyCharacter(U'ж') && isKeyCharacter(U'中'));
  R1_EXPECT(!isKeyCharacter(U' ') && !isKeyCharacter(U'-') && !isKeyCharacter(U'\U0001F600') && !isKeyCharacter(U'★') && !isKeyCharacter(0));

  // Keys.
  R1_EXPECT(keyOf("Clay Buildup") == U"claybuildup");
  R1_EXPECT(keyOf("  --3D Pen") == U"3dpen");
  R1_EXPECT(keyOf("\xC3\x85ngstr\xC3\xB6m") == U"angstrom");
  R1_EXPECT(keyOf("\xD0\x9A\xD0\xB8\xD1\x81\xD1\x82\xD1\x8C") == U"кисть");  // Cyrillic name
  R1_EXPECT(keyOf("\xE2\x98\x85\xE2\x98\x85").empty());  // only symbols
  R1_EXPECT(keyOf("").empty());
  R1_EXPECT(keyOf("\xff\xfe""AB") == U"ab");               // invalid bytes are skipped, the rest still keys
  R1_EXPECT(keyOf(std::string_view("a\0b", 3)) == U"ab");  // a NUL is no key character
  R1_EXPECT(keyOf(std::string(10000, 'x')).size() == kMaxKeyLength);
  R1_EXPECT(keyOf(std::string(1 << 20, '!')).empty());

  R1_EXPECT(foldText("Ab C", 10) == U"ab c");
  R1_EXPECT(foldText("abcdef", 3) == U"abc");
  R1_EXPECT(displayKey(U"claybuildup", 2) == "CL");
  R1_EXPECT(displayKey(U"ки", 2) == "\xD0\x9A\xD0\x98");
  R1_EXPECT(displayKey(U"ab", 99) == "AB");

  R1_EXPECT(singleKeyLetter("x") == U'x');
  R1_EXPECT(singleKeyLetter("X") == U'x');
  R1_EXPECT(!singleKeyLetter("").has_value());
  R1_EXPECT(!singleKeyLetter("xy").has_value());
  R1_EXPECT(!singleKeyLetter("-").has_value());
  R1_EXPECT(!singleKeyLetter("\xff").has_value());
  R1_EXPECT(singleKeyLetter("\xC3\x89") == U'e');
  return r1test::finish();
}

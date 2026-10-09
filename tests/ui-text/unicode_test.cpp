// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for Utf8 (decode, sanitize) and Grapheme (cluster and word boundaries),
//   including hostile input: invalid bytes, surrogates, combining-mark storms, long flag runs.
// Callers: CTest (label fast). Exit code 0 = pass.
#include <cstdint>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/text/Grapheme.h"
#include "r1ui/text/Utf8.h"

using namespace r1ui::text;
using namespace r1ui::text::testing;

namespace {

std::vector<std::size_t> forwardBoundaries(const std::string& s) {
  std::vector<std::size_t> out{0};
  for (std::size_t p = 0; p < s.size();) {
    p = nextGraphemeBoundary(s, p);
    out.push_back(p);
  }
  return out;
}

std::vector<std::size_t> backwardBoundaries(const std::string& s) {
  std::vector<std::size_t> out{s.size()};
  for (std::size_t p = s.size(); p > 0;) {
    p = prevGraphemeBoundary(s, p);
    out.insert(out.begin(), p);
  }
  return out;
}

std::size_t clusterCount(const std::string& s) { return forwardBoundaries(s).size() - 1; }

void testUtf8() {
  expect(isValidUtf8(""), "empty is valid");
  expect(isValidUtf8(utf8({'a', 0xE9, 0x20AC, 0x1F600, 0x10FFFF, 0})), "mixed valid text");
  expect(!isValidUtf8("\xC0\x80"), "overlong 2-byte NUL");
  expect(!isValidUtf8("\xE0\x80\x80"), "overlong 3-byte");
  expect(!isValidUtf8("\xED\xA0\x80"), "UTF-8 encoded surrogate");
  expect(!isValidUtf8("\xF4\x90\x80\x80"), "above U+10FFFF");
  expect(!isValidUtf8("\xFE"), "invalid lead byte");
  expect(!isValidUtf8("\x80"), "lone continuation");
  expect(!isValidUtf8("\xE2\x82"), "truncated sequence");

  expect(sanitizeUtf8("a\xE2\x82" "b") == utf8({'a', 0xFFFD, 'b'}), "truncated sequence is one U+FFFD");
  expect(sanitizeUtf8("\xED\xA0\x80") == utf8({0xFFFD, 0xFFFD, 0xFFFD}), "surrogate becomes 3 replacements");
  expect(sanitizeUtf8("\xFF\xFE") == utf8({0xFFFD, 0xFFFD}), "two invalid lead bytes");
  const std::string withNul("a\0b", 3);
  expect(sanitizeUtf8(withNul) == withNul, "NUL passes through the sanitizer");
  expect(isValidUtf8(sanitizeUtf8("\xF0\x9F\x98")), "sanitized output is valid");

  std::string appended;
  appendUtf8(appended, 0xD800);
  appendUtf8(appended, 0x110000);
  expect(appended == utf8({0xFFFD, 0xFFFD}), "appendUtf8 refuses surrogates and out-of-range values");

  // Every single byte alone and every 2-byte prefix combination decodes without overrun.
  for (int a = 0; a < 256; ++a) {
    for (int b : {0x00, 0x80, 0xA0, 0xBF, 0xC2, 0xFF}) {
      const char buf[2] = {static_cast<char>(a), static_cast<char>(b)};
      const std::string s(buf, 2);
      std::size_t pos = 0;
      while (pos < s.size()) pos += decodeUtf8(s, pos).length;
      expect(pos == s.size(), "decoder consumes exactly the input");
    }
  }
}

void testGraphemes() {
  expect(forwardBoundaries("abc") == (std::vector<std::size_t>{0, 1, 2, 3}), "ASCII boundaries");
  expect(forwardBoundaries(utf8({'e', 0x301, 'x'})) == (std::vector<std::size_t>{0, 3, 4}), "combining acute");
  expect(forwardBoundaries("a\r\nb") == (std::vector<std::size_t>{0, 1, 3, 4}), "CRLF is one cluster");
  expect(clusterCount(utf8({0x1F1FA, 0x1F1F8, 0x1F1EB, 0x1F1F7})) == 2, "two flags");
  expect(clusterCount(utf8({0x1F1FA, 0x1F1F8, 0x1F1EB})) == 2, "odd regional indicator stands alone");
  expect(clusterCount(utf8({0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467, 0x200D, 0x1F466})) == 1, "ZWJ family");
  expect(clusterCount(utf8({0x1F469, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468})) == 1, "ZWJ couple with VS16");
  expect(clusterCount(utf8({0x1F44D, 0x1F3FD})) == 1, "skin tone modifier");
  expect(clusterCount(utf8({0x2764, 0xFE0F})) == 1, "variation selector 16");
  expect(clusterCount(utf8({'1', 0xFE0F, 0x20E3})) == 1, "keycap sequence");
  expect(clusterCount(utf8({0xD55C})) == 1, "precomposed Hangul syllable");
  expect(clusterCount(utf8({0x1112, 0x1161, 0x11AB})) == 1, "Hangul L V T jamo");
  expect(clusterCount(utf8({0xAC00, 0x11A8})) == 1, "Hangul LV + T");
  expect(clusterCount(utf8({0xAC01, 0x11A8})) == 1, "Hangul LVT + T");
  expect(clusterCount(utf8({0x1100, 0x1100})) == 1, "Hangul L + L");
  expect(clusterCount(utf8({0xAC00, 0xAC00})) == 2, "two syllables stay apart");
  expect(clusterCount(utf8({0x0915, 0x093E})) == 1, "Devanagari consonant + spacing mark");
  expect(clusterCount(utf8({0x1F600, 0x1F600})) == 2, "two emoji");
  expect(clusterCount(utf8({'a', 0x200D, 'b'})) == 2, "ZWJ joins to its base; the next letter starts a cluster");
  expect(clusterCount(std::string("a\0b", 3)) == 3, "NUL is its own cluster");

  expect(!isGraphemeBoundary(utf8({0x20AC, 'a'}), 1), "not a boundary inside a multi-byte character");
  expect(!isGraphemeBoundary(utf8({'e', 0x301}), 1), "not a boundary before a combining mark");
  expect(isGraphemeBoundary("abc", 3), "end is a boundary");
  expect(!isGraphemeBoundary("abc", 4), "past the end is not a boundary");
  expect(nextGraphemeBoundary("abc", 99) == 3, "next clamps");
  expect(prevGraphemeBoundary("abc", 0) == 0, "prev at 0 stays");

  // Forward and backward walks must agree on every string, including hostile ones.
  const std::vector<std::string> samples = {
      "", "abc", utf8({'e', 0x301, 0x302, 'x'}), utf8({0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467}),
      utf8({0x1F1FA, 0x1F1F8, 0x1F1FA, 0x1F1F8, 0x1F1FA}), "a\r\n\r\nb", utf8({0x1100, 0x1161, 0x11A8, 'x'}),
      "\xFF\xE2\x82" "a\xED\xA0\x80", utf8({0x200D, 0x200D, 0x1F600}), utf8({0x301, 0x301})};
  for (const std::string& s : samples) {
    expect(forwardBoundaries(s) == backwardBoundaries(s), "forward and backward boundaries agree");
  }

  // Combining-mark storm: one cluster, linear time.
  std::string storm = "a";
  for (int i = 0; i < 100000; ++i) appendUtf8(storm, 0x301);
  storm += "b";
  expect(forwardBoundaries(storm).size() == 3, "100k combining marks form one cluster");
  expect(prevGraphemeBoundary(storm, storm.size()) == storm.size() - 1, "prev over a mark storm");
  expect(prevGraphemeBoundary(storm, storm.size() - 1) == 0, "prev jumps across the whole storm");

  // Long regional-indicator run: must terminate quickly (bounded look-back) and stay consistent.
  std::string flags;
  for (int i = 0; i < 20000; ++i) appendUtf8(flags, 0x1F1E6 + (i % 26));
  expect(forwardBoundaries(flags) == backwardBoundaries(flags), "long flag run is consistent");
}

void testWords() {
  const std::string s = "hello  world.foo";
  expect(nextWordBoundary(s, 0) == 7, "word right skips word and spaces");
  expect(nextWordBoundary(s, 7) == 12, "word right stops before punctuation run");
  expect(nextWordBoundary(s, 12) == 13, "punctuation run is its own unit");
  expect(nextWordBoundary(s, s.size()) == s.size(), "word right at end");
  expect(prevWordBoundary(s, s.size()) == 13, "word left from end");
  expect(prevWordBoundary(s, 7) == 0, "word left skips spaces then word");
  expect(prevWordBoundary(s, 0) == 0, "word left at start");
  expect(wordRangeAt(s, 2) == (std::pair<std::size_t, std::size_t>{0, 5}), "word at offset 2");
  expect(wordRangeAt(s, 6) == (std::pair<std::size_t, std::size_t>{5, 7}), "whitespace run");
  expect(wordRangeAt(s, 12) == (std::pair<std::size_t, std::size_t>{12, 13}), "punctuation unit");
  expect(wordRangeAt(s, s.size()) == (std::pair<std::size_t, std::size_t>{13, 16}), "word at end of text");
  expect(wordRangeAt("", 0) == (std::pair<std::size_t, std::size_t>{0, 0}), "empty text");
  const std::string accented = utf8({0x00E9, 't', 0x00E9, ' ', 'x'});
  expect(wordRangeAt(accented, 1) == (std::pair<std::size_t, std::size_t>{0, 5}), "non-ASCII letters are word characters");
}

// Random strings over a small alphabet of troublesome code points and raw bytes: every walk
// must terminate, move strictly, and keep both directions consistent.
void testRandomized() {
  std::uint32_t state = 12345;
  const auto next = [&]() {
    state = state * 1664525u + 1013904223u;
    return state >> 8;
  };
  const char32_t alphabet[] = {'a', ' ', '.', 0x301, 0x200D, 0x1F600, 0x1F1E6, 0x1F3FD, 0xFE0F, 0x1100,
                               0x1161, 0x11A8, 0xAC00, 0x0915, 0x093E, '\r', '\n', 0x2764, 0x20E3, 0};
  for (int iter = 0; iter < 3000; ++iter) {
    std::string s;
    const int n = static_cast<int>(next() % 24);
    for (int i = 0; i < n; ++i) {
      if (next() % 10 == 0) {
        s.push_back(static_cast<char>(next() & 0xFF));
      } else {
        appendUtf8(s, alphabet[next() % (sizeof(alphabet) / sizeof(alphabet[0]))]);
      }
    }
    expect(forwardBoundaries(s) == backwardBoundaries(s), "random: directions agree");
    for (std::size_t off = 0; off <= s.size() + 1; ++off) {
      const std::size_t nb = nextGraphemeBoundary(s, off);
      const std::size_t pb = prevGraphemeBoundary(s, off);
      expect(nb <= s.size() && pb <= s.size(), "random: results within the text");
      expect(off >= s.size() || nb > off || nb == s.size(), "random: next advances");
      const std::size_t w = nextWordBoundary(s, off);
      const std::size_t v = prevWordBoundary(s, off);
      expect(w <= s.size() && v <= s.size(), "random: word moves in range");
      const auto range = wordRangeAt(s, off);
      expect(range.first <= range.second && range.second <= s.size(), "random: word range sane");
    }
  }
}

}  // namespace

int main() {
  testUtf8();
  testGraphemes();
  testWords();
  testRandomized();
  return finish("unicode");
}

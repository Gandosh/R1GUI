// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for strict UTF-8/UTF-16 conversion and code point assembly (Utf.h), including
//   hostile byte sequences.
// Callers: CTest (label fast). Exit code 0 = pass.
#include <string>

#include "TestSupport.h"
#include "r1ui/platform/Utf.h"

using namespace r1ui::platform;
using platform_test::expect;
using platform_test::runCase;

namespace {

void validRoundTrips() {
  const std::string text = "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";  // A, e-acute, euro, grinning face
  const auto u16 = utf8ToUtf16(text);
  expect(u16.has_value() && u16->size() == 5, "1+1+1+2 UTF-16 units");
  expect(u16 && (*u16)[3] == 0xD83D && (*u16)[4] == 0xDE00, "emoji becomes a surrogate pair");
  const auto back = u16 ? utf16ToUtf8(*u16) : std::nullopt;
  expect(back && *back == text, "round trip is exact");
  expect(isValidUtf8(""), "empty is valid");
  expect(utf8ToUtf16("") && utf8ToUtf16("")->empty(), "empty converts");
  expect(isValidUtf8(std::string("a\0b", 3)), "embedded NUL is valid UTF-8 (rejected by the clipboard, not here)");
  expect(isValidUtf8("\xF4\x8F\xBF\xBF"), "U+10FFFF is the last valid code point");
  expect(isValidUtf8("\xED\x9F\xBF"), "U+D7FF just below the surrogates is valid");
  expect(isValidUtf8("\xEE\x80\x80"), "U+E000 just above the surrogates is valid");
}

void invalidBytesBecomeReplacementCharacters() {
  expect(replaceInvalidUtf8("plain \xE2\x82\xAC ok") == "plain \xE2\x82\xAC ok", "valid text is unchanged");
  expect(replaceInvalidUtf8("\xC3\x28") == "\xEF\xBF\xBD(", "bad continuation: lead byte replaced, '(' kept");
  expect(replaceInvalidUtf8("a\xE9z") == "a\xEF\xBF\xBD" "z", "an ANSI e-acute becomes U+FFFD");
  expect(replaceInvalidUtf8("\xED\xA0\x80") == "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD", "surrogate bytes are replaced one by one");
  expect(replaceInvalidUtf8("\xE2\x82") == "\xEF\xBF\xBD\xEF\xBF\xBD", "truncated sequence");
  std::string every;
  for (int b = 0; b < 256; ++b) every.push_back(static_cast<char>(b));
  expect(isValidUtf8(replaceInvalidUtf8(every)), "the result is always valid UTF-8");
  expect(replaceInvalidUtf8("").empty(), "empty");
}

void invalidUtf8IsRejected() {
  const char* bad[] = {
      "\x80",              // lone continuation byte
      "\xC0\x80",          // overlong NUL
      "\xC1\xBF",          // overlong
      "\xE0\x80\x80",      // overlong 3-byte
      "\xF0\x80\x80\x80",  // overlong 4-byte
      "\xED\xA0\x80",      // U+D800 surrogate
      "\xED\xBF\xBF",      // U+DFFF surrogate
      "\xF4\x90\x80\x80",  // U+110000 too large
      "\xF5\x80\x80\x80",  // invalid lead byte
      "\xFF",              // never valid
      "\xFE",              //
      "\xE2\x82",          // truncated 3-byte
      "\xF0\x9F\x98",      // truncated 4-byte
      "\xC3",              // truncated 2-byte
      "\xC3\x28",          // bad continuation
      "A\xE2\x28\xA1",     // bad continuation mid-string
  };
  for (const char* s : bad) {
    expect(!isValidUtf8(s), "invalid sequence rejected by isValidUtf8");
    expect(!utf8ToUtf16(s).has_value(), "invalid sequence rejected by utf8ToUtf16");
  }
}

void invalidUtf16IsRejected() {
  expect(!utf16ToUtf8(u"\xD800").has_value(), "lone high surrogate");
  expect(!utf16ToUtf8(u"\xDC00").has_value(), "lone low surrogate");
  expect(!utf16ToUtf8(u"a\xD800" u"b").has_value(), "high surrogate followed by a BMP unit");
  expect(!utf16ToUtf8(u"\xDC00\xD800").has_value(), "reversed pair");
  expect(utf16ToUtf8(u"\xD83D\xDE00").has_value(), "valid pair accepted");
}

void textCodePointFilter() {
  expect(!isTextCodePoint(0x00) && !isTextCodePoint(0x08) && !isTextCodePoint(0x0D) && !isTextCodePoint(0x1F),
         "C0 controls filtered");
  expect(!isTextCodePoint(0x7F) && !isTextCodePoint(0x80) && !isTextCodePoint(0x9F), "DEL and C1 filtered");
  expect(isTextCodePoint(0x20) && isTextCodePoint(0xA0) && isTextCodePoint(0x1F600), "printable accepted");
  expect(!isTextCodePoint(0xD800) && !isTextCodePoint(0x110000), "surrogates and out-of-range filtered");
}

void assemblerPairsSurrogates() {
  CodePointAssembler a;
  expect(a.feed(u'a') == U'a', "BMP unit passes through");
  expect(!a.feed(0xD83D).has_value(), "high surrogate is held");
  expect(a.feed(0xDE00) == char32_t{0x1F600}, "low surrogate completes the pair");
  expect(!a.feed(0xDE00).has_value(), "unpaired low surrogate is discarded");
  expect(!a.feed(0xD83D).has_value(), "high surrogate held again");
  expect(a.feed(u'b') == U'b', "BMP unit after an unpaired high surrogate still arrives");
  expect(!a.feed(0xDE00).has_value(), "the discarded high surrogate does not pair later");
  expect(!a.feed(0xD83D).has_value() && !a.feed(0xD83E).has_value(), "second high replaces the first");
  expect(a.feed(0xDD00) == char32_t{0x1F900}, "pair formed with the newer high surrogate");
  a.feed(0xD83D);
  a.reset();
  expect(!a.feed(0xDE00).has_value(), "reset drops the held surrogate");
}

}  // namespace

int main() {
  runCase("valid_round_trips", validRoundTrips);
  runCase("invalid_utf8_rejected", invalidUtf8IsRejected);
  runCase("invalid_bytes_replaced", invalidBytesBecomeReplacementCharacters);
  runCase("invalid_utf16_rejected", invalidUtf16IsRejected);
  runCase("text_code_point_filter", textCodePointFilter);
  runCase("assembler_pairs_surrogates", assemblerPairsSurrogates);
  return platform_test::finish("ui-platform utf test");
}

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for core::parseJson: valid documents parse to the expected values, and every
//   hostile input (truncation, depth bomb, bad escapes, bad UTF-8, huge numbers, trailing data)
//   is rejected with an error instead of crashing or throwing.
// Callers: CTest (label fast). Exit code 0 = pass.
#include <cstdio>
#include <string>

#include "r1ui/core/Json.h"

using r1ui::core::JsonLimits;
using r1ui::core::JsonResult;
using r1ui::core::JsonType;
using r1ui::core::parseJson;

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

void expectRejected(const std::string& text, const char* what) {
  const JsonResult r = parseJson(text);
  expect(!r.ok() && !r.error.message.empty(), what);
}

std::string nested(size_t depth) {
  return std::string(depth, '[') + std::string(depth, ']');
}

void testHappyPath() {
  const JsonResult r = parseJson(
      R"({"b":true,"n":null,"x":-12.5e1,"s":"a\n\u00e9\ud83d\ude00","a":[1,2,{"k":"v"}],"o":{}})");
  expect(r.ok(), "valid document parses");
  if (!r.ok()) return;
  const auto& v = *r.value;
  expect(v.isObject() && v.size() == 6, "object has six members");
  expect(v.keyAt(0) == "b" && v.keyAt(5) == "o", "member order is document order");
  expect(v.find("b")->boolValue(), "bool member");
  expect(v.find("n")->type() == JsonType::Null, "null member");
  expect(v.find("x")->numberValue() == -125.0, "number with exponent");
  expect(v.find("s")->stringValue() == "a\n\xC3\xA9\xF0\x9F\x98\x80", "escapes and surrogate pair");
  expect(v.find("a")->size() == 3 && v.find("a")->child(2).find("k")->stringValue() == "v",
         "nested array/object");
  expect(v.find("missing") == nullptr, "absent key is nullptr");
  expect(parseJson("  42 \n").ok(), "top-level scalar with whitespace");
  expect(parseJson("\"\xC3\xA9\"").ok(), "valid raw UTF-8 in string");
}

void testHostile() {
  expectRejected("", "empty input");
  expectRejected("   ", "whitespace only");
  expectRejected("{\"a\":1", "truncated object");
  expectRejected("[1,2", "truncated array");
  expectRejected("\"abc", "truncated string");
  expectRejected("[1,2,]", "trailing comma in array");
  expectRejected("{\"a\":1,}", "trailing comma in object");
  expectRejected("{} x", "trailing garbage");
  expectRejected("{}{}", "two documents");
  expectRejected("{\"a\":1,\"a\":2}", "duplicate key");
  expectRejected("\"\\q\"", "unknown escape");
  expectRejected("\"\\u12\"", "truncated unicode escape");
  expectRejected("\"\\u12zz\"", "non-hex unicode escape");
  expectRejected("\"\\ud800\"", "lone high surrogate");
  expectRejected("\"\\ud800\\u0041\"", "high surrogate followed by non-surrogate");
  expectRejected("\"\\udc00\"", "lone low surrogate");
  expectRejected(std::string("\"a\nb\""), "raw newline in string");
  expectRejected(std::string("\"\x01\""), "raw control character in string");
  expectRejected("\"\xC3\"", "truncated UTF-8 sequence");
  expectRejected("\"\xC0\x80\"", "overlong UTF-8");
  expectRejected("\"\xED\xA0\x80\"", "UTF-8 encoded surrogate");
  expectRejected("\"\xF4\x90\x80\x80\"", "code point above U+10FFFF");
  expectRejected("\"\xFF\"", "invalid lead byte");
  expectRejected("\"\xC3\x28\"", "bad continuation byte");
  expectRejected("1e999", "number overflowing double");
  expectRejected("-1e999", "negative number overflowing double");
  expectRejected("01", "leading zero");
  expectRejected("1.", "dangling decimal point");
  expectRejected("-", "bare minus");
  expectRejected("+1", "leading plus");
  expectRejected(".5", "leading decimal point");
  expectRejected("NaN", "NaN literal");
  expectRejected("tru", "truncated literal");
  expectRejected("\xEF\xBB\xBF{}", "byte order mark");
  expectRejected("{\"a\" 1}", "missing colon");
  expectRejected("{a:1}", "unquoted key");
  expectRejected(std::string("{\"a\":1}\0", 8), "embedded NUL after document");
}

void testLimits() {
  expect(parseJson(nested(64)).ok(), "64 levels of nesting accepted");
  expectRejected(nested(65), "65 levels of nesting rejected");
  expectRejected(nested(100000), "depth bomb rejected without stack overflow");
  expectRejected(std::string(100000, '{'), "unterminated object depth bomb");

  JsonLimits tiny;
  tiny.maxInputBytes = 4;
  expect(!parseJson("[1,2,3]", tiny).ok(), "input size limit enforced");
  tiny = {};
  tiny.maxNodes = 3;
  expect(parseJson("[1,2]", tiny).ok(), "node count at the limit accepted");
  expect(!parseJson("[1,2,3]", tiny).ok(), "node count above the limit rejected");

  const auto r = parseJson("[1,x]");
  expect(!r.ok() && r.error.offset == 3, "error offset points at the bad byte");
}

}  // namespace

int main() {
  testHappyPath();
  testHostile();
  testLimits();
  if (failures == 0) std::puts("json_test: all checks passed");
  return failures == 0 ? 0 : 1;
}

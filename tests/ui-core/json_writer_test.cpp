// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for core::appendQuoted / appendNumber: escapes are correct, output parses back to
//   the same value through parseJson, invalid UTF-8 is replaced, non-finite numbers are refused.
// Callers: CTest (label fast). Exit code 0 = pass.
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

#include "r1ui/core/Json.h"
#include "r1ui/core/JsonWriter.h"

using namespace r1ui::core;

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

std::string quoted(const std::string& s) {
  std::string out;
  appendQuoted(out, s);
  return out;
}

void testStrings() {
  expect(quoted("a\"b\\c") == "\"a\\\"b\\\\c\"", "quote and backslash escaped");
  expect(quoted("\n\r\t\b\f") == "\"\\n\\r\\t\\b\\f\"", "short escapes");
  expect(quoted(std::string("\x01\x1f", 2)) == "\"\\u0001\\u001f\"", "control characters use \\u00XX");
  expect(quoted(std::string("a\0b", 3)) == "\"a\\u0000b\"", "embedded NUL is escaped");
  expect(quoted("\xC3\xA9\xF0\x9F\x98\x80") == "\"\xC3\xA9\xF0\x9F\x98\x80\"", "valid UTF-8 kept");
  expect(quoted("\xC0\x80") == "\"\xEF\xBF\xBD\xEF\xBF\xBD\"", "overlong form replaced");
  expect(quoted("\xED\xA0\x80") == "\"\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\"", "surrogate encoding replaced");
  expect(quoted("\xE2\x82") == "\"\xEF\xBF\xBD\xEF\xBF\xBD\"", "truncated sequence replaced");

  const std::string nasty = std::string("q\"\\/\n\x02 \xC3\xA9 \xF0\x9F\x98\x80", 14);
  const JsonResult parsed = parseJson(quoted(nasty));
  expect(parsed.ok() && parsed.value->stringValue() == nasty, "quoted text round-trips through parseJson");
}

void testNumbers() {
  for (const double v : {0.0, -0.5, 1.0, 1e-300, 1.7976931348623157e308, 0.1, 1.0 / 3.0, 123456789012345.0}) {
    std::string out;
    appendNumber(out, v);
    const JsonResult parsed = parseJson(out);
    expect(parsed.ok() && parsed.value->numberValue() == v, "number round-trips exactly");
  }
  bool threw = false;
  try {
    std::string out;
    appendNumber(out, std::numeric_limits<double>::quiet_NaN());
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expect(threw, "NaN is refused");
  threw = false;
  try {
    std::string out;
    appendNumber(out, std::numeric_limits<double>::infinity());
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expect(threw, "infinity is refused");
}

}  // namespace

int main() {
  testStrings();
  testNumbers();
  if (failures == 0) std::puts("json_writer_test: all checks passed");
  return failures == 0 ? 0 : 1;
}

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for core::checkedCast: exact values pass, out-of-range values throw.
// Callers: CTest (label fast). Exit code 0 = pass.
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>

#include "r1ui/core/CheckedCast.h"

using r1ui::core::checkedCast;

namespace {
int failures = 0;
void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}
template <class To, class From>
bool throws(From v) {
  try {
    (void)checkedCast<To>(v);
  } catch (const std::range_error&) {
    return true;
  }
  return false;
}
}  // namespace

int main() {
  expect(checkedCast<int>(size_t{42}) == 42, "size_t 42 -> int");
  expect(checkedCast<uint32_t>(int{0}) == 0u, "int 0 -> uint32");
  expect(checkedCast<uint8_t>(255) == 255, "255 -> uint8 boundary");
  expect(throws<uint8_t>(256), "256 -> uint8 throws");
  expect(throws<uint32_t>(-1), "-1 -> uint32 throws");
  expect(throws<int>(std::numeric_limits<size_t>::max()), "size_t max -> int throws");
  expect(throws<int32_t>(int64_t{1} << 40), "int64 2^40 -> int32 throws");
  expect(checkedCast<int64_t>(std::numeric_limits<int32_t>::min()) ==
             std::numeric_limits<int32_t>::min(),
         "int32 min -> int64");
  return failures == 0 ? 0 : 1;
}

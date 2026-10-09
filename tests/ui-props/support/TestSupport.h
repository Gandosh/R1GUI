// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the expectation macros every ui-props test shares (count failures, print the failing
//   expression, never abort) and finish(), which turns the count into the process exit code.
// Why: tests are plain executables like the other modules'; one page of expectations per behaviour.
// Callers: tests/ui-props/*_test.cpp. Usage: `int main() { ...; return r1test::finish(); }`.
#pragma once

#include <cmath>
#include <cstdio>

namespace r1test {

inline int& failureCount() {
  static int failures = 0;
  return failures;
}

inline int& checkCount() {
  static int checks = 0;
  return checks;
}

inline void report(bool ok, const char* expression, const char* file, int line) {
  ++checkCount();
  if (ok) return;
  std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
  ++failureCount();
}

inline int finish() {
  if (failureCount() != 0) {
    std::fprintf(stderr, "%d of %d expectation(s) failed\n", failureCount(), checkCount());
    return 1;
  }
  std::printf("ok (%d expectations)\n", checkCount());
  return 0;
}

#define R1_EXPECT(cond) ::r1test::report(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define R1_EXPECT_NEAR(a, b, tol) ::r1test::report(std::abs((a) - (b)) <= (tol), #a " ~ " #b, __FILE__, __LINE__)

}  // namespace r1test

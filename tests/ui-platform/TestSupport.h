// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the tiny assertion/case runner shared by the ui-platform test programs.
// Why: the repo's tests are plain executables (exit code 0 = pass); named cases let a failure
//   point at the scenario it belongs to.
// Callers: tests/ui-platform/*.cpp only.
#pragma once

#include <cstdio>

namespace platform_test {

inline int& failures() {
  static int count = 0;
  return count;
}

inline const char*& currentCase() {
  static const char* name = "";
  return name;
}

inline void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL [%s]: %s\n", currentCase(), what);
    ++failures();
  }
}

inline void runCase(const char* name, void (*fn)()) {
  std::fprintf(stderr, "case %s\n", name);
  currentCase() = name;
  fn();
}

inline int finish(const char* program) {
  if (failures() == 0) std::printf("%s: all checks passed\n", program);
  return failures() == 0 ? 0 : 1;
}

}  // namespace platform_test

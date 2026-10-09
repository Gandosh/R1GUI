// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the tiny assertion and named-case runner shared by the ui-render test programs.
// Why: the repo's tests are plain executables (exit code 0 = pass); a named case lets a failure
//   point at the scenario it belongs to, and the GPU program can run one case per CTest entry.
// Callers: tests/ui-render/*.cpp only.
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace render_test {

inline int& failures() {
  static int count = 0;
  return count;
}

inline const char*& currentCase() {
  static const char* name = "";
  return name;
}

inline void expect(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL [%s]: %s\n", currentCase(), what.c_str());
    ++failures();
  }
}

inline bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

struct Case {
  const char* name;
  void (*run)();
};

// Runs the cases whose name equals argv[1] (all when no argument is given). An unknown name is a
// failure so a stale CTest registration cannot silently pass. Exceptions fail the case.
inline int runCases(const std::vector<Case>& cases, int argc, char** argv) {
  const char* only = argc > 1 ? argv[1] : nullptr;
  bool matched = only == nullptr;
  for (const Case& c : cases) {
    if (only != nullptr && std::strcmp(only, c.name) != 0) continue;
    matched = true;
    std::fprintf(stderr, "case %s\n", c.name);
    currentCase() = c.name;
    try {
      c.run();
    } catch (const std::exception& e) {
      expect(false, std::string("unexpected exception: ") + e.what());
    }
  }
  if (!matched) {
    std::fprintf(stderr, "FAIL: unknown case '%s'\n", only);
    return 2;
  }
  std::fprintf(stderr, "%s (%d failure%s)\n", failures() == 0 ? "PASS" : "FAILED", failures(), failures() == 1 ? "" : "s");
  return failures() == 0 ? 0 : 1;
}

}  // namespace render_test

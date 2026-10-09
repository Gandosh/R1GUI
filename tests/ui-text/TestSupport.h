// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the tiny assertion helpers and font fixtures shared by the ui-text tests.
// Callers: every test executable under tests/ui-text. Exit code 0 = pass.
// Fixtures: fonts are loaded from the real assets/fonts files (R1UI_ASSETS_DIR is set by CMake).
#pragma once

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "r1ui/text/Font.h"
#include "r1ui/text/Utf8.h"

namespace r1ui::text::testing {

inline int failures = 0;

inline void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

inline int finish(const char* name) {
  if (failures != 0) {
    std::fprintf(stderr, "%s: %d failure(s)\n", name, failures);
    return 1;
  }
  std::printf("%s: ok\n", name);
  return 0;
}

inline std::string assetPath(const char* relative) { return std::string(R1UI_ASSETS_DIR) + "/" + relative; }

inline std::vector<unsigned char> readFileBytes(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Builds a UTF-8 string from code points (avoids hex escapes swallowing following hex digits).
inline std::string utf8(std::initializer_list<char32_t> codePoints) {
  std::string out;
  for (const char32_t cp : codePoints) appendUtf8(out, cp);
  return out;
}

// Loads one Inter face; exits the process with a diagnostic if the asset is missing.
inline FontHandle loadInter(FontLibrary& lib, const char* file, int weight) {
  auto r = lib.loadFromFile(assetPath((std::string("fonts/") + file).c_str()), weight);
  if (!r.ok()) {
    std::fprintf(stderr, "cannot load %s: %s\n", file, r.error().message.c_str());
    std::exit(2);
  }
  return r.value();
}

}  // namespace r1ui::text::testing

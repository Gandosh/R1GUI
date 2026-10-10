// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the shared fixtures of the brush library tests: a small named sample set, a seeded generator of
//   many brushes (ASCII, accented, Cyrillic, digits, symbols, duplicates) and the guard that turns CRT
//   assertion dialogs into stderr lines.
// Why: every brush test needs the same data; hostile inputs are generated from a seed so a failure is
//   reproducible from the printed seed.
// Callers: tests/ui-commands/brushes/*_test.cpp.
#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/commands/brushes/BrushLibraryModel.h"

#ifdef _WIN32
#include <crtdbg.h>
#include <cstdlib>
#endif

namespace r1test {

namespace br = r1ui::commands::brushes;

struct NoBrushDialogs {
  NoBrushDialogs() {
#ifdef _WIN32
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
    for (const int kind : {_CRT_ASSERT, _CRT_ERROR, _CRT_WARN}) {
      _CrtSetReportMode(kind, _CRTDBG_MODE_FILE);
      _CrtSetReportFile(kind, _CRTDBG_FILE_STDERR);
    }
#endif
#endif
  }
};
inline const NoBrushDialogs noBrushDialogs;

inline br::BrushInfo brush(std::string id, std::string name, std::string category = "Sculpt") {
  br::BrushInfo info;
  info.id = std::move(id);
  info.name = std::move(name);
  info.category = std::move(category);
  return info;
}

// The names of the owner's examples; ids are lower-case names.
inline std::vector<br::BrushInfo> sampleBrushes() {
  const std::pair<const char*, const char*> names[] = {
      {"Standard", "Sculpt"}, {"Smooth", "Smooth"}, {"Snake Hook", "Move"}, {"Slash", "Cut"},   {"Clay Buildup", "Sculpt"}, {"Clay", "Sculpt"},
      {"Inflate", "Sculpt"},  {"Pinch", "Sculpt"},  {"Move", "Move"},       {"Blob", "Sculpt"},  {"Flatten", "Surface"},    {"Polish", "Surface"},
      {"Hpolish", "Surface"}, {"Crease", "Sculpt"}, {"Dam Standard", "Sculpt"}, {"Layer", "Surface"}, {"Trim", "Cut"}, {"Mask Pen", "Mask"}};
  std::vector<br::BrushInfo> list;
  for (const auto& [name, category] : names) {
    std::string id = name;
    for (char& c : id) c = c == ' ' ? '-' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    list.push_back(brush(id, name, category));
  }
  return list;
}

// splitmix64: a small seeded generator, the same on every platform.
struct Rng {
  uint64_t state;
  explicit Rng(uint64_t seed) : state(seed) {}
  uint64_t next() {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  size_t below(size_t n) { return n == 0 ? 0 : static_cast<size_t>(next() % n); }
};

// Names drawn from a small alphabet (so prefixes collide a lot) plus sometimes accents, Cyrillic, digits,
// leading symbols and spaces.
inline std::string randomName(Rng& rng) {
  static const char* const pieces[] = {"a", "b", "c", "d", "e", "s", "t", "m", "n", "o", "l", "p", "A", "S", "C", " ", "-", "3", "\xC3\xA9", "\xC3\x85", "\xD0\x9A", "\xD0\xB8", "\xE2\x98\x85"};
  std::string name;
  const size_t length = 1 + rng.below(9);
  for (size_t i = 0; i < length; ++i) name += pieces[rng.below(sizeof(pieces) / sizeof(pieces[0]))];
  return name;
}

inline std::vector<br::BrushInfo> randomBrushes(size_t count, uint64_t seed, bool uniqueIds = true) {
  Rng rng(seed);
  std::vector<br::BrushInfo> list;
  for (size_t i = 0; i < count; ++i) {
    br::BrushInfo info = brush(uniqueIds ? "b" + std::to_string(i) : "b" + std::to_string(rng.below(count / 2 + 1)), randomName(rng), "Cat" + std::to_string(rng.below(5)));
    info.enabled = rng.below(12) != 0;
    list.push_back(std::move(info));
  }
  return list;
}

}  // namespace r1test

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: what the custom menu and workspace tests share: suppression of modal failure dialogs, a temporary
//   directory that is removed on scope exit, a small deterministic random generator, sample menus and the
//   text mutations used by the fuzz tests.
// Why: owner rule: a test must never leave a modal dialog on the screen; file tests need a real directory
//   (atomic writes), and the fuzz tests must be reproducible.
// Callers: tests/ui-commands/custommenu and tests/ui-commands/workspace.
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/commands/Text.h"
#include "r1ui/commands/custommenu/CustomMenuIo.h"
#include "r1ui/commands/custommenu/CustomMenuSet.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <crtdbg.h>
#include <cstdlib>
#include <windows.h>
#endif

namespace r1test {

namespace cm = r1ui::commands::custommenu;

inline const bool kDialogsSuppressed = [] {
#ifdef _WIN32
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
  for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
    _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
  }
#endif
#endif
  return true;
}();

// A fresh directory under the system temp folder, removed (with its content) on destruction.
class TempDir {
 public:
  explicit TempDir(const std::string& tag) {
    static int counter = 0;
    path_ = std::filesystem::temp_directory_path() / ("r1ui-test-" + tag + "-" + std::to_string(++counter) + "-" + std::to_string(reinterpret_cast<uintptr_t>(this) & 0xffff));
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
    std::filesystem::create_directories(path_, ec);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

inline std::string slurp(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

inline void spit(const std::filesystem::path& p, const std::string& text) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

inline size_t countFiles(const std::filesystem::path& dir) {
  size_t n = 0;
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
    (void)e;
    ++n;
  }
  return n;
}

// xorshift64*: reproducible, no <random> distribution differences between standard libraries.
class Rng {
 public:
  explicit Rng(uint64_t seed) : s_(seed * 2685821657736338717ull + 88172645463325252ull) {}
  uint64_t next() {
    s_ ^= s_ >> 12;
    s_ ^= s_ << 25;
    s_ ^= s_ >> 27;
    return s_ * 2685821657736338717ull;
  }
  size_t below(size_t n) { return n == 0 ? 0 : static_cast<size_t>(next() % n); }

 private:
  uint64_t s_;
};

// Applies one random damage to the text: flip, delete, insert, truncate, duplicate a slice.
inline std::string mutate(std::string text, Rng& rng) {
  if (text.empty()) return text;
  switch (rng.below(5)) {
    case 0: text[rng.below(text.size())] = static_cast<char>(rng.below(256)); break;
    case 1: text.erase(rng.below(text.size()), 1 + rng.below(8)); break;
    case 2: text.insert(rng.below(text.size()), std::string(1 + rng.below(4), static_cast<char>(rng.below(256)))); break;
    case 3: text.resize(rng.below(text.size())); break;
    default: {
      const size_t from = rng.below(text.size());
      text.insert(rng.below(text.size()), text.substr(from, rng.below(32)));
    }
  }
  return text;
}

inline std::string randomGarbage(Rng& rng, size_t length) {
  std::string s(length, '\0');
  for (char& c : s) c = static_cast<char>(rng.below(256));
  return s;
}

// A pie with a few commands (some unknown to any registry) and a panel with labels and icons.
inline cm::CustomMenu samplePie(const std::string& name = "Tools") {
  cm::CustomMenu m = cm::makeEmptyMenu(cm::MenuKind::Pie, name);
  m.id = "menu.1";
  m.serial = 1;
  m.entries[0] = {"tool.move", "", ""};
  m.entries[2] = {"tool.rotate", "Rotate", "rotate-cw"};
  m.entries[5] = {"plugin.gone", "", ""};
  return m;
}

inline cm::CustomMenu samplePanel(const std::string& name = "Quick") {
  cm::CustomMenu m = cm::makeEmptyMenu(cm::MenuKind::Panel, name);
  m.id = "menu.2";
  m.serial = 2;
  m.panel.columns = 2;
  m.panel.buttonSize = 36;
  m.panel.showLabels = true;
  m.panel.width = 240.0;
  m.panel.height = 180.0;
  m.entries = {{"tool.move", "Move", "move"}, {"edit.undo", "", ""}, {"plugin.gone", "Gone", ""}};
  return m;
}

}  // namespace r1test

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for theme::Tokens: the real assets/theme/tokens.json loads with the expected
//   colours, and structurally wrong or hostile token files are rejected without partial state.
// Callers: CTest (label fast); argv[1] is the path of the real tokens.json.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "r1ui/theme/Tokens.h"

using r1ui::theme::Color;
using r1ui::theme::ThemeId;
using r1ui::theme::Tokens;

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

void expectRejected(const std::string& json, const char* what) {
  const auto r = Tokens::parse(json);
  expect(!r.ok() && !r.error.empty(), what);
}

void testRealFile(const std::string& path) {
  const auto r = Tokens::loadFile(path);
  expect(r.ok(), "real tokens.json loads");
  if (!r.ok()) {
    std::fprintf(stderr, "  error: %s\n", r.error.c_str());
    return;
  }
  const Tokens& t = *r.tokens;
  expect(t.colorCount() == 34, "34 colours in each theme");
  expect(t.colorNames().size() == 34 && t.colorNames().front() == "panel", "file order kept");
  expect(t.color(ThemeId::Dark, "panel") == Color{0x2a, 0x2a, 0x2a, 255}, "dark panel is #2a2a2a");
  expect(t.color(ThemeId::Light, "panel") == Color{255, 255, 255, 255}, "light panel is #ffffff");
  expect(t.color(ThemeId::Dark, "warning-bg")->a == 0x1a, "alpha channel parsed");
  expect(!t.color(ThemeId::Dark, "no-such-colour").has_value(), "unknown name");
  expect(!t.colorAt(ThemeId::Light, 34).has_value(), "index past the end");
  expect(r1ui::theme::toHex(*t.color(ThemeId::Dark, "panel")) == "#2a2a2a", "toHex opaque");
  expect(r1ui::theme::toHex(*t.color(ThemeId::Dark, "warning-bg")) == "#f59e0b1a", "toHex alpha");
  for (size_t i = 0; i < t.colorCount(); ++i) {
    expect(t.colorAt(ThemeId::Dark, i) == t.color(ThemeId::Dark, t.colorNames()[i]),
           "colorAt matches color by name");
  }
}

void testHostile() {
  const std::string ok = R"({"themes":{"dark":{"a":"#000000","b":"#ffffff80"},"light":{"b":"#111111","a":"#222222"}}})";
  const auto r = Tokens::parse(ok);
  expect(r.ok() && r.tokens->color(ThemeId::Light, "a") == Color{0x22, 0x22, 0x22, 255},
         "light colours are matched by name, not position");

  expectRejected("", "empty file");
  expectRejected("[]", "top level not an object");
  expectRejected("{}", "missing themes");
  expectRejected(R"({"themes":{"dark":{"a":"#000000"}}})", "missing light theme");
  expectRejected(R"({"themes":{"light":{"a":"#000000"}}})", "missing dark theme");
  expectRejected(R"({"themes":{"dark":{},"light":{}}})", "empty themes");
  expectRejected(R"({"themes":{"dark":{"a":"#000000"},"light":{"b":"#000000"}}})",
                 "same size but different names");
  expectRejected(R"({"themes":{"dark":{"a":"#000000","b":"#000000"},"light":{"a":"#000000"}}})",
                 "different colour counts");
  expectRejected(R"({"themes":{"dark":{"a":"000000"},"light":{"a":"#000000"}}})", "missing hash");
  expectRejected(R"({"themes":{"dark":{"a":"#00000"},"light":{"a":"#000000"}}})", "five hex digits");
  expectRejected(R"({"themes":{"dark":{"a":"#0000000"},"light":{"a":"#000000"}}})", "seven hex digits");
  expectRejected(R"({"themes":{"dark":{"a":"#00000g"},"light":{"a":"#000000"}}})", "non-hex digit");
  expectRejected(R"({"themes":{"dark":{"a":"#000000"},"light":{"a":"#00000z"}}})", "bad light colour");
  expectRejected(R"({"themes":{"dark":{"a":12},"light":{"a":"#000000"}}})", "number instead of string");
  expectRejected(R"({"themes":{"dark":{"a":"#000000","a":"#111111"},"light":{"a":"#000000"}}})",
                 "duplicate colour name");
  expectRejected(R"({"themes":{"dark":{"a":"#000000"},"light":{"a":"#000000"}})", "truncated file");
  expectRejected(R"({"themes":[]})", "themes not an object");

  const auto missing = Tokens::loadFile("this/path/does/not/exist.json");
  expect(!missing.ok() && !missing.tokens.has_value(), "missing file reports an error");

  const std::filesystem::path temp =
      std::filesystem::temp_directory_path() / "r1ui_tokens_test_bad.json";
  {
    std::ofstream out(temp, std::ios::binary);
    out << R"({"themes":{"dark":{"a":"#12345"},"light":{"a":"#000000"}}})";
  }
  const auto bad = Tokens::loadFile(temp);
  expect(!bad.ok() && bad.error.find("r1ui_tokens_test_bad.json") != std::string::npos,
         "bad file on disk is rejected and named in the error");
  std::error_code ignored;
  std::filesystem::remove(temp, ignored);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: ui-theme-tokens-test <path-to-tokens.json>\n");
    return 2;
  }
  testRealFile(argv[1]);
  testHostile();
  if (failures == 0) std::puts("tokens_test: all checks passed");
  return failures == 0 ? 0 : 1;
}

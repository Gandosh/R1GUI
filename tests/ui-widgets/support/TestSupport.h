// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the helpers every ui-widgets test shares: expectation macros that count failures and print
//   the failing expression, the asset / reference / artifact directories (compile definitions set by
//   tests/ui-widgets/CMakeLists.txt) and TestUi, a headless UiContext with real tokens and fonts
//   over NullTextureFactory (no GPU, no window).
// Why: tests are plain executables (like the other modules); a widget's unit tests should be a page
//   of expectations around a TestUi, with synthetic input and a recording Painter.
// Callers: every tests/ui-widgets/<folder>/*_test.cpp. Usage: `int main() { ...; return r1test::finish(); }`.
// Failure behavior: expectations never abort; finish() returns 1 when any failed. Fixture
//   construction failures (missing assets) throw; the test's main should let them terminate.
#pragma once

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "r1ui/theme/Tokens.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1test {

inline int& failureCount() {
  static int failures = 0;
  return failures;
}

inline void report(bool ok, const char* expression, const char* file, int line) {
  if (ok) return;
  std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
  ++failureCount();
}

inline int finish() {
  if (failureCount() != 0) {
    std::fprintf(stderr, "%d expectation(s) failed\n", failureCount());
    return 1;
  }
  std::printf("ok\n");
  return 0;
}

#define R1_EXPECT(cond) ::r1test::report(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define R1_EXPECT_NEAR(a, b, tol) ::r1test::report(std::abs((a) - (b)) <= (tol), #a " ~ " #b, __FILE__, __LINE__)

inline std::filesystem::path assetsDir() { return R1UI_ASSETS_DIR; }
inline std::filesystem::path referenceDir() { return R1UI_REFERENCE_DIR; }
inline std::filesystem::path artifactDir() { return R1UI_ARTIFACT_DIR; }

inline std::shared_ptr<const r1ui::theme::Tokens> loadTokens() {
  auto result = r1ui::theme::Tokens::loadFile(assetsDir() / "theme" / "tokens.json");
  if (!result.ok()) throw std::runtime_error("tokens.json: " + result.error);
  return std::make_shared<const r1ui::theme::Tokens>(std::move(*result.tokens));
}

inline r1ui::widgets::ServicesPaths assetPaths() {
  return {assetsDir() / "fonts", {assetsDir() / "icons" / "lucide", assetsDir() / "icons" / "custom"}};
}

// A headless window: services over in-memory textures, a UiContext sized 400 x 300 at scale 1,
// animations off. Services are heap allocated so TestUi can be moved around freely.
struct TestUi {
  explicit TestUi(int width = 400, int height = 300, float scale = 1.0f)
      : textures(), services(loadTokens(), textures, assetPaths()), ui(services) {
    ui.setViewport(width, height, scale);
    ui.setAnimationsEnabled(false);
  }
  // Runs layout (and overlay placement) and returns nothing; use ui.absRect to inspect.
  void layout() { ui.frame(); }

  r1ui::widgets::NullTextureFactory textures;
  r1ui::widgets::Services services;
  r1ui::widgets::UiContext ui;
};

}  // namespace r1test

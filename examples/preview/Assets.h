// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: locating the files the preview needs next to its executable (design tokens, fonts,
//   icons, reference screens, the dock layout file) and loading the tokens with a message that
//   names the problem.
// Why: a missing or damaged asset must end the program with a readable message box, never a crash
//   or a silent blank window; every loader here throws std::runtime_error with that message.
// Callers: PreviewApp, Bench, tests/preview. The build copies the assets into <exe dir>/assets and
//   <exe dir>/reference (examples/preview/CMakeLists.txt).
#pragma once

#include <filesystem>

#include "r1ui/theme/Tokens.h"

namespace preview {

std::filesystem::path executableDir();

struct AssetPaths {
  std::filesystem::path root;  // directory holding assets/ and reference/ (the executable's directory)

  std::filesystem::path tokens() const { return root / "assets" / "theme" / "tokens.json"; }
  std::filesystem::path fonts() const { return root / "assets" / "fonts"; }
  std::filesystem::path icons() const { return root / "assets" / "icons" / "lucide"; }
  std::filesystem::path customIcons() const { return root / "assets" / "icons" / "custom"; }
  std::filesystem::path references() const { return root / "reference"; }
  std::filesystem::path layoutFile() const { return root / "layout.json"; }
};

// Loads and validates tokens.json (all sections required). Throws std::runtime_error.
r1ui::theme::Tokens loadTokens(const AssetPaths& paths);

}  // namespace preview

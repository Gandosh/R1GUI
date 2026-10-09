// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the asset location and token loading declared in Assets.h.
// Callers: PreviewApp.cpp, Bench.cpp, tests/preview.
#include "Assets.h"

#include <windows.h>

#include <stdexcept>
#include <string>

#include "r1ui/core/CheckedCast.h"

namespace preview {

std::filesystem::path executableDir() {
  std::wstring buffer(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), r1ui::core::checkedCast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size()) throw std::runtime_error("cannot locate the executable");
  buffer.resize(length);
  return std::filesystem::path(buffer).parent_path();
}

r1ui::theme::Tokens loadTokens(const AssetPaths& paths) {
  auto result = r1ui::theme::Tokens::loadFile(paths.tokens(), {.requireAllSections = true});
  if (!result.ok()) {
    throw std::runtime_error("Design tokens are missing or invalid (" + paths.tokens().string() + "):\n" + result.error +
                             "\n\nRebuild the r1gui-preview target to copy assets next to the executable.");
  }
  return std::move(*result.tokens);
}

}  // namespace preview

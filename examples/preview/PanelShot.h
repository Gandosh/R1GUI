// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the offscreen render of the Widgets scene (no window): the whole 1440x900 scene and the
//   258x724 crop of the properties panel, written as PNG files for visual comparison against
//   tests/reference/openpencil/<theme>/screen-rectangle-selected.png (right panel region).
// Why: the owner-facing look is judged against the reference screenshot; this makes that comparison
//   scriptable with tools/spec/imgdiff.py.
// Callers: main.cpp (--panel-shot), tests/preview. Calls: RenderDevice, OffscreenTarget, Scene.
// Failure behavior: throws std::runtime_error naming the failing asset or file.
#pragma once

#include <filesystem>

#include "r1ui/theme/Tokens.h"

namespace preview {

inline constexpr uint32_t kShotWidth = 1440;
inline constexpr uint32_t kShotHeight = 900;
inline constexpr uint32_t kPanelCropWidth = 258;
inline constexpr uint32_t kPanelCropHeight = 724;

// Writes <directory>/scene_<theme>.png and <directory>/panel_<theme>.png.
void renderPanelShot(const std::filesystem::path& directory, r1ui::theme::ThemeId theme);

}  // namespace preview

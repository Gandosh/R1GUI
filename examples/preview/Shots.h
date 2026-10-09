// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the offscreen render of the preview's widget modes (no window): the composed Widgets screen
//   and every Gallery page at 1440x900, in the given theme, written as PNG files.
// Why: the owner-facing look is judged against the reference screenshot and the galleries; this
//   makes those renders scriptable (compare the Widgets screen with
//   tests/reference/openpencil/<theme>/screen-rectangle-selected.png) without a window.
// Callers: main.cpp (--shot), tests/preview. Calls: RenderDevice, OffscreenTarget, Services,
//   UiContext, GalleryApp, ComposedApp.
// Failure behavior: throws std::runtime_error naming the failing asset or file.
#pragma once

#include <filesystem>

#include "r1ui/theme/Tokens.h"

namespace preview {

inline constexpr uint32_t kShotWidth = 1440;
inline constexpr uint32_t kShotHeight = 900;

// Writes <directory>/widgets_<theme>.png and <directory>/gallery_<page>_<theme>.png (page names in
// lower case).
void renderShots(const std::filesystem::path& directory, r1ui::theme::ThemeId theme);

}  // namespace preview

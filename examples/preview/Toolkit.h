// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the preview's names for the toolkit services it uses (text engine, icon cache, GPU texture
//   factory), which moved from the preview into the ui-widgets module in Phase 4.
// Why: the Phase 3 scene, modes and benchmark keep their code unchanged; one header maps the old
//   preview names onto the shared services so nothing else here needed rewriting.
// Callers: every preview source that draws text or icons.
#pragma once

#include "r1ui/widgets/gpu/GpuTextures.h"
#include "r1ui/widgets/icons/IconCache.h"
#include "r1ui/widgets/text/TextEngine.h"

namespace preview {

using TextEngine = r1ui::widgets::TextEngine;
using IconSet = r1ui::widgets::IconCache;
using r1ui::widgets::GpuTextureFactory;

}  // namespace preview

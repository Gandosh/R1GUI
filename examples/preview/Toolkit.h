// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the preview's names for the toolkit services its direct-drawn modes use (text engine, GPU
//   texture factory), which live in the ui-widgets module.
// Why: the title bar, the swatches, the screens and the docking sandbox keep their code; one header
//   maps their names onto the shared services so nothing else needed rewriting.
// Callers: every preview source that draws text.
#pragma once

#include "r1ui/widgets/gpu/GpuTextures.h"
#include "r1ui/widgets/text/TextEngine.h"

namespace preview {

using TextEngine = r1ui::widgets::TextEngine;
using r1ui::widgets::GpuTextureFactory;

}  // namespace preview

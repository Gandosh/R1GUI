// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the gallery entry of the editor widgets (colour picker, gradient editor, curve editor,
//   thumbnail grid).
// Why: the gallery shows every widget in one place with realistic content, so that a developer can
//   see and try them without writing a host.
// Contract: appends the four widgets under `parent`; the thumbnail grid's model lives for the whole
//   process (function-local), nothing else is retained.
// Callers: the widget gallery host.
#pragma once

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

void buildGalleryEditors(UiContext& ui, core::tree::WidgetId parent);

}  // namespace r1ui::widgets

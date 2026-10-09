// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the gallery page of the field group (group g2): one labelled instance of every TextInput,
//   NumberField and Select variant and state, laid out in flexbox grids.
// Why: the Phase 4 gallery preview mounts every widget group's page; this one shows the text input
//   tones and sizes in idle, filled, mixed, bound, invalid, disabled and read-only states, the number
//   field with a label, a glyph, a suffix, units, mixed, bound and disabled displays, and selects
//   (value, placeholder, disabled, grouped, searchable) that open real popups when clicked.
// Callers: the gallery preview (mounts it under any container), tests/ui-widgets/textinput/gallery_test.cpp.
// Contract: builds children of `parent` only (a column container is created for the page); every widget
//   is interactive; nothing is global, so the page can be built in several windows.
#pragma once

#include "r1ui/core/tree/WidgetId.h"

namespace r1ui::widgets {

class UiContext;

void buildGalleryFields(UiContext& ui, core::tree::WidgetId parent);

}  // namespace r1ui::widgets

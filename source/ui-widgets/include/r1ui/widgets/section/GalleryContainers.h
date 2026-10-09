// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the gallery page of the containers and navigation group (group g3): one labelled instance of
//   every PropertySection / PanelHeader / FieldGroup / FieldGrid, ScrollArea, Splitter, TabBar, Toolbar
//   and TreeView variant and every state that can be shown without a pointer (idle, collapsed, active,
//   disabled, selected with and without focus, overflowing, vertical, renaming), laid out in a wrapping
//   flexbox grid.
// Why: the Phase 4 gallery preview mounts one function per widget group; this one is the group's entry
//   and doubles as a smoke test of every constructor and style row (the gallery test builds it in
//   both themes and paints it).
// Callers: the gallery preview application (examples/preview), tests/ui-widgets/section/gallery_test.
// Contract: builds children of `parent` only; the caller owns the window and the UiContext; the models
//   and callbacks created here live as long as the widgets (shared_ptr / by-value captures).
#pragma once

#include "r1ui/core/tree/WidgetId.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

void buildGalleryContainers(UiContext& ui, core::tree::WidgetId parent);

}  // namespace r1ui::widgets

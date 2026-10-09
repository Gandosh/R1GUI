// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the gallery entry of the docking group: one labelled, live DockHost with six coloured
//   panels (two stacked on the left, two tabbed in the middle, one on the right) and a seventh that
//   opens as a floating window, so the preview's gallery page can show tab dragging, drop zones,
//   splitters, the context menu and a floating panel with real widgets.
// Why: the Phase 4 convention (docs/dev/widgets.md section 9) gives every widget group one
//   `buildGallery...` function that the gallery mounts, a fast test builds and a GPU test renders.
// Callers: the gallery preview application, tests/ui-widgets/dock/gallery tests.
// Contract: builds children of `parent` only; registers nothing global; the registry, the in-window
//   floating backend and the host live exactly as long as the widgets built here. Colours come from
//   style rows (theme tokens), never literals.
#pragma once

#include "r1ui/core/tree/WidgetId.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

void buildGalleryDock(UiContext& ui, core::tree::WidgetId parent);

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the gallery page of the custom menu group: static pies (8, 6 and 4 slots, one with a highlighted
//   slot, empty, dim and missing slots), three dockable menu panels (labels and icons, icons only, tall
//   buttons; a checked toggle, a disabled command and a missing command among them), the "Custom Menus"
//   main menu, and a live area where holding the right mouse button opens a real pie that runs real
//   commands.
// Why: Phase 5 gallery convention: one labelled instance of every widget in every state it can show, plus
//   live triggers for what only exists while open, so the owner can see and try the pie and the panels.
// Callers: the preview's gallery mode, the group's gallery tests (fast build/layout test, GPU render).
// Contract: builds children of `parent` only (one column container, typeName "GalleryCustomMenus"); the
//   page owns its registry, keymap, router, refresh hub, menu set and command binder, and everything goes
//   away with the page. Nothing is global.
#pragma once

#include "r1ui/core/tree/WidgetId.h"

namespace r1ui::widgets {

class UiContext;

void buildGalleryCustomMenus(UiContext& ui, core::tree::WidgetId parent);

}  // namespace r1ui::widgets

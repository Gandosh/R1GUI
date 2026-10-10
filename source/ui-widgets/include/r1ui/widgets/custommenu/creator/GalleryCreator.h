// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the gallery page of the menu creator: the real "Create Custom Menu" window over a sample command
//   set (every command with a description), a live CustomMenuSet that already holds one pie and one panel
//   menu, and a status line that reports what Create, Cancel and the file buttons did. Save to file and
//   Load from file work through the in-toolkit file path dialog in a temporary folder.
// Why: Phase 5 gallery convention: one labelled instance of every widget so the owner can try it; the
//   page proves the creator works outside the preview application.
// Callers: the preview's gallery, the group's gallery tests (fast build test, GPU render).
// Contract: builds children of `parent` only (one column container, typeName "GalleryCreator"); the page
//   owns its registry, router, menu set and session and everything goes away with the page. Nothing is
//   global; files go to <temp>/r1gui-gallery-menus.
#pragma once

#include "r1ui/core/tree/WidgetId.h"

namespace r1ui::widgets {

class UiContext;

void buildGalleryCreator(UiContext& ui, core::tree::WidgetId parent);

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the gallery page of the command group: a sample registry of 20 commands (undo and redo,
//   clipboard, view toggles, a sequence shortcut, a tool radio group, a command of a panel context),
//   a menu bar, a toolbar and a context menu built from it, a status line, and the keybinding editor
//   over the same data, so changing a chord in the editor changes the menu text and the tooltips.
// Why: the Phase 5 gallery and the preview show "one registration drives menu, toolbar and shortcut"
//   (spec 07) with real widgets and real key routing.
// Callers: the gallery preview (mounts it under any container), the group's gallery tests.
// Contract: builds children of `parent` only (one column container, typeName "GalleryCommands"); the
//   page owns its registry, overrides, keymap, router and refresh hub, and everything goes away when the
//   page is destroyed. Nothing is global: chords work while a widget of the page has focus (the page
//   handles keys that its widgets leave unused), so several pages can live in several windows.
#pragma once

#include "r1ui/core/tree/WidgetId.h"

namespace r1ui::widgets {

class UiContext;

void buildGalleryCommands(UiContext& ui, core::tree::WidgetId parent);

}  // namespace r1ui::widgets

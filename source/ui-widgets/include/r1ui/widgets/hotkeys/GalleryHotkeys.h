// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the gallery pages of the action list and the hotkey editor: sample command sets (about 50
//   commands in ten categories, every one with a description, a panel context whose Delete clashes
//   with the global Delete, and two same-context clashes to try Replace), the HotkeyEditor over them,
//   and an action list whose rows can be dragged onto a drop zone that lists what was dropped.
// Why: the Phase 5 gallery and the preview show both widgets with real data; the drop zone is a stand
//   in for the menu creator and proves the drag payload is accepted like a palette row.
// Callers: the gallery preview (mounts a page under any container), the group's gallery tests.
// Contract: each function builds children of `parent` only (one column container); the page owns its
//   registry, overrides, keymap, router and drag hub, and everything goes away when the page is
//   destroyed. Nothing is global.
#pragma once

#include "r1ui/core/tree/WidgetId.h"

namespace r1ui::widgets {

class UiContext;

// The HotkeyEditor over a sample command set.
void buildGalleryHotkeys(UiContext& ui, core::tree::WidgetId parent);

// An ActionList over the same kind of data plus a drop zone.
void buildGalleryActions(UiContext& ui, core::tree::WidgetId parent);

}  // namespace r1ui::widgets

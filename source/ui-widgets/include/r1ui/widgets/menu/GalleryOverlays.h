// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the gallery entry of the overlay widget group: buildGalleryOverlays() lays out one labelled
//   instance of every menu, tooltip, popover, dialog and toast variant (static previews of the
//   surfaces and their states) plus live triggers that open the real thing (a menu bar, a context
//   menu button, a tooltip target, a popover, a dialog and toast buttons).
// Why: the Phase 4 gallery preview mounts one builder per widget group; this is the group's builder.
//   Static previews let a reviewer compare every state side by side without clicking; the live
//   triggers exercise placement, timers, dismissal and focus in the running application.
// Callers: the gallery preview application (examples/preview), tests/ui-widgets/menu/gallery_*.
// Ownership: the controllers behind the live triggers (menu, toasts, rich tooltips) are owned by the
//   group's root widget (a child of `parent`) and die with it.
// Boundaries: texts are fixed strings; nothing here reads input from outside.
#pragma once

#include "r1ui/core/tree/WidgetId.h"

namespace r1ui::widgets {

class UiContext;

// Adds the gallery of this group as a child of `parent` (a column container with room to grow).
void buildGalleryOverlays(UiContext& ui, core::tree::WidgetId parent);

}  // namespace r1ui::widgets

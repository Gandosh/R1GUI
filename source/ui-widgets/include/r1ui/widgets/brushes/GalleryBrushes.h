// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the gallery entry of the brush library group (slice 5.21): one labelled, static instance of the
//   popup in every state it can show: browsing with the Recent section, favourites and the active brush
//   outlined; typing a letter (the next letters shown); an exact match with a longer sibling; the search
//   mode; the Assign letter popover with its conflict feedback; the tile menu; no match; and the empty library.
// Why: docs/dev/widgets.md section 9: every widget group provides one gallery function that builds into a
//   parent, creates only layout boxes, Labels and the group's widgets, takes every colour from tokens and
//   registers nothing global.
// Callers: the preview's Gallery mode, the group's gallery tests. The page owns its brush model (a widget
//   of the page holds it), so nothing outlives the page.
#pragma once

#include "r1ui/core/tree/WidgetId.h"

namespace r1ui::widgets {

class UiContext;

void buildGalleryBrushes(UiContext& ui, core::tree::WidgetId parent);

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the gallery page of the push / toggle controls: one labelled instance of every Button tone
//   and size, IconButton size and state, Checkbox state, Switch size and state and Segmented
//   variant, laid out as wrapping flexbox rows under section headings.
// Why: the Phase 4 gallery preview mounts one builder per widget group so the owner can see and
//   operate every widget and state in one window; the same builder is what the gallery smoke test
//   builds, lays out and paints.
// Callers: the gallery preview (examples), tests. Calls: Button, IconButton, Checkbox, Switch,
//   Segmented, Label.
// Hover, pressed and keyboard focus are not drawn statically: they appear when the instances are
//   operated (hover, press, Tab); disabled, active, checked and mixed are shown as separate instances.
// Failure behavior: widget creation throws std::length_error at the tree node limit (the gallery
//   needs about 210 nodes); a stale `parent` throws std::invalid_argument (UiContext::create).
#pragma once

#include "r1ui/core/tree/WidgetId.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

// Adds the gallery page as the last child of `parent`.
void buildGalleryButtons(UiContext& ui, core::tree::WidgetId parent);

}  // namespace r1ui::widgets

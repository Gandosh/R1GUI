// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the property panel's gallery entry (docs/dev/widgets.md section 9): buildGalleryProps() builds a
//   live PropertyPanel over sample objects (a Transform, a Material and a Light, two of each so that
//   mixed values can be shown), with controls to switch the type and to select one or two objects, undo
//   and redo buttons with the step names, and a button that changes a value "from outside" to show the
//   live refresh.
// Why: the gallery convention gives every widget group one function the preview mounts, the group's
//   fast test builds, and the group's GPU test renders; this is the property panel's.
// Callers: the preview's Gallery mode, gallery_test, gallery_gpu_test. The page owns its sample data and
//   registers nothing global; everything it creates is destroyed with the page.
#pragma once

#include "r1ui/core/tree/WidgetId.h"

namespace r1ui::widgets {

class UiContext;

void buildGalleryProps(UiContext& ui, core::tree::WidgetId parent);

}  // namespace r1ui::widgets

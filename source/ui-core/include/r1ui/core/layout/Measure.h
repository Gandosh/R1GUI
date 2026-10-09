// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the host-supplied content measurement interface for leaf widgets (text, images).
// Why: ui-core must not depend on text shaping; the host answers "how big is this content under
//   these constraints" and the layout engine does the rest.
// Callers: layout::FlexLayout calls measure(); the host (ui-text integration) implements it.
// Contract: measure() must be a pure function of (widget, input) for a given widget state, must
//   not mutate the widget tree (the tree is locked during layout and refuses structural edits),
//   and its result is sanitised by the engine (NaN/negative -> 0, clamped to kMaxExtent, and
//   clamped to the available size in AtMost mode).
#pragma once

#include "r1ui/core/tree/WidgetId.h"

namespace r1ui::core::layout {

// How an axis constraint of a measurement is to be read.
enum class MeasureMode : unsigned char {
  Undefined,  // unconstrained: report the content's natural (max-content) size
  AtMost,     // `size` is the available space; report a size no larger
  Exactly     // `size` is fixed; the result on this axis is ignored
};

struct MeasureInput {
  double width = 0.0;
  double height = 0.0;
  MeasureMode widthMode = MeasureMode::Undefined;
  MeasureMode heightMode = MeasureMode::Undefined;
};

struct MeasureResult {
  double width = 0.0;
  double height = 0.0;
};

class MeasureProvider {
 public:
  virtual ~MeasureProvider() = default;
  virtual MeasureResult measure(tree::WidgetId widget, const MeasureInput& input) = 0;
};

}  // namespace r1ui::core::layout

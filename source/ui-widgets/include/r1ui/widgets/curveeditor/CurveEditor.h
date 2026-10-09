// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CurveEditor, the composite the application mounts: the CurveGraph (ruler, grid, curves, all
//   pointer and keyboard editing) above CurveKeyFields, the entry fields for the selected keys (time,
//   value with a "Mixed" placeholder, interpolation, tangent mode, weights) and the extrapolation of
//   the curve of the first selected key (spec 11 rules 61, 64, 65, 66).
// Why: the graph is the part that needs maths and gestures; the fields are ordinary controls bound to
//   the graph's selection. Mounting one object keeps them in step and gives the host one set of
//   callbacks (begin / change / end of an interaction for undo, selection, view, scrub, context menu).
// Callers: application code, the gallery, tests. Calls: CurveGraph, CurveKeyFields.
// Data contract: setCurves replaces the data silently (sanitised copy); curves() is the edited copy.
//   Every user edit is bracketed by onBeginInteraction(label) / onEndInteraction(committed) with
//   onChanged(curve ids) for each intermediate state, exactly as CurveGraph documents.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "r1ui/widgets/curveeditor/CurveGraph.h"
#include "r1ui/widgets/curveeditor/CurveKeyFields.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class CurveEditor : public WidgetObject {
 public:
  const char* typeName() const override { return "CurveEditor"; }
  void onAttached() override;
  std::string_view accessibleName() const override;

  CurveGraph& graph() const;
  CurveKeyFields& fields() const;
  void setShowFields(bool show);

  // ---- callbacks (the graph's, re-exported; the editor keeps the fields in step first) ----
  std::function<void(const std::string& label)> onBeginInteraction;
  std::function<void(const std::vector<uint32_t>& changedCurves)> onChanged;
  std::function<void(bool committed)> onEndInteraction;
  std::function<void()> onSelectionChanged;
  std::function<void()> onViewChanged;
  std::function<void(double time)> onScrubChanged;
  std::function<void(const CurveContext&)> onContextMenu;

 private:
  core::tree::WidgetId graph_;
  core::tree::WidgetId fields_;
};

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CurveKeyFields, the bar of entry fields under the curve graph: key time and value (text
//   "Mixed" when the selected keys disagree), the interpolation and tangent mode selects, the
//   weighted-tangent toggle, and the pre / post extrapolation selects of the curve of the first
//   selected key.
// Why: rule 64 - 66 of spec 11: typed numbers set the selected keys exactly, each commit is one undo
//   step. The fields only read the graph's SelectionInfo and call its edit commands, so they carry
//   no editing logic of their own.
// Callers: CurveEditor, tests. Calls: CurveGraph (selectionInfo and the setSelected* commands),
//   PickerEntry, PickerDropdown, PickerButton.
// Behaviour: with no key selected every field is disabled and empty; a commit that does not parse is
//   reverted; setting a field to the value it already shows does nothing (no undo step).
#pragma once

#include "r1ui/widgets/colorpicker/PickerButton.h"
#include "r1ui/widgets/colorpicker/PickerDropdown.h"
#include "r1ui/widgets/colorpicker/PickerEntry.h"
#include "r1ui/widgets/curveeditor/CurveGraph.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class CurveKeyFields : public WidgetObject {
 public:
  explicit CurveKeyFields(core::tree::WidgetId graph) : graph_(graph) {}

  const char* typeName() const override { return "CurveKeyFields"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;

  // Re-reads the graph's selection into the fields (called by CurveEditor after every change).
  void refresh();
  PickerEntry& timeEntry() const;
  PickerEntry& valueEntry() const;
  PickerDropdown& interpolationSelect() const;
  PickerDropdown& tangentSelect() const;
  PickerButton& weightedButton() const;
  PickerDropdown& preSelect() const;
  PickerDropdown& postSelect() const;

 private:
  CurveGraph* graph() const;
  uint32_t activeCurve() const;

  core::tree::WidgetId graph_;
  core::tree::WidgetId time_, value_, interp_, tangent_, weighted_, pre_, post_;
};

}  // namespace r1ui::widgets

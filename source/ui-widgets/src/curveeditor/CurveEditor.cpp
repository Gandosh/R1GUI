// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CurveEditor.h.
// Invariants: the graph's callbacks refresh the fields before the host's callbacks run, so a host
//   that reads the fields in its handler sees the new state; callbacks may destroy the editor.
// Callers: application code, tests, the gallery.
#include "r1ui/widgets/curveeditor/CurveEditor.h"

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

std::string_view CurveEditor::accessibleName() const {
  return WidgetObject::accessibleName().empty() ? std::string_view("Curve editor") : WidgetObject::accessibleName();
}

CurveGraph& CurveEditor::graph() const { return *ui().objectAs<CurveGraph>(graph_); }
CurveKeyFields& CurveEditor::fields() const { return *ui().objectAs<CurveKeyFields>(fields_); }

void CurveEditor::setShowFields(bool show) {
  if (WidgetObject* f = ui().object(fields_)) {
    f->style().display = show ? core::layout::Display::Flex : core::layout::Display::None;
    f->requestLayout();
  }
}

void CurveEditor::onAttached() {
  core::layout::Style& s = style();
  s.direction = core::layout::FlexDirection::Column;
  s.alignItems = core::layout::Align::Stretch;
  CurveGraph& graph = ui().create<CurveGraph>(id());
  graph_ = graph.id();
  CurveKeyFields& fields = ui().create<CurveKeyFields>(id(), graph.id());
  fields_ = fields.id();

  const auto refresh = [this] {
    if (CurveKeyFields* f = ui().objectAs<CurveKeyFields>(fields_)) f->refresh();
  };
  graph.onBeginInteraction = [this](const std::string& label) {
    if (onBeginInteraction) onBeginInteraction(label);
  };
  graph.onChanged = [this, refresh](const std::vector<uint32_t>& ids) {
    const core::tree::WidgetId self = id();
    refresh();
    if (onChanged) onChanged(ids);
    (void)self;
  };
  graph.onEndInteraction = [this, refresh](bool committed) {
    refresh();
    if (onEndInteraction) onEndInteraction(committed);
  };
  graph.onSelectionChanged = [this, refresh] {
    refresh();
    if (onSelectionChanged) onSelectionChanged();
  };
  graph.onViewChanged = [this] {
    if (onViewChanged) onViewChanged();
  };
  graph.onScrubChanged = [this](double t) {
    if (onScrubChanged) onScrubChanged(t);
  };
  graph.onContextMenu = [this](const CurveContext& c) {
    if (onContextMenu) onContextMenu(c);
  };
}

}  // namespace r1ui::widgets

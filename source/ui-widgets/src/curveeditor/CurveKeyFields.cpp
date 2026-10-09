// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CurveKeyFields.h.
// Invariants: the fields never hold edit state: refresh() rewrites every field from the graph, and a
//   commit calls exactly one graph command (one undo step) then refreshes; callbacks may destroy the
//   graph or this bar, so the graph is looked up by id at every use.
// Callers: CurveEditor; tests.
#include "r1ui/widgets/curveeditor/CurveKeyFields.h"

#include "r1ui/widgets/colorpicker/NumberText.h"
#include "r1ui/widgets/colorpicker/Swatches.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

namespace layout = core::layout;

template <class T>
T& partOf(const WidgetObject& owner, core::tree::WidgetId id) {
  return *owner.ui().objectAs<T>(id);
}

}  // namespace

CurveGraph* CurveKeyFields::graph() const { return ui().objectAs<CurveGraph>(graph_); }

PickerEntry& CurveKeyFields::timeEntry() const { return partOf<PickerEntry>(*this, time_); }
PickerEntry& CurveKeyFields::valueEntry() const { return partOf<PickerEntry>(*this, value_); }
PickerDropdown& CurveKeyFields::interpolationSelect() const { return partOf<PickerDropdown>(*this, interp_); }
PickerDropdown& CurveKeyFields::tangentSelect() const { return partOf<PickerDropdown>(*this, tangent_); }
PickerButton& CurveKeyFields::weightedButton() const { return partOf<PickerButton>(*this, weighted_); }
PickerDropdown& CurveKeyFields::preSelect() const { return partOf<PickerDropdown>(*this, pre_); }
PickerDropdown& CurveKeyFields::postSelect() const { return partOf<PickerDropdown>(*this, post_); }

void CurveKeyFields::onAttached() {
  layout::Style& s = style();
  s.direction = layout::FlexDirection::Row;
  s.wrap = layout::FlexWrap::Wrap;
  s.alignItems = layout::Align::Center;
  s.gapColumn = 6.0;
  s.gapRow = 6.0;
  s.flexShrink = 0.0;
  for (double& p : s.padding) p = 6.0;

  PickerEntry& time = ui().create<PickerEntry>(id(), PickerEntry::Look::Field);
  time_ = time.id();
  time.style().width = layout::Length::px(86);
  time.setSuffix("s");
  time.setAccessibleName("Key time");
  time.setTooltip("Time of the selected keys");
  time.setPadLeft(8.0);
  time.onCommit = [this](std::string_view text) {
    const auto v = parseNumber(text);
    if (!v) return false;
    if (CurveGraph* g = graph()) g->setSelectedTime(*v);
    refresh();
    return true;
  };

  PickerEntry& value = ui().create<PickerEntry>(id(), PickerEntry::Look::Field);
  value_ = value.id();
  value.style().width = layout::Length::px(86);
  value.setAccessibleName("Key value");
  value.setTooltip("Value of the selected keys");
  value.setPadLeft(8.0);
  value.onCommit = [this](std::string_view text) {
    const auto v = parseNumber(text);
    if (!v) return false;
    if (CurveGraph* g = graph()) g->setSelectedValue(*v);
    refresh();
    return true;
  };

  PickerDropdown& interp = ui().create<PickerDropdown>(id());
  interp_ = interp.id();
  interp.style().width = layout::Length::px(92);
  interp.setItems({"Constant", "Linear", "Cubic"});
  interp.setAccessibleName("Interpolation");
  interp.setTooltip("Interpolation toward the next key");
  interp.onSelect = [this](int index) {
    if (CurveGraph* g = graph()) g->setSelectedInterpolation(static_cast<curve::Interp>(index));
    refresh();
  };

  PickerDropdown& tangent = ui().create<PickerDropdown>(id());
  tangent_ = tangent.id();
  tangent.style().width = layout::Length::px(104);
  tangent.setItems({"Smooth", "Average", "Linked", "Independent"});
  tangent.setAccessibleName("Tangent mode");
  tangent.setTooltip("Tangent mode of cubic keys");
  tangent.onSelect = [this](int index) {
    if (CurveGraph* g = graph()) g->setSelectedTangentMode(static_cast<curve::TangentMode>(index));
    refresh();
  };

  PickerButton& weighted = ui().create<PickerButton>(id(), "spline", PickerButton::Look::Tab, 26.0, 14.0);
  weighted_ = weighted.id();
  weighted.setTooltipAndName("Weighted tangents");
  weighted.onActivate = [this] {
    if (CurveGraph* g = graph()) g->toggleWeights();
    refresh();
  };

  const std::vector<std::string> kinds = {"Constant", "Linear", "Repeat", "Repeat offset", "Ping pong"};
  for (const bool post : {false, true}) {
    PickerDropdown& d = ui().create<PickerDropdown>(id());
    (post ? post_ : pre_) = d.id();
    d.style().width = layout::Length::px(138);
    std::vector<std::string> items;
    for (const std::string& k : kinds) items.push_back(std::string(post ? "Post: " : "Pre: ") + k);
    d.setItems(std::move(items));
    d.setAccessibleName(post ? "Post extrapolation" : "Pre extrapolation");
    d.setTooltip(post ? "How the curve continues after its last key" : "How the curve continues before its first key");
    d.onSelect = [this, post](int index) {
      if (CurveGraph* g = graph()) g->setExtrapolation(activeCurve(), post, static_cast<curve::Extrapolation>(index));
      refresh();
    };
  }
  refresh();
}

uint32_t CurveKeyFields::activeCurve() const {
  const CurveGraph* g = graph();
  if (g == nullptr || g->selection().empty()) return 0;
  return g->selection().items().front().curve;
}

void CurveKeyFields::refresh() {
  const CurveGraph* g = graph();
  if (g == nullptr) return;
  const SelectionInfo info = g->selectionInfo();
  const bool any = info.keyCount > 0;
  timeEntry().setEnabled(any);
  valueEntry().setEnabled(any);
  interpolationSelect().setEnabled(any);
  tangentSelect().setEnabled(any);
  weightedButton().setEnabled(any);
  const curve::Curve* c = any ? curve::findCurve(g->curves(), activeCurve()) : nullptr;
  preSelect().setEnabled(c != nullptr && !c->locked);
  postSelect().setEnabled(c != nullptr && !c->locked);
  if (!any) {
    timeEntry().setText("");
    valueEntry().setText("");
    interpolationSelect().setSelected(-1);
    tangentSelect().setSelected(-1);
    weightedButton().setActive(false);
    preSelect().setSelected(-1);
    postSelect().setSelected(-1);
    return;
  }
  timeEntry().setText(info.mixedTime ? "Mixed" : formatNumber(info.time, 6));
  valueEntry().setText(info.mixedValue ? "Mixed" : formatNumber(info.value, 6));
  interpolationSelect().setSelected(info.mixedInterp ? -1 : static_cast<int>(info.interp));
  tangentSelect().setSelected(info.mixedTangent ? -1 : static_cast<int>(info.tangent));
  weightedButton().setActive(!info.mixedWeighted && info.weighted);
  if (c != nullptr) {
    preSelect().setSelected(static_cast<int>(c->pre));
    postSelect().setSelected(static_cast<int>(c->post));
  }
}

void CurveKeyFields::paint(PaintContext& ctx) {
  const render::Rect box = ctx.box();
  ctx.painter().fillRect(box, ctx.color("panel"));
  ctx.painter().fillRect({box.x, box.y, box.w, ctx.hairline()}, ctx.color("border"));
}

}  // namespace r1ui::widgets

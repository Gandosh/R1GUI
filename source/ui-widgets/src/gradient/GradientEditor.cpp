// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of GradientEditor.h.
// Invariants: model_ is the only copy of the gradient; the bar, the rows, the geometry fields and
//   the picker are views refreshed from it (parts ignore refreshes equal to what they show, so a
//   dragged part never fights its own value); begin / end callbacks are balanced by gestureDepth_;
//   selected_ always names a stop of model_; callbacks may destroy the editor.
// Callers: application code, tests, the gallery.
#include "r1ui/widgets/gradient/GradientEditor.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/colorpicker/NumberText.h"
#include "r1ui/widgets/colorpicker/PickerDraw.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

namespace layout = core::layout;

constexpr double kPad = 9.0;
constexpr double kContentWidth = 222.0;
constexpr double kSelectWidth = 112.0;

template <class T>
T& partOf(const WidgetObject& owner, core::tree::WidgetId id) {
  return *owner.ui().objectAs<T>(id);
}

}  // namespace

std::string_view GradientEditor::accessibleName() const {
  return WidgetObject::accessibleName().empty() ? std::string_view("Gradient editor") : WidgetObject::accessibleName();
}

void GradientEditor::onAttached() {
  layout::Style& s = style();
  s.direction = layout::FlexDirection::Column;
  s.alignItems = layout::Align::Stretch;
  s.gapRow = 8.0;
  s.flexShrink = 0.0;
  model_ = gradient::Gradient();
  selected_ = model_.stops().front().id;

  // ---- mode strip ----
  PickerBox& tabs = ui().create<PickerBox>(id(), "GradientTabs");
  tabs_ = tabs.id();
  tabs.style().direction = layout::FlexDirection::Row;
  tabs.style().gapColumn = 2.0;
  tabs.style().flexShrink = 0.0;
  static const char* const kIcons[3] = {"square", "blend", "image"};
  static const char* const kNames[3] = {"Solid", "Gradient", "Image"};
  for (int i = 0; i < 3; ++i) {
    PickerButton& b = ui().create<PickerButton>(tabs.id(), kIcons[i], PickerButton::Look::Tab, 24.0, 14.0);
    tabButtons_[i] = b.id();
    b.setTooltipAndName(kNames[i]);
    b.onActivate = [this, i] {
      const PickerMode picked = static_cast<PickerMode>(i);
      if (picked == mode_) return;
      setMode(picked);
      if (onModeChanged) onModeChanged(picked);
    };
  }
  setMode(PickerMode::Gradient);

  // ---- type control ----
  PickerBox& typeRow = ui().create<PickerBox>(id(), "GradientTypeRow");
  typeRow_ = typeRow.id();
  typeRow.style().direction = layout::FlexDirection::Row;
  typeRow.style().gapColumn = 2.0;
  typeRow.style().flexShrink = 0.0;
  PickerDropdown& select = ui().create<PickerDropdown>(typeRow.id());
  typeSelect_ = select.id();
  select.style().width = layout::Length::px(kSelectWidth);
  select.setItems({"Linear", "Radial", "Angular"});
  select.setSelected(0);
  select.setAccessibleName("Gradient type");
  select.onSelect = [this](int index) { applyType(static_cast<gradient::GradientType>(index)); };
  static const char* const kTypeIcons[3] = {"move-horizontal", "circle", "rotate-cw"};
  static const char* const kTypeNames[3] = {"Linear", "Radial", "Angular"};
  for (int i = 0; i < 3; ++i) {
    PickerButton& b = ui().create<PickerButton>(typeRow.id(), kTypeIcons[i], PickerButton::Look::Tab, 24.0, 14.0);
    typeButtons_[i] = b.id();
    b.setTooltipAndName(kTypeNames[i]);
    b.style().display = layout::Display::None;
    b.onActivate = [this, i] { applyType(static_cast<gradient::GradientType>(i)); };
  }

  // ---- angle and centre fields ----
  PickerBox& geometry = ui().create<PickerBox>(id(), "GradientGeometryRow");
  geometryRow_ = geometry.id();
  geometry.style().direction = layout::FlexDirection::Row;
  geometry.style().gapColumn = 6.0;
  geometry.style().flexShrink = 0.0;
  const char* const kSuffix[3] = {"\xC2\xB0", "%", "%"};
  const char* const kFieldNames[3] = {"Gradient angle", "Gradient center X", "Gradient center Y"};
  core::tree::WidgetId* const fieldIds[3] = {&angle_, &centerX_, &centerY_};
  for (int i = 0; i < 3; ++i) {
    PickerEntry& e = ui().create<PickerEntry>(geometry.id(), PickerEntry::Look::Field);
    *fieldIds[i] = e.id();
    e.style().flexGrow = 1.0;
    e.style().flexBasis = layout::Length::px(0);
    e.setSuffix(kSuffix[i]);
    e.setFontSize(13.0);
    e.setPadLeft(8.0);
    e.setAccessibleName(kFieldNames[i]);
    e.setTooltip(kFieldNames[i]);
    e.onCommit = [this, i](std::string_view t) { return commitGeometry(i, t); };
  }

  // ---- bar ----
  GradientBar& bar = ui().create<GradientBar>(id());
  bar_ = bar.id();
  bar.style().margin[layout::kLeft] = layout::Length::px(-GradientBar::kOverhang);
  bar.style().margin[layout::kRight] = layout::Length::px(-GradientBar::kOverhang);
  bar.onBegin = [this] { beginGesture(); };
  bar.onEdit = [this](const gradient::Gradient& g) {
    apply([&](gradient::Gradient& m) {
      if (m == g) return false;
      m = g;
      return true;
    }, false);
  };
  bar.onSelect = [this](uint32_t sid) { selectStop(sid); };
  bar.onEnd = [this] { endGesture(); };

  // ---- stops ----
  PickerBox& stops = ui().create<PickerBox>(id(), "GradientStops");
  stopsBox_ = stops.id();
  stops.style().direction = layout::FlexDirection::Column;
  stops.style().gapRow = 4.0;
  stops.style().flexShrink = 0.0;
  PickerBox& header = ui().create<PickerBox>(stops.id(), "GradientStopsHeader");
  header_ = header.id();
  header.style().direction = layout::FlexDirection::Row;
  header.style().alignItems = layout::Align::Center;
  header.style().justifyContent = layout::Justify::End;
  header.style().height = layout::Length::px(16.5);  // 11 px text at line height 1.5
  PickerButton& add = ui().create<PickerButton>(header.id(), "plus", PickerButton::Look::Plain, 16.0, 12.0);
  add_ = add.id();
  add.setTooltipAndName("Add stop");
  add.onActivate = [this] { addStopAtGap(); };
  PickerBox& rows = ui().create<PickerBox>(stops.id(), "GradientStopRows");
  rowsBox_ = rows.id();
  rows.style().direction = layout::FlexDirection::Column;

  // ---- colour of the selected stop ----
  ColorPicker& picker = ui().create<ColorPicker>(id());
  picker_ = picker.id();
  picker.setChrome(false);
  picker.setShowModeTabs(false);
  picker.onBeginInteraction = [this] { beginGesture(); };
  picker.onChanged = [this](const ColorChange& c) {
    const uint32_t stop = selected_;
    apply([&](gradient::Gradient& m) { return m.setStopColor(stop, c.color); }, false);
  };
  picker.onEndInteraction = [this] { endGesture(); };

  setChrome(true);
  setGradient(model_);
}

GradientBar& GradientEditor::bar() const { return partOf<GradientBar>(*this, bar_); }
ColorPicker& GradientEditor::picker() const { return partOf<ColorPicker>(*this, picker_); }
PickerDropdown& GradientEditor::typeSelect() const { return partOf<PickerDropdown>(*this, typeSelect_); }
PickerButton& GradientEditor::typeButton(gradient::GradientType type) const { return partOf<PickerButton>(*this, typeButtons_[static_cast<int>(type)]); }
PickerButton& GradientEditor::addStopButton() const { return partOf<PickerButton>(*this, add_); }
PickerButton& GradientEditor::modeTab(PickerMode mode) const { return partOf<PickerButton>(*this, tabButtons_[static_cast<int>(mode)]); }
PickerEntry& GradientEditor::angleEntry() const { return partOf<PickerEntry>(*this, angle_); }
PickerEntry& GradientEditor::centerXEntry() const { return partOf<PickerEntry>(*this, centerX_); }
PickerEntry& GradientEditor::centerYEntry() const { return partOf<PickerEntry>(*this, centerY_); }
GradientStopRow& GradientEditor::row(size_t index) const { return partOf<GradientStopRow>(*this, rows_.at(index)); }

// ---- look -------------------------------------------------------------------------------------

void GradientEditor::setChrome(bool chrome) {
  chrome_ = chrome;
  layout::Style& s = style();
  for (double& p : s.padding) p = chrome ? kPad : 0.0;
  s.width = chrome ? layout::Length::px(kContentWidth + 2.0 * kPad) : layout::Length::autoValue();
  requestLayout();
  requestPaint();
}

void GradientEditor::setShowModeTabs(bool show) {
  if (WidgetObject* tabs = ui().object(tabs_)) {
    tabs->style().display = show ? layout::Display::Flex : layout::Display::None;
    tabs->requestLayout();
  }
}

void GradientEditor::setMode(PickerMode mode) {
  mode_ = mode;
  for (int i = 0; i < 3; ++i) partOf<PickerButton>(*this, tabButtons_[i]).setActive(i == static_cast<int>(mode));
}

void GradientEditor::setTypeControl(TypeControl control) {
  typeControl_ = control;
  typeSelect().style().display = control == TypeControl::Select ? layout::Display::Flex : layout::Display::None;
  typeSelect().requestLayout();
  for (int i = 0; i < 3; ++i) {
    PickerButton& b = partOf<PickerButton>(*this, typeButtons_[i]);
    b.style().display = control == TypeControl::Buttons ? layout::Display::Flex : layout::Display::None;
    b.requestLayout();
  }
  requestLayout();
  refreshGeometry();
}

void GradientEditor::setShowGeometryFields(bool show) {
  showGeometry_ = show;
  refreshGeometry();
}

// ---- value ------------------------------------------------------------------------------------

void GradientEditor::setGradient(const gradient::Gradient& g) {
  model_ = g;
  if (model_.indexOf(selected_) == gradient::Gradient::npos) selected_ = model_.stops().front().id;
  refreshAll(true);
}

void GradientEditor::setSelectedStop(uint32_t id) {
  if (model_.indexOf(id) == gradient::Gradient::npos || id == selected_) return;
  selected_ = id;
  refreshAll(true);
}

void GradientEditor::rebuildRows() {
  const auto& stops = model_.stops();
  bool same = rows_.size() == stops.size();
  for (size_t i = 0; same && i < stops.size(); ++i) {
    const GradientStopRow* r = ui().objectAs<GradientStopRow>(rows_[i]);
    same = r != nullptr && r->stopId() == stops[i].id;
  }
  if (same) return;
  for (const core::tree::WidgetId r : rows_) ui().destroy(r);
  rows_.clear();
  for (const gradient::Stop& s : stops) {
    GradientStopRow& r = ui().create<GradientStopRow>(rowsBox_, s.id);
    rows_.push_back(r.id());
    r.onSelect = [this](uint32_t sid) { selectStop(sid); };
    r.onBegin = [this] { beginGesture(); };
    r.onEnd = [this] { endGesture(); };
    r.onPosition = [this](uint32_t sid, double position) {
      apply([&](gradient::Gradient& m) { return m.moveStop(sid, position); }, false);
    };
    r.onColour = [this](uint32_t sid, const color::Rgba& c) {
      const bool isSelected = sid == selected_;
      apply([&](gradient::Gradient& m) { return m.setStopColor(sid, c); }, isSelected);
    };
    r.onRemove = [this](uint32_t sid) {
      edit([&](gradient::Gradient& m) {
        const size_t index = m.indexOf(sid);
        if (!m.removeStop(sid)) return false;
        selected_ = m.stops()[std::min(index, m.size() - 1)].id;
        return true;
      });
    };
  }
  if (WidgetObject* box = ui().object(rowsBox_)) box->requestLayout();
}

void GradientEditor::refreshGeometry() {
  const gradient::GradientType type = model_.type();
  typeSelect().setSelected(static_cast<int>(type));
  for (int i = 0; i < 3; ++i) partOf<PickerButton>(*this, typeButtons_[i]).setActive(i == static_cast<int>(type));
  const bool angle = type != gradient::GradientType::Radial;
  const bool centre = type != gradient::GradientType::Linear;
  angleEntry().style().display = angle ? layout::Display::Flex : layout::Display::None;
  centerXEntry().style().display = centre ? layout::Display::Flex : layout::Display::None;
  centerYEntry().style().display = centre ? layout::Display::Flex : layout::Display::None;
  if (WidgetObject* g = ui().object(geometryRow_)) {
    g->style().display = showGeometry_ ? layout::Display::Flex : layout::Display::None;
    g->requestLayout();
  }
  angleEntry().setText(formatNumber(model_.angleDegrees(), 1));
  centerXEntry().setText(formatNumber(model_.centerX() * 100.0, 1));
  centerYEntry().setText(formatNumber(model_.centerY() * 100.0, 1));
}

void GradientEditor::refreshAll(bool refreshPicker) {
  bar().setGradient(model_);
  bar().setSelectedId(selected_);
  rebuildRows();
  for (size_t i = 0; i < rows_.size(); ++i) {
    const gradient::Stop& s = model_.stops()[i];
    row(i).setStop(s, s.id == selected_);
  }
  refreshGeometry();
  if (refreshPicker) {
    if (const gradient::Stop* s = model_.find(selected_)) picker().setColor(s->color);
  }
  requestPaint();
}

// ---- gestures and edits -----------------------------------------------------------------------

void GradientEditor::beginGesture() {
  if (gestureDepth_++ != 0) return;
  if (onBeginInteraction) onBeginInteraction();
}

void GradientEditor::endGesture() {
  if (gestureDepth_ <= 0) return;
  if (--gestureDepth_ != 0) return;
  if (onEndInteraction) onEndInteraction();
}

void GradientEditor::emit() {
  const core::tree::WidgetId self = id();
  if (onChanged) onChanged(GradientChange{model_, selected_, gestureDepth_ > 0});
  if (!ui().alive(self)) return;
  requestPaint();
}

void GradientEditor::commitModel(gradient::Gradient next, bool refreshPicker) {
  model_ = std::move(next);
  if (model_.indexOf(selected_) == gradient::Gradient::npos) selected_ = model_.stops().front().id;
  refreshAll(refreshPicker);
  emit();
}

bool GradientEditor::apply(const std::function<bool(gradient::Gradient&)>& change, bool refreshPicker) {
  gradient::Gradient next = model_;
  if (!change(next)) return false;
  commitModel(std::move(next), refreshPicker);
  return true;
}

void GradientEditor::edit(const std::function<bool(gradient::Gradient&)>& change) {
  const core::tree::WidgetId self = id();
  const uint32_t selectedBefore = selected_;
  gradient::Gradient next = model_;
  if (!change(next)) {
    selected_ = selectedBefore;  // a rejected change leaves the selection alone
    return;
  }
  beginGesture();
  if (!ui().alive(self)) return;
  commitModel(std::move(next), true);
  if (!ui().alive(self)) return;
  endGesture();
}

void GradientEditor::selectStop(uint32_t sid) {
  if (sid == selected_ || model_.indexOf(sid) == gradient::Gradient::npos) return;
  selected_ = sid;
  refreshAll(true);
  const core::tree::WidgetId self = id();
  if (onSelectionChanged) onSelectionChanged(sid);
  if (!ui().alive(self)) return;
}

void GradientEditor::addStopAtGap() {
  edit([&](gradient::Gradient& m) {
    // The middle of the widest gap between neighbouring stops (or between a stop and the nearer end).
    const auto& s = m.stops();
    double bestStart = 0.0;
    double bestWidth = -1.0;
    double previous = 0.0;
    for (size_t i = 0; i <= s.size(); ++i) {
      const double next = i < s.size() ? s[i].position : 1.0;
      if (next - previous > bestWidth) {
        bestWidth = next - previous;
        bestStart = previous;
      }
      previous = next;
    }
    const uint32_t added = m.addStop(bestStart + bestWidth * 0.5);
    if (added == 0) return false;
    selected_ = added;
    return true;
  });
}

void GradientEditor::applyType(gradient::GradientType type) {
  edit([&](gradient::Gradient& m) { return m.setType(type); });
}

bool GradientEditor::commitGeometry(int field, std::string_view text) {
  const auto v = parseNumber(text);
  if (!v) return false;
  edit([&](gradient::Gradient& m) {
    if (field == 0) return m.setAngle(*v);
    return field == 1 ? m.setCenter(*v / 100.0, m.centerY()) : m.setCenter(m.centerX(), *v / 100.0);
  });
  // A value that did not change the model still shows its canonical text.
  refreshGeometry();
  return true;
}

// ---- painting ---------------------------------------------------------------------------------

void GradientEditor::paint(PaintContext& ctx) {
  if (chrome_) pickerdraw::drawPanelSurface(ctx);
  // The title of the stop list sits in the header row, left of the add button.
  const core::layout::Rect h = ui().absRect(header_);
  const theme::ResolvedStyle& rs = ctx.resolve("picker.swatches.title", 0);
  TextOptions options;
  ctx.drawText("Stops", rs.text, ctx.toPhysical(h.x, h.y, std::max(0, h.w - 20), h.h), options);
}

}  // namespace r1ui::widgets

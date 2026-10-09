// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of PropertyRow.h: building the controls of a row from its descriptor, wiring them
//   to PropertyContext edits, and mapping PropertyState onto widget states.
// Why: see PropertyRow.h. Geometry follows the reference panel: a caption line of 11 px muted text with a
//   4 px gap to the controls, 26 px high controls, fields sharing the 234 px content width (two 114 px
//   fields with a 6 px gap), the colour chip 26 px, the alpha field 64 px (as the reference paint field).
// Invariants: refresh() never calls an edit callback (programmatic setters only); every edit goes
//   through PropertyContext so validation, clamping and undo are identical for all kinds; the row never
//   keeps a raw pointer to another widget (ids only).
// Callers: PropertyPanel, gallery, tests.
#include "r1ui/widgets/props/PropertyRow.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/checkbox/Checkbox.h"
#include "r1ui/widgets/colorpicker/ColorPicker.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/numberfield/NumberField.h"
#include "r1ui/widgets/props/PropControls.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/section/Section.h"
#include "r1ui/widgets/select/Select.h"
#include "r1ui/widgets/switch/Switch.h"
#include "r1ui/widgets/textinput/TextInput.h"
#include "r1ui/widgets/toolbar/FlyoutList.h"

namespace r1ui::widgets {

namespace {

namespace layout = core::layout;
using core::tree::WidgetId;
using props::PropertyDescriptor;
using props::PropertyState;
using props::Value;
using props::ValueKind;

constexpr double kCaptionGap = 4.0;   // docs/spec/widgets.md 3: label to control
constexpr double kFieldGap = 6.0;     // gap between the fields of a row
constexpr double kAlphaWidth = 64.0;  // opacity field of a paint row
constexpr double kSliderFieldWidth = 84.0;  // the value field beside a slider: room for "Mixed" and the variable button
constexpr const char* kAxisNames[] = {"X", "Y", "Z"};

SectionBox& flexBox(UiContext& ui, WidgetId parent, bool row, double gap) {
  SectionBox& box = ui.create<SectionBox>(parent);
  box.style().direction = row ? layout::FlexDirection::Row : layout::FlexDirection::Column;
  box.style().gapRow = gap;
  box.style().gapColumn = gap;
  box.style().flexShrink = 0.0;
  box.style().minWidth = layout::Length::px(0);
  return box;
}

void grow(layout::Style& s) {
  s.flexGrow = 1.0;
  s.flexShrink = 1.0;
  s.flexBasis = layout::Length::px(0.0);
  s.minWidth = layout::Length::px(0.0);
}

color::Rgba toRgba(const props::Color& c) { return color::sanitized(color::Rgba{{c.r, c.g, c.b}, c.a}); }
props::Color toProps(const color::Rgba& c) {
  return {static_cast<float>(c.rgb.r), static_cast<float>(c.rgb.g), static_cast<float>(c.rgb.b), static_cast<float>(c.a)};
}

// A pointer position as a whole logical pixel, clamped so a hostile coordinate cannot overflow.
int32_t pixel(double v) { return static_cast<int32_t>(std::clamp(std::isfinite(v) ? std::lround(v) : 0L, -1000000L, 1000000L)); }

bool numericScalar(ValueKind kind) { return kind == ValueKind::Int || kind == ValueKind::Double || kind == ValueKind::Range; }

}  // namespace

PropertyRowView::PropertyRowView(props::PropertyContext& context, size_t row, PropertyRowOptions options)
    : ctx_(&context), row_(row), options_(std::move(options)) {}

PropertyRowView::~PropertyRowView() = default;

std::string_view PropertyRowView::accessibleName() const {
  if (!WidgetObject::accessibleName().empty() || row_ >= ctx_->rowCount()) return WidgetObject::accessibleName();
  return ctx_->descriptor(row_).shownLabel();
}

void PropertyRowView::onAttached() {
  build();
  refresh();
}

void PropertyRowView::onDetached() {
  if (colorPopup_.valid()) closePopover(ui(), colorPopup_);
}

// ---- construction ---------------------------------------------------------------------------------------------

void PropertyRowView::build() {
  layout::Style& s = style();
  s.direction = layout::FlexDirection::Column;
  s.alignItems = layout::Align::Stretch;
  s.gapRow = kCaptionGap;
  s.flexShrink = 0.0;
  s.minWidth = layout::Length::px(0);
  if (row_ >= ctx_->rowCount()) return;
  const PropertyDescriptor& d = ctx_->descriptor(row_);
  kind_ = d.kind;
  setTooltip(d.meta.tooltip);

  UiContext& u = ui();
  WidgetId line = id();
  if (options_.caption) {
    SectionBox& captionLine = flexBox(u, id(), true, kCaptionGap);
    captionLine.style().alignItems = layout::Align::Center;
    line = captionLine.id();
    if (kind_ != ValueKind::Bool) {
      Label& label = u.create<Label>(line, d.shownLabel(), LabelRole::Caption);
      grow(label.style());
      caption_ = label.id();
    }
  }

  switch (kind_) {
    case ValueKind::Bool: buildBool(line); break;
    case ValueKind::Int:
    case ValueKind::Double: buildNumbers(id(), 1, false); break;
    case ValueKind::Range: buildNumbers(id(), 1, true); break;
    case ValueKind::Vec2: buildNumbers(id(), 2, false); break;
    case ValueKind::Vec3: buildNumbers(id(), 3, false); break;
    case ValueKind::Enum: buildEnum(id()); break;
    case ValueKind::String: buildString(id()); break;
    case ValueKind::Color: buildColor(id()); break;
  }

  if (options_.caption) {
    PropResetButton& reset = u.create<PropResetButton>(line);
    reset.setReset(options_.strings.reset, [this] {
      if (row_ >= ctx_->rowCount()) return;
      report_ = ctx_->reset(row_);
      refresh();
    });
    reset_ = reset.id();
  }
}

void PropertyRowView::configureNumber(NumberField& field, int axis) {
  const props::PropertyMeta& m = ctx_->descriptor(row_).meta;
  if (m.min || m.max) field.setRange(m.min.value_or(-1.0e15), m.max.value_or(1.0e15));
  if (m.softMin && m.softMax) field.setSoftRange(*m.softMin, *m.softMax);
  if (m.step > 0.0) field.setIncrement(m.step);
  field.setInteger(kind_ == ValueKind::Int);
  if (m.precision >= 0) field.setFractionDigits(m.precision, m.precision);
  if (!m.unit.empty()) field.setSuffix(m.unit);
  if (!m.units.empty()) {
    std::vector<NumberUnit> units;
    for (const props::UnitConversion& u : m.units) units.push_back({u.suffix, u.factor});
    field.setUnits(std::move(units));
  }
  if (axis >= 0) field.setLabel(kAxisNames[std::min(axis, 2)]);
  if (axis < 0 && numericScalar(kind_) && ctx_->provider() != nullptr) {
    field.setVariableButton(true, options_.strings.applyVariable);
    field.setOnVariableButton([this] { openBindingMenu(); });
  }
  field.setOnBeginInteraction([this] { beginGesture(); });
  field.setOnValueChanged([this, axis](double value, bool interactive) { edit(axis, value, interactive); });
  field.setOnEndInteraction([this](const InteractionEnd& end) { endGesture(end.cancelled); });
  field.setOnMixedExpression([this, axis](const NumberExpression& expression) {
    if (row_ >= ctx_->rowCount()) return;
    report_ = ctx_->setComputed(row_, axis, [&](double current) { return expression.evaluate(current); });
  });
}

void PropertyRowView::buildNumbers(WidgetId parent, size_t count, bool withSlider) {
  UiContext& u = ui();
  SectionBox& area = flexBox(u, parent, true, kFieldGap);
  area.style().alignItems = layout::Align::Center;
  if (withSlider) {
    const props::PropertyMeta& m = ctx_->descriptor(row_).meta;
    PropSlider& slider = u.create<PropSlider>(area.id());
    slider.setRange(m.min.value_or(0.0), m.max.value_or(1.0));
    slider.setOnBegin([this] { beginGesture(); });
    slider.setOnChanged([this](double value, bool interactive) { edit(-1, value, interactive); });
    slider.setOnEnd([this](bool cancelled, bool) { endGesture(cancelled); });
    slider_ = slider.id();
  }
  for (size_t i = 0; i < count; ++i) {
    NumberField& field = u.create<NumberField>(area.id());
    if (withSlider) {
      field.style().width = layout::Length::px(kSliderFieldWidth);
      field.style().flexShrink = 0.0;
    } else {
      grow(field.style());
    }
    configureNumber(field, count > 1 ? static_cast<int>(i) : -1);
    numbers_.push_back(field.id());
  }
}

void PropertyRowView::buildBool(WidgetId captionLine) {
  UiContext& u = ui();
  const PropertyDescriptor& d = ctx_->descriptor(row_);
  const auto commit = [this](bool value) {
    if (row_ >= ctx_->rowCount()) return;
    report_ = ctx_->setValue(row_, Value(value));
    afterEdit();
  };
  if (d.meta.asSwitch) {
    Label& label = u.create<Label>(captionLine, d.shownLabel(), LabelRole::Caption);
    grow(label.style());
    caption_ = label.id();
    Switch& toggle = u.create<Switch>(captionLine, SwitchSize::Sm);
    toggle.setOnChange(commit);
    toggle_ = toggle.id();
  } else {
    Checkbox& box = u.create<Checkbox>(captionLine, d.shownLabel());
    grow(box.style());
    box.setOnChange(commit);
    check_ = box.id();
  }
}

void PropertyRowView::buildEnum(WidgetId parent) {
  Select& select = ui().create<Select>(parent);
  for (const props::EnumEntry& e : ctx_->descriptor(row_).enumEntries) select.addItem(e.label.empty() ? e.name : e.label, e.name);
  select.setOnChanged([this](size_t, std::string_view value) {
    if (row_ >= ctx_->rowCount()) return;
    report_ = ctx_->setText(row_, value);
    afterEdit();
  });
  select_ = select.id();
}

void PropertyRowView::buildString(WidgetId parent) {
  TextInput& input = ui().create<TextInput>(parent, TextInputTone::Panel, TextInputSize::Md);
  input.setOnCommitted([this](std::string_view text) {
    if (row_ >= ctx_->rowCount()) return;
    report_ = ctx_->setText(row_, text);
    syncText_ = true;
    afterEdit();
  });
  text_ = input.id();
}

void PropertyRowView::buildColor(WidgetId parent) {
  UiContext& u = ui();
  SectionBox& area = flexBox(u, parent, true, kFieldGap);
  area.style().alignItems = layout::Align::Center;
  ColorChip& chip = u.create<ColorChip>(area.id());
  chip.setOnActivate([this] { openColorPopup(); });
  chip_ = chip.id();
  TextInput& hex = u.create<TextInput>(area.id(), TextInputTone::Panel, TextInputSize::Md);
  grow(hex.style());
  hex.setMaxLength(32);
  hex.setOnCommitted([this](std::string_view text) {
    if (row_ >= ctx_->rowCount()) return;
    std::string typed(text);
    if (!typed.empty() && typed.front() != '#' && typed.rfind("rgba(", 0) != 0) typed.insert(typed.begin(), '#');
    // Only a hex with an alpha pair (#rgba, #rrggbbaa) changes the opacity; otherwise each object keeps its own.
    const auto parsed = color::parseHex(typed);
    const size_t digits = typed.empty() ? 0 : typed.size() - 1;
    if (parsed && digits != 4 && digits != 8) {
      beginGesture();
      edit(0, parsed->rgb.r);
      edit(1, parsed->rgb.g);
      edit(2, parsed->rgb.b);
      syncText_ = true;
      endGesture(false);
      return;
    }
    report_ = ctx_->setText(row_, typed);
    syncText_ = true;
    afterEdit();
  });
  text_ = hex.id();
  NumberField& alpha = u.create<NumberField>(area.id());
  alpha.style().width = layout::Length::px(kAlphaWidth);
  alpha.style().flexShrink = 0.0;
  alpha.setRange(0.0, 100.0);
  alpha.setInteger(true);
  alpha.setSuffix("%");
  alpha.setOnBeginInteraction([this] { beginGesture(); });
  alpha.setOnValueChanged([this](double percent, bool interactive) { edit(3, percent / 100.0, interactive); });
  alpha.setOnEndInteraction([this](const InteractionEnd& end) { endGesture(end.cancelled); });
  alpha_ = alpha.id();
}

// ---- edits ---------------------------------------------------------------------------------------------------------

void PropertyRowView::beginGesture() {
  if (!begun_ && row_ < ctx_->rowCount()) begun_ = ctx_->beginInteraction(row_);
}

void PropertyRowView::endGesture(bool cancelled) {
  if (begun_) {
    begun_ = false;
    // A gesture without interactive values is one notch or one typed commit; with mergeSteps notches group.
    if (cancelled) ctx_->cancelInteraction();
    else ctx_->endInteraction(options_.mergeSteps && !scrubbed_);
    scrubbed_ = false;
  }
  afterEdit();
}

// axis < 0: the whole value; otherwise one axis of a vector or channel of a colour.
void PropertyRowView::edit(int axis, double value, bool interactive) {
  if (row_ >= ctx_->rowCount()) return;
  scrubbed_ = scrubbed_ || interactive;
  report_ = axis < 0 ? ctx_->setValue(row_, Value(value)) : ctx_->setComponent(row_, static_cast<size_t>(axis), value);
}

void PropertyRowView::afterEdit() {
  if (ui().alive(id())) refresh();
}

// ---- state -> widgets ----------------------------------------------------------------------------------------------

void PropertyRowView::refresh() {
  if (row_ >= ctx_->rowCount()) return;
  const PropertyState state = ctx_->state(row_);
  const bool editable = state.enabled && !state.readOnly;
  UiContext& u = ui();
  if (PropResetButton* reset = u.objectAs<PropResetButton>(reset_)) reset->setShown(state.canReset);
  if (WidgetObject* caption = u.object(caption_)) caption->setEnabled(state.enabled);
  refreshNumbers(state, editable);
  refreshOthers(state, editable);
  syncText_ = false;
}

void PropertyRowView::refreshNumbers(const PropertyState& state, bool editable) {
  UiContext& u = ui();
  const auto component = [&](size_t axis) { return props::componentOf(state.value, axis).value_or(0.0); };
  if (PropSlider* slider = u.objectAs<PropSlider>(slider_)) {
    if (!slider->dragging()) slider->setValue(component(0));
    slider->setMixed(state.mixed);
    slider->setEnabled(editable);
  }
  for (size_t i = 0; i < numbers_.size(); ++i) {
    NumberField* field = u.objectAs<NumberField>(numbers_[i]);
    if (field == nullptr) continue;
    const bool vector = numbers_.size() > 1;
    if (!field->editing() && !field->scrubbing()) field->setValue(component(i));
    field->setMixed(vector ? state.componentMixed[i] : state.mixed);
    field->setEnabled(editable);
    if (vector) continue;
    // D16: scalar rows show their binding. Bound: the variable pill; broken: the pill in the invalid look.
    const bool shown = state.binding == props::BindingState::Bound || state.binding == props::BindingState::Broken;
    field->setBoundVariable(shown ? state.bindingSource : std::string());
    field->setInvalid(state.binding == props::BindingState::Broken);
    field->setTooltip(state.binding == props::BindingState::Broken ? options_.strings.brokenPrefix + state.bindingSource : std::string());
  }
}

void PropertyRowView::refreshOthers(const PropertyState& state, bool editable) {
  UiContext& u = ui();
  const props::PropertyDescriptor& d = ctx_->descriptor(row_);
  if (Checkbox* box = u.objectAs<Checkbox>(check_)) {
    box->setChecked(std::holds_alternative<bool>(state.value) && std::get<bool>(state.value));
    box->setMixed(state.mixed);
    box->setEnabled(editable);
  }
  if (Switch* toggle = u.objectAs<Switch>(toggle_)) {
    toggle->setChecked(std::holds_alternative<bool>(state.value) && std::get<bool>(state.value));
    toggle->setMixed(state.mixed);
    toggle->setEnabled(editable);
  }
  if (Select* select = u.objectAs<Select>(select_)) {
    const auto* v = std::get_if<int64_t>(&state.value);
    const props::EnumEntry* entry = v != nullptr ? d.findEnum(*v) : nullptr;
    if (entry == nullptr || state.mixed || !select->setSelectedValue(entry->name)) select->setSelectedIndex(std::nullopt);
    select->setPlaceholder(state.mixed ? options_.strings.mixed : std::string());
    select->setEnabled(editable);
  }
  if (kind_ == ValueKind::String) {
    if (TextInput* input = u.objectAs<TextInput>(text_)) {
      if (!input->focused() || syncText_) {
        const bool hidden = d.meta.password;
        input->setText(state.mixed || hidden ? std::string() : (std::holds_alternative<std::string>(state.value) ? std::get<std::string>(state.value) : std::string()));
        input->setPlaceholder(hidden ? options_.strings.hiddenValue : std::string());
      }
      input->setMixed(state.mixed && !d.meta.password);
      input->setReadOnly(state.readOnly);
      input->setEnabled(state.enabled);
    }
  }
  if (kind_ == ValueKind::Color) {
    const auto* c = std::get_if<props::Color>(&state.value);
    const color::Rgba rgba = c != nullptr ? toRgba(*c) : color::Rgba{};
    if (ColorChip* chip = u.objectAs<ColorChip>(chip_)) {
      chip->setColor(rgba);
      chip->setMixed(state.mixed);
      chip->setEnabled(editable);
    }
    if (TextInput* hex = u.objectAs<TextInput>(text_)) {
      if (!hex->focused() || syncText_) hex->setText(state.mixed ? std::string() : color::formatHex(rgba));
      hex->setMixed(state.mixed);
      hex->setReadOnly(state.readOnly);
      hex->setEnabled(state.enabled);
    }
    if (NumberField* alpha = u.objectAs<NumberField>(alpha_)) {
      if (!alpha->editing() && !alpha->scrubbing()) alpha->setValue(std::round(rgba.a * 100.0));
      alpha->setMixed(state.componentMixed[3]);
      alpha->setEnabled(editable);
    }
  }
}

// ---- popups --------------------------------------------------------------------------------------------------------

bool PropertyRowView::colorPopupOpen() const { return colorPopup_.valid() && isPopoverOpen(ui(), colorPopup_); }

void PropertyRowView::openColorPopup() {
  UiContext& u = ui();
  if (colorPopupOpen()) {
    closePopover(u, colorPopup_);
    return;
  }
  if (row_ >= ctx_->rowCount() || !ctx_->state(row_).enabled) return;
  PopoverOptions options;
  options.anchorWidget = chip_;
  options.owner = chip_;
  options.placement = Placement::LeftStart;
  options.gap = 8.0;
  options.padding = 8.0;
  options.width = ColorPicker::kContentWidth + 2.0 * options.padding + 2.0;
  colorPopup_ = openPopover(u, options);
  if (!colorPopup_.valid()) return;
  ColorPicker& picker = u.create<ColorPicker>(colorPopup_.host);
  picker.setChrome(false);
  const PropertyState state = ctx_->state(row_);
  if (const auto* c = std::get_if<props::Color>(&state.value)) picker.setColor(toRgba(*c));
  picker.onBeginInteraction = [this] { beginGesture(); };
  picker.onChanged = [this](const ColorChange& change) {
    if (row_ >= ctx_->rowCount()) return;
    report_ = ctx_->setValue(row_, Value(toProps(change.color)));
  };
  picker.onEndInteraction = [this] { endGesture(false); };
}

void PropertyRowView::openBindingMenu() {
  if (row_ >= ctx_->rowCount() || ctx_->provider() == nullptr) return;
  const PropertyState state = ctx_->state(row_);
  std::vector<FlyoutItem> items;
  std::vector<std::string> sources;  // source of each item (empty: the unbind entry)
  items.push_back({.label = options_.strings.unbind, .icon = "unlink", .enabled = state.binding != props::BindingState::Unbound});
  sources.emplace_back();
  items.push_back({.separator = true});
  sources.emplace_back();
  size_t listed = 0;
  for (const props::VariableInfo& v : ctx_->provider()->variables()) {
    const bool numeric = v.kind == ValueKind::Int || v.kind == ValueKind::Double || v.kind == ValueKind::Range;
    if (!numeric || listed >= 256) continue;
    items.push_back({.label = v.name, .checked = v.name == state.bindingSource});
    sources.push_back(v.name);
    ++listed;
  }
  if (listed == 0) {
    items.push_back({.label = options_.strings.noVariables, .enabled = false});
    sources.emplace_back();
  }
  FlyoutOpenOptions open;
  open.anchor = ui().absRect(numbers_.empty() ? id() : numbers_.front());
  open.anchorWidget = numbers_.empty() ? id() : numbers_.front();
  open.placement = Placement::BelowStart;
  const WidgetId self = id();
  openFlyout(
      ui(), std::move(items),
      [this_ui = &ui(), self, sources = std::move(sources)](size_t index) {
        auto* row = this_ui->objectAs<PropertyRowView>(self);
        if (row == nullptr || index >= sources.size() || row->row_ >= row->ctx_->rowCount()) return;
        row->report_ = sources[index].empty() ? row->ctx_->unbind(row->row_) : row->ctx_->bind(row->row_, sources[index]);
        row->afterEdit();
      },
      open);
}

void PropertyRowView::onClick(Event& e) {
  if (e.button != core::events::Button::Right) return;
  e.markHandled();
  openRowMenu(e.x, e.y);
}

void PropertyRowView::openRowMenu(double x, double y) {
  if (row_ >= ctx_->rowCount()) return;
  const PropertyState state = ctx_->state(row_);
  const PropertyStrings& words = options_.strings;
  const UiHost& host = ui().host();
  const std::optional<std::string> copied = ctx_->copyValue(row_);
  const bool editable = state.enabled && !state.readOnly;
  std::vector<FlyoutItem> items;
  items.push_back({.label = words.copyValue, .enabled = copied.has_value() && static_cast<bool>(host.writeClipboard)});
  items.push_back({.label = words.pasteValue, .enabled = editable && static_cast<bool>(host.readClipboard)});
  items.push_back({.label = words.resetMenu, .enabled = state.canReset});
  items.push_back({.separator = true});
  items.push_back({.label = words.copyName, .enabled = static_cast<bool>(host.writeClipboard)});
  FlyoutOpenOptions open;
  open.anchor = {pixel(x), pixel(y), 0, 0};
  open.anchorWidget = id();
  open.placement = Placement::BelowStart;
  open.gap = 0.0;
  const WidgetId self = id();
  openFlyout(
      ui(), std::move(items),
      [u = &ui(), self, copied](size_t index) {
        auto* row = u->objectAs<PropertyRowView>(self);
        if (row == nullptr || row->row_ >= row->ctx_->rowCount()) return;
        // The clipboard belongs to the host: it may be missing, empty or throw; a failure changes nothing.
        try {
          if (index == 0 && copied) u->host().writeClipboard(*copied);
          if (index == 1) {
            const std::optional<std::string> text = u->host().readClipboard();
            if (text) row->report_ = row->ctx_->pasteValue(row->row_, *text);
          }
          if (index == 2) row->report_ = row->ctx_->reset(row->row_);
          if (index == 4) u->host().writeClipboard(row->ctx_->descriptor(row->row_).name);
        } catch (...) {
        }
        row->afterEdit();
      },
      open);
}

// ---- parts and description -----------------------------------------------------------------------------------------

NumberField* PropertyRowView::number(size_t axis) const { return axis < numbers_.size() ? ui().objectAs<NumberField>(numbers_[axis]) : nullptr; }
NumberField* PropertyRowView::alpha() const { return ui().objectAs<NumberField>(alpha_); }
PropSlider* PropertyRowView::slider() const { return ui().objectAs<PropSlider>(slider_); }
Checkbox* PropertyRowView::checkbox() const { return ui().objectAs<Checkbox>(check_); }
Switch* PropertyRowView::toggle() const { return ui().objectAs<Switch>(toggle_); }
Select* PropertyRowView::select() const { return ui().objectAs<Select>(select_); }
TextInput* PropertyRowView::text() const { return ui().objectAs<TextInput>(text_); }
ColorChip* PropertyRowView::chip() const { return ui().objectAs<ColorChip>(chip_); }
PropResetButton* PropertyRowView::resetButton() const { return ui().objectAs<PropResetButton>(reset_); }

std::string PropertyRowView::describe() const {
  if (row_ >= ctx_->rowCount()) return "(stale row)";
  const PropertyDescriptor& d = ctx_->descriptor(row_);
  const PropertyState state = ctx_->state(row_);
  std::string out = "\"" + d.shownLabel() + "\" " + props::kindName(d.kind);
  const auto flag = [](const WidgetObject* w, const char* name) { return w != nullptr && w->hasState(StateFlag::kMixed) ? std::string(" ") + name : std::string(); };
  for (size_t i = 0; i < numbers_.size(); ++i) {
    const NumberField* f = number(i);
    if (f == nullptr) continue;
    out += numbers_.size() > 1 ? std::string(" ") + kAxisNames[i] + "=" : " number=";
    out += f->hasState(StateFlag::kMixed) ? "Mixed" : f->displayText();
    if (!f->boundVariable().empty()) out += std::string(" bound:") + f->boundVariable() + (f->hasState(StateFlag::kInvalid) ? "(broken)" : "");
    if (!f->enabled()) out += " (disabled)";
  }
  if (const PropSlider* s = slider()) out += " slider=" + props::formatValue(Value(s->value())) + flag(s, "(mixed)");
  if (const Checkbox* c = checkbox()) out += std::string(" checkbox=") + (c->mixed() ? "mixed" : (c->checked() ? "on" : "off")) + (c->enabled() ? "" : " (disabled)");
  if (const Switch* t = toggle()) out += std::string(" switch=") + (t->mixed() ? "mixed" : (t->checked() ? "on" : "off")) + (t->enabled() ? "" : " (disabled)");
  if (const Select* s = select()) out += " select=" + (s->selectedIndex() ? std::string(s->selectedLabel()) : std::string("<none>")) + (s->enabled() ? "" : " (disabled)");
  if (const TextInput* t = text()) {
    out += (kind_ == ValueKind::Color ? " hex=\"" : " text=\"") + t->text() + "\"" + flag(t, "(mixed)") + (t->enabled() ? "" : " (disabled)");
    if (t->readOnly()) out += " (readonly)";
  }
  if (const ColorChip* c = chip()) out += " chip=" + color::formatHex(c->color()) + flag(c, "(mixed)");
  if (const NumberField* a = alpha()) out += std::string(" alpha=") + (a->hasState(StateFlag::kMixed) ? "Mixed" : a->displayText());
  if (const PropResetButton* r = resetButton()) out += r->shown() ? " [reset]" : "";
  if (state.binding == props::BindingState::Mixed) out += " [binding mixed]";
  return out;
}

}  // namespace r1ui::widgets

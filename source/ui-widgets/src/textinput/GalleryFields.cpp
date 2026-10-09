// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of GalleryFields.h.
// Invariants: only layout containers (GalleryBox), Labels and the three field widgets are created;
//   all colours come from the widgets' style rows.
// Callers: the gallery preview, gallery_test.
#include "r1ui/widgets/textinput/GalleryFields.h"

#include <string>
#include <utility>
#include <vector>

#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/numberfield/NumberField.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/select/Select.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace r1ui::widgets {

namespace {

namespace layout = core::layout;
using core::tree::WidgetId;

// A plain flex container for the gallery's grids and cells.
class GalleryBox final : public WidgetObject {
 public:
  const char* typeName() const override { return "GalleryBox"; }
};

GalleryBox& column(UiContext& ui, WidgetId parent, double gap) {
  GalleryBox& box = ui.create<GalleryBox>(parent);
  box.style().direction = layout::FlexDirection::Column;
  box.style().gapRow = gap;
  box.style().flexShrink = 0.0;  // the page may be taller than its window: never squeeze the rows
  return box;
}

GalleryBox& wrapRow(UiContext& ui, WidgetId parent, double gap) {
  GalleryBox& box = ui.create<GalleryBox>(parent);
  box.style().direction = layout::FlexDirection::Row;
  box.style().wrap = layout::FlexWrap::Wrap;
  box.style().flexShrink = 0.0;
  box.style().gapRow = gap;
  box.style().gapColumn = gap;
  return box;
}

void heading(UiContext& ui, WidgetId parent, const char* text) {
  Label& label = ui.create<Label>(parent, text, LabelRole::Title);
  label.style().margin[layout::kTop] = layout::Length::px(8);
}

// One labelled cell: a small caption above the widget.
WidgetId cell(UiContext& ui, WidgetId parent, const char* caption, double width) {
  GalleryBox& box = column(ui, parent, 4);
  box.style().width = layout::Length::px(width);
  ui.create<Label>(box.id(), caption, LabelRole::Caption);
  return box.id();
}

template <class T, class... Args>
T& field(UiContext& ui, WidgetId parent, const char* caption, double width, Args&&... args) {
  const WidgetId holder = cell(ui, parent, caption, width);
  T& widget = ui.create<T>(holder, std::forward<Args>(args)...);
  widget.style().width = layout::Length::px(width);
  return widget;
}

void textInputs(UiContext& ui, WidgetId page) {
  heading(ui, page, "Text input");
  for (const TextInputTone tone : {TextInputTone::Default, TextInputTone::Panel}) {
    for (const TextInputSize size : {TextInputSize::Md, TextInputSize::Sm}) {
      const std::string name = std::string(tone == TextInputTone::Default ? "default" : "panel") + (size == TextInputSize::Md ? " md" : " sm");
      GalleryBox& grid = wrapRow(ui, page, 12);
      const auto make = [&](const char* state) -> TextInput& {
        TextInput& input = field<TextInput>(ui, grid.id(), (name + " " + state).c_str(), 200, tone, size);
        return input;
      };
      make("idle").setPlaceholder("Placeholder");
      make("filled").setText("Filled value");
      TextInput& mixed = make("mixed");
      mixed.setMixed(true);
      TextInput& bound = make("bound");
      bound.setText("Variable value");
      bound.setBound(true);
      TextInput& invalid = make("invalid");
      invalid.setText("Invalid value");
      invalid.setInvalid(true);
      TextInput& disabled = make("disabled");
      disabled.setText("Disabled");
      disabled.setEnabled(false);
      TextInput& readOnly = make("read-only");
      readOnly.setText("Read only text");
      readOnly.setReadOnly(true);
      TextInput& search = make("clearable search");
      search.setText("btn");
      search.setClearable(true);
      search.setPlaceholder("Search");
      TextInput& limited = make("max length 8");
      limited.setMaxLength(8);
      limited.setPlaceholder("8 characters");
    }
  }
}

void numberFields(UiContext& ui, WidgetId page) {
  heading(ui, page, "Number field");
  GalleryBox& grid = wrapRow(ui, page, 12);
  NumberField& x = field<NumberField>(ui, grid.id(), "label", 114);
  x.setLabel("X");
  x.setValue(141);
  x.setRange(-10000, 10000);

  NumberField& w = field<NumberField>(ui, grid.id(), "label + buttons", 114);
  w.setLabel("W");
  w.setValue(300);
  w.setVariableButton(true);
  w.setDropdownButton(true);

  NumberField& opacity = field<NumberField>(ui, grid.id(), "glyph, suffix, scrub", 114);
  opacity.setLeadingIcon("blend");
  opacity.setSuffix("%");
  opacity.setValue(100);
  opacity.setRange(0, 100);
  opacity.setSoftRange(0, 100);
  opacity.setInteger(true);
  opacity.setVariableButton(true);

  NumberField& rotation = field<NumberField>(ui, grid.id(), "units (type 1.5rad)", 114);
  rotation.setLeadingIcon("rotate-cw");
  rotation.setSuffix("deg");
  rotation.setUnits({{"rad", 57.29577951308232}, {"deg", 1.0}});
  rotation.setValue(12.5);
  rotation.setRange(-360, 360);
  rotation.setSoftRange(-180, 180);

  NumberField& mixed = field<NumberField>(ui, grid.id(), "mixed (Mixed+5)", 114);
  mixed.setLabel("X");
  mixed.setMixed(true);

  NumberField& bound = field<NumberField>(ui, grid.id(), "bound variable", 114);
  bound.setLabel("W");
  bound.setValue(300);
  bound.setVariableButton(true);
  bound.setDropdownButton(true);
  bound.setBoundVariable("New number");

  NumberField& step = field<NumberField>(ui, grid.id(), "increment 0.25", 114);
  step.setLabel("Z");
  step.setIncrement(0.25);
  step.setRange(0, 10);
  step.setValue(2.5);
  step.setFractionDigits(0, 2);

  NumberField& disabled = field<NumberField>(ui, grid.id(), "disabled", 114);
  disabled.setLabel("H");
  disabled.setValue(220);
  disabled.setEnabled(false);
}

void selects(UiContext& ui, WidgetId page) {
  heading(ui, page, "Select");
  GalleryBox& grid = wrapRow(ui, page, 12);
  const std::vector<SelectEntry> blend = {
      {SelectEntryKind::Item, "Pass through", "pass", false}, {SelectEntryKind::Item, "Normal", "normal", false},   {SelectEntryKind::Item, "Darken", "darken", false},
      {SelectEntryKind::Item, "Multiply", "multiply", false}, {SelectEntryKind::Item, "Color burn", "burn", false}, {SelectEntryKind::Item, "Lighten", "lighten", false},
      {SelectEntryKind::Item, "Screen", "screen", false},     {SelectEntryKind::Item, "Overlay", "overlay", true},  {SelectEntryKind::Item, "Hue", "hue", false}};

  Select& value = field<Select>(ui, grid.id(), "value", 140);
  value.setEntries(blend);
  value.setSelectedValue("pass");

  Select& placeholder = field<Select>(ui, grid.id(), "placeholder", 140);
  placeholder.setEntries(blend);
  placeholder.setPlaceholder("Choose a mode");

  Select& grouped = field<Select>(ui, grid.id(), "grouped, compact", 140);
  grouped.setCompact(true);
  grouped.setEntries({{SelectEntryKind::Group, "Basic", "", false},
                      {SelectEntryKind::Item, "Pass through", "pass", false},
                      {SelectEntryKind::Item, "Normal", "normal", false},
                      {SelectEntryKind::Separator, "", "", false},
                      {SelectEntryKind::Group, "Darken", "", false},
                      {SelectEntryKind::Item, "Darken", "darken", false},
                      {SelectEntryKind::Item, "Multiply", "multiply", false},
                      {SelectEntryKind::Group, "Lighten", "", false},
                      {SelectEntryKind::Item, "Lighten", "lighten", false},
                      {SelectEntryKind::Item, "Screen", "screen", false}});
  grouped.setSelectedValue("multiply");

  Select& searchable = field<Select>(ui, grid.id(), "searchable (combobox)", 140);
  searchable.setSearchable(true);
  searchable.setEntries(blend);
  searchable.setSelectedValue("burn");

  std::vector<SelectEntry> many;
  for (int i = 1; i <= 60; ++i) many.push_back({SelectEntryKind::Item, "Option " + std::to_string(i), "", false});
  Select& scrolling = field<Select>(ui, grid.id(), "60 items, scrolls", 140);
  scrolling.setEntries(std::move(many));
  scrolling.setSelectedIndex(0);

  Select& disabled = field<Select>(ui, grid.id(), "disabled", 140);
  disabled.setEntries(blend);
  disabled.setSelectedValue("normal");
  disabled.setEnabled(false);
}

}  // namespace

void buildGalleryFields(UiContext& ui, WidgetId parent) {
  GalleryBox& page = column(ui, parent, 8);
  page.style().padding[layout::kLeft] = page.style().padding[layout::kRight] = 12.0;
  page.style().padding[layout::kTop] = page.style().padding[layout::kBottom] = 12.0;
  textInputs(ui, page.id());
  numberFields(ui, page.id());
  selects(ui, page.id());
}

}  // namespace r1ui::widgets

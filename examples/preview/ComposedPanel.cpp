// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the properties panel of the composed screen, built from the real widgets: ScrollArea around
//   PanelHeader, Segmented (Design | Code), a Name TextInput, and the sections Position, Layout,
//   Appearance, Fill, Stroke, Effects, Export (PropertySection, FieldGroup, FieldGrid) holding
//   NumberFields, Selects, IconButtons, a Checkbox, the paint field (swatch, hex TextInput, opacity
//   NumberField) and the eye toggle.
// Why: this replaces the Phase 3 hand-drawn panel; field edits write the AppDocument and repaint the
//   canvas, and programmatic refreshes (a canvas drag) write the document back into the fields
//   without callbacks.
// Callers: ComposedApp's constructor. Invariants: every field id lives in Ids; refreshPanel is the
//   only place that pushes the document into the fields.
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>

#include "ComposedApp.h"
#include "ComposedUtil.h"
#include "PreviewParts.h"
#include "r1ui/widgets/checkbox/Checkbox.h"
#include "r1ui/widgets/iconbutton/IconButton.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/numberfield/NumberField.h"
#include "r1ui/widgets/scroll/ScrollArea.h"
#include "r1ui/widgets/segmented/Segmented.h"
#include "r1ui/widgets/select/Select.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace preview {

namespace layout = r1ui::core::layout;
using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;

namespace {

struct NumberSpec {
  const char* label = nullptr;  // leading text such as "X"
  const char* icon = nullptr;   // leading glyph
  const char* suffix = nullptr;
  double min = -100000.0;
  double max = 100000.0;
  bool integer = false;
  bool variableButton = false;
  const char* tooltip = nullptr;
};

NumberField& makeNumber(UiContext& ui, WidgetId parent, const NumberSpec& spec) {
  NumberField& field = ui.create<NumberField>(parent);
  if (spec.label != nullptr) field.setLabel(spec.label);
  if (spec.icon != nullptr) field.setLeadingIcon(spec.icon);
  if (spec.suffix != nullptr) field.setSuffix(spec.suffix);
  field.setRange(spec.min, spec.max);
  field.setInteger(spec.integer);
  if (spec.variableButton) field.setVariableButton(true);
  if (spec.tooltip != nullptr) field.setTooltip(spec.tooltip);
  return field;
}

void setBlendEntries(Select& select) {
  for (const char* mode : {"Pass through", "Normal", "Darken", "Multiply", "Color burn", "Lighten", "Screen", "Overlay"}) select.addItem(mode, mode);
}

// The fields that mirror a double of the document, with the member they show.
struct Mirror {
  WidgetId ComposedApp::Ids::*field;
  double RectangleProps::*member;
};

constexpr std::array<Mirror, 8> kMirrors = {{{&ComposedApp::Ids::x, &RectangleProps::x},
                                             {&ComposedApp::Ids::y, &RectangleProps::y},
                                             {&ComposedApp::Ids::rotation, &RectangleProps::rotation},
                                             {&ComposedApp::Ids::width, &RectangleProps::width},
                                             {&ComposedApp::Ids::height, &RectangleProps::height},
                                             {&ComposedApp::Ids::opacity, &RectangleProps::opacity},
                                             {&ComposedApp::Ids::radius, &RectangleProps::radius},
                                             {&ComposedApp::Ids::smoothing, &RectangleProps::smoothing}}};

}  // namespace

// ---- panel --------------------------------------------------------------------------------------

void ComposedApp::buildPanel(WidgetId pane) {
  Surface& surface = ui_.create<Surface>(pane, "panel", Surface::kLeft);
  surface.style().direction = layout::FlexDirection::Column;
  build::grow(surface.style());
  ScrollArea& scroll = ui_.create<ScrollArea>(surface.id());
  build::grow(scroll.style());
  ids_.panelScroll = scroll.id();
  const WidgetId content = scroll.content();

  SectionBox& strip = build::flex(ui_, content, true);
  build::pad(strip.style(), 12, 8, 12, 8);
  strip.style().flexShrink = 0.0;
  Segmented& mode = ui_.create<Segmented>(strip.id(), SegmentedSize::Md);
  mode.setItems({{.text = "Design"}, {.text = "Code", .tooltip = "Code view"}});
  mode.setSelectedIndex(0);
  build::grow(mode.style());
  mode.setOnChange([this, id = mode.id()](int index) {
    if (index != 1) return;
    toast("Code view is not part of this preview");
    if (Segmented* s = ui_.objectAs<Segmented>(id)) s->setSelectedIndex(0);
  });

  PanelHeader& header = ui_.create<PanelHeader>(content, doc_.name, "square");
  ids_.panelHeader = header.id();
  header.addAction("shapes", "Create component", [this](ActionButton&) { toast("Component created"); });

  SectionBox& nameRow = build::flex(ui_, content, false);
  build::pad(nameRow.style(), 12, 8, 12, 12);
  nameRow.style().flexShrink = 0.0;
  FieldGroup& nameGroup = ui_.create<FieldGroup>(nameRow.id(), "Name");
  TextInput& name = ui_.create<TextInput>(nameGroup.control(), TextInputTone::Panel, TextInputSize::Md);
  name.setText(doc_.name);
  name.setMaxLength(64);
  name.setOnCommitted([this](std::string_view text) {
    if (text.empty()) {
      if (TextInput* input = ui_.objectAs<TextInput>(ids_.nameInput)) input->setText(doc_.name);
      return;
    }
    renameRectangle(std::string(text));
  });
  ids_.nameInput = name.id();

  buildPositionSection(content);
  buildLayoutSection(content);
  buildAppearanceSection(content);
  buildFillSection(content);
  buildEmptySections(content);
}

void ComposedApp::buildPositionSection(WidgetId panel) {
  PropertySection& section = ui_.create<PropertySection>(panel, "Position");
  const auto cluster = [&](WidgetId parent, std::array<std::pair<const char*, const char*>, 3> icons) {
    SectionBox& row = build::flex(ui_, parent, true, 2);
    for (const auto& [icon, tip] : icons) {
      IconButton& b = ui_.create<IconButton>(row.id(), icon, IconButtonSize::Md);
      b.setTooltip(tip);
      b.setOnClick([this, tip = std::string(tip)] { toast(tip); });
    }
  };
  SectionBox& aligns = build::flex(ui_, section.content(), true);
  aligns.style().justifyContent = layout::Justify::SpaceBetween;
  cluster(aligns.id(), {{{"align-left", "Align left"}, {"align-center", "Align horizontal centres"}, {"align-right", "Align right"}}});
  cluster(aligns.id(), {{{"align-start-horizontal", "Align top"}, {"align-center-horizontal", "Align vertical centres"}, {"align-end-horizontal", "Align bottom"}}});

  FieldGrid& grid = ui_.create<FieldGrid>(section.content());
  const FieldGrid::Row xy = grid.addRow(2);
  NumberField& x = makeNumber(ui_, xy.first, {.label = "X", .tooltip = "X position"});
  NumberField& y = makeNumber(ui_, xy.second, {.label = "Y", .tooltip = "Y position"});
  ids_.x = x.id();
  ids_.y = y.id();
  x.setOnValueChanged([this](double v, bool) { doc_.rect.x = v; rectangleEdited(); });
  y.setOnValueChanged([this](double v, bool) { doc_.rect.y = v; rectangleEdited(); });

  const FieldGrid::Row rot = grid.addRow(2);
  NumberField& rotation = makeNumber(ui_, rot.first, {.icon = "rotate-cw", .suffix = "\xC2\xB0", .min = -360.0, .max = 360.0, .tooltip = "Rotation"});
  ids_.rotation = rotation.id();
  rotation.setOnValueChanged([this](double v, bool) { doc_.rect.rotation = v; rectangleEdited(); });
  SectionBox& flips = build::flex(ui_, rot.second, true, 2);
  for (const auto& [icon, tip] : {std::pair{"flip-horizontal", "Flip horizontal"}, std::pair{"flip-vertical2", "Flip vertical"}}) {
    IconButton& b = ui_.create<IconButton>(flips.id(), icon, IconButtonSize::Md);
    b.setTooltip(tip);
    b.setOnClick([this, tip = std::string(tip)] { toast(tip); });
  }
}

void ComposedApp::buildLayoutSection(WidgetId panel) {
  PropertySection& section = ui_.create<PropertySection>(panel, "Layout");
  FieldGrid& grid = ui_.create<FieldGrid>(section.content());
  const FieldGrid::Row wh = grid.addRow(2);
  NumberField& w = makeNumber(ui_, wh.first, {.label = "W", .min = 1.0, .max = 100000.0, .variableButton = true, .tooltip = "Width"});
  NumberField& h = makeNumber(ui_, wh.second, {.label = "H", .min = 1.0, .max = 100000.0, .variableButton = true, .tooltip = "Height"});
  ids_.width = w.id();
  ids_.height = h.id();
  w.setOnValueChanged([this](double v, bool) { doc_.rect.width = v; rectangleEdited(); });
  h.setOnValueChanged([this](double v, bool) { doc_.rect.height = v; rectangleEdited(); });
  w.setOnVariableButton([this] { openVariablesDialog(); });
  h.setOnVariableButton([this] { openVariablesDialog(); });

  Checkbox& clip = ui_.create<Checkbox>(section.content(), "Clip content");
  clip.setOnChange([this](bool on) { doc_.rect.clipContent = on; rectangleEdited(); });
  ids_.clipContent = clip.id();
}

void ComposedApp::buildAppearanceSection(WidgetId panel) {
  PropertySection& section = ui_.create<PropertySection>(panel, "Appearance");
  section.addAction("eye", "Hide", [this](ActionButton& b) {
    b.setIcon(b.icon() == "eye" ? "eye-off" : "eye");
    toast(b.icon() == "eye" ? "Shown" : "Hidden");
  });
  FieldGrid& grid = ui_.create<FieldGrid>(section.content());
  const FieldGrid::Row top = grid.addRow(2);
  Select& blend = ui_.create<Select>(ui_.create<FieldGroup>(top.first, "Blend mode").control());
  setBlendEntries(blend);
  blend.setSelectedValue("Pass through");
  ids_.blendMode = blend.id();
  NumberField& opacity = makeNumber(ui_, ui_.create<FieldGroup>(top.second, "Opacity").control(),
                                    {.icon = "blend", .suffix = "%", .min = 0.0, .max = 100.0, .integer = true, .variableButton = true, .tooltip = "Opacity"});
  ids_.opacity = opacity.id();
  opacity.setOnValueChanged([this](double v, bool) { doc_.rect.opacity = v; rectangleEdited(); });

  const FieldGrid::Row radiusRow = grid.addRow(1, true);
  NumberField& radius = makeNumber(ui_, ui_.create<FieldGroup>(radiusRow.first, "Radius").control(),
                                   {.icon = "square-round-corner", .min = 0.0, .max = 10000.0, .variableButton = true, .tooltip = "Corner radius"});
  ids_.radius = radius.id();
  radius.setOnValueChanged([this](double v, bool) { doc_.rect.radius = v; rectangleEdited(); });
  IconButton& independent = ui_.create<IconButton>(radiusRow.rail, "square-round-corner", IconButtonSize::Md);
  independent.setTooltip("Independent corners");
  independent.setOnClick([this] { toast("Independent corners are not part of this preview"); });

  const FieldGrid::Row styleRow = grid.addRow(2);
  Select& cornerStyle = ui_.create<Select>(ui_.create<FieldGroup>(styleRow.first, "Corner style").control());
  cornerStyle.addItem("Round", "round");
  cornerStyle.addItem("Smooth", "smooth");
  cornerStyle.setSelectedValue("round");
  ids_.cornerStyle = cornerStyle.id();
  NumberField& smoothing = makeNumber(ui_, ui_.create<FieldGroup>(styleRow.second, "Corner smoothing").control(),
                                      {.icon = "squircle", .suffix = "%", .min = 0.0, .max = 100.0, .integer = true, .tooltip = "Corner smoothing"});
  ids_.smoothing = smoothing.id();
  smoothing.setOnValueChanged([this](double v, bool) { doc_.rect.smoothing = v; rectangleEdited(); });
}

void ComposedApp::buildFillSection(WidgetId panel) {
  PropertySection& section = ui_.create<PropertySection>(panel, "Fill");
  section.addAction("plus", "Add fill", [this](ActionButton&) { toast("Fill added"); });
  SectionBox& paint = build::flex(ui_, section.content(), true, 6);
  paint.style().alignItems = layout::Align::Center;

  ColorSwatch& swatch = ui_.create<ColorSwatch>(paint.id());
  swatch.setTooltip("Fill colour");
  swatch.setOnClick([this] { toggleFillPicker(); });
  ids_.fillSwatch = swatch.id();
  TextInput& hex = ui_.create<TextInput>(paint.id(), TextInputTone::Panel, TextInputSize::Md);
  build::grow(hex.style());
  hex.setMaxLength(9);
  hex.setTooltip("Hex colour");
  ids_.fillHex = hex.id();
  hex.setOnCommitted([this](std::string_view text) {
    const auto parsed = color::parseHex(text);
    if (parsed) {
      // Only a hex with an alpha pair (#rgba, #rrggbbaa) changes the fill's alpha.
      const size_t digits = text.size() - (!text.empty() && text.front() == '#' ? 1 : 0);
      doc_.rect.fill = {parsed->rgb, digits == 4 || digits == 8 ? parsed->a : doc_.rect.fill.a};
    }
    rectangleEdited();
    refreshPanel();
  });
  NumberField& alpha = makeNumber(ui_, paint.id(), {.suffix = "%", .min = 0.0, .max = 100.0, .integer = true, .tooltip = "Fill opacity"});
  build::fixed(alpha.style(), 64, 26);
  ids_.fillOpacity = alpha.id();
  alpha.setOnValueChanged([this](double v, bool) {
    doc_.rect.fill.a = v / 100.0;
    rectangleEdited();
    refreshPanel();
  });
  IconButton& eye = ui_.create<IconButton>(paint.id(), "eye", IconButtonSize::Md);
  eye.setTooltip("Hide fill");
  eye.setOnClick([this, id = eye.id()] {
    if (IconButton* b = ui_.objectAs<IconButton>(id)) {
      b->setIcon(b->icon() == "eye" ? "eye-off" : "eye");
      b->setTooltip(b->icon() == "eye" ? "Hide fill" : "Show fill");
    }
  });

  Select& fillBlend = ui_.create<Select>(ui_.create<FieldGroup>(section.content(), "Blend mode").control());
  for (const char* mode : {"Normal", "Multiply", "Screen", "Overlay"}) fillBlend.addItem(mode, mode);
  fillBlend.setSelectedValue("Normal");
}

void ComposedApp::buildEmptySections(WidgetId panel) {
  for (const char* title : {"Stroke", "Effects", "Export"}) {
    PropertySection& section = ui_.create<PropertySection>(panel, title, SectionOptions{.collapsible = true});
    section.addAction("plus", std::string("Add ") + title, [this, title = std::string(title)](ActionButton&) { toast(title + " added"); });
    ui_.create<Label>(section.content(), std::string("No ") + static_cast<char>(std::tolower(static_cast<unsigned char>(title[0]))) + (title + 1), LabelRole::Muted);
  }
}

// ---- document <-> fields ------------------------------------------------------------------------

void ComposedApp::rectangleEdited() {
  if (WidgetObject* canvas = ui_.object(ids_.canvas)) canvas->requestPaint();
}

void ComposedApp::renameRectangle(const std::string& name) {
  doc_.name = name;
  layerModel_->setLabel(kRectangleNode, name);
  if (TextInput* input = ui_.objectAs<TextInput>(ids_.nameInput)) {
    if (input->text() != name) input->setText(name);
  }
  if (PanelHeader* header = ui_.objectAs<PanelHeader>(ids_.panelHeader)) header->setTitle(name);

}

void ComposedApp::refreshPanel() {
  for (const Mirror& m : kMirrors) {
    if (NumberField* f = ui_.objectAs<NumberField>(ids_.*(m.field))) f->setValue(doc_.rect.*(m.member));
  }
  if (Checkbox* c = ui_.objectAs<Checkbox>(ids_.clipContent)) c->setChecked(doc_.rect.clipContent);
  if (ColorSwatch* s = ui_.objectAs<ColorSwatch>(ids_.fillSwatch)) s->setColor(doc_.rect.fill);
  if (TextInput* hex = ui_.objectAs<TextInput>(ids_.fillHex)) hex->setText(color::formatHex(doc_.rect.fill));
  if (NumberField* a = ui_.objectAs<NumberField>(ids_.fillOpacity)) a->setValue(std::round(doc_.rect.fill.a * 100.0));
}

}  // namespace preview

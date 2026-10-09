// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: construction of the properties panel resembling OpenPencil's panel for a selected
//   rectangle: the Design/Code strip with the Name field, the header, and the sections Position,
//   Layout, Appearance, Fill, Stroke, Effects and Export, built from the field kinds (number
//   field, select trigger, icon button, segmented control, paint field).
// Geometry: every row height, gap and offset comes from the measured reference screen
//   (tests/reference/openpencil/dark/screen-rectangle-selected.png, panel top at y = 41):
//   strip 40, header 43, Position 123, Layout 60, Appearance 184, Fill 126, collapsed sections 49;
//   content width 234 inside 12 px side padding; controls 26 px; gaps 6 px.
// Callers: SceneBuilder::build (SceneBuild.cpp).
#include <stdexcept>

#include "SceneBuilder.h"

namespace preview {

namespace layout = r1ui::core::layout;
namespace tree = r1ui::core::tree;

namespace {

constexpr double kControl = 26.0;
constexpr double kRowGap = 6.0;
constexpr double kSidePadding = 12.0;
constexpr double kLeadWidth = 173.0;  // paint field and fill blend select (two columns plus rail)

}  // namespace

SceneBuilder::WidgetId SceneBuilder::iconButton(WidgetId parent, const char* icon, bool small) {
  Node node;
  node.kind = NodeKind::IconButton;
  node.style = small ? "button.ghost.sm" : "button.ghost";
  node.icon = icon;
  node.iconPx = small ? 12 : 14;  // 14 px panel icons (space.panel-icon), 12 px inside fields
  const double side = small ? 20.0 : kControl;
  return add(parent, std::move(node), StyleBuilder().size(side, side).fixed().build(), true);
}

SceneBuilder::WidgetId SceneBuilder::iconCluster(WidgetId parent, std::initializer_list<const char*> icons) {
  const WidgetId cluster = add(parent, Node{}, StyleBuilder().row().center().gap(2).fixed().build());
  for (const char* icon : icons) iconButton(cluster, icon);
  return cluster;
}

SceneBuilder::WidgetId SceneBuilder::row(WidgetId parent, double marginTop) {
  return add(parent, Node{}, StyleBuilder().row().center().gap(kRowGap).height(kControl).marginTop(marginTop).fixed().build());
}

SceneBuilder::WidgetId SceneBuilder::pairRow(WidgetId parent, double marginTop, bool alignEnd) {
  StyleBuilder style;
  style.row().gap(kRowGap).marginTop(marginTop).fixed();
  if (alignEnd) style.alignEnd();
  return add(parent, Node{}, style.build());
}

SceneBuilder::WidgetId SceneBuilder::group(WidgetId parentRow, const char* label) {
  const WidgetId g = add(parentRow, Node{}, StyleBuilder().column().gap(4).grow().build());
  text(g, label, "text.label", StyleBuilder().fixed().build());
  return g;
}

// ---- Field kinds ----------------------------------------------------------------------------

SceneBuilder::WidgetId SceneBuilder::numberField(WidgetId parent, const NumberSpec& spec, bool inRow) {
  Node node;
  node.style = "field.panel";
  node.cursor = r1ui::platform::CursorShape::ResizeHorizontal;
  const bool trailing = spec.variableButton || spec.chevronButton;
  StyleBuilder style;
  style.row().center().gap(5).height(kControl).padding(6, 0, trailing ? 3 : 6, 0);
  if (inRow) style.grow(); else style.fixed();
  const WidgetId field = add(parent, std::move(node), style.build(), true);
  if (spec.glyphIcon != nullptr) {
    Node glyph;
    glyph.kind = NodeKind::Icon;
    glyph.textStyle = "text.muted";
    glyph.icon = spec.glyphIcon;
    glyph.iconPx = 14;
    add(field, std::move(glyph), StyleBuilder().size(14, 14).fixed().build());
  } else if (spec.glyphText != nullptr) {
    text(field, spec.glyphText, "text.muted", StyleBuilder().fixed().build());
  }
  text(field, spec.value, "text.value", StyleBuilder().grow().build());
  if (spec.suffix != nullptr) text(field, spec.suffix, "text.muted", StyleBuilder().marginRight(trailing ? 4 : 2).fixed().build());
  if (spec.variableButton) iconButton(field, "apply-variable", true);
  if (spec.chevronButton) iconButton(field, "chevron-down", true);
  return field;
}

SceneBuilder::WidgetId SceneBuilder::selectField(WidgetId parent, const char* value, bool inRow, double width) {
  Node node;
  node.style = "field.panel";
  StyleBuilder style;
  style.row().center().height(kControl).padding(7, 0, 6, 0);
  if (width > 0.0) style.width(width).fixed();
  else if (inRow) style.grow();
  else style.fixed();
  const WidgetId field = add(parent, std::move(node), style.build(), true);
  text(field, value, "text.value", StyleBuilder().grow().build());
  Node chevron;
  chevron.kind = NodeKind::Icon;
  chevron.textStyle = "text.muted";
  chevron.icon = "chevron-down";
  chevron.iconPx = 14;
  add(field, std::move(chevron), StyleBuilder().size(14, 14).fixed().build());
  return field;
}

SceneBuilder::WidgetId SceneBuilder::segmented(WidgetId parent, std::initializer_list<const char*> items) {
  Node node;
  node.style = "segmented";
  const WidgetId control = add(parent, std::move(node), StyleBuilder().row().center().gap(2).padding(2, 2, 2, 2).height(kControl).fixed().build());
  bool first = true;
  for (const char* label : items) {
    Node item;
    item.kind = NodeKind::Text;
    item.style = "segmented.item";
    item.text = label;
    item.selectable = true;
    item.state = first ? r1ui::theme::State::kSelected : r1ui::theme::State::kNone;
    first = false;
    Style st = StyleBuilder().padding(10, 0, 10, 0).height(22).fixed().build();
    st.hasMeasure = true;
    st.alignItems = layout::Align::Center;
    add(control, std::move(item), st, true);
  }
  return control;
}

SceneBuilder::WidgetId SceneBuilder::paintField(WidgetId parent) {
  Node node;
  node.style = "paint.field";
  const WidgetId field = add(parent, std::move(node), StyleBuilder().row().center().gap(6).height(28).width(kLeadWidth).padding(3, 0, 3, 0).fixed().build(), true);
  Node swatch;
  swatch.kind = NodeKind::Swatch;
  swatch.style = "swatch";
  swatch.swatchRgb = 0xD4D4D4;
  add(field, std::move(swatch), StyleBuilder().size(20, 20).fixed().build());
  text(field, "D4D4D4", "text.hex", StyleBuilder().grow().build());
  text(field, "100", "text.value", StyleBuilder().fixed().build());
  text(field, "%", "text.muted", StyleBuilder().fixed().build());
  iconButton(field, "apply-variable", true);
  return field;
}

// ---- Sections -------------------------------------------------------------------------------

SceneBuilder::WidgetId SceneBuilder::section(WidgetId parent, const char* title, bool borderTop, double bottomPadding,
                                             const char* trailingIcon) {
  // A header with a trailing button is a full 26 px row below 8 px of padding; a plain title is a
  // compact 25 px row (both measured on the reference screen).
  const bool tall = trailingIcon != nullptr;
  Node node;
  node.borderTop = borderTop;
  const double top = (borderTop ? 1.0 : 0.0) + (tall ? 8.0 : 0.0);
  const WidgetId sec = add(parent, std::move(node),
                           StyleBuilder().column().padding(kSidePadding, top, kSidePadding, bottomPadding).fixed().build());
  const WidgetId titleRow = add(sec, Node{}, StyleBuilder().row().center().justify(layout::Justify::SpaceBetween).height(tall ? 26.0 : 25.0).fixed().build());
  text(titleRow, title, "panel.section", StyleBuilder().fixed().build());
  if (trailingIcon != nullptr) iconButton(titleRow, trailingIcon);
  return sec;
}

SceneBuilder::WidgetId SceneBuilder::collapsedSection(WidgetId parent, const char* title) {
  Node node;
  node.borderTop = true;
  const WidgetId sec = add(parent, std::move(node),
                           StyleBuilder().row().center().justify(layout::Justify::SpaceBetween).padding(kSidePadding, 1, kSidePadding, 5).height(49).fixed().build());
  text(sec, title, "panel.section", StyleBuilder().fixed().build());
  iconButton(sec, "plus");
  return sec;
}

void SceneBuilder::buildStrip(WidgetId parent) {
  Node node;
  node.borderBottom = true;
  const WidgetId strip = add(parent, std::move(node), StyleBuilder().row().center().gap(kRowGap).padding(kSidePadding, 0, kSidePadding, 1).height(40).fixed().build());
  segmented(strip, {"Design", "Code"});
  Node field;
  field.kind = NodeKind::TextField;
  field.style = "field.panel";
  field.textStyle = "text.value";
  field.text = "Name";
  field.cursor = r1ui::platform::CursorShape::Text;
  s_.ids_.nameField = add(strip, std::move(field), StyleBuilder().height(kControl).grow().build(), true);
}

void SceneBuilder::buildHeader(WidgetId parent) {
  Node node;
  node.borderBottom = true;
  const WidgetId header = add(parent, std::move(node), StyleBuilder().row().center().padding(kSidePadding, 0, kSidePadding, 0).height(43).fixed().build());
  Node icon;
  icon.kind = NodeKind::Icon;
  icon.textStyle = "text.value";
  icon.icon = "square";
  icon.iconPx = 14;
  add(header, std::move(icon), StyleBuilder().size(14, 14).fixed().build());
  text(header, "Rectangle", "text.header", StyleBuilder().marginLeft(6).fixed().build());
  add(header, Node{}, StyleBuilder().grow().build());
  iconButton(header, "shapes");
}

void SceneBuilder::buildPosition(WidgetId parent) {
  const WidgetId sec = section(parent, "Position", false, 8);
  const WidgetId align = add(sec, Node{}, StyleBuilder().row().center().justify(layout::Justify::SpaceBetween).height(kControl).fixed().build());
  iconCluster(align, {"align-start-vertical", "align-center-vertical", "align-end-vertical"});
  iconCluster(align, {"align-start-horizontal", "align-center-horizontal", "align-end-horizontal"});
  const WidgetId xy = row(sec, kRowGap);
  numberField(xy, {.glyphText = "X", .value = "141"}, true);
  numberField(xy, {.glyphText = "Y", .value = "200"}, true);
  const WidgetId rotation = row(sec, kRowGap);
  numberField(rotation, {.glyphIcon = "rotate-cw", .value = "0", .suffix = "\xC2\xB0"}, true);
  iconCluster(rotation, {"flip-horizontal2", "flip-vertical2", "rotate-cw-square"});
}

void SceneBuilder::buildLayoutSection(WidgetId parent) {
  const WidgetId sec = section(parent, "Layout", true, 8);
  const WidgetId wh = row(sec, 0);
  numberField(wh, {.glyphText = "W", .value = "300", .variableButton = true, .chevronButton = true}, true);
  numberField(wh, {.glyphText = "H", .value = "220", .variableButton = true, .chevronButton = true}, true);
}

void SceneBuilder::buildAppearance(WidgetId parent) {
  const WidgetId sec = section(parent, "Appearance", true, 8, "eye");
  const WidgetId first = pairRow(sec, 3);
  selectField(group(first, "Blend mode"), "Pass through", false);
  numberField(group(first, "Opacity"), {.glyphIcon = "blend", .value = "100", .suffix = "%", .variableButton = true}, false);
  const WidgetId radius = pairRow(sec, 3, true);
  numberField(group(radius, "Radius"), {.glyphIcon = "square-round-corner", .value = "0", .variableButton = true}, false);
  iconButton(radius, "square-round-corner");
  const WidgetId second = pairRow(sec, 3);
  selectField(group(second, "Corner style"), "Round", false);
  numberField(group(second, "Corner smoothing"), {.glyphIcon = "circle", .value = "0", .suffix = "%"}, false);
}

void SceneBuilder::buildFill(WidgetId parent) {
  const WidgetId sec = section(parent, "Fill", true, 10, "plus");
  const WidgetId paint = add(sec, Node{}, StyleBuilder().row().center().gap(kRowGap).height(28).marginTop(7).fixed().build());
  paintField(paint);
  iconButton(paint, "eye");
  const WidgetId blend = pairRow(sec, 2);
  const WidgetId g = add(blend, Node{}, StyleBuilder().column().gap(4).width(kLeadWidth).fixed().build());
  text(g, "Blend mode", "text.label", StyleBuilder().fixed().build());
  selectField(g, "Normal", false);
}

void SceneBuilder::buildPanel(WidgetId parent) {
  buildStrip(parent);
  buildHeader(parent);
  buildPosition(parent);
  buildLayoutSection(parent);
  buildAppearance(parent);
  buildFill(parent);
  collapsedSection(parent, "Stroke");
  collapsedSection(parent, "Effects");
  collapsedSection(parent, "Export");
}

}  // namespace preview

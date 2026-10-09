// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of GalleryButtons.h.
// Invariants: every instance sits in a cell (caption above the widget); cells wrap inside a row of
//   its section; nothing here holds state after the call (the widgets own their callbacks).
// Callers: gallery preview, gallery smoke test.
#include "r1ui/widgets/button/GalleryButtons.h"

#include <string>

#include "r1ui/widgets/button/Button.h"
#include "r1ui/widgets/checkbox/Checkbox.h"
#include "r1ui/widgets/iconbutton/IconButton.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/segmented/Segmented.h"
#include "r1ui/widgets/switch/Switch.h"

namespace r1ui::widgets {

namespace {

namespace layout = core::layout;
using core::tree::WidgetId;

// A plain layout container with no look of its own.
class GalleryBox final : public WidgetObject {
 public:
  const char* typeName() const override { return "GalleryBox"; }
  void onAttached() override { node().flags.hitTestTransparent = true; }
};

GalleryBox& column(UiContext& ui, WidgetId parent, double gap) {
  GalleryBox& box = ui.create<GalleryBox>(parent);
  box.style().direction = layout::FlexDirection::Column;
  box.style().gapRow = gap;
  box.style().alignItems = layout::Align::Start;
  box.style().flexShrink = 0.0;
  return box;
}

GalleryBox& wrapRow(UiContext& ui, WidgetId parent) {
  GalleryBox& box = ui.create<GalleryBox>(parent);
  box.style().wrap = layout::FlexWrap::Wrap;
  box.style().gapColumn = 16.0;
  box.style().gapRow = 12.0;
  box.style().alignItems = layout::Align::Start;
  box.style().alignSelf = layout::Align::Stretch;  // wraps at the page width
  box.style().flexShrink = 0.0;
  return box;
}

// Captions and headings keep their natural width (a Label would otherwise shrink inside its cell).
Label& makeCaption(UiContext& ui, WidgetId parent, const std::string& text, LabelRole role) {
  Label& label = ui.create<Label>(parent, text, role);
  label.style().flexShrink = 0.0;
  return label;
}

void heading(UiContext& ui, WidgetId parent, const std::string& text) { makeCaption(ui, parent, text, LabelRole::Title); }

// A caption above an instance: returns the cell so the caller adds the widget to it.
WidgetId cell(UiContext& ui, WidgetId row, const std::string& caption) {
  GalleryBox& c = column(ui, row, 4.0);
  makeCaption(ui, c.id(), caption, LabelRole::Caption);
  return c.id();
}

const char* toneName(ButtonTone t) {
  switch (t) {
    case ButtonTone::Ghost: return "ghost";
    case ButtonTone::Accent: return "accent";
    case ButtonTone::Panel: return "panel";
    case ButtonTone::PanelAccent: return "panelAccent";
    case ButtonTone::Neutral: return "neutral";
  }
  return "";
}

void buttons(UiContext& ui, WidgetId page) {
  heading(ui, page, "Button");
  for (const ButtonTone tone : {ButtonTone::Ghost, ButtonTone::Accent, ButtonTone::Panel, ButtonTone::PanelAccent, ButtonTone::Neutral}) {
    WidgetId row = wrapRow(ui, page).id();
    const std::string name = toneName(tone);
    ui.create<Button>(cell(ui, row, name + " sm"), "Label", tone, ButtonSize::Sm);
    ui.create<Button>(cell(ui, row, name + " md"), "Label", tone, ButtonSize::Md);
    Button& both = ui.create<Button>(cell(ui, row, name + " icon + text"), "Share", tone, ButtonSize::Sm);
    both.setIcon("share2");
    Button& trailing = ui.create<Button>(cell(ui, row, name + " + trailing"), "Add", tone, ButtonSize::Sm);
    trailing.setIcon("plus");
    trailing.setTrailingIcon("chevron-down");
    ui.create<Button>(cell(ui, row, name + " icon"), "", tone, ButtonSize::Icon).setIcon("plus");
    ui.create<Button>(cell(ui, row, name + " iconSm"), "", tone, ButtonSize::IconSm).setIcon("plus");
    ui.create<Button>(cell(ui, row, name + " disabled"), "Label", tone, ButtonSize::Sm).setEnabled(false);
  }
}

void iconButtons(UiContext& ui, WidgetId page) {
  heading(ui, page, "IconButton");
  WidgetId row = wrapRow(ui, page).id();
  ui.create<IconButton>(cell(ui, row, "sm 20"), "plus", IconButtonSize::Sm);
  ui.create<IconButton>(cell(ui, row, "md 26"), "flip-horizontal2", IconButtonSize::Md);
  IconButton& active = ui.create<IconButton>(cell(ui, row, "md active"), "flip-horizontal2", IconButtonSize::Md);
  active.setActive(true);
  ui.create<IconButton>(cell(ui, row, "sm disabled"), "plus", IconButtonSize::Sm).setEnabled(false);
  ui.create<IconButton>(cell(ui, row, "md disabled"), "flip-horizontal2", IconButtonSize::Md).setEnabled(false);
  IconButton& close = ui.create<IconButton>(cell(ui, row, "close 24"), "x", IconButtonSize::Md);
  close.setBoxSize(24);
  close.setIconSize(16);
  ui.create<IconButton>(cell(ui, row, "apply variable"), "apply-variable", IconButtonSize::Sm);
}

void checkboxes(UiContext& ui, WidgetId page) {
  heading(ui, page, "Checkbox");
  WidgetId row = wrapRow(ui, page).id();
  ui.create<Checkbox>(cell(ui, row, "unchecked"), "Clip content");
  ui.create<Checkbox>(cell(ui, row, "checked"), "Clip content").setChecked(true);
  ui.create<Checkbox>(cell(ui, row, "mixed"), "Clip content").setMixed(true);
  ui.create<Checkbox>(cell(ui, row, "disabled"), "Clip content").setEnabled(false);
  Checkbox& off = ui.create<Checkbox>(cell(ui, row, "disabled checked"), "Clip content");
  off.setChecked(true);
  off.setEnabled(false);
  ui.create<Checkbox>(cell(ui, row, "no label"));
}

void switches(UiContext& ui, WidgetId page) {
  heading(ui, page, "Switch");
  WidgetId row = wrapRow(ui, page).id();
  for (const SwitchSize size : {SwitchSize::Sm, SwitchSize::Md}) {
    const std::string n = size == SwitchSize::Sm ? "sm " : "md ";
    ui.create<Switch>(cell(ui, row, n + "off"), size);
    ui.create<Switch>(cell(ui, row, n + "on"), size).setChecked(true);
    ui.create<Switch>(cell(ui, row, n + "mixed"), size).setMixed(true);
    ui.create<Switch>(cell(ui, row, n + "disabled"), size).setEnabled(false);
    Switch& onOff = ui.create<Switch>(cell(ui, row, n + "on disabled"), size);
    onOff.setChecked(true);
    onOff.setEnabled(false);
  }
}

void segmented(UiContext& ui, WidgetId page) {
  heading(ui, page, "Segmented");
  WidgetId row = wrapRow(ui, page).id();
  Segmented& md = ui.create<Segmented>(cell(ui, row, "md text"), SegmentedSize::Md);
  md.setItems({{.text = "File"}, {.text = "Assets"}, {.text = "Layers"}});
  md.setSelectedIndex(0);
  md.style().width = layout::Length::px(240);
  Segmented& sm = ui.create<Segmented>(cell(ui, row, "sm text"), SegmentedSize::Sm);
  sm.setItems({{.text = "One"}, {.text = "Two"}, {.text = "Three"}});
  sm.setSelectedIndex(1);
  sm.style().width = layout::Length::px(200);
  Segmented& icons = ui.create<Segmented>(cell(ui, row, "icons"), SegmentedSize::Md);
  icons.setItems({{.icon = "align-left", .tooltip = "Left"}, {.icon = "align-center", .tooltip = "Centre"}, {.icon = "align-right", .tooltip = "Right"}});
  icons.setSelectedIndex(0);
  icons.style().width = layout::Length::px(120);
  Segmented& both = ui.create<Segmented>(cell(ui, row, "icon + text"), SegmentedSize::Md);
  both.setItems({{.text = "Design", .icon = "pen-tool"}, {.text = "Code", .icon = "code"}});
  both.setSelectedIndex(1);
  both.style().width = layout::Length::px(220);
  Segmented& partly = ui.create<Segmented>(cell(ui, row, "disabled item"), SegmentedSize::Md);
  partly.setItems({{.text = "On"}, {.text = "Off", .enabled = false}, {.text = "Auto"}});
  partly.setSelectedIndex(0);
  partly.style().width = layout::Length::px(220);
  Segmented& all = ui.create<Segmented>(cell(ui, row, "disabled"), SegmentedSize::Md);
  all.setItems({{.text = "A"}, {.text = "B"}});
  all.setSelectedIndex(0);
  all.style().width = layout::Length::px(120);
  all.setEnabled(false);
}

}  // namespace

void buildGalleryButtons(UiContext& ui, WidgetId parent) {
  GalleryBox& page = column(ui, parent, 12.0);
  page.style().padding[layout::kLeft] = page.style().padding[layout::kRight] = 12.0;
  page.style().padding[layout::kTop] = page.style().padding[layout::kBottom] = 12.0;
  buttons(ui, page.id());
  iconButtons(ui, page.id());
  checkboxes(ui, page.id());
  switches(ui, page.id());
  segmented(ui, page.id());
}

}  // namespace r1ui::widgets

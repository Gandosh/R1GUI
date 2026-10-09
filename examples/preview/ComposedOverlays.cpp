// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the transient surfaces of the composed screen: toasts, the colour picker popover opened
//   from the fill swatch, the Variables dialog (search, collection select, a small table of
//   values, Cancel and Done) and the canvas context menu.
// Why: popover, dialog (focus trap, Escape, default action), toast and context menu are the
//   widgets that live on the overlay layer; composing them with the panel's controls is where
//   focus restore, outside-press rules and stacking are exercised for real.
// Callers: ComposedApp (menus, buttons, canvas right click).
#include <string>
#include <vector>

#include "ComposedApp.h"
#include "ComposedUtil.h"
#include "PreviewParts.h"
#include "r1ui/widgets/colorpicker/ColorPicker.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/numberfield/NumberField.h"
#include "r1ui/widgets/select/Select.h"
#include "r1ui/widgets/switch/Switch.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace preview {

namespace layout = r1ui::core::layout;
using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;

void ComposedApp::toast(const std::string& text, ToastTone tone) {
  ToastSpec spec;
  spec.text = text;
  spec.tone = tone;
  toasts_->show(std::move(spec));
}

// ---- colour picker popover ----------------------------------------------------------------------

void ComposedApp::toggleFillPicker() {
  if (isPopoverOpen(ui_, fillPopover_)) {
    closePopover(ui_, fillPopover_);
    return;
  }
  PopoverOptions options;
  options.anchorWidget = ids_.fillSwatch;
  options.owner = ids_.fillSwatch;
  options.placement = Placement::LeftStart;
  options.gap = 8.0;
  options.padding = 8.0;
  options.width = ColorPicker::kContentWidth + 2.0 * options.padding + 2.0;
  fillPopover_ = openPopover(ui_, options);
  if (!fillPopover_.valid()) return;
  ColorPicker& picker = ui_.create<ColorPicker>(fillPopover_.host);
  picker.setChrome(false);
  picker.setColor(doc_.rect.fill);
  picker.setSwatches({{{0.83, 0.83, 0.83}, 1.0}, {{0.23, 0.51, 0.96}, 1.0}, {{0.94, 0.27, 0.27}, 1.0}, {{0.13, 0.77, 0.37}, 1.0}});
  picker.onChanged = [this](const ColorChange& change) {
    doc_.rect.fill = change.color;
    rectangleEdited();
    refreshPanel();
  };
  picker.onEyedropper = [this] { toast("The eyedropper is not part of this preview"); };
}

// ---- dialog -------------------------------------------------------------------------------------

void ComposedApp::openVariablesDialog() {
  if (variables_.valid() && isDialogOpen(ui_, variables_)) return;
  DialogSpec spec;
  spec.title = "Variables";
  spec.description = "Variables hold values that fields and styles can reference.";
  spec.actions = {{.id = "cancel", .label = "Cancel", .kind = DialogActionKind::Neutral, .isCancel = true},
                  {.id = "done", .label = "Done", .kind = DialogActionKind::Primary, .isDefault = true}};
  spec.width = 560.0;
  spec.headerDivider = true;
  spec.onResult = [this](const DialogResult& result) {
    variables_ = {};
    if (!result.dismissed && result.action == "done") toast("Variables saved");
  };
  variables_ = openDialog(ui_, std::move(spec));
  if (!variables_.valid()) return;

  SectionBox& body = build::flex(ui_, variables_.body, false, 12);
  TextInput& search = ui_.create<TextInput>(body.id(), TextInputTone::Default, TextInputSize::Md);
  search.setPlaceholder("Search variables");
  search.setClearable(true);
  Select& collection = ui_.create<Select>(body.id());
  for (const char* name : {"Spacing", "Colors", "Typography"}) collection.addItem(name, name);
  collection.setSelectedValue("Spacing");
  struct Row {
    const char* name;
    double value;
  };
  for (const Row& row : {Row{"spacing/small", 8.0}, Row{"spacing/medium", 16.0}, Row{"radius/card", 12.0}}) {
    SectionBox& line = build::flex(ui_, body.id(), true, 12);
    line.style().alignItems = layout::Align::Center;
    Label& name = ui_.create<Label>(line.id(), row.name, LabelRole::Body);
    build::grow(name.style());
    NumberField& value = ui_.create<NumberField>(line.id());
    value.setRange(0.0, 1000.0);
    value.setValue(row.value);
    build::fixed(value.style(), 120, 26);
  }
  SectionBox& unused = build::flex(ui_, body.id(), true, 8);
  unused.style().alignItems = layout::Align::Center;
  Switch& showUnused = ui_.create<Switch>(unused.id(), SwitchSize::Sm);
  showUnused.setAccessibleName("Show unused variables");
  ui_.create<Label>(unused.id(), "Show unused variables", LabelRole::Muted);
  ui_.focusWidget(search.id());
}

// ---- context menu -------------------------------------------------------------------------------

void ComposedApp::openContextMenuAt(double x, double y) {
  MenuSpec spec;
  spec.look.arrowGlyph = true;
  spec.minWidth = 200.0;
  spec.onCommand = [this](const MenuItemSpec& item) { commandRun(item.id); };
  std::vector<MenuItemSpec> arrange = {menuAction("arrange.front", "Bring to front", "Ctrl+]"), menuAction("arrange.forward", "Bring forward"),
                                       menuAction("arrange.backward", "Send backward"), menuAction("arrange.back", "Send to back", "Ctrl+[")};
  MenuItemSpec remove = menuAction("edit.delete", "Delete", "Del");
  remove.enabled = doc_.selected;
  spec.items = {menuAction("edit.copy", "Copy", "Ctrl+C"), menuAction("edit.paste", "Paste", "Ctrl+V"), menuAction("edit.duplicate", "Duplicate", "Ctrl+D"),
                menuSeparator(), menuSubmenu("Arrange", std::move(arrange)), menuSeparator(), menuAction("layer.lock", "Lock", "Ctrl+Shift+L"),
                menuAction("layer.hide", "Hide", "Ctrl+Shift+H"), menuSeparator(), remove};
  contextMenu_->openContextMenu(std::move(spec), x, y);
}

}  // namespace preview

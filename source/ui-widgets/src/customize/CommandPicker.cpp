// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CommandPicker.h.
// Invariants: the chosen command is recorded while the dialog is open (the palette is gone when the
//   result callback runs), the result callback runs once, and nothing is called after a cancel.
// Callers: the customize edit displays and the tool strip.
#include "r1ui/widgets/customize/CommandPicker.h"

#include <memory>

#include "r1ui/widgets/customize/CommandPalette.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

DialogHandle openCommandPicker(CustomizeController& controller, CommandPickerOptions options) {
  UiContext& ui = controller.ui();
  const auto chosen = std::make_shared<std::string>();
  DialogSpec spec;
  spec.title = options.title;
  spec.actions = {{"cancel", "Cancel", DialogActionKind::Neutral, false, true, true}, {"add", options.confirmLabel, DialogActionKind::Primary, true, false, true}};
  spec.width = 460.0;
  spec.height = 440.0;
  spec.onResult = [chosen, onChosen = std::move(options.onChosen)](const DialogResult& result) {
    if (result.action == "add" && !result.dismissed && !chosen->empty() && onChosen) onChosen(*chosen);
  };
  DialogHandle handle = openDialog(ui, std::move(spec));
  if (!handle.valid()) return handle;
  CommandPalette& palette = ui.create<CommandPalette>(handle.body, controller);
  palette.style().height = core::layout::Length::px(330.0);
  palette.style().flexShrink = 0.0;
  palette.list().setOnSelect([chosen](const std::string& id) { *chosen = id; });
  palette.setOnChoose([chosen, context = &ui, handle](const std::string& id) {
    *chosen = id;
    closeDialog(*context, handle, "add");
  });
  palette.focusSearch();
  return handle;
}

}  // namespace r1ui::widgets

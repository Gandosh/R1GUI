// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: openCommandPicker, the modal command-picker dialog: a CommandPalette in a dialog with Add and
//   Cancel.
// Why: decision D2: dragging from the palette is the main path, the picker is the keyboard and
//   accessibility path: the user selects a target position in an edit display with the keyboard,
//   opens the picker (Insert, or the context menu), chooses a command with the arrows and Enter (or
//   double-click, or Add) and the edit display inserts it at the selected position.
// Callers: MenuEditor, ToolbarEditStrip, FreeFormPanel, the tool strip. Calls: CommandPalette,
//   openDialog.
// Contract: onChosen runs once with the command id when the user confirms (Enter in the list or the
//   search field, double-click, the Add button); Escape, Cancel and the close button run nothing. The
//   search field has the keyboard focus when the dialog opens.
#pragma once

#include <functional>
#include <string>

#include "r1ui/widgets/customize/CustomizeController.h"
#include "r1ui/widgets/dialog/Dialog.h"

namespace r1ui::widgets {

struct CommandPickerOptions {
  std::string title = "Add a command";
  std::string confirmLabel = "Add";
  std::function<void(const std::string& commandId)> onChosen;
};

DialogHandle openCommandPicker(CustomizeController& controller, CommandPickerOptions options);

}  // namespace r1ui::widgets

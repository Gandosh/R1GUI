// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CommandsLayoutBox, a plain flex container, and the two helpers (column, row) the keybinding
//   editor and the command gallery use to build their layouts.
// Why: the editor's rows, header and popup content are only arrangements of other widgets; one
//   private container type keeps them out of the public widget set.
// Callers: KeybindingEditor.cpp, KeybindingCapture.cpp, GalleryCommands.cpp. Calls: UiContext.
#pragma once

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

class CommandsLayoutBox final : public WidgetObject {
 public:
  const char* typeName() const override { return "CommandsLayoutBox"; }
};

inline CommandsLayoutBox& layoutColumn(UiContext& ui, core::tree::WidgetId parent, double gap = 0.0) {
  CommandsLayoutBox& box = ui.create<CommandsLayoutBox>(parent);
  box.style().direction = core::layout::FlexDirection::Column;
  box.style().gapRow = gap;
  return box;
}

inline CommandsLayoutBox& layoutRow(UiContext& ui, core::tree::WidgetId parent, double gap = 0.0) {
  CommandsLayoutBox& box = ui.create<CommandsLayoutBox>(parent);
  box.style().direction = core::layout::FlexDirection::Row;
  box.style().alignItems = core::layout::Align::Center;
  box.style().gapColumn = gap;
  return box;
}

}  // namespace r1ui::widgets

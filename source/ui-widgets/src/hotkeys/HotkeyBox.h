// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: HotkeyBox, a plain flex container, and the row and column helpers the hotkey editor and its
//   gallery page use to arrange widgets.
// Why: the editor's header, columns, tab pages and button rows are only arrangements of other widgets;
//   one private container type keeps them out of the public widget set.
// Callers: HotkeyEditor.cpp, HotkeyAssign.cpp, GalleryHotkeys.cpp. Calls: UiContext.
#pragma once

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

class HotkeyBox final : public WidgetObject {
 public:
  const char* typeName() const override { return "HotkeyBox"; }
};

inline HotkeyBox& hotkeyColumn(UiContext& ui, core::tree::WidgetId parent, double gap = 0.0) {
  HotkeyBox& box = ui.create<HotkeyBox>(parent);
  box.style().direction = core::layout::FlexDirection::Column;
  box.style().gapRow = gap;
  box.style().alignItems = core::layout::Align::Stretch;
  box.style().minHeight = core::layout::Length::px(0);
  box.style().minWidth = core::layout::Length::px(0);
  return box;
}

inline HotkeyBox& hotkeyRow(UiContext& ui, core::tree::WidgetId parent, double gap = 0.0) {
  HotkeyBox& box = ui.create<HotkeyBox>(parent);
  box.style().direction = core::layout::FlexDirection::Row;
  box.style().alignItems = core::layout::Align::Center;
  box.style().gapColumn = gap;
  box.style().minWidth = core::layout::Length::px(0);
  return box;
}

}  // namespace r1ui::widgets

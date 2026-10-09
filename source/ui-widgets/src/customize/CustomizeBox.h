// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CustomizeBox, a plain flex container, and the column / row helpers the customize widgets use to
//   arrange their child widgets (headers, control rows, the gallery page).
// Why: the edit displays are arrangements of labels, segmented controls and buttons around custom
//   painted areas; one private container type keeps them out of the public widget set.
// Callers: ToolbarEditor, FreeFormPanel, CustomizeToolStrip, GalleryCustomize. Calls: UiContext.
#pragma once

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets::cust {

class CustomizeBox final : public WidgetObject {
 public:
  const char* typeName() const override { return "CustomizeBox"; }
};

inline CustomizeBox& column(UiContext& ui, core::tree::WidgetId parent, double gap = 0.0) {
  CustomizeBox& box = ui.create<CustomizeBox>(parent);
  box.style().direction = core::layout::FlexDirection::Column;
  box.style().gapRow = gap;
  return box;
}

inline CustomizeBox& row(UiContext& ui, core::tree::WidgetId parent, double gap = 0.0) {
  CustomizeBox& box = ui.create<CustomizeBox>(parent);
  box.style().direction = core::layout::FlexDirection::Row;
  box.style().alignItems = core::layout::Align::Center;
  box.style().gapColumn = gap;
  return box;
}

}  // namespace r1ui::widgets::cust

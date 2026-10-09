// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the small layout helpers the composed screen's builders share (a flex box, fixed and
//   growing sizes, padding).
// Why: the screen is built from about two hundred widgets; one-line helpers keep the builders about
//   what is composed, not about style fields.
// Callers: ComposedApp*.cpp, GalleryApp.cpp.
#pragma once

#include "r1ui/core/layout/Style.h"
#include "r1ui/core/tree/WidgetId.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/section/Section.h"

namespace preview::build {

namespace layout = r1ui::core::layout;

// A plain flex container (SectionBox) as the last child of `parent`.
inline r1ui::widgets::SectionBox& flex(r1ui::widgets::UiContext& ui, r1ui::core::tree::WidgetId parent, bool row, double gap = 0.0) {
  r1ui::widgets::SectionBox& box = ui.create<r1ui::widgets::SectionBox>(parent);
  box.style().direction = row ? layout::FlexDirection::Row : layout::FlexDirection::Column;
  box.style().gapRow = gap;
  box.style().gapColumn = gap;
  return box;
}

inline void fixed(layout::Style& s, double width, double height) {
  s.width = layout::Length::px(width);
  s.height = layout::Length::px(height);
  s.flexShrink = 0.0;
}

// flex: 1 1 0 with no minimum content size, so the widget shares the free space.
inline void grow(layout::Style& s) {
  s.flexGrow = 1.0;
  s.flexShrink = 1.0;
  s.flexBasis = layout::Length::px(0.0);
  s.minWidth = layout::Length::px(0.0);
  s.minHeight = layout::Length::px(0.0);
}

inline void pad(layout::Style& s, double left, double top, double right, double bottom) {
  s.padding[layout::kLeft] = left;
  s.padding[layout::kTop] = top;
  s.padding[layout::kRight] = right;
  s.padding[layout::kBottom] = bottom;
}

}  // namespace preview::build

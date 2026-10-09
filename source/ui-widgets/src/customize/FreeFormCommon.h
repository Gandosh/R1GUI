// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the small helpers the free-form panel files share: children of a widget, conversions between the
//   model's and the layout's rectangle types, rectangle tests and the drawing of a free-form button's
//   content (icon, then the label when the button is wide enough).
// Why: the live command button (FreeFormButton) and the edit-mode painting of the canvas draw the same
//   content, and the canvas files (display, input) use the same rectangle helpers.
// Callers: FreeFormPanel.cpp, FreeFormCanvas.cpp, FreeFormCanvasInput.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "CustomizeCommon.h"
#include "r1ui/commands/customize/Layout.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets::cust {

inline std::vector<core::tree::WidgetId> childrenOf(const UiContext& ui, core::tree::WidgetId parent) {
  std::vector<core::tree::WidgetId> out;
  for (core::tree::WidgetId c = ui.tree().firstChild(parent); c.valid(); c = ui.tree().nextSibling(c)) out.push_back(c);
  return out;
}

inline core::layout::RectD toD(const commands::customize::Rect& r) { return {r.x, r.y, r.w, r.h}; }
inline commands::customize::Rect toC(const core::layout::RectD& r) { return {r.x, r.y, r.w, r.h}; }
inline bool same(const core::layout::RectD& a, const core::layout::RectD& b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }

inline core::layout::RectD normalized(double x0, double y0, double x1, double y1) {
  return {std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0), std::abs(y1 - y0)};
}

inline bool intersects(const core::layout::RectD& a, const core::layout::RectD& b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// Draws a free-form button's content: the icon, then the label when the button is wide enough.
inline void paintContent(PaintContext& ctx, const core::layout::RectD& r, const std::string& icon, const std::string& label, const render::Color& tint) {
  const bool showLabel = !label.empty() && r.w >= 64.0;
  const double iconBox = showLabel ? 28.0 : r.w;
  drawIconSafe(ctx, icon, 16.0, ctx.toPhysical(r.x, r.y, iconBox, r.h), tint);
  if (showLabel) {
    const theme::TextStyle text = ctx.style("label.body").text;
    TextOptions o;
    o.padLeft = 28.0;
    o.padRight = 6.0;
    o.color = tint;
    ctx.drawText(label, text, ctx.toPhysical(r.x, r.y, r.w, r.h), o);
  }
}

}  // namespace r1ui::widgets::cust

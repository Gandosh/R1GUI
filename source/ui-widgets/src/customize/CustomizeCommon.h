// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the small helpers the customize widgets share: text width, drawing an icon that may not exist,
//   the drag grip, point-in-rectangle tests and the icon lookup of a command the edit displays present.
// Why: the edit displays are custom-painted rows and cells; they all draw the same glyphs the same
//   way, and a command's icon name is only checked for form by the registry (a missing file throws at
//   paint), so one place falls back to a neutral icon instead of letting a paint pass fail.
// Callers: MenuEditor, ToolbarEditStrip, FreeFormPanel and its canvas, CommandPalette.
#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

#include "r1ui/core/layout/Geometry.h"
#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets::cust {

template <class R>
inline bool inside(const R& r, double x, double y) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// Logical width of one line of text in `style`, rounded up (what the layout and the painter agree on).
inline double textWidth(UiContext& ui, std::string_view text, const theme::TextStyle& style) {
  const double scale = ui.scale();
  const float px = static_cast<float>(style.fontSize * scale);
  return std::ceil(static_cast<double>(ui.text().measure(text, px, style.weight)) / scale);
}

// Draws a named icon; an icon that cannot be loaded is replaced by "circle" instead of failing the frame.
inline void drawIconSafe(PaintContext& ctx, std::string_view name, double size, const render::Rect& box, const render::Color& tint) {
  try {
    ctx.drawIcon(name, size, box, tint);
  } catch (const std::exception&) {
    ctx.drawIcon("circle", size, box, tint);
  }
}

// The six-dot drag grip (two columns of three) centred in `box`.
inline void drawGrip(PaintContext& ctx, const core::layout::RectD& box, const render::Color& color) {
  const double dot = 2.0, gap = 2.0;
  const double w = 2 * dot + gap, h = 3 * dot + 2 * gap;
  const double x0 = box.x + (box.w - w) * 0.5, y0 = box.y + (box.h - h) * 0.5;
  for (int c = 0; c < 2; ++c) {
    for (int r = 0; r < 3; ++r) {
      ctx.painter().fillRoundedRect(ctx.toPhysical(x0 + c * (dot + gap), y0 + r * (dot + gap), dot, dot), render::CornerRadii::uniform(ctx.px(1.0)), color);
    }
  }
}

inline std::string commandIconOf(const CommandServices& services, const std::string& commandId) {
  const commands::CommandDef* def = services.registry.find(commandId);
  return def != nullptr && !def->icon.empty() ? def->icon : std::string("circle");
}

}  // namespace r1ui::widgets::cust

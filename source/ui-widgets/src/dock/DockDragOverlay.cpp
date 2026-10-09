// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: DockDragOverlay painting (see DockDragOverlay.h): translucent tint, outlines, the cross
//   with diagonals, edge bars, ghost with title.
// Invariants: paints nothing when the visual is empty; never changes the tree; colours come from the
//   `dock.drop` and `dock.ghost` style rows (accent and panel), only alpha is applied here.
// Callers: UiContext paint traversal.
#include "r1ui/widgets/dock/DockDragOverlay.h"

#include <algorithm>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace layout = core::layout;
namespace State = theme::State;
using theme::StyleProperty;

namespace {

constexpr theme::StyleRuleEntry kRows[] = {
    {"dock.drop", State::kNone, StyleProperty::Background, "color:accent"},
    {"dock.drop", State::kNone, StyleProperty::BorderColor, "color:accent"},
    {"dock.drop", State::kNone, StyleProperty::Radius, "radius:sm"},
    {"dock.ghost", State::kNone, StyleProperty::Background, "color:panel"},
    {"dock.ghost", State::kNone, StyleProperty::BorderColor, "color:border-strong"},
    {"dock.ghost", State::kNone, StyleProperty::Foreground, "color:surface"},
    {"dock.ghost", State::kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"dock.ghost", State::kNone, StyleProperty::LineHeight, "number:16"},
    {"dock.ghost", State::kNone, StyleProperty::Radius, "radius:lg"},
};

constexpr double kTintAlpha = 0.25;     // spec 02 rule 30: 25 percent alpha targets
constexpr double kGhostAlpha = 0.45;    // spec 02 rule 17

}  // namespace

std::span<const theme::StyleRuleEntry> DockDragOverlay::styleRows() { return kRows; }

void DockDragOverlay::onAttached() {
  layout::Style& s = style();
  s.position = layout::Position::Absolute;
  for (int e = 0; e < 4; ++e) s.inset[e] = layout::Length::px(0);
  node().flags.hitTestTransparent = true;
  node().layer = 100;
}

void DockDragOverlay::setVisual(DragVisual visual) {
  visual_ = std::move(visual);
  requestPaint();
}

void DockDragOverlay::paint(PaintContext& ctx) {
  render::Painter& painter = ctx.painter();
  const theme::ResolvedStyle& drop = ctx.resolve("dock.drop", 0);
  const render::Color accent = ctx.color(drop.background);
  const auto tint = [&](double alpha) {
    render::Color c = accent;
    c.a = static_cast<float>(alpha);
    return c;
  };
  const auto box = [&](const dock::Rect& r) { return ctx.toPhysical(r.x, r.y, r.w, r.h); };
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(drop.radius));

  for (size_t i = 0; i < visual_.edges.size(); ++i) {
    const bool hot = static_cast<int>(i) == visual_.hotEdge;
    painter.fillRect(box(visual_.edges[i]), hot ? accent : tint(kTintAlpha));
  }
  if (visual_.emptyTarget) {
    painter.fillRoundedRect(box(*visual_.emptyTarget), radii, tint(kTintAlpha));
    painter.border(box(*visual_.emptyTarget), radii, std::max(1.0f, ctx.px(1.5)), tint(0.9));
  }
  if (visual_.crossOuter) {
    const dock::Rect& o = *visual_.crossOuter;
    const dock::Rect& in = visual_.crossInner;
    const float w = ctx.hairline();
    painter.border(box(o), render::CornerRadii::uniform(0.0f), w, tint(0.5));
    painter.border(box(in), render::CornerRadii::uniform(0.0f), w, tint(0.5));
    const auto line = [&](double x0, double y0, double x1, double y1) {
      painter.line(ctx.px(x0), ctx.px(y0), ctx.px(x1), ctx.px(y1), w, tint(0.5));
    };
    line(o.x, o.y, in.x, in.y);
    line(o.right(), o.y, in.right(), in.y);
    line(o.x, o.bottom(), in.x, in.bottom());
    line(o.right(), o.bottom(), in.right(), in.bottom());
  }
  if (visual_.preview) {
    painter.fillRoundedRect(box(*visual_.preview), radii, tint(kTintAlpha));
    painter.border(box(*visual_.preview), radii, std::max(1.0f, ctx.px(1.5)), tint(0.9));
  }
  if (visual_.ghost) {
    const theme::ResolvedStyle& ghost = ctx.resolve("dock.ghost", 0);
    const render::Rect gb = box(*visual_.ghost);
    painter.pushOpacity(static_cast<float>(kGhostAlpha));
    painter.fillRoundedRect(gb, render::CornerRadii::uniform(ctx.px(ghost.radius)), ctx.color(ghost.background));
    painter.border(gb, render::CornerRadii::uniform(ctx.px(ghost.radius)), ctx.hairline(), ctx.color(ghost.border.color));
    if (!visual_.ghostTitle.empty()) {
      TextOptions o;
      o.padLeft = 10.0;
      o.padRight = 10.0;
      ctx.drawText(visual_.ghostTitle, ghost.text, ctx.toPhysical(visual_.ghost->x, visual_.ghost->y, visual_.ghost->w, 25.0), o);
    }
    painter.popOpacity();
  }
}

}  // namespace r1ui::widgets

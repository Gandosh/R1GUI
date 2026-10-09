// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: painting of DockTabStrip (strip background and border, tabs with icon, label, close button
//   or lock glyph, the active-region marker, overflow arrows and list button, focus ring), its cursor
//   and its tooltips.
// Invariants: paint() never changes the tab model; hover indices are validated against the tab list
//   (they are reset by every structural change); the dragged (lifted) tab is not drawn.
// Callers: UiContext (paint traversal), the tooltip manager.
#include <algorithm>
#include <cmath>

#include "r1ui/widgets/dock/DockTabStrip.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace State = theme::State;

namespace {
constexpr double kIconBelowWidth = 90.0;  // narrower tabs drop their icon to leave room for the label
constexpr double kMarkerHeight = 2.0;
}  // namespace

void DockTabStrip::paint(PaintContext& ctx) {
  const dock::Rect strip = toDockRect(ctx.rect());
  const Layout& L = layout();
  render::Painter& painter = ctx.painter();
  const theme::ResolvedStyle& bar = ctx.resolve("dock.strip", 0);
  painter.fillRect(ctx.box(), ctx.color(bar.background));
  painter.fillRect(ctx.toPhysical(strip.x, strip.y + strip.h - 1.0, strip.w, 1.0), ctx.color(bar.border.color));

  if (L.overflow) painter.pushClip(ctx.toPhysical(strip.x + L.viewportX, strip.y, L.viewportW, strip.h));
  const dock::PanelId globalActive = host_ != nullptr ? host_->activePanel() : 0;
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (isLifted(i)) continue;
    const DockTabInfo& t = tabs_[i];
    const bool front = i == active_;
    const bool hot = (hover_.part == Part::Tab || hover_.part == Part::Close) && hover_.index == i;
    const uint8_t bits = (hot ? State::kHover : State::kNone) | (front ? State::kSelected : State::kNone);
    const theme::ResolvedStyle& rs = ctx.resolve("dock.tab", bits);
    const double x = tabLeft(i);
    const render::Rect box = ctx.toPhysical(x, strip.y, L.tabW, strip.h - 1.0);
    if (rs.background.a > 0) painter.fillRect(box, ctx.color(rs.background));
    painter.fillRect(ctx.toPhysical(x + L.tabW - 1.0, strip.y, 1.0, strip.h - 1.0), ctx.color(rs.border.color));
    const render::Color text = ctx.color(rs.text.color);

    double left = kPadX;
    if (!t.icon.empty() && L.tabW >= kIconBelowWidth) {
      render::Color tint = text;
      tint.a *= 0.6f;
      ctx.drawIcon(t.icon, kIconSize, ctx.toPhysical(x + left, strip.y, kIconSize, strip.h - 1.0), tint);
      left += kIconSize + kGap;
    }
    // Hidden close buttons still occupy their space (spec 02 rule 12).
    const double right = 1.0 + 6.0 + kCloseSize + 2.0;
    TextOptions o;
    o.padLeft = left;
    o.padRight = right;
    o.color = text;
    ctx.drawText(t.title, rs.text, box, o);

    const dock::Rect c = closeRect(i);
    if (t.locked) {
      render::Color tint = text;
      tint.a *= 0.7f;
      ctx.drawIcon("lock", 10.0, ctx.toPhysical(c.x, c.y, c.w, c.h), tint);
    } else if (t.closable && (front || hot)) {
      const bool overClose = hover_.part == Part::Close && hover_.index == i;
      const theme::ResolvedStyle& cs = ctx.resolve("dock.close", overClose ? State::kHover : State::kNone);
      const render::Rect closeBox = ctx.toPhysical(c.x, c.y, c.w, c.h);
      if (cs.background.a > 0) painter.fillRoundedRect(closeBox, render::CornerRadii::uniform(ctx.px(cs.radius)), ctx.color(cs.background));
      ctx.drawIcon("x", 12.0, closeBox, text);
    }
    if (front && t.panel == globalActive && globalActive != 0) {
      painter.fillRect(ctx.toPhysical(x, strip.y, L.tabW - 1.0, kMarkerHeight), ctx.color(ctx.resolve("dock.marker", 0).background));
    }
  }
  if (L.overflow) painter.popClip();

  if (L.overflow) {
    const auto button = [&](Part part, const dock::Rect& r, const char* icon, bool enabled) {
      const bool hot = hover_.part == part && enabled;
      const theme::ResolvedStyle& bs = ctx.resolve("dock.button", (hot ? State::kHover : State::kNone) | (enabled ? State::kNone : State::kDisabled));
      ctx.drawIcon(icon, 14.0, ctx.toPhysical(r.x, r.y, r.w, r.h - 1.0), ctx.color(bs.text.color));
    };
    button(Part::Left, leftArrowRect(), "chevron-left", scroll_ > 0.0);
    button(Part::Right, rightArrowRect(), "chevron-right", scroll_ < L.contentW - L.viewportW - 0.5);
    button(Part::List, listButtonRect(), "chevron-down", true);
  }
}

void DockTabStrip::paintOver(PaintContext& ctx) {
  if (!focusVisible() || active_ >= tabs_.size() || isLifted(active_)) return;
  const dock::Rect t = tabRect(active_);
  ctx.focusRing(ctx.toPhysical(t.x, t.y, t.w, t.h), 0.0f);
}

Cursor DockTabStrip::cursor() const {
  if (drag_.active) return Cursor::Move;
  return hover_.part == Part::None || hover_.part == Part::Empty ? Cursor::Default : Cursor::Pointer;
}

std::string_view DockTabStrip::tooltipText() const {
  if (drag_.active) return {};
  switch (hover_.part) {
    case Part::Tab:
      if (hover_.index >= tabs_.size()) return {};
      tooltipScratch_ = tabs_[hover_.index].title;
      if (tabs_[hover_.index].locked) tooltipScratch_ += " (locked)";
      return tooltipScratch_;
    case Part::Close:
      if (hover_.index >= tabs_.size()) return {};
      tooltipScratch_ = "Close " + tabs_[hover_.index].title;
      return tooltipScratch_;
    case Part::Left: return "Scroll tabs left";
    case Part::Right: return "Scroll tabs right";
    case Part::List: return "All tabs";
    default: return {};
  }
}

}  // namespace r1ui::widgets

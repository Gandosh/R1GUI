// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: painting of TabBar (bar, tabs with icon / label / close button, the new-tab and overflow
//   buttons, the live drag preview, the keyboard focus ring), the cursor and the tooltips.
// Invariants: paint() never changes the tab model; hover and drag indices are validated against the
//   tab list (they are reset by every structural change).
// Callers: UiContext (paint traversal), the tooltip manager.
#include <algorithm>
#include <cmath>

#include "TabBarMetrics.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace layout = core::layout;
namespace State = theme::State;

// ---- painting -----------------------------------------------------------------------------------

void TabBar::paint(PaintContext& ctx) {
  const Layout& L = layout();
  const layout::Rect bar = ctx.rect();
  const theme::ResolvedStyle& barStyle = ctx.resolve("tabbar.bar", 0);
  render::Painter& painter = ctx.painter();
  painter.fillRect(ctx.box(), ctx.color(barStyle.background));
  painter.fillRect(ctx.toPhysical(bar.x, bar.y + kHeight - 1.0, bar.w, 1.0), ctx.color(barStyle.border.color));

  // Positions of the tabs; while dragging the others make room for the dragged one.
  std::vector<double> xs = L.x;
  const size_t n = tabs_.size();
  if (drag_.active && n > 0) {
    std::vector<size_t> order;
    for (size_t i = 0; i < n; ++i) {
      if (i != drag_.index) order.push_back(i);
    }
    order.insert(order.begin() + static_cast<std::ptrdiff_t>(std::min(drag_.slot, order.size())), drag_.index);
    double cursor = L.x[0];
    for (const size_t i : order) {
      xs[i] = cursor;
      cursor += L.w[i];
    }
    const double lo = L.stripX;
    const double hi = L.stripX + std::max(L.overflow ? L.stripW : L.contentW, L.w[drag_.index]) - L.w[drag_.index];
    xs[drag_.index] = std::clamp(drag_.pointerX - bar.x - drag_.grab, lo, hi);
  }

  if (L.overflow) painter.pushClip(ctx.toPhysical(bar.x + L.stripX, bar.y, L.stripW, kHeight));
  const auto paintTab = [&](size_t i) {
    const TabInfo& t = tabs_[i];
    const bool activeTab = active_ && *active_ == t.id;
    const bool hot = (hover_.part == Part::Tab || hover_.part == Part::Close) && hover_.index == i;
    const uint8_t bits = (hot ? State::kHover : State::kNone) | (activeTab ? State::kSelected : State::kNone);
    const theme::ResolvedStyle& rs = ctx.resolve("tabbar.tab", bits);
    const double x = bar.x + xs[i];
    const render::Rect box = ctx.toPhysical(x, bar.y, L.w[i], kTabHeight);
    if (rs.background.a > 0) painter.fillRect(box, ctx.color(rs.background));
    painter.fillRect(ctx.toPhysical(x + L.w[i] - kBorder, bar.y, kBorder, kTabHeight), ctx.color(rs.border.color));
    const render::Color text = ctx.animatedColor(slotOf(t.id, 0), ctx.color(rs.text.color));
    const bool compact = L.w[i] < kCompactBelow;
    double left = kPadX;
    if (!t.icon.empty() && !compact) {
      render::Color iconTint = text;
      iconTint.a *= 0.5f;  // measured: the file icon is drawn at 50% opacity
      ctx.drawIcon(t.icon, kIconSize, ctx.toPhysical(x + left, bar.y, kIconSize, kTabHeight), iconTint);
      left += kIconSize + kGap;
    }
    const bool reserveClose = t.closable && (!compact || activeTab || hot);
    const double right = reserveClose ? kBorder + kPadX + kCloseSize + kGap : kPadX + kBorder;
    TextOptions o;
    o.padLeft = left;
    o.padRight = right - 1.0;  // the natural width is rounded to a pixel: never shorten a label that was measured to fit
    o.color = text;
    ctx.drawText(t.title, rs.text, box, o);
    if (t.closable) {
      const float visible = ctx.animatedValue(slotOf(t.id, 1), (activeTab || hot) ? 1.0f : 0.0f);
      if (visible > 0.001f) {
        const bool overClose = hover_.part == Part::Close && hover_.index == i;
        const theme::ResolvedStyle& cs = ctx.resolve("tabbar.close", overClose ? State::kHover : State::kNone);
        const render::Rect closeBox = ctx.toPhysical(x + L.w[i] - kBorder - kPadX - kCloseSize, bar.y + kCloseTop, kCloseSize, kCloseSize);
        if (cs.background.a > 0) {
          render::Color bg = ctx.color(cs.background);
          bg.a *= visible;
          painter.fillRoundedRect(closeBox, render::CornerRadii::uniform(ctx.px(cs.radius)), bg);
        }
        render::Color tint = text;
        tint.a *= visible;
        ctx.drawIcon("x", kCloseIcon, closeBox, tint);
      }
    }
  };
  for (size_t i = 0; i < n; ++i) {
    if (!(drag_.active && i == drag_.index)) paintTab(i);
  }
  if (drag_.active && drag_.index < n) paintTab(drag_.index);
  if (L.overflow) painter.popClip();

  const auto button = [&](Part part, double x, double w, const char* icon, bool enabled) {
    const bool hot = hover_.part == part && enabled;
    const theme::ResolvedStyle& bs = ctx.resolve("tabbar.button", (hot ? State::kHover : State::kNone) | (enabled ? State::kNone : State::kDisabled));
    const render::Color tint = ctx.animatedColor(slotOf(static_cast<TabId>(part) + 1000000, 0), ctx.color(bs.text.color));
    ctx.drawIcon(icon, kButtonIcon, ctx.toPhysical(bar.x + x, bar.y, w, kTabHeight), tint);
  };
  if (L.hasNew) button(Part::New, L.newX, kNewButtonWidth, "plus", true);
  if (L.overflow) {
    button(Part::Left, L.leftX, kOverflowButtonWidth, "chevron-left", scroll_ > 0.0);
    button(Part::Right, L.rightX, kOverflowButtonWidth, "chevron-right", scroll_ < L.contentW - L.stripW - 0.5);
    button(Part::List, L.listX, kOverflowButtonWidth, "chevron-down", true);
  }
}

void TabBar::paintOver(PaintContext& ctx) {
  if (!focusVisible() || !active_) return;
  const auto at = indexOf(*active_);
  if (!at) return;
  const Layout& L = layout();
  const layout::Rect bar = ctx.rect();
  ctx.focusRing(ctx.toPhysical(bar.x + L.x[*at], bar.y, L.w[*at], kTabHeight), 0.0f);
}

// ---- cursor, tooltips ---------------------------------------------------------------------------

Cursor TabBar::cursor() const {
  if (drag_.active) return Cursor::Move;
  return hover_.part == Part::None ? Cursor::Default : Cursor::Pointer;
}

std::string_view TabBar::fullTitle(size_t index) const {
  const TabInfo& t = tabs_[index];
  return t.tooltip.empty() ? std::string_view(t.title) : std::string_view(t.tooltip);
}

std::string_view TabBar::tooltipText() const {
  if (drag_.active) return {};
  switch (hover_.part) {
    case Part::Tab: return hover_.index < tabs_.size() ? fullTitle(hover_.index) : std::string_view();
    case Part::Close:
      if (hover_.index >= tabs_.size()) return {};
      tooltipScratch_ = "Close " + tabs_[hover_.index].title;
      return tooltipScratch_;
    case Part::New: return newTooltip_;
    case Part::Left: return "Scroll tabs left";
    case Part::Right: return "Scroll tabs right";
    case Part::List: return "All tabs";
    case Part::None: break;
  }
  return {};
}

}  // namespace r1ui::widgets

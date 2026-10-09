// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: FloatingFrame and FloatingHolder (see FloatingFrame.h): painting of the window chrome and
//   the title bar gestures (move, resize, maximize toggle, close request, activation).
// Invariants: pointer capture is held only while a move or resize gesture runs and is released on
//   every exit; the gesture computes a wanted rectangle from the rectangle at its start, so repeated
//   moves never accumulate rounding; minimum sizes keep the opposite edge fixed.
// Callers: UiContext (events, paint), InWindowFloatingBackend.
#include "FloatingFrame.h"
#include "r1ui/widgets/dock/DockInteraction.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace layout = core::layout;
namespace State = theme::State;
using core::events::Button;
using core::events::Phase;
using theme::StyleProperty;

namespace {

constexpr theme::StyleRuleEntry kRows[] = {
    {"dock.float", State::kNone, StyleProperty::Background, "color:panel"},
    {"dock.float", State::kNone, StyleProperty::BorderColor, "color:border-strong"},
    {"dock.float.title", State::kNone, StyleProperty::Background, "color:panel-secondary"},
    {"dock.float.title", State::kNone, StyleProperty::Foreground, "color:surface"},
    {"dock.float.title", State::kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"dock.float.title", State::kNone, StyleProperty::FontWeight, "weight:semibold"},
    {"dock.float.title", State::kNone, StyleProperty::LineHeight, "number:16"},
    {"dock.float.button", State::kNone, StyleProperty::Background, "transparent"},
    {"dock.float.button", State::kNone, StyleProperty::Foreground, "color:muted"},
    {"dock.float.button", State::kNone, StyleProperty::Radius, "radius:md"},
    {"dock.float.button", State::kHover, StyleProperty::Background, "color:hover"},
    {"dock.float.button", State::kHover, StyleProperty::Foreground, "color:surface"},
};

constexpr double kButton = 24.0;
constexpr double kButtonGap = 4.0;
constexpr double kButtonMargin = 8.0;

}  // namespace

void FloatingHolder::onAttached() {
  style().position = layout::Position::Absolute;
  style().overflow = layout::Overflow::Hidden;
}

std::span<const theme::StyleRuleEntry> FloatingFrame::styleRows() { return kRows; }

void FloatingFrame::onAttached() {
  style().position = layout::Position::Absolute;
  style().flexShrink = 0.0;
}

uint8_t FloatingFrame::phases() const { return core::events::kListenCapture | core::events::kListenTarget | core::events::kListenBubble; }

// ---- geometry ---------------------------------------------------------------------------------

dock::Rect FloatingFrame::titleRect() const {
  const dock::Rect r = toDockRect(ui().absRect(id()));
  return {r.x, r.y, r.w, titleHeight_};
}

dock::Rect FloatingFrame::closeButtonRect() const {
  const dock::Rect r = toDockRect(ui().absRect(id()));
  return {r.right() - kButtonMargin - kButton, r.y + (titleHeight_ - kButton) / 2.0, kButton, kButton};
}

dock::Rect FloatingFrame::maximizeButtonRect() const {
  const dock::Rect c = closeButtonRect();
  return {c.x - kButtonGap - kButton, c.y, kButton, kButton};
}

FloatingFrame::Hit FloatingFrame::hitTest(double x, double y) const {
  const dock::Rect r = toDockRect(ui().absRect(id()));
  if (!(x >= r.x && x < r.right() && y >= r.y && y < r.bottom())) return {};
  const auto inside = [&](const dock::Rect& b) { return x >= b.x && x < b.right() && y >= b.y && y < b.bottom(); };
  if (inside(closeButtonRect())) return {Part::Close, kNone};
  if (maximizable_ && inside(maximizeButtonRect())) return {Part::Maximize, kNone};
  if (resizable_ && !maximized_) {
    unsigned edges = kNone;
    if (x < r.x + band_) edges |= kLeft;
    if (x >= r.right() - band_) edges |= kRight;
    if (y < r.y + std::min(band_, 4.0)) edges |= kTop;  // a thin top edge keeps the title bar draggable
    if (y >= r.bottom() - band_) edges |= kBottom;
    if (edges != kNone) return {Part::Edge, edges};
  }
  if (y < r.y + titleHeight_) return {Part::Title, kNone};
  return {};
}

void FloatingFrame::setHover(Hit hit) {
  if (hit == hover_) return;
  hover_ = hit;
  requestPaint();
}

// ---- paint ------------------------------------------------------------------------------------

void FloatingFrame::paint(PaintContext& ctx) {
  render::Painter& painter = ctx.painter();
  const dock::Rect r = toDockRect(ctx.rect());
  const render::Rect box = ctx.box();
  const float radius = maximized_ ? 0.0f : ctx.px(radius_);
  const render::CornerRadii all = render::CornerRadii::uniform(radius);
  // Shadows are issued last to first so the first listed layer ends up on top.
  if (!maximized_) {
    if (const auto layers = ctx.ui().services().tokens().shadow("xl")) {
      for (auto it = layers->rbegin(); it != layers->rend(); ++it) {
        render::ShadowSpec spec;
        spec.offsetX = ctx.px(it->offsetX);
        spec.offsetY = ctx.px(it->offsetY);
        spec.blur = ctx.px(it->blur);
        spec.spread = ctx.px(it->spread);
        spec.color = ctx.color(it->color);
        painter.shadow(box, all, spec);
      }
    }
  }
  const theme::ResolvedStyle& body = ctx.resolve("dock.float", 0);
  painter.fillRoundedRect(box, all, ctx.color(body.background));
  const theme::ResolvedStyle& title = ctx.resolve("dock.float.title", 0);
  const render::CornerRadii top{radius, radius, 0.0f, 0.0f};
  painter.fillRoundedRect(ctx.toPhysical(r.x, r.y, r.w, titleHeight_), top, ctx.color(title.background));
  painter.fillRect(ctx.toPhysical(r.x, r.y + titleHeight_ - 1.0, r.w, 1.0), ctx.color(body.border.color));
  TextOptions o;
  o.padLeft = 12.0;
  o.padRight = kButtonMargin + 2.0 * kButton + kButtonGap + 8.0;
  ctx.drawText(title_, title.text, ctx.toPhysical(r.x, r.y, r.w, titleHeight_ - 1.0), o);

  const auto button = [&](Part part, const dock::Rect& b, const char* icon) {
    const bool hot = hover_.part == part;
    const theme::ResolvedStyle& bs = ctx.resolve("dock.float.button", hot ? State::kHover : State::kNone);
    const render::Rect bb = ctx.toPhysical(b.x, b.y, b.w, b.h);
    if (bs.background.a > 0) painter.fillRoundedRect(bb, render::CornerRadii::uniform(ctx.px(bs.radius)), ctx.color(bs.background));
    ctx.drawIcon(icon, 14.0, bb, ctx.color(bs.text.color));
  };
  if (maximizable_) button(Part::Maximize, maximizeButtonRect(), maximized_ ? "minimize" : "maximize");
  button(Part::Close, closeButtonRect(), "x");
  painter.border(box, all, ctx.hairline(), ctx.color(body.border.color));
}

// ---- input --------------------------------------------------------------------------------------

Cursor FloatingFrame::cursor() const {
  const Hit h = gesture_ ? pressed_ : hover_;
  if (h.part == Part::Edge) {
    const bool horizontal = (h.edges & (kLeft | kRight)) != 0;
    const bool vertical = (h.edges & (kTop | kBottom)) != 0;
    if (horizontal && vertical) return ((h.edges & kLeft) != 0) == ((h.edges & kTop) != 0) ? Cursor::ResizeNwSe : Cursor::ResizeNeSw;
    return horizontal ? Cursor::ResizeHorizontal : Cursor::ResizeVertical;
  }
  if (h.part == Part::Title && gesture_) return Cursor::Move;
  return Cursor::Default;
}

void FloatingFrame::onPointerDown(Event& e) {
  if (e.phase == Phase::Bubble) return;
  const bool onFrame = e.target == id();
  backend_->userPressed(window_);  // raise on any press, content included (spec 03 rule 40)
  if (e.button != Button::Left) return;
  // The resize band overlaps the content, so it is taken in the capture phase; the rest of the
  // chrome (title bar, buttons) is not covered by children and is handled on the frame itself.
  const Hit hit = hitTest(e.x, e.y);
  const bool edgeFromContent = e.phase == Phase::Capture && !onFrame && hit.part == Part::Edge;
  if (!edgeFromContent && !(e.phase == Phase::Target && onFrame)) return;
  pressed_ = hit;
  if (hit.part == Part::Close || hit.part == Part::Maximize) {
    e.markHandled();
    return;
  }
  if (hit.part != Part::Title && hit.part != Part::Edge) return;
  if (maximized_ && hit.part == Part::Title) return;
  const InWindowFloatingBackend::Window* w = backend_->find(window_);
  if (w == nullptr) return;
  gesture_ = true;
  startRect_ = w->content;
  startX_ = e.x;
  startY_ = e.y;
  ui().router().capturePointer(id());
  if (edgeFromContent) e.stopPropagation();
  e.markHandled();
}

void FloatingFrame::onPointerMove(Event& e) {
  if (!gesture_) {
    if (e.phase != Phase::Bubble) {
      const Hit hit = hitTest(e.x, e.y);
      setHover(e.target == id() || hit.part == Part::Edge ? hit : Hit{});
    }
    return;
  }
  if (e.phase == Phase::Capture) return;
  const InWindowFloatingBackend::Window* w = backend_->find(window_);
  if (w == nullptr) return;
  const double dx = e.x - startX_;
  const double dy = e.y - startY_;
  dock::Rect wanted = startRect_;
  if (pressed_.part == Part::Title) {
    wanted.x += dx;
    wanted.y += dy;
  } else {
    if ((pressed_.edges & kLeft) != 0) {
      wanted.w = std::max(w->minSize.x, startRect_.w - dx);
      wanted.x = startRect_.x + startRect_.w - wanted.w;
    }
    if ((pressed_.edges & kRight) != 0) wanted.w = std::max(w->minSize.x, startRect_.w + dx);
    if ((pressed_.edges & kTop) != 0) {
      wanted.h = std::max(w->minSize.y, startRect_.h - dy);
      wanted.y = startRect_.y + startRect_.h - wanted.h;
    }
    if ((pressed_.edges & kBottom) != 0) wanted.h = std::max(w->minSize.y, startRect_.h + dy);
  }
  e.markHandled();
  backend_->userChangedRect(window_, wanted);
}

void FloatingFrame::onPointerUp(Event& e) {
  if (e.button != Button::Left || !gesture_) return;
  gesture_ = false;
  requestPaint();
}

void FloatingFrame::onCaptureLost(Event&) { gesture_ = false; }

void FloatingFrame::onPointerLeave(Event&) { setHover({}); }

void FloatingFrame::onClick(Event& e) {
  if (e.button != Button::Left || e.target != id()) return;
  const Hit hit = hitTest(e.x, e.y);
  if (hit.part != pressed_.part) return;
  if (hit.part == Part::Close) {
    e.markHandled();
    backend_->userRequestedClose(window_);
  } else if (hit.part == Part::Maximize) {
    e.markHandled();
    backend_->userToggledMaximize(window_);
  }
}

void FloatingFrame::onDoubleClick(Event& e) {
  if (e.button != Button::Left || e.target != id() || !maximizable_) return;
  if (hitTest(e.x, e.y).part == Part::Title) {
    e.markHandled();
    backend_->userToggledMaximize(window_);
  }
}

}  // namespace r1ui::widgets

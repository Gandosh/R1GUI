// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of ScrollArea.h.
// Invariants: the content margin always equals -offset; onLayout() is the only place that
//   derives viewport / content sizes and gutters from the laid-out rectangles, and it re-clamps the
//   offsets there, so a content that shrinks never leaves the area scrolled past its end.
// Callers: UiContext (layout callback, paint, events).
#include "r1ui/widgets/scroll/ScrollArea.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace layout = core::layout;
using core::events::Button;
using core::events::Key;
using core::events::Phase;

void ScrollContent::onAttached() {
  layout::Style& s = style();
  s.direction = layout::FlexDirection::Column;
  s.flexShrink = 0.0;
  node().flags.hitTestTransparent = true;
}

uint8_t ScrollArea::phases() const { return core::events::kListenCapture | core::events::kListenTarget | core::events::kListenBubble; }

void ScrollArea::onAttached() {
  layout::Style& s = style();
  s.direction = layout::FlexDirection::Column;
  s.overflow = layout::Overflow::Hidden;
  s.alignItems = layout::Align::Stretch;
  setWantsLayoutCallback(true);
  content_ = ui().create<ScrollContent>(id()).id();
}

void ScrollArea::onDetached() { setWantsLayoutCallback(false); }

// ---- geometry -----------------------------------------------------------------------------------

ScrollArea::Geometry ScrollArea::area() const {
  const layout::Rect r = ui().absRect(id());
  return {static_cast<double>(r.x), static_cast<double>(r.y), static_cast<double>(r.w), static_cast<double>(r.h)};
}

double ScrollArea::maxOffsetX() const { return std::max(0.0, contentW_ - viewportW_); }
double ScrollArea::maxOffsetY() const { return std::max(0.0, contentH_ - viewportH_); }

void ScrollArea::placeBars() {
  const Geometry g = area();
  const double gutter = ScrollBar::kGutter;
  const double vw = showV_ ? gutter : 0.0;
  const double hh = showH_ ? gutter : 0.0;
  vbar_.setTrack(g.x + g.w - gutter, g.y, gutter, g.h - hh);
  hbar_.setTrack(g.x, g.y + g.h - gutter, g.w - vw, gutter);
  vbar_.update(viewportH_, contentH_, offsetY_);
  hbar_.update(viewportW_, contentW_, offsetX_);
  if (!showV_) vbar_.setTrack(0, 0, 0, 0);
  if (!showH_) hbar_.setTrack(0, 0, 0, 0);
}

// ---- configuration ------------------------------------------------------------------------------

void ScrollArea::setContentMinWidth(double width) {
  if (!std::isfinite(width) || width < 0.0) return;
  minContentW_ = width;
  ui().tree().get(content_)->style.minWidth = layout::Length::px(width);
  ui().invalidator().requestLayout(content_);
}

void ScrollArea::setContentMinHeight(double height) {
  if (!std::isfinite(height) || height < 0.0) return;
  ui().tree().get(content_)->style.minHeight = layout::Length::px(height);
  ui().invalidator().requestLayout(content_);
}

void ScrollArea::setVerticalPolicy(ScrollbarPolicy policy) {
  vPolicy_ = policy;
  requestLayout();
}

void ScrollArea::setHorizontalPolicy(ScrollbarPolicy policy) {
  hPolicy_ = policy;
  requestLayout();
}

void ScrollArea::setScrollbarStyle(ScrollbarStyle style) {
  barStyle_ = style;
  requestLayout();
}

void ScrollArea::setWheelStep(double pixels) {
  if (std::isfinite(pixels) && pixels > 0.0) wheelStep_ = pixels;
}

// ---- scrolling ----------------------------------------------------------------------------------

void ScrollArea::applyOffset(double x, double y) {
  offsetX_ = x;
  offsetY_ = y;
  layout::Style& cs = ui().tree().get(content_)->style;
  cs.margin[layout::kLeft] = layout::Length::px(-x);
  cs.margin[layout::kTop] = layout::Length::px(-y);
  ui().invalidator().requestLayout(content_);
  requestPaint();
  // A copy is called: the handler may replace this callback (setOn...) while it runs.
  if (onScroll_) {
    const auto callback = onScroll_;
    callback(*this);
  }
}

bool ScrollArea::scrollTo(double x, double y) {
  if (!std::isfinite(x) || !std::isfinite(y)) return false;
  const double nx = horizontal() ? std::clamp(x, 0.0, maxOffsetX()) : 0.0;
  const double ny = vertical() ? std::clamp(y, 0.0, maxOffsetY()) : 0.0;
  if (nx == offsetX_ && ny == offsetY_) return false;
  applyOffset(nx, ny);
  return true;
}

bool ScrollArea::scrollBy(double dx, double dy) {
  if (!std::isfinite(dx) || !std::isfinite(dy)) return false;
  return scrollTo(offsetX_ + dx, offsetY_ + dy);
}

bool ScrollArea::scrollByLines(double lines) { return scrollBy(0.0, lines * lineStep_); }

bool ScrollArea::scrollByPages(double pages) { return scrollBy(0.0, pages * std::max(lineStep_, viewportH_ - lineStep_)); }

double ScrollArea::alignedOffset(double start, double length, double view, double current, ScrollAlign align, double margin) const {
  switch (align) {
    case ScrollAlign::Start: return start - margin;
    case ScrollAlign::End: return start + length - view + margin;
    case ScrollAlign::Center: return start + length * 0.5 - view * 0.5;
    case ScrollAlign::Nearest: break;
  }
  if (start - margin < current) return start - margin;
  if (start + length + margin > current + view) return std::min(start - margin, start + length + margin - view);
  return current;
}

bool ScrollArea::scrollRectIntoView(double x, double y, double w, double h, ScrollAlign align, double margin) {
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) || !std::isfinite(margin)) return false;
  const double ny = vertical() ? alignedOffset(y, std::max(0.0, h), viewportH_, offsetY_, align, margin) : offsetY_;
  const double nx = horizontal() ? alignedOffset(x, std::max(0.0, w), viewportW_, offsetX_, align, margin) : offsetX_;
  return scrollTo(nx, ny);
}

bool ScrollArea::scrollIntoView(core::tree::WidgetId child, ScrollAlign align, double margin) {
  if (!ui().alive(child) || child == content_) return false;
  bool inside = false;
  for (core::tree::WidgetId p = ui().tree().parent(child); p.valid(); p = ui().tree().parent(p)) {
    if (p == content_) {
      inside = true;
      break;
    }
  }
  if (!inside) return false;
  const layout::Rect c = ui().absRect(child);
  const layout::Rect base = ui().absRect(content_);
  return scrollRectIntoView(static_cast<double>(c.x) - base.x, static_cast<double>(c.y) - base.y, c.w, c.h, align, margin);
}

// ---- layout -------------------------------------------------------------------------------------

void ScrollArea::onLayout() {
  layout::Style& s = style();
  const layout::Rect a = ui().absRect(id());
  const layout::Rect c = ui().absRect(content_);
  const double fullW = a.w;
  const double fullH = a.h;
  const double gutter = ScrollBar::kGutter;
  const double wantW = horizontal() ? minContentW_ : 0.0;
  // Which bars are needed: the content must overflow the area minus the other bar's strip.
  const auto decide = [&](ScrollbarPolicy policy, bool axisOn, double content, double full, bool otherShown) {
    if (!axisOn || policy == ScrollbarPolicy::Never) return false;
    if (policy == ScrollbarPolicy::Always) return true;
    return content > full - (otherShown ? gutter : 0.0) + 1e-6;
  };
  const double contentH = static_cast<double>(c.h);
  bool v = false;
  bool h = false;
  for (int i = 0; i < 2; ++i) {
    h = decide(hPolicy_, horizontal(), wantW, fullW, v);
    v = decide(vPolicy_, vertical(), contentH, fullH, h);
  }
  const bool reserve = barStyle_ == ScrollbarStyle::Gutter;
  const double padR = v && reserve ? gutter : 0.0;
  const double padB = h && reserve ? gutter : 0.0;
  if (s.padding[layout::kRight] != padR || s.padding[layout::kBottom] != padB) {
    s.padding[layout::kRight] = padR;
    s.padding[layout::kBottom] = padB;
    showV_ = v;
    showH_ = h;
    requestLayout();  // the content width changed; the frame loop runs the next pass
    return;
  }
  const double oldVw = viewportW_, oldVh = viewportH_, oldCw = contentW_, oldCh = contentH_;
  const bool oldV = showV_, oldH = showH_;
  showV_ = v;
  showH_ = h;
  viewportW_ = std::max(0.0, fullW - padR - s.padding[layout::kLeft]);
  viewportH_ = std::max(0.0, fullH - padB - s.padding[layout::kTop]);
  contentW_ = horizontal() ? std::max(viewportW_, minContentW_) : viewportW_;
  contentH_ = vertical() ? contentH : viewportH_;
  const double nx = horizontal() ? std::clamp(offsetX_, 0.0, maxOffsetX()) : 0.0;
  const double ny = vertical() ? std::clamp(offsetY_, 0.0, maxOffsetY()) : 0.0;
  if (nx != offsetX_ || ny != offsetY_) applyOffset(nx, ny);
  // The bars change only with these numbers; an unchanged layout pass must not cost a repaint.
  if (oldV != v || oldH != h || oldVw != viewportW_ || oldVh != viewportH_ || oldCw != contentW_ || oldCh != contentH_) requestPaint();
}

// ---- painting -----------------------------------------------------------------------------------

void ScrollArea::paintOver(PaintContext& ctx) {
  placeBars();
  vbar_.paint(ctx);
  hbar_.paint(ctx);
}

// ---- pointer ------------------------------------------------------------------------------------

bool ScrollArea::barHit(double x, double y) const {
  if (barStyle_ == ScrollbarStyle::Overlay) return vbar_.thumb().contains(x, y) || hbar_.thumb().contains(x, y);
  return vbar_.inTrack(x, y) || hbar_.inTrack(x, y);
}

void ScrollArea::onPointerDown(Event& e) {
  if (e.phase == Phase::Bubble || e.button != Button::Left) return;
  placeBars();
  if (!barHit(e.x, e.y)) return;
  ScrollBar& bar = vbar_.inTrack(e.x, e.y) ? vbar_ : hbar_;
  const std::optional<double> moved = bar.pointerDown(e.x, e.y);
  ui().router().capturePointer(id());
  draggingBar_ = true;
  if (moved) {
    if (bar.axis() == ScrollAxis::Vertical) scrollTo(offsetX_, *moved);
    else scrollTo(*moved, offsetY_);
  }
  e.stopPropagation();
  e.markHandled();
}

void ScrollArea::onPointerMove(Event& e) {
  if (e.phase == Phase::Bubble) return;
  placeBars();
  if (draggingBar_) {
    if (const auto v = vbar_.pointerMove(e.x, e.y)) scrollTo(offsetX_, *v);
    if (const auto h = hbar_.pointerMove(e.x, e.y)) scrollTo(*h, offsetY_);
    e.markHandled();
    return;
  }
  const layout::Rect a = ui().absRect(id());
  const bool inside = layout::containsPoint(a, e.x, e.y);
  const bool changed = vbar_.setPointer(e.x, e.y, inside) | hbar_.setPointer(e.x, e.y, inside);
  if (changed) requestPaint();
}

void ScrollArea::onPointerUp(Event& e) {
  if (!draggingBar_ || e.button != Button::Left) return;
  draggingBar_ = false;
  vbar_.pointerUp();
  hbar_.pointerUp();
  requestPaint();
}

void ScrollArea::onCaptureLost(Event&) {
  draggingBar_ = false;
  vbar_.cancel();
  hbar_.cancel();
  requestPaint();
}

void ScrollArea::onStateChanged(uint16_t) {
  // A disabled area gets no capture-lost event; end a thumb drag here so re-enabling cannot resume it.
  if (!enabled() && draggingBar_) {
    draggingBar_ = false;
    vbar_.cancel();
    hbar_.cancel();
  }
}

void ScrollArea::onPointerLeave(Event&) {
  if (draggingBar_) return;
  if (vbar_.setPointer(0, 0, false) | hbar_.setPointer(0, 0, false)) requestPaint();
}

void ScrollArea::onPointerWheel(Event& e) {
  if (e.phase == Phase::Capture) return;
  double dx = -e.wheelX * wheelStep_;
  double dy = -e.wheelY * wheelStep_;
  if ((e.modifiers & core::events::Mod::kShift) != 0 && dx == 0.0) {
    dx = dy;
    dy = 0.0;
  }
  if (!horizontal()) dx = 0.0;
  if (!vertical()) dy = 0.0;
  if (dx == 0.0 && dy == 0.0) return;
  const double nx = std::clamp(offsetX_ + dx, 0.0, maxOffsetX());
  const double ny = std::clamp(offsetY_ + dy, 0.0, maxOffsetY());
  if (nx == offsetX_ && ny == offsetY_) return;  // at the end: let an enclosing area scroll
  scrollTo(nx, ny);
  e.stopPropagation();
  e.markHandled();
}

// ---- keyboard -----------------------------------------------------------------------------------

void ScrollArea::onKeyDown(Event& e) {
  if (e.modifiers & (core::events::Mod::kCtrl | core::events::Mod::kAlt | core::events::Mod::kMeta)) return;
  bool moved = false;
  bool known = true;
  switch (e.key) {
    case Key::Up: moved = scrollByLines(-1); break;
    case Key::Down: moved = scrollByLines(1); break;
    case Key::PageUp: moved = scrollByPages(-1); break;
    case Key::PageDown: moved = scrollByPages(1); break;
    case Key::Home: moved = scrollTo(offsetX_, 0.0); break;
    case Key::End: moved = scrollTo(offsetX_, maxOffsetY()); break;
    case Key::Left: moved = scrollBy(-lineStep_, 0.0); break;
    case Key::Right: moved = scrollBy(lineStep_, 0.0); break;
    default: known = false; break;
  }
  if (known && moved) e.markHandled();
}

}  // namespace r1ui::widgets

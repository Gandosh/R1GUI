// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CanvasView.h.
// Invariants: the model is only changed by a drag, and every change goes through onMoved_ so the
//   panel refreshes; the drag holds the pointer capture and ends on release, capture loss or when
//   the widget goes away.
// Callers: ComposedApp.
#include "CanvasView.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace preview {

namespace layout = r1ui::core::layout;
namespace render = r1ui::render;
namespace events = r1ui::core::events;
using r1ui::widgets::PaintContext;

namespace {

constexpr double kHandleSize = 8.0;

render::Color toColor(const r1ui::widgets::color::Rgba& c, double opacityPercent) {
  const auto to8 = [](double v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
  return render::Color::fromRgba8(to8(c.rgb.r), to8(c.rgb.g), to8(c.rgb.b), to8(c.a * std::clamp(opacityPercent, 0.0, 100.0) / 100.0));
}

}  // namespace

void CanvasView::onAttached() {
  style().flexGrow = 1.0;
  style().flexShrink = 1.0;
  style().flexBasis = layout::Length::px(0);
  style().overflow = layout::Overflow::Hidden;
}

layout::Rect CanvasView::rectangle() const {
  const layout::Rect& r = node().absRect;
  const auto at = [](double v) { return static_cast<int32_t>(std::lround(v)); };
  return {at(r.x + doc_.rect.x + kOriginX), at(r.y + doc_.rect.y + kOriginY), std::max(0, at(doc_.rect.width)), std::max(0, at(doc_.rect.height))};
}

r1ui::widgets::Cursor CanvasView::cursor() const {
  return dragging_ ? r1ui::widgets::Cursor::Move : r1ui::widgets::Cursor::Default;
}

void CanvasView::paint(PaintContext& ctx) {
  ctx.painter().fillRect(ctx.box(), ctx.color("canvas"));
  const render::Rect box = ctx.toPhysical(rectangle().x, rectangle().y, rectangle().w, rectangle().h);
  const float radius = ctx.px(std::clamp(doc_.rect.radius, 0.0, std::min(doc_.rect.width, doc_.rect.height) * 0.5));
  ctx.painter().fillRoundedRect(box, render::CornerRadii::uniform(radius), toColor(doc_.rect.fill, doc_.rect.opacity));
}

// The selection outline and the eight handles sit above the rectangle (and above nothing else).
void CanvasView::paintOver(PaintContext& ctx) {
  if (!doc_.selected) return;
  const layout::Rect r = rectangle();
  const render::Color accent = ctx.color("accent");
  ctx.painter().border(ctx.toPhysical(r.x, r.y, r.w, r.h), render::CornerRadii::uniform(0.0f), ctx.hairline(), accent);
  const double xs[3] = {static_cast<double>(r.x), r.x + r.w * 0.5, static_cast<double>(r.x + r.w)};
  const double ys[3] = {static_cast<double>(r.y), r.y + r.h * 0.5, static_cast<double>(r.y + r.h)};
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      if (row == 1 && col == 1) continue;
      const render::Rect handle = ctx.toPhysical(xs[col] - kHandleSize * 0.5, ys[row] - kHandleSize * 0.5, kHandleSize, kHandleSize);
      ctx.painter().fillRect(handle, ctx.color("surface"));
      ctx.painter().border(handle, render::CornerRadii::uniform(0.0f), ctx.hairline(), accent);
    }
  }
}

void CanvasView::onPointerDown(Event& e) {
  if (e.button != events::Button::Left) return;
  const layout::Rect r = rectangle();
  const bool inside = e.x >= r.x && e.y >= r.y && e.x < r.x + r.w && e.y < r.y + r.h;
  if (doc_.selected != inside) {
    doc_.selected = inside;
    requestPaint();
    if (onSelect_) onSelect_(inside);
  }
  if (!inside) return;
  e.markHandled();
  grabX_ = e.x - doc_.rect.x;
  grabY_ = e.y - doc_.rect.y;
  dragging_ = ui().router().capturePointer(id());
}

void CanvasView::onPointerMove(Event& e) {
  if (!dragging_) return;
  const double x = std::round(e.x - grabX_);
  const double y = std::round(e.y - grabY_);
  if (x == doc_.rect.x && y == doc_.rect.y) return;
  doc_.rect.x = x;
  doc_.rect.y = y;
  requestPaint();
  if (onMoved_) onMoved_();
}

void CanvasView::onPointerUp(Event& e) {
  dragging_ = false;
  if (e.button == events::Button::Right && onContext_) {
    e.markHandled();
    onContext_(e.x, e.y);
  }
}

void CanvasView::onCaptureLost(Event&) { dragging_ = false; }

}  // namespace preview

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: painting of the native window's frame and title bar, its hover state, the chrome description
//   handed to the platform (see NativeFrame.h).
// Invariants: the frame never changes geometry (the window's size decides); it fills the root, the
//   holder sits inside the border under the title bar; the maximized window draws no border and no
//   rounded corner of its own (the window manager squares the corners then; otherwise it rounds them).
// Callers: UiContext (paint, pointer events), NativeWindow.
#include "NativeFrame.h"

#include <algorithm>
#include <cmath>

#include "r1ui/platform/Dpi.h"
#include "r1ui/widgets/dock/DockInteraction.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets::native {

namespace layout = core::layout;
namespace State = theme::State;
using theme::StyleProperty;

namespace {

// Same look as the in-window floating frame: panel body, secondary title bar, hairline border.
constexpr theme::StyleRuleEntry kRows[] = {
    {"dock.native", State::kNone, StyleProperty::Background, "color:panel"},
    {"dock.native", State::kNone, StyleProperty::BorderColor, "color:border-strong"},
    {"dock.native.title", State::kNone, StyleProperty::Background, "color:panel-secondary"},
    {"dock.native.title", State::kNone, StyleProperty::Foreground, "color:surface"},
    {"dock.native.title", State::kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"dock.native.title", State::kNone, StyleProperty::FontWeight, "weight:semibold"},
    {"dock.native.title", State::kNone, StyleProperty::LineHeight, "number:16"},
    {"dock.native.button", State::kNone, StyleProperty::Background, "transparent"},
    {"dock.native.button", State::kNone, StyleProperty::Foreground, "color:muted"},
    {"dock.native.button", State::kNone, StyleProperty::Radius, "radius:md"},
    {"dock.native.button", State::kHover, StyleProperty::Background, "color:hover"},
    {"dock.native.button", State::kHover, StyleProperty::Foreground, "color:surface"},
};

constexpr double kButton = 24.0;
constexpr double kButtonGap = 4.0;
constexpr double kButtonMargin = 8.0;

platform::Rect physicalRect(const dock::Rect& r, float scale) {
  const auto px = [&](double v) { return platform::logicalToPhysical(static_cast<float>(v), scale); };
  const int x0 = px(r.x);
  const int y0 = px(r.y);
  return {x0, y0, px(r.right()) - x0, px(r.bottom()) - y0};
}

}  // namespace

void NativeHolder::onAttached() {
  style().position = layout::Position::Absolute;
  style().overflow = layout::Overflow::Hidden;
}

std::span<const theme::StyleRuleEntry> NativeFrame::styleRows() { return kRows; }

void NativeFrame::onAttached() {
  style().position = layout::Position::Absolute;
  style().flexShrink = 0.0;
  style().inset[layout::kLeft] = layout::Length::px(0);
  style().inset[layout::kTop] = layout::Length::px(0);
  style().inset[layout::kRight] = layout::Length::px(0);
  style().inset[layout::kBottom] = layout::Length::px(0);
}

void NativeFrame::setTitle(std::string title) {
  if (title == title_) return;
  title_ = std::move(title);
  requestPaint();
}

void NativeFrame::setMaximized(bool maximized) {
  if (maximized == maximized_) return;
  maximized_ = maximized;
  requestPaint();
}

void NativeFrame::setResizable(bool resizable) {
  if (resizable == resizable_) return;
  resizable_ = resizable;
  requestPaint();
}

// ---- geometry ---------------------------------------------------------------------------------

dock::Rect NativeFrame::closeRect(double clientWidth) const {
  return {clientWidth - kButtonMargin - kButton, (metrics_.titleHeight - kButton) / 2.0, kButton, kButton};
}

dock::Rect NativeFrame::maximizeRect(double clientWidth) const {
  const dock::Rect c = closeRect(clientWidth);
  return {c.x - kButtonGap - kButton, c.y, kButton, kButton};
}

NativeFrame::Part NativeFrame::partAt(double x, double y) const {
  const dock::Rect me = toDockRect(ui().absRect(id()));
  const dock::Point local{x - me.x, y - me.y};
  if (closeRect(me.w).contains(local)) return Part::Close;
  if (resizable_ && maximizeRect(me.w).contains(local)) return Part::Maximize;
  return Part::None;
}

void NativeFrame::setHover(Part part) {
  if (part == hover_) return;
  hover_ = part;
  requestPaint();
}

platform::ChromeLayout NativeFrame::chromeLayout(double clientWidthLogical, float scale) const {
  platform::ChromeLayout chrome;
  chrome.captionRects.push_back(physicalRect({0.0, 0.0, clientWidthLogical, metrics_.titleHeight}, scale));
  chrome.closeButton = physicalRect(closeRect(clientWidthLogical), scale);
  if (resizable_) chrome.maximizeButton = physicalRect(maximizeRect(clientWidthLogical), scale);
  return chrome;
}

// ---- paint ------------------------------------------------------------------------------------

void NativeFrame::paint(PaintContext& ctx) {
  render::Painter& painter = ctx.painter();
  const dock::Rect r = toDockRect(ctx.rect());
  const render::Rect box = ctx.box();
  const theme::ResolvedStyle& body = ctx.resolve("dock.native", 0);
  painter.fillRect(box, ctx.color(body.background));
  const theme::ResolvedStyle& title = ctx.resolve("dock.native.title", 0);
  painter.fillRect(ctx.toPhysical(r.x, r.y, r.w, metrics_.titleHeight), ctx.color(title.background));
  painter.fillRect(ctx.toPhysical(r.x, r.y + metrics_.titleHeight - 1.0, r.w, 1.0), ctx.color(body.border.color));
  TextOptions o;
  o.padLeft = 12.0;
  o.padRight = kButtonMargin + (resizable_ ? 2.0 : 1.0) * kButton + kButtonGap + 8.0;
  ctx.drawText(title_, title.text, ctx.toPhysical(r.x, r.y, r.w, metrics_.titleHeight - 1.0), o);

  const auto button = [&](Part part, const dock::Rect& local, const char* icon) {
    const theme::ResolvedStyle& bs = ctx.resolve("dock.native.button", hover_ == part ? State::kHover : State::kNone);
    const render::Rect bb = ctx.toPhysical(r.x + local.x, r.y + local.y, local.w, local.h);
    if (bs.background.a > 0) painter.fillRoundedRect(bb, render::CornerRadii::uniform(ctx.px(bs.radius)), ctx.color(bs.background));
    ctx.drawIcon(icon, 14.0, bb, ctx.color(bs.text.color));
  };
  if (resizable_) button(Part::Maximize, maximizeRect(r.w), maximized_ ? "minimize" : "maximize");
  button(Part::Close, closeRect(r.w), "x");
  if (!maximized_) painter.border(box, render::CornerRadii::uniform(0.0f), ctx.hairline(), ctx.color(body.border.color));
}

// ---- pointer ----------------------------------------------------------------------------------

void NativeFrame::onPointerMove(Event& e) {
  if (e.target == id()) setHover(partAt(e.x, e.y));
}

void NativeFrame::onPointerLeave(Event&) { setHover(Part::None); }

}  // namespace r1ui::widgets::native

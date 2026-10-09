// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of ScrollBar.h (thumb geometry, pointer interaction, painting).
// Invariants: every double that reaches the Painter is finite; geometry is derived only from the
//   sanitised viewport / content / offset triple, so hostile input cannot produce a thumb outside
//   the track.
// Callers: ScrollArea, TreeView.
#include "r1ui/widgets/scroll/ScrollBar.h"

#include <algorithm>
#include <cmath>

namespace r1ui::widgets {

namespace {

namespace State = theme::State;
using theme::StyleProperty;

constexpr theme::StyleRuleEntry kRows[] = {
    {"scroll.thumb", State::kNone, StyleProperty::Background, "color:border"},
    {"scroll.thumb", State::kNone, StyleProperty::Radius, "metric:scrollbarThin.radius"},
    {"scroll.thumb", State::kHover, StyleProperty::Background, "color:muted"},
    {"scroll.thumb", State::kActive, StyleProperty::Background, "color:muted"},
};

double finiteOrZero(double v) { return std::isfinite(v) ? std::max(0.0, v) : 0.0; }

}  // namespace

std::span<const theme::StyleRuleEntry> ScrollBar::styleRows() { return kRows; }

void ScrollBar::setTrack(double x, double y, double w, double h) {
  trackX_ = std::isfinite(x) ? x : 0.0;
  trackY_ = std::isfinite(y) ? y : 0.0;
  trackW_ = finiteOrZero(w);
  trackH_ = finiteOrZero(h);
}

double ScrollBar::update(double viewport, double content, double offset) {
  viewport_ = finiteOrZero(viewport);
  content_ = finiteOrZero(content);
  offset_ = std::clamp(std::isfinite(offset) ? offset : 0.0, 0.0, maxOffset());
  if (!scrollable()) dragging_ = false;
  return offset_;
}

double ScrollBar::thumbLength() const {
  const double length = trackLength();
  const double raw = length * (viewport_ / content_);
  return std::clamp(raw, std::min(kMinThumb, length), length);
}

ScrollBar::Box ScrollBar::thumb() const {
  if (!scrollable()) return {};
  const double length = trackLength();
  const double thumbLen = thumbLength();
  const double travel = length - thumbLen;
  const double max = maxOffset();
  const double start = max > 0.0 ? travel * (offset_ / max) : 0.0;
  const double cross = axis_ == ScrollAxis::Vertical ? trackW_ : trackH_;
  const double thickness = std::min(kThumbSize, cross);
  const double inset = (cross - thickness) * 0.5;
  if (axis_ == ScrollAxis::Vertical) return {trackX_ + inset, trackY_ + start, thickness, thumbLen};
  return {trackX_ + start, trackY_ + inset, thumbLen, thickness};
}

double ScrollBar::offsetForThumbStart(double start) const {
  const double travel = trackLength() - thumbLength();
  if (travel <= 0.0) return 0.0;
  return std::clamp(start / travel, 0.0, 1.0) * maxOffset();
}

std::optional<double> ScrollBar::pointerDown(double x, double y) {
  if (!scrollable() || !std::isfinite(x) || !std::isfinite(y) || !track().contains(x, y)) return std::nullopt;
  const Box t = thumb();
  const double along = axis_ == ScrollAxis::Vertical ? y : x;
  const double thumbStart = axis_ == ScrollAxis::Vertical ? t.y : t.x;
  const double thumbLen = axis_ == ScrollAxis::Vertical ? t.h : t.w;
  if (along >= thumbStart && along < thumbStart + thumbLen) {
    dragging_ = true;
    grab_ = along - thumbStart;
    return std::nullopt;  // pressing the thumb itself does not move it
  }
  // Track click: one page towards the click (the page keeps one line of overlap like a browser).
  const double page = std::max(viewport_ - 16.0, viewport_ * 0.5);
  const double target = std::clamp(along < thumbStart ? offset_ - page : offset_ + page, 0.0, maxOffset());
  if (target == offset_) return std::nullopt;
  offset_ = target;
  return offset_;
}

std::optional<double> ScrollBar::pointerMove(double x, double y) {
  if (!dragging_ || !std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
  const double along = axis_ == ScrollAxis::Vertical ? y : x;
  const double trackStart = axis_ == ScrollAxis::Vertical ? trackY_ : trackX_;
  const double target = offsetForThumbStart(along - grab_ - trackStart);
  if (target == offset_) return std::nullopt;
  offset_ = target;
  return offset_;
}

bool ScrollBar::setPointer(double x, double y, bool inside) {
  const bool over = inside && scrollable() && thumb().contains(x, y);
  if (over == hover_) return false;
  hover_ = over;
  return true;
}

void ScrollBar::paint(PaintContext& ctx) const {
  if (!scrollable()) return;
  const Box t = thumb();
  if (t.w <= 0 || t.h <= 0) return;
  const bool hot = dragging_ || hover_;
  const theme::ResolvedStyle& rest = ctx.resolve("scroll.thumb", State::kNone);
  const render::Rect box = ctx.toPhysical(t.x, t.y, t.w, t.h);
  render::Color fill = ctx.color(rest.background);
  if (hot) {
    // Measured: a hovered or dragged thin thumb is drawn halfway between its idle colour and `muted`
    // (#606060 in the dark panel, #a1a4aa in the light one).
    const render::Color target = ctx.color(ctx.resolve("scroll.thumb", dragging_ ? State::kActive : State::kHover).background);
    fill = {(fill.r + target.r) * 0.5f, (fill.g + target.g) * 0.5f, (fill.b + target.b) * 0.5f, 1.0f};
  }
  ctx.painter().fillRoundedRect(box, render::CornerRadii::uniform(ctx.px(rest.radius)), fill);
}

}  // namespace r1ui::widgets

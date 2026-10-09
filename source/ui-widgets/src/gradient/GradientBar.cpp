// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of GradientBar.h.
// Invariants: a gesture is one onBegin ... onEnd bracket (closed on release, Escape, capture loss);
//   the gradient copy always satisfies the model's invariants because every edit goes through
//   Gradient; callbacks may destroy the bar and nothing is touched afterwards.
// Callers: the gradient editor; tests.
#include "r1ui/widgets/gradient/GradientBar.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/colorpicker/PickerDraw.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

namespace events = core::events;

constexpr double kBarRadius = 4.0;
constexpr double kHandleRadius = 4.0;
constexpr double kHandleBorder = 2.0;
constexpr double kHandleSlop = 1.0;

render::Color toColor(const color::Rgba& c, float opacity = 1.0f) {
  return {static_cast<float>(c.rgb.r), static_cast<float>(c.rgb.g), static_cast<float>(c.rgb.b), static_cast<float>(c.a) * opacity};
}

}  // namespace

void GradientBar::onAttached() {
  core::layout::Style& s = style();
  s.height = core::layout::Length::px(kHeight);
  s.flexShrink = 0.0;
  setFocusable(true);
}

std::string_view GradientBar::accessibleName() const {
  return WidgetObject::accessibleName().empty() ? std::string_view("Gradient stops") : WidgetObject::accessibleName();
}

void GradientBar::setGradient(const gradient::Gradient& g) {
  if (g == gradient_) return;
  gradient_ = g;
  if (gradient_.indexOf(selected_) == gradient::Gradient::npos) selected_ = gradient_.stops().front().id;
  requestPaint();
}

void GradientBar::setSelectedId(uint32_t id) {
  if (gradient_.indexOf(id) == gradient::Gradient::npos || id == selected_) return;
  selected_ = id;
  requestPaint();
}

double GradientBar::trackWidth() const { return std::max(0.0, static_cast<double>(ui().absRect(id()).w) - 2.0 * kOverhang); }

double GradientBar::positionAt(double localX) const {
  const double w = trackWidth();
  return w > 0.0 ? color::clamp01((localX - kOverhang) / w) : 0.0;
}

double GradientBar::handleCentreX(uint32_t id) const {
  const gradient::Stop* s = gradient_.find(id);
  return s == nullptr ? 0.0 : kOverhang + s->position * trackWidth();
}

Cursor GradientBar::cursor() const {
  if (drag_.active) return Cursor::Move;
  if (!ui().pointerKnown()) return Cursor::Pointer;
  const core::layout::Rect r = ui().absRect(id());
  return handleAt(ui().pointerX() - r.x, ui().pointerY() - r.y) != 0 ? Cursor::Move : Cursor::Pointer;
}

// ---- painting ---------------------------------------------------------------------------------

void GradientBar::paint(PaintContext& ctx) {
  const render::Rect all = ctx.box();
  const render::Rect box{all.x + ctx.px(kOverhang), all.y, std::max(0.0f, all.w - 2.0f * ctx.px(kOverhang)), all.h};
  const float radius = ctx.px(kBarRadius);
  pickerdraw::drawCheckerboard(ctx, box, radius, 4.0, ctx.color("checkerboard"), ctx.color("checkerboard-muted"));
  pickerdraw::fillRoundedHorizontal(ctx.painter(), box, radius, [&](float t) { return toColor(gradient_.evaluate(static_cast<double>(t))); });
}

void GradientBar::paintOver(PaintContext& ctx) {
  const render::Rect all = ctx.box();
  const render::Rect box{all.x + ctx.px(kOverhang), all.y, std::max(0.0f, all.w - 2.0f * ctx.px(kOverhang)), all.h};
  const float size = ctx.px(kHandle);
  const render::Color white{1, 1, 1, 1};
  const auto drawHandle = [&](const gradient::Stop& s) {
    const bool isSelected = s.id == selected_;
    const bool fading = drag_.active && drag_.removing && s.id == drag_.id;
    const float opacity = fading ? 0.4f : 1.0f;
    const render::Rect h{box.x + static_cast<float>(s.position) * box.w - size * 0.5f, box.y + (box.h - size) * 0.5f, size, size};
    render::Color border = white;
    border.a = (isSelected ? 1.0f : 0.6f) * opacity;
    pickerdraw::drawSquareHandle(ctx, h, kHandleRadius, kHandleBorder, border, toColor(s.color, opacity));
  };
  for (const gradient::Stop& s : gradient_.stops()) {
    if (s.id != selected_) drawHandle(s);
  }
  if (const gradient::Stop* s = gradient_.find(selected_)) drawHandle(*s);
  if (focusVisible()) ctx.focusRing(box, ctx.px(kBarRadius));
}

// ---- hit testing and editing ------------------------------------------------------------------

uint32_t GradientBar::handleAt(double localX, double localY) const {
  const core::layout::Rect r = ui().absRect(id());
  if (r.w <= 0 || r.h <= 0 || localY < -2.0 || localY > r.h + 2.0) return 0;
  // The nearest handle wins; handles closer than half a pixel to the best one are drawn above it when
  // they come later (or are selected), so they win the tie.
  uint32_t best = 0;
  double bestDistance = 1e300;
  double selectedDistance = 1e300;
  for (const gradient::Stop& s : gradient_.stops()) {
    const double d = std::fabs(localX - handleCentreX(s.id));
    if (d > kHandle * 0.5 + kHandleSlop) continue;
    if (s.id == selected_) selectedDistance = d;
    if (d <= bestDistance + 0.5) {
      best = s.id;
      bestDistance = std::min(bestDistance, d);
    }
  }
  if (best != 0 && selectedDistance <= bestDistance + 0.5) return selected_;
  return best;
}

void GradientBar::select(uint32_t id) {
  if (id == selected_) return;
  selected_ = id;
  requestPaint();
  if (onSelect) onSelect(id);
}

void GradientBar::emit() {
  requestPaint();
  if (onEdit) onEdit(gradient_);
}

void GradientBar::onPointerDown(Event& e) {
  if (e.button != events::Button::Left) return;
  const core::layout::Rect r = ui().absRect(id());
  if (trackWidth() <= 0.0) return;
  e.markHandled();
  if (!focused()) ui().router().focus(id(), events::FocusReason::Pointer);
  const core::tree::WidgetId self = id();
  drag_ = Drag{};
  drag_.start = gradient_;
  drag_.startSelected = selected_;
  ui().router().capturePointer(id());
  if (onBegin) onBegin();
  if (!ui().alive(self)) return;
  uint32_t hit = handleAt(e.localX, e.localY);
  if (hit == 0) {
    hit = gradient_.addStop(positionAt(e.localX));
    if (hit == 0) {  // full: nothing to add, still end the gesture on release
      drag_.active = true;
      drag_.id = 0;
      return;
    }
    selected_ = hit;
    emit();
    if (!ui().alive(self)) return;
    if (onSelect) onSelect(hit);
    if (!ui().alive(self)) return;
  } else {
    select(hit);
    if (!ui().alive(self)) return;
  }
  drag_.active = true;
  drag_.id = hit;
}

void GradientBar::moveTo(double localX, double localY) {
  if (!drag_.active || drag_.id == 0) return;
  const core::layout::Rect r = ui().absRect(id());
  if (trackWidth() <= 0.0) return;
  drag_.removing = gradient_.size() > gradient::kMinStops && (localY < -kRemoveDistance || localY > r.h + kRemoveDistance);
  if (gradient_.moveStop(drag_.id, positionAt(localX))) emit();
  else requestPaint();
}

void GradientBar::onPointerMove(Event& e) {
  if (drag_.active) moveTo(e.localX, e.localY);
}

void GradientBar::finish(bool cancel) {
  if (!drag_.active) return;
  const core::tree::WidgetId self = id();
  const Drag d = drag_;
  drag_ = Drag{};
  if (cancel) {
    gradient_ = d.start;
    selected_ = d.startSelected;
    emit();
    if (!ui().alive(self)) return;
    if (onSelect) onSelect(selected_);
    if (!ui().alive(self)) return;
  } else if (d.removing && d.id != 0) {
    // The neighbour that takes over the selection.
    const size_t index = gradient_.indexOf(d.id);
    if (gradient_.removeStop(d.id)) {
      const size_t next = std::min(index, gradient_.size() - 1);
      selected_ = gradient_.stops()[next].id;
      emit();
      if (!ui().alive(self)) return;
      if (onSelect) onSelect(selected_);
      if (!ui().alive(self)) return;
    }
  }
  requestPaint();
  if (onEnd) onEnd();
}

void GradientBar::onPointerUp(Event& e) {
  if (e.button == events::Button::Left) finish(false);
}

void GradientBar::onCaptureLost(Event&) { finish(true); }

void GradientBar::onKeyDown(Event& e) {
  using events::Key;
  if (e.key == Key::Escape && drag_.active) {
    e.markHandled();
    finish(true);
    ui().router().releaseCapture();
    return;
  }
  if (drag_.active) {
    e.markHandled();
    return;
  }
  const size_t index = gradient_.indexOf(selected_);
  if (index == gradient::Gradient::npos) return;
  const double step = (e.modifiers & events::Mod::kShift) != 0 ? 0.1 : 0.01;
  const core::tree::WidgetId self = id();
  const gradient::Stop current = gradient_.stops()[index];
  bool changed = false;
  switch (e.key) {
    case Key::Left:
    case Key::Right: {
      const double target = current.position + (e.key == Key::Left ? -step : step);
      e.markHandled();
      if (onBegin) onBegin();
      if (!ui().alive(self)) return;
      changed = gradient_.moveStop(current.id, target);
      break;
    }
    case Key::Home:
    case Key::End:
      e.markHandled();
      if (onBegin) onBegin();
      if (!ui().alive(self)) return;
      changed = gradient_.moveStop(current.id, e.key == Key::Home ? 0.0 : 1.0);
      break;
    case Key::Delete:
    case Key::Backspace: {
      e.markHandled();
      if (gradient_.size() <= gradient::kMinStops) return;
      if (onBegin) onBegin();
      if (!ui().alive(self)) return;
      if (gradient_.removeStop(current.id)) {
        selected_ = gradient_.stops()[std::min(index, gradient_.size() - 1)].id;
        changed = true;
      }
      break;
    }
    case Key::PageUp:
    case Key::PageDown: {
      e.markHandled();
      const size_t count = gradient_.size();
      const size_t next = e.key == Key::PageUp ? (index + count - 1) % count : (index + 1) % count;
      select(gradient_.stops()[next].id);
      return;
    }
    default: return;
  }
  if (changed) {
    emit();
    if (!ui().alive(self)) return;
    if (current.id != selected_ && onSelect) onSelect(selected_);
    if (!ui().alive(self)) return;
  }
  if (onEnd) onEnd();
}

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: UiContext painting (depth-first walk honouring layer order, clipping, culling and subtree
//   opacity) and the animation service (colour / value tweens keyed by widget and slot).
// Invariants: paint() never mutates the tree; animations ask the Invalidator for frames only while
//   a tween runs and cancel the request when the last tween of a widget ends; tweens of destroyed
//   widgets are dropped at the end of each paint.
// Callers: the shell (paint, finishPaint), widgets (animatedColor through PaintContext).
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>

#include "r1ui/widgets/runtime/Easing.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace tree = core::tree;
using tree::WidgetId;

namespace {

constexpr std::array<double, 4> kDefaultEasing = {0.4, 0.0, 0.2, 1.0};
constexpr double kDefaultDurationMs = 150.0;

// Sibling paint order: ascending layer, and within a layer absolutely positioned widgets above
// in-flow ones (the same stacking the router's hit test uses).
int64_t stackKey(const tree::Widget& w) {
  return int64_t{w.layer} * 2 + (w.style.position == core::layout::Position::Absolute ? 1 : 0);
}

bool outside(const core::layout::Rect& r, double viewW, double viewH) {
  return r.x >= viewW || r.y >= viewH || r.right() <= 0 || r.bottom() <= 0;
}

}  // namespace

// ---- paint ------------------------------------------------------------------------------------

void UiContext::paint(render::Painter& painter) {
  DispatchGuard guard(*this);
  services_.text().beginFrame();
  paintWidget(painter, root_);
  endAnimationPass();
}

void UiContext::paintWidget(render::Painter& painter, WidgetId id) {
  tree::Widget* widget = tree_.get(id);
  if (widget == nullptr || !widget->shown()) return;
  if (id != root_ && outside(widget->absRect, viewportWidth(), viewportHeight())) return;
  WidgetObject* object = this->object(id);
  const float opacity = object != nullptr ? std::clamp(object->paintOpacity(), 0.0f, 1.0f) : 1.0f;
  const bool fade = opacity < 1.0f;
  if (fade) painter.pushOpacity(opacity);
  if (object != nullptr) {
    PaintContext pc(*this, painter, *object, widget->absRect, scale_);
    object->paint(pc);
  }
  const bool clips = widget->clips();
  if (clips) {
    const float s = scale_;
    const core::layout::Rect& r = widget->absRect;
    painter.pushClip({static_cast<float>(r.x) * s, static_cast<float>(r.y) * s, static_cast<float>(r.w) * s, static_cast<float>(r.h) * s});
  }

  // Children in sibling order unless a layer or an absolute child requires a different stacking.
  bool ordered = true;
  int64_t previous = std::numeric_limits<int64_t>::min();
  for (WidgetId c = tree_.firstChild(id); c.valid(); c = tree_.nextSibling(c)) {
    const int64_t key = stackKey(*tree_.get(c));
    if (key < previous) ordered = false;
    previous = key;
  }
  if (ordered) {
    // A child may destroy siblings only from event handlers, never while painting, so plain links are safe.
    for (WidgetId c = tree_.firstChild(id); c.valid(); c = tree_.nextSibling(c)) paintWidget(painter, c);
  } else {
    std::vector<WidgetId> children;
    for (WidgetId c = tree_.firstChild(id); c.valid(); c = tree_.nextSibling(c)) children.push_back(c);
    std::stable_sort(children.begin(), children.end(), [&](WidgetId a, WidgetId b) { return stackKey(*tree_.get(a)) < stackKey(*tree_.get(b)); });
    for (const WidgetId c : children) paintWidget(painter, c);
  }

  if (clips) painter.popClip();
  if (object != nullptr) {
    PaintContext pc(*this, painter, *object, widget->absRect, scale_);
    object->paintOver(pc);
  }
  if (fade) painter.popOpacity();
}

void UiContext::finishPaint() {
  services_.text().uploadAtlas();
  const bool textOverflow = services_.text().consumeAtlasOverflow();
  const bool iconOverflow = services_.icons().consumeOverflow();
  if (textOverflow || iconOverflow) repaintRequested_ = true;
}

// ---- animation ------------------------------------------------------------------------------------

render::Color UiContext::animatedColor(WidgetId widget, int slot, const render::Color& target) {
  const float t[4] = {target.r, target.g, target.b, target.a};
  float out[4];
  animate(widget, slot, t, 4, out);
  return {out[0], out[1], out[2], out[3]};
}

float UiContext::animatedValue(WidgetId widget, int slot, float target) {
  float out = target;
  animate(widget, slot, &target, 1, &out);
  return out;
}

void UiContext::animate(WidgetId widget, int slot, const float* target, int count, float* out) {
  const auto [it, inserted] = tweens_.try_emplace(TweenKey{widget, slot});
  Tween& t = it->second;
  const auto assign = [&](float* dst, const float* src) { std::copy(src, src + count, dst); };
  if (inserted) {
    assign(t.from, target);
    assign(t.to, target);
    assign(t.current, target);
    assign(out, target);
    return;
  }
  if (!std::equal(target, target + count, t.to)) {
    if (!animationsActive()) {
      assign(t.from, target);
      assign(t.to, target);
      assign(t.current, target);
      t.running = false;
    } else {
      assign(t.from, t.current);  // continue from what was last drawn
      assign(t.to, target);
      t.startMs = nowMs_;
      t.running = true;
    }
  }
  if (t.running) {
    const auto& tokens = services_.tokens();
    const double duration = tokens.motionDuration() ? *tokens.motionDuration() * 1000.0 : kDefaultDurationMs;
    const double progress = duration > 0.0 ? static_cast<double>(nowMs_ - t.startMs) / duration : 1.0;
    if (progress >= 1.0) {
      assign(t.current, t.to);
      t.running = false;
    } else {
      const double eased = cubicBezierEase(tokens.motionEasing() ? *tokens.motionEasing() : kDefaultEasing, progress);
      for (int i = 0; i < count; ++i) t.current[i] = t.from[i] + (t.to[i] - t.from[i]) * static_cast<float>(eased);
    }
    if (!t.scheduled) {
      invalidator_.requestAnimation(widget);
      t.scheduled = true;
    }
  }
  assign(out, t.current);
}

void UiContext::endAnimationPass() {
  std::unordered_set<WidgetId> moving;
  for (auto it = tweens_.begin(); it != tweens_.end();) {
    if (!tree_.alive(it->first.widget)) {
      it = tweens_.erase(it);
      continue;
    }
    if (it->second.running) moving.insert(it->first.widget);
    ++it;
  }
  for (auto& [key, tween] : tweens_) {
    if (tween.scheduled && !tween.running) {
      tween.scheduled = false;
      // A widget that drives its own frames (a blinking caret) keeps its animation request when a
      // colour transition ends.
      const WidgetObject* owner = object(key.widget);
      const bool ownFrames = owner != nullptr && owner->wantsContinuousFrames();
      if (moving.count(key.widget) == 0 && !ownFrames) invalidator_.cancelAnimation(key.widget);
    }
  }
}

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of EditorViewport.h.
// Invariants: world y points up (screen y down); a drag opens exactly one property interaction (one undo
//   step, restored when cancelled); selection changes go through EditorModel only, so the outliner and the
//   inspector see them; the widget never keeps a pointer into the model's objects.
// Callers: the viewport panel factory.
#include "EditorViewport.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace preview::editor {

namespace rw = r1ui::widgets;
namespace render = r1ui::render;
using r1ui::core::events::Key;

namespace {

constexpr double kShapeUnits = 1.6;
constexpr double kDragThresholdPx = 5.0;

render::Color toColor(const r1ui::props::Color& c, float alpha = 1.0f) { return {c.r, c.g, c.b, c.a * alpha}; }

}  // namespace

void ViewportCanvas::onAttached() {
  setFocusable(true);
  style().flexGrow = 1.0;
  style().minWidth = r1ui::core::layout::Length::px(0.0);
  style().minHeight = r1ui::core::layout::Length::px(0.0);
  selectionListener_ = model_.onSelectionChanged([this] { requestPaint(); });
  stateListener_ = model_.onState([this] { requestPaint(); });
  notifierToken_ = model_.context().notifier().subscribe([this](const r1ui::props::ChangeEvent&) { requestPaint(); });
}

void ViewportCanvas::onDetached() {
  model_.removeSelectionListener(selectionListener_);
  model_.removeStateListener(stateListener_);
  model_.context().notifier().unsubscribe(notifierToken_);
  if (dragging_) model_.context().cancelInteraction();
  dragging_ = pressed_ = false;
}

ViewportCanvas::Point ViewportCanvas::toLocal(double wx, double wy) const {
  const r1ui::core::layout::Rect r = ui().absRect(id());
  return {r.x + r.w * 0.5 + wx * kPixelsPerUnit, r.y + r.h * 0.5 - wy * kPixelsPerUnit};
}

ViewportCanvas::Point ViewportCanvas::toWorld(double x, double y) const {
  const r1ui::core::layout::Rect r = ui().absRect(id());
  return {(x - r.x - r.w * 0.5) / kPixelsPerUnit, -(y - r.y - r.h * 0.5) / kPixelsPerUnit};
}

uint64_t ViewportCanvas::hit(double x, double y) const {
  const Point w = toWorld(x, y);
  uint64_t best = 0;
  double bestDistance = 1e9;
  for (const SceneItem& item : model_.items()) {
    if (!model_.visibleOf(item) || (item.kind == ObjectKind::Light && !model_.showLights)) continue;
    const r1ui::props::Vec3 p = model_.positionOf(item);
    const double half = kShapeUnits * 0.5;
    if (std::abs(w.x - p.x) <= half && std::abs(w.y - p.y) <= half) {
      const double d = std::hypot(w.x - p.x, w.y - p.y);
      if (d < bestDistance) {
        bestDistance = d;
        best = item.id;
      }
    }
  }
  return best;
}

rw::Cursor ViewportCanvas::cursor() const { return dragging_ ? rw::Cursor::Move : rw::Cursor::Default; }

// ---- input ----------------------------------------------------------------------------------

void ViewportCanvas::onPointerDown(r1ui::core::events::Event& e) {
  if (e.button != r1ui::core::events::Button::Left) return;
  ui().focusWidget(id());
  const bool ctrl = (e.modifiers & r1ui::core::events::Mod::kCtrl) != 0;
  pressedItem_ = hit(e.x, e.y);
  toggleOnRelease_ = false;
  pressed_ = true;
  dragging_ = false;
  press_ = last_ = toWorld(e.x, e.y);
  if (pressedItem_ != 0) {
    if (ctrl) {
      toggleOnRelease_ = true;
    } else if (!model_.isSelected(pressedItem_)) {
      model_.select({pressedItem_});
    }
  } else if (!ctrl) {
    model_.select({});
  }
  e.markHandled();
}

void ViewportCanvas::onPointerMove(r1ui::core::events::Event& e) {
  if (!pressed_) return;
  const Point now = toWorld(e.x, e.y);
  if (!dragging_) {
    const double px = std::hypot(now.x - press_.x, now.y - press_.y) * kPixelsPerUnit;
    if (px <= kDragThresholdPx || model_.tool != "tool.move" || model_.selection().empty() || pressedItem_ == 0) return;
    const std::optional<size_t> row = model_.context().findRow("position");
    if (!row || !model_.context().beginInteraction(*row)) return;
    dragging_ = true;
    toggleOnRelease_ = false;
    last_ = press_;
  }
  const std::optional<size_t> row = model_.context().findRow("position");
  if (!row) return;
  const double dx = now.x - last_.x;
  const double dy = now.y - last_.y;
  model_.context().setComputed(*row, 0, [dx](double v) { return v + dx; });
  model_.context().setComputed(*row, 1, [dy](double v) { return v + dy; });
  last_ = now;
  e.markHandled();
}

void ViewportCanvas::finishDrag(bool commit) {
  if (dragging_) {
    if (commit) model_.context().endInteraction(false);
    else model_.context().cancelInteraction();
  }
  dragging_ = false;
  pressed_ = false;
}

void ViewportCanvas::onPointerUp(r1ui::core::events::Event& e) {
  if (!pressed_) return;
  const bool wasDragging = dragging_;
  const uint64_t item = pressedItem_;
  const bool toggle = toggleOnRelease_;
  finishDrag(true);
  if (!wasDragging && item != 0) {
    std::vector<uint64_t> next = model_.selection();
    if (toggle) {
      const auto it = std::find(next.begin(), next.end(), item);
      if (it == next.end()) next.push_back(item);
      else next.erase(it);
      model_.select(std::move(next));
    } else if (model_.selection().size() > 1) {
      model_.select({item});  // a plain click inside a multiple selection narrows it to the clicked object
    }
  }
  e.markHandled();
}

void ViewportCanvas::onCaptureLost(r1ui::core::events::Event&) { finishDrag(false); }

void ViewportCanvas::onKeyDown(r1ui::core::events::Event& e) {
  if (e.key == Key::Escape && dragging_) {
    finishDrag(false);
    e.markHandled();
  }
}

// ---- painting -------------------------------------------------------------------------------

void ViewportCanvas::paint(rw::PaintContext& ctx) {
  render::Painter& painter = ctx.painter();
  const render::Rect box = ctx.box();
  painter.fillRect(box, ctx.color("canvas"));
  painter.pushClip(box);
  const float s = ctx.scale();
  const r1ui::core::layout::Rect r = ctx.rect();
  const double cx = r.x + r.w * 0.5;
  const double cy = r.y + r.h * 0.5;
  const auto toBox = [&](double x, double y, double w, double h) { return ctx.toPhysical(x, y, w, h); };

  if (model_.showGrid) {
    const render::Color minor = ctx.color("border", 0.45);
    const render::Color axis = ctx.color("border-strong", 0.9);
    const double step = kPixelsPerUnit * 2.0;
    for (double x = std::fmod(r.w * 0.5, step); x <= r.w; x += step) {
      const bool isAxis = std::abs(x - r.w * 0.5) < 0.5;
      painter.fillRect(toBox(r.x + x, r.y, isAxis ? 2.0 : 1.0, r.h), isAxis ? axis : minor);
    }
    for (double y = std::fmod(r.h * 0.5, step); y <= r.h; y += step) {
      const bool isAxis = std::abs(y - r.h * 0.5) < 0.5;
      painter.fillRect(toBox(r.x, r.y + y, r.w, isAxis ? 2.0 : 1.0), isAxis ? axis : minor);
    }
  }

  const r1ui::theme::TextStyle caption = ctx.resolve(rw::Label::styleKeyFor(rw::LabelRole::Caption), 0).text;
  for (const SceneItem& item : model_.items()) {
    if (!model_.visibleOf(item)) continue;
    const bool light = item.kind == ObjectKind::Light;
    if (light && !model_.showLights) continue;
    const r1ui::props::Vec3 p = model_.positionOf(item);
    const double size = (light ? kShapeUnits * 0.7 : kShapeUnits) * kPixelsPerUnit;
    const double x = cx + p.x * kPixelsPerUnit - size * 0.5;
    const double y = cy - p.y * kPixelsPerUnit - size * 0.5;
    const render::Rect shape = toBox(x, y, size, size);
    const float radius = light ? shape.w * 0.5f : 4.0f * s;
    if (light) {
      painter.fillRoundedRect(shape, {radius, radius, radius, radius}, toColor(model_.colorOf(item), 0.35f));
      painter.border(shape, {radius, radius, radius, radius}, 2.0f * s, toColor(model_.colorOf(item)));
    } else {
      painter.fillRoundedRect(shape, {radius, radius, radius, radius}, toColor(model_.colorOf(item)));
    }
    if (model_.isSelected(item.id)) {
      const render::Rect ring = toBox(x - 4.0, y - 4.0, size + 8.0, size + 8.0);
      const float rr = light ? ring.w * 0.5f : 7.0f * s;
      painter.border(ring, {rr, rr, rr, rr}, 2.0f * s, ctx.color("accent"), true);
    }
    rw::TextOptions options;
    options.align = rw::TextAlign::Center;
    options.ellipsis = false;
    ctx.drawText(model_.nameOf(item), caption, toBox(x - 30.0, y + size + 6.0, size + 60.0, 16.0), options);
  }

  const std::string label = "Viewport   tool: " + model_.tool.substr(model_.tool.find('.') + 1) + (model_.selection().empty() ? "" : "   selected: " + std::to_string(model_.selection().size()));
  ctx.drawText(label, caption, toBox(r.x + 10.0, r.y + 8.0, r.w - 20.0, 16.0), {});
  painter.popClip();
  if (focusVisible()) ctx.focusRing(0.0f);
}

}  // namespace preview::editor

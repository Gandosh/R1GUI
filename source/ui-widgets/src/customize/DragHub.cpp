// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of DragHub.h.
// Invariants: `over_` is either invalid or the id of a registered, alive target that has been told
//   about the pointer; leave() is called exactly once per entered target; the ghost overlay exists only
//   while a drag is active; target callbacks are never called after the target unregistered.
// Callers: drag sources and targets of the customize widgets.
#include "r1ui/widgets/customize/DragHub.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

DragHub::DragHub(UiContext& ui) : ui_(ui) {}

DragHub::~DragHub() { closeGhost(); }

void DragHub::addTarget(core::tree::WidgetId widget, DragTarget* target) {
  removeTarget(widget);
  targets_.push_back({widget, target});
}

void DragHub::removeTarget(core::tree::WidgetId widget) {
  if (over_ == widget) {
    over_ = {};
    accepting_ = false;
  }
  targets_.erase(std::remove_if(targets_.begin(), targets_.end(), [&](const Entry& e) { return e.widget == widget; }), targets_.end());
}

DragHub::Entry* DragHub::hit(double x, double y) {
  for (auto it = targets_.rbegin(); it != targets_.rend(); ++it) {
    if (!ui_.alive(it->widget)) continue;
    const core::layout::Rect r = ui_.absRect(it->widget);
    if (r.w <= 0.0 || r.h <= 0.0) continue;
    if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return &*it;
  }
  return nullptr;
}

bool DragHub::begin(DragPayload payload, double x, double y) {
  if (active_ || !std::isfinite(x) || !std::isfinite(y)) return false;
  active_ = true;
  payload_ = std::move(payload);
  OverlayOptions options;
  options.surface = OverlaySurface::Tooltip;
  options.placement = Placement::Manual;
  options.anchor = {static_cast<int32_t>(std::lround(x + 12.0)), static_cast<int32_t>(std::lround(y + 14.0)), 0, 0};
  options.interactive = false;
  options.dismissOnOutsidePress = false;
  options.dismissOnEscape = false;
  options.restoreFocus = false;
  options.maxHeightFraction = 0.0;
  const OverlayHandle handle = ui_.overlays().open(options);
  if (handle.valid()) {
    ghost_ = handle.id;
    Label& label = ui_.create<Label>(handle.host, payload_.text, LabelRole::Body);
    label.setEllipsis(false);
  }
  move(x, y);
  return true;
}

void DragHub::leave() {
  if (over_.valid()) {
    for (Entry& e : targets_) {
      if (e.widget == over_) e.target->dragLeave();
    }
  }
  over_ = {};
  accepting_ = false;
}

void DragHub::move(double x, double y) {
  if (!active_ || !std::isfinite(x) || !std::isfinite(y)) return;
  if (ghost_.valid()) ui_.overlays().setPosition(ghost_, x + 12.0, y + 14.0);
  Entry* entry = hit(x, y);
  if (entry == nullptr) {
    leave();
    return;
  }
  if (over_ != entry->widget) leave();
  over_ = entry->widget;
  accepting_ = entry->target->dragOver(payload_, x, y);
}

bool DragHub::end(double x, double y) {
  if (!active_) return false;
  move(x, y);  // the target judges the drop at the release position, not at the last move
  bool applied = false;
  Entry* entry = std::isfinite(x) && std::isfinite(y) ? hit(x, y) : nullptr;
  const core::tree::WidgetId widget = entry != nullptr ? entry->widget : core::tree::WidgetId{};
  DragTarget* target = entry != nullptr ? entry->target : nullptr;
  const DragPayload payload = payload_;
  active_ = false;
  closeGhost();
  if (target != nullptr && over_ == widget && accepting_) {
    // Clear the indicator first: the drop may rebuild the target's content.
    over_ = {};
    accepting_ = false;
    target->dragLeave();
    applied = target->dragDrop(payload, x, y);
  } else {
    leave();
  }
  payload_ = {};
  return applied;
}

void DragHub::cancel() {
  if (!active_) return;
  active_ = false;
  closeGhost();
  leave();
  payload_ = {};
}

void DragHub::closeGhost() {
  if (ghost_.valid()) ui_.overlays().close(ghost_);
  ghost_ = {};
}

}  // namespace r1ui::widgets

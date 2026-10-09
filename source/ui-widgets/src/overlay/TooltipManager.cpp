// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of TooltipManager.h.
// Invariants: overlay_ is valid only while the tooltip is shown; source_ is the widget the pending
//   or shown tooltip belongs to and is cleared when it dies; the tooltip overlay is never modal and
//   never takes focus.
// Callers: UiContext (input hooks, tick, afterLayout), tests.
#include "r1ui/widgets/overlay/TooltipManager.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/overlay/Placement.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

using core::tree::WidgetId;

void TooltipManager::setEnabled(bool enabled) {
  enabled_ = enabled;
  if (!enabled_) hide();
}

WidgetId TooltipManager::findSource(WidgetId hovered) const {
  for (WidgetId id = hovered; id.valid(); id = ui_.tree().parent(id)) {
    const WidgetObject* object = ui_.object(id);
    if (object != nullptr && !object->tooltipText().empty()) return id;
  }
  return {};
}

bool TooltipManager::mayShow() const {
  return enabled_ && !suppressed_ && ui_.windowActive() && ui_.router().heldButtons() == 0 && !ui_.overlays().anyModal();
}

void TooltipManager::closeOverlay() {
  if (!overlay_.valid()) return;
  const OverlayId id = overlay_.id;
  overlay_ = {};
  placedX_ = placedY_ = -1e9;
  ui_.overlays().close(id, DismissReason::Programmatic);
}

void TooltipManager::hide() {
  closeOverlay();
  source_ = {};
  waiting_ = false;
}

void TooltipManager::onPointerMoved(double x, double y) {
  pointerX_ = x;
  pointerY_ = y;
  lastMoveMs_ = ui_.now();
  suppressed_ = false;
  const WidgetId next = findSource(ui_.router().hovered());
  if (next != source_) {
    closeOverlay();
    source_ = next;
    waiting_ = next.valid();
    return;
  }
  // Same widget. While waiting, the move restarted the rest timer (lastMoveMs_). While showing, the
  // tooltip follows the pointer: a layout request makes the next frame run afterLayout().
  if (overlay_.valid()) ui_.invalidator().requestLayout(overlay_.host);
}

void TooltipManager::onPointerPressed() {
  suppressed_ = true;
  hide();
}

void TooltipManager::onPointerLeftWindow() { hide(); }

void TooltipManager::onModalOpened() {
  hide();
}

void TooltipManager::show() {
  const WidgetObject* object = ui_.object(source_);
  if (object == nullptr || object->tooltipText().empty()) return;
  text_ = std::string(object->tooltipText());
  OverlayOptions options;
  options.surface = OverlaySurface::Tooltip;
  options.placement = Placement::Manual;
  options.anchor = {static_cast<int32_t>(std::lround(pointerX_ + 12.0)), static_cast<int32_t>(std::lround(pointerY_ + 8.0)), 0, 0};
  options.interactive = false;
  options.dismissOnOutsidePress = false;
  options.dismissOnEscape = false;
  options.restoreFocus = false;
  options.maxHeightFraction = 0.0;
  options.fadeInMs = static_cast<double>(timing_.fadeMs);
  overlay_ = ui_.overlays().open(options);
  if (!overlay_.valid()) return;
  Label& label = ui_.create<Label>(overlay_.host, text_, LabelRole::Body);
  label.setEllipsis(false);
}

bool TooltipManager::tick() {
  if (!enabled_) return false;
  if (source_.valid() && !ui_.alive(source_)) {
    hide();
    return true;
  }
  if (overlay_.valid() && !ui_.overlays().isOpen(overlay_.id)) {
    overlay_ = {};
    return true;
  }
  if (!mayShow()) {
    if (overlay_.valid() || waiting_) {
      const bool wasVisible = overlay_.valid();
      hide();
      return wasVisible;
    }
    return false;
  }
  if (waiting_ && !overlay_.valid() && ui_.now() >= lastMoveMs_ + timing_.restMs + timing_.showDelayMs) {
    waiting_ = false;
    show();
    return overlay_.valid();
  }
  return false;
}

std::optional<uint64_t> TooltipManager::msUntilTick() const {
  if (!enabled_ || !waiting_ || overlay_.valid() || !mayShow()) return std::nullopt;
  const uint64_t due = lastMoveMs_ + timing_.restMs + timing_.showDelayMs;
  return ui_.now() >= due ? uint64_t{0} : due - ui_.now();
}

bool TooltipManager::afterLayout() {
  if (!overlay_.valid()) return false;
  const core::layout::Rect size = ui_.absRect(overlay_.host);
  if (size.empty()) return false;
  TooltipPlacementInput in;
  in.width = size.w;
  in.height = size.h;
  in.pointerX = pointerX_;
  in.pointerY = pointerY_;
  in.bounds = {4, 4, static_cast<int32_t>(std::lround(ui_.viewportWidth())) - 8, static_cast<int32_t>(std::lround(ui_.viewportHeight())) - 8};
  const Point p = placeTooltip(in);
  if (std::abs(p.x - placedX_) < 0.5 && std::abs(p.y - placedY_) < 0.5) return false;
  placedX_ = p.x;
  placedY_ = p.y;
  ui_.overlays().setPosition(overlay_.id, p.x, p.y);
  return true;
}

}  // namespace r1ui::widgets

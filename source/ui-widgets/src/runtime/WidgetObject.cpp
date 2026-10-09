// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of WidgetObject.h: state flag bookkeeping, the style-state mapping and the
//   default event handler that updates hover / pressed / focus flags before calling the hooks.
// Invariants: a flag change always requests a repaint; node() never returns a dangling reference
//   (it throws std::logic_error for a destroyed or unattached widget).
// Callers: every widget (through the base class), UiContext (bind).
#include "r1ui/widgets/runtime/WidgetObject.h"

#include <stdexcept>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

using core::events::EventType;

void WidgetObject::bind(UiContext& ui, core::tree::WidgetId id) {
  ui_ = &ui;
  id_ = id;
}

core::tree::Widget& WidgetObject::node() const {
  core::tree::Widget* w = ui_ != nullptr ? ui_->tree().get(id_) : nullptr;
  if (w == nullptr) throw std::logic_error("WidgetObject::node: the widget is not attached or was destroyed");
  return *w;
}

core::layout::Style& WidgetObject::style() const { return node().style; }

void WidgetObject::setState(uint16_t flag, bool on) {
  const uint16_t next = on ? static_cast<uint16_t>(state_ | flag) : static_cast<uint16_t>(state_ & ~flag);
  if (next == state_) return;
  const uint16_t previous = state_;
  state_ = next;
  onStateChanged(previous);
  requestPaint();
}

void WidgetObject::setEnabled(bool enabled) {
  if (enabled == this->enabled()) return;
  node().flags.enabled = enabled;
  setState(StateFlag::kDisabled, !enabled);
  // Hover and focus on a widget that just became unreachable are released by the router.
  ui().router().sync();
}

void WidgetObject::setFocusable(bool focusable) { node().flags.focusable = focusable; }

uint8_t WidgetObject::styleState() const {
  namespace S = theme::State;
  uint8_t s = S::kNone;
  if (hasState(StateFlag::kHover)) s |= S::kHover;
  if (hasState(StateFlag::kPressed)) s |= S::kActive;
  if (hasState(StateFlag::kFocused)) s |= S::kFocus;
  if (hasState(StateFlag::kDisabled)) s |= S::kDisabled;
  if (hasState(StateFlag::kSelected)) s |= S::kSelected;
  if (hasState(StateFlag::kMixed)) s |= S::kMixed;
  if (hasState(StateFlag::kBound)) s |= S::kBound;
  if (hasState(StateFlag::kInvalid)) s |= S::kInvalid;
  return s;
}

void WidgetObject::requestLayout() { ui().invalidator().requestLayout(id_); }

void WidgetObject::requestPaint() {
  if (ui_ != nullptr) ui_->invalidator().requestPaint(id_);
}

void WidgetObject::setWantsLayoutCallback(bool wants) {
  if (wants == wantsLayout_) return;
  wantsLayout_ = wants;
  ui().setLayoutCallback(id_, wants);
}

core::layout::MeasureResult WidgetObject::measure(const core::layout::MeasureInput&) { return {}; }

void WidgetObject::updateStateFromEvent(Event& e) {
  switch (e.type) {
    case EventType::PointerEnter: setState(StateFlag::kHover, true); break;
    case EventType::PointerLeave:
      setState(StateFlag::kHover, false);
      setState(StateFlag::kPressed, false);
      break;
    case EventType::PointerDown:
      if (e.button == core::events::Button::Left) setState(StateFlag::kPressed, true);
      break;
    case EventType::PointerUp:
    case EventType::CaptureLost: setState(StateFlag::kPressed, false); break;
    case EventType::FocusIn:
      setState(StateFlag::kFocused, true);
      setState(StateFlag::kFocusVisible, e.focusReason == core::events::FocusReason::Keyboard);
      break;
    case EventType::FocusOut:
      setState(StateFlag::kFocused, false);
      setState(StateFlag::kFocusVisible, false);
      break;
    default: break;
  }
}

void WidgetObject::onEvent(Event& e, core::events::Router&) {
  updateStateFromEvent(e);
  switch (e.type) {
    case EventType::PointerMove: onPointerMove(e); break;
    case EventType::PointerDown: onPointerDown(e); break;
    case EventType::PointerUp: onPointerUp(e); break;
    case EventType::PointerWheel: onPointerWheel(e); break;
    case EventType::PointerEnter: onPointerEnter(e); break;
    case EventType::PointerLeave: onPointerLeave(e); break;
    case EventType::Click: onClick(e); break;
    case EventType::DoubleClick: onDoubleClick(e); break;
    case EventType::DragStart: onDragStart(e); break;
    case EventType::CaptureLost: onCaptureLost(e); break;
    case EventType::KeyDown: onKeyDown(e); break;
    case EventType::KeyUp: onKeyUp(e); break;
    case EventType::TextInput: onTextInput(e); break;
    case EventType::FocusIn: onFocusIn(e); break;
    case EventType::FocusOut: onFocusOut(e); break;
  }
}

}  // namespace r1ui::widgets

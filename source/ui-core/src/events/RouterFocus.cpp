// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: Router keyboard focus (set, clear, Tab traversal, validation) and key / text routing.
// Why: see Router.h. Focus changes are the one place where a handler may legitimately redirect
//   the operation in progress (spec 01 rule 7), so the focus epoch counter detects that and
//   lets the handler's choice stand.
// Callers: Router.h consumers. Calls: TreeQueries, FocusObserver, EventHandler.
#include <cstdint>

#include "r1ui/core/events/Router.h"
#include "r1ui/core/events/TreeQueries.h"

namespace r1ui::core::events {

using tree::kNoWidget;
using tree::WidgetId;

WidgetId Router::focused() const {
  return (focus_.valid() && isFocusable(tree_, focus_)) ? focus_ : kNoWidget;
}

// Moves focus to `next` (invalid = clear). FocusOut goes to the old widget first; if its handler
// moves focus itself the original request is dropped. Returns true when focus ended on `next`.
bool Router::setFocus(WidgetId next, FocusReason reason) {
  const WidgetId old = focus_;
  if (old == next) return true;
  const uint32_t epochBefore = focusEpoch_;
  if (tree_.alive(old)) {
    Event out;
    out.type = EventType::FocusOut;
    out.related = next;
    out.focusReason = reason;
    deliver(out, old, Delivery::TargetOnly);
  }
  if (focusEpoch_ != epochBefore) return false;  // the handler chose another focus
  if (next.valid() && !isFocusable(tree_, next)) next = kNoWidget;  // vanished during FocusOut
  focus_ = next;
  focusVisible_ = next.valid() && reason == FocusReason::Keyboard;
  ++focusEpoch_;
  if (observer_ != nullptr) observer_->onFocusChanged(old, next, reason, *this);
  if (focus_ == next && next.valid()) {
    Event in;
    in.type = EventType::FocusIn;
    in.related = old;
    in.focusReason = reason;
    deliver(in, next, Delivery::TargetOnly);
  }
  return focus_ == next && next.valid();
}

bool Router::focus(WidgetId widget, FocusReason reason) {
  validateFocus();
  if (!isFocusable(tree_, widget)) return false;
  return setFocus(widget, reason);
}

void Router::clearFocus() {
  validateFocus();
  if (focus_.valid()) setFocus(kNoWidget, FocusReason::Program);
}

bool Router::focusNext(bool backwards) {
  validateFocus();
  const WidgetId next = nextFocusable(tree_, root_, focus_, backwards);
  if (!next.valid()) return false;
  return setFocus(next, FocusReason::Keyboard);
}

// A destroyed focus owner is dropped (the observer still hears about it); a hidden, disabled or
// no-longer-focusable one receives FocusOut.
void Router::validateFocus() {
  if (!focus_.valid()) return;
  if (!tree_.alive(focus_)) {
    const WidgetId old = focus_;
    focus_ = kNoWidget;
    focusVisible_ = false;
    ++focusEpoch_;
    if (observer_ != nullptr) observer_->onFocusChanged(old, kNoWidget, FocusReason::Program, *this);
  } else if (!isFocusable(tree_, focus_)) {
    setFocus(kNoWidget, FocusReason::Program);
  }
}

// ---- keyboard ----

bool Router::keyDown(Key key, uint8_t modifiers, bool repeat, uint64_t timestampMs) {
  validateFocus();
  Event e;
  e.type = EventType::KeyDown;
  e.key = key;
  e.modifiers = modifiers;
  e.repeat = repeat;
  e.timestampMs = timestampMs;
  e.buttons = buttons_;
  if (focus_.valid()) {
    deliver(e, focus_, Delivery::KeyBubbling);
    if (e.handled) return true;
    if (e.stopped) return false;  // propagation stopped: no navigation, no global shortcuts
  }
  const uint8_t navBlockers = Mod::kCtrl | Mod::kAlt | Mod::kMeta;
  if (key == Key::Tab && (modifiers & navBlockers) == 0 && focusNext((modifiers & Mod::kShift) != 0)) {
    return true;
  }
  if (global_ != nullptr) {
    e.stopped = false;
    return global_->onGlobalKey(e, *this);
  }
  return false;
}

bool Router::keyUp(Key key, uint8_t modifiers, uint64_t timestampMs) {
  validateFocus();
  if (!focus_.valid()) return false;
  Event e;
  e.type = EventType::KeyUp;
  e.key = key;
  e.modifiers = modifiers;
  e.timestampMs = timestampMs;
  e.buttons = buttons_;
  return deliver(e, focus_, Delivery::KeyBubbling);
}

bool Router::textInput(char32_t codePoint, uint8_t modifiers, uint64_t timestampMs) {
  const bool surrogate = codePoint >= 0xD800 && codePoint <= 0xDFFF;
  if (codePoint == 0 || surrogate || codePoint > 0x10FFFF) return false;
  validateFocus();
  if (!focus_.valid()) return false;
  Event e;
  e.type = EventType::TextInput;
  e.codePoint = codePoint;
  e.modifiers = modifiers;
  e.timestampMs = timestampMs;
  return deliver(e, focus_, Delivery::KeyBubbling);
}

}  // namespace r1ui::core::events

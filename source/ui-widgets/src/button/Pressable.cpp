// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Pressable.h.
// Invariants: armed_ and keyArmed_ are false whenever the widget is disabled or unfocused (keyboard
//   press); the pressed flag is only set by a held left button over the widget or by an accept key.
// Callers: Button, IconButton, Checkbox, Switch.
#include "r1ui/widgets/button/Pressable.h"

namespace r1ui::widgets {

using core::events::Button;
using core::events::EventType;
using core::events::Key;
namespace Mod = core::events::Mod;

namespace {
bool isAcceptKey(Key key) { return key == Key::Space || key == Key::Enter; }
}  // namespace

void Pressable::onAttached() { setFocusable(true); }

uint8_t Pressable::styleState() const {
  uint8_t s = WidgetObject::styleState();
  if (!focusVisible()) s &= static_cast<uint8_t>(~theme::State::kFocus);
  return s;
}

void Pressable::onPointerDown(Event& e) {
  if (e.button == Button::Left && enabled()) armed_ = true;
}

void Pressable::onPointerUp(Event& e) {
  if (e.button == Button::Left) armed_ = false;
}

void Pressable::onCaptureLost(Event&) { armed_ = false; }

void Pressable::onPointerEnter(Event& e) {
  // The base class cleared the pressed look when the pointer left; show it again on return.
  if (armed_ && enabled() && (e.buttons & core::events::buttonBit(Button::Left)) != 0) setState(StateFlag::kPressed, true);
}

void Pressable::onClick(Event& e) {
  if (e.button != Button::Left || !enabled()) return;
  e.markHandled();
  activate();
}

void Pressable::onKeyDown(Event& e) {
  if (!isAcceptKey(e.key) || e.modifiers != Mod::kNone || !enabled()) return;
  e.markHandled();
  if (e.repeat) return;
  keyArmed_ = true;
  key_ = e.key;
  setState(StateFlag::kPressed, true);
}

void Pressable::onKeyUp(Event& e) {
  if (!keyArmed_ || e.key != key_) return;
  keyArmed_ = false;
  e.markHandled();
  setState(StateFlag::kPressed, false);
  if (enabled()) activate();
}

void Pressable::onFocusOut(Event&) {
  if (!keyArmed_) return;
  keyArmed_ = false;
  setState(StateFlag::kPressed, false);
}

void Pressable::onStateChanged(uint16_t previous) {
  if (hasState(StateFlag::kDisabled) && (previous & StateFlag::kDisabled) == 0) {
    armed_ = false;
    keyArmed_ = false;
    setState(StateFlag::kPressed, false);
  }
}

}  // namespace r1ui::widgets

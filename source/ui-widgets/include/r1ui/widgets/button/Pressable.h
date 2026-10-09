// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: Pressable, the shared behaviour of every "press to activate" control (Button, IconButton,
//   Checkbox, Switch): pointer press / release-inside activation, re-arming when the pointer
//   returns while the button is still held, Space / Enter activation on key-up, focus-visible only
//   styling, pointer cursor.
// Why: the four widgets must behave identically (spec 01 rules 1-5 and 22, spec 08 click rules); one
//   base keeps that in one place and lets each widget own only its look and its value.
// Callers: Button, IconButton, Checkbox, Switch (derive). Calls: WidgetObject, Router (through
//   events).
// Behaviour: a left press shows the pressed state (it does not mark the press handled, so the Router
//   still focuses the control without a focus indication). Click is synthesised by the Router only
//   when the button is released over this widget, which is the release-inside rule. If the pointer
//   leaves while the button is held the pressed look is dropped and comes back when it returns.
//   Space or Enter (no Ctrl/Alt/Meta/Shift) shows pressed on key-down and activates on key-up when
//   the key-down began on this widget; repeats are consumed but do not re-arm. Losing focus, being
//   disabled or losing the pointer capture cancels a keyboard press without activating.
// Style state: the focus bit reaches the style sheet only for keyboard focus (focusVisible), so a
//   mouse click leaves no focus look; widgets draw ctx.focusRing() when focusVisible().
// Lifetime: activate() may destroy the widget; nothing touches members after it returns.
#pragma once

#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class Pressable : public WidgetObject {
 public:
  void onAttached() override;
  Cursor cursor() const override { return enabled() ? Cursor::Pointer : Cursor::Default; }
  uint8_t styleState() const override;

  void onPointerDown(Event& e) override;
  void onPointerUp(Event& e) override;
  void onPointerEnter(Event& e) override;
  void onClick(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;
  void onKeyUp(Event& e) override;
  void onFocusOut(Event& e) override;
  void onStateChanged(uint16_t previous) override;

 protected:
  // The control was operated (pointer click or accept key). Not called while disabled.
  virtual void activate() = 0;

 private:
  bool armed_ = false;       // a left press began on this widget and the button is still held
  bool keyArmed_ = false;    // an accept key went down on this widget and has not come up
  core::events::Key key_ = core::events::Key::Unknown;
};

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the toolkit-level input vocabulary: buttons, modifiers, key codes, the host-facing
//   PointerInput, and the Event record delivered to widget handlers.
// Why: the platform layer translates OS messages into these types once; widgets never see OS
//   types, and the Router never sees widget internals.
// Callers: ui-platform (produces PointerInput / key calls), events::Router (builds Event),
//   widget handlers (consume Event). Calls: nothing.
// Coordinates are logical pixels in root (window) space; Event::localX/Y are relative to the
//   absolute origin of the widget whose handler is running.
// Phases: pointer events travel Capture (root down) -> Target -> Bubble (target up). Key and
//   text events have no capture phase: Target -> Bubble. Enter/Leave, FocusIn/Out and
//   CaptureLost are delivered to their target only.
// stopPropagation() ends delivery to further widgets (including the global key fallback).
//   markHandled() records that the event was used; for key and text events it also ends
//   delivery (the first user wins, spec 01 rule 16), for pointer events it does not (the Router
//   uses it for focus-on-press and to suppress click after an accepted drag).
#pragma once

#include <cstdint>

#include "r1ui/core/tree/WidgetId.h"

namespace r1ui::core::events {

enum class Button : uint8_t { None, Left, Middle, Right, X1, X2 };

// Bit of a button in a held-buttons mask.
constexpr uint32_t buttonBit(Button b) { return b == Button::None ? 0u : (1u << static_cast<unsigned>(b)); }

namespace Mod {
inline constexpr uint8_t kNone = 0;
inline constexpr uint8_t kShift = 1;
inline constexpr uint8_t kCtrl = 2;
inline constexpr uint8_t kAlt = 4;
inline constexpr uint8_t kMeta = 8;  // command / windows key
}  // namespace Mod

// Platform-independent key identity (physical key, not the typed character). Letters and digits
// use their ASCII codes.
enum class Key : uint16_t {
  Unknown = 0,
  Backspace = 8,
  Tab = 9,
  Enter = 13,
  Escape = 27,
  Space = 32,
  PageUp = 33,
  PageDown = 34,
  End = 35,
  Home = 36,
  Left = 37,
  Up = 38,
  Right = 39,
  Down = 40,
  Insert = 45,
  Delete = 46,
  Digit0 = 48,  // Digit0..Digit9 = 48..57
  A = 65,       // A..Z = 65..90
  F1 = 112      // F1..F12 = 112..123
};

enum class EventType : uint8_t {
  PointerMove,
  PointerDown,
  PointerUp,
  PointerWheel,
  PointerEnter,  // target only; related = the widget the pointer came from
  PointerLeave,  // target only; related = the widget the pointer went to
  Click,         // synthesised on release over the press target
  DoubleClick,   // synthesised after the second Click of a sequence
  DragStart,     // pointer moved strictly more than the drag threshold with a button held
  CaptureLost,   // target only: the widget no longer owns the pointer
  KeyDown,
  KeyUp,
  TextInput,
  FocusIn,       // target only
  FocusOut       // target only
};

enum class Phase : uint8_t { Capture, Target, Bubble };

// Bit masks for EventHandler::phases().
inline constexpr uint8_t kListenCapture = 1;
inline constexpr uint8_t kListenTarget = 2;
inline constexpr uint8_t kListenBubble = 4;

// Why focus moved; keyboard focus shows the focus indication, pointer and program focus do not.
enum class FocusReason : uint8_t { Program, Pointer, Keyboard };

// One pointer sample from the host. `button` names the button that changed for down/up and is
// None for move/wheel. Non-finite coordinates or wheel deltas cause the sample to be dropped.
struct PointerInput {
  double x = 0.0;
  double y = 0.0;
  Button button = Button::None;
  uint8_t modifiers = Mod::kNone;
  double wheelX = 0.0;
  double wheelY = 0.0;
  uint64_t timestampMs = 0;  // monotonic host clock
};

struct Event {
  EventType type = EventType::PointerMove;
  Phase phase = Phase::Target;
  tree::WidgetId target;   // the widget the event is aimed at
  tree::WidgetId current;  // the widget whose handler is running
  tree::WidgetId related;  // enter/leave/focus: the other widget involved, if any
  uint64_t timestampMs = 0;
  uint8_t modifiers = Mod::kNone;

  // Pointer.
  double x = 0.0;
  double y = 0.0;
  double localX = 0.0;
  double localY = 0.0;
  double originX = 0.0;  // DragStart: where the press happened
  double originY = 0.0;
  Button button = Button::None;
  uint32_t buttons = 0;     // mask of buttons currently held (see buttonBit)
  uint32_t clickCount = 0;  // Down/Up/Click: 1 for a single click, 2 for the second of a double
  double wheelX = 0.0;
  double wheelY = 0.0;

  // Keyboard / text / focus.
  Key key = Key::Unknown;
  bool repeat = false;
  char32_t codePoint = 0;
  FocusReason focusReason = FocusReason::Program;

  // Outcome, set by handlers.
  bool stopped = false;
  bool handled = false;
  void stopPropagation() { stopped = true; }
  void markHandled() { handled = true; }
};

}  // namespace r1ui::core::events

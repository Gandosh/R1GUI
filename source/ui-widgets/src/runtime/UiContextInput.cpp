// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: UiContext input: translation of platform events (physical pixels, Windows key codes) into
//   router input (logical pixels, toolkit keys), the interception rules for overlays and tooltips
//   (outside presses, Escape, the Tab trap) and the cursor query.
// Order of key handling: overlays flagged escapeFirst take Escape, a trapping overlay takes Tab,
//   then the Router runs (focused widget, ancestors, Tab navigation) and finally the global
//   handler, which offers an unused Escape to the remaining overlays before the application's
//   shortcuts (spec 01 rule 16).
// Fault boundary: every entry point below runs inside guarded(); a throwing handler is reported
//   (UiHost::reportFault or stderr), counted in inputFaults() and does not reach the host.
// Callers: the shell (handlePlatformEvent), tests (direct input).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace events = core::events;
using core::tree::WidgetId;

namespace {

uint8_t routerModifiers(const platform::Modifiers& m) {
  return static_cast<uint8_t>((m.shift ? events::Mod::kShift : 0) | (m.ctrl ? events::Mod::kCtrl : 0) | (m.alt ? events::Mod::kAlt : 0) |
                              (m.meta ? events::Mod::kMeta : 0));
}

events::Button routerButton(platform::MouseButton b) {
  switch (b) {
    case platform::MouseButton::Left: return events::Button::Left;
    case platform::MouseButton::Middle: return events::Button::Middle;
    case platform::MouseButton::Right: return events::Button::Right;
    case platform::MouseButton::X1: return events::Button::X1;
    case platform::MouseButton::X2: return events::Button::X2;
  }
  return events::Button::None;
}

// Windows virtual-key codes the router knows by name; every other key is Unknown.
events::Key routerKey(uint32_t vk) {
  const bool named = vk == 8 || vk == 9 || vk == 13 || vk == 27 || vk == 32 || (vk >= 33 && vk <= 40) || vk == 45 || vk == 46 ||
                     (vk >= 48 && vk <= 57) || (vk >= 65 && vk <= 90) || (vk >= 112 && vk <= 123);
  return named ? static_cast<events::Key>(vk) : events::Key::Unknown;
}

}  // namespace

// ---- platform events ----------------------------------------------------------------------------

void UiContext::handlePlatformEvent(const platform::Event& e) {
  using ET = platform::EventType;
  const double inverse = 1.0 / static_cast<double>(scale_);
  const double x = static_cast<double>(e.x) * inverse;
  const double y = static_cast<double>(e.y) * inverse;
  const uint8_t mods = routerModifiers(e.modifiers);
  switch (e.type) {
    case ET::MouseMove: pointerMove(x, y, mods); break;
    case ET::MouseDown: pointerDown(x, y, routerButton(e.button), mods); break;
    case ET::MouseUp: pointerUp(x, y, routerButton(e.button), mods); break;
    case ET::Wheel: wheel(x, y, static_cast<double>(e.wheelX), static_cast<double>(e.wheelY), mods); break;
    case ET::MouseLeave: pointerLeftWindow(); break;
    case ET::CaptureLost:
      guarded([&] {
        DispatchGuard guard(*this);
        router_.cancelPointerInteraction();
        return true;
      });
      break;
    case ET::KeyDown: keyDown(routerKey(e.virtualKey), mods, e.repeat); break;
    case ET::KeyUp: keyUp(routerKey(e.virtualKey), mods); break;
    case ET::Char: textInput(e.codePoint, mods); break;
    case ET::FocusGained: setWindowActive(true); break;
    case ET::FocusLost: setWindowActive(false); break;
    default: break;  // size, DPI, move and close are the shell's business
  }
}

// ---- direct input ---------------------------------------------------------------------------------

events::PointerInput UiContext::makePointer(double x, double y, events::Button button, uint8_t modifiers) const {
  events::PointerInput in;
  in.x = x;
  in.y = y;
  in.button = button;
  in.modifiers = modifiers;
  in.timestampMs = nowMs_;
  return in;
}

// ---- fault boundary -------------------------------------------------------------------------------

// A widget handler or application callback that throws must not unwind through the host's event loop
// (it would end the program in the middle of a gesture). The exception is reported, counted, and any
// pointer capture is released so the next event starts clean.
void UiContext::noteFault(const char* what) {
  ++inputFaults_;
  lastInputFault_ = what != nullptr ? what : "unknown exception";
  if (host_.reportFault) {
    host_.reportFault(lastInputFault_);
  } else {
    std::fprintf(stderr, "r1ui: an input handler threw: %s\n", lastInputFault_.c_str());
  }
  try {
    DispatchGuard guard(*this);
    router_.cancelPointerInteraction();
  } catch (...) {
    // The capture owner threw again while losing capture; nothing more can be done here.
  }
}

template <class F>
bool UiContext::guarded(F&& body) {
  try {
    return body();
  } catch (const std::exception& e) {
    noteFault(e.what());
  } catch (...) {
    noteFault("unknown exception");
  }
  return false;
}

bool UiContext::pointerMove(double x, double y, uint8_t modifiers) {
  return guarded([&] {
    DispatchGuard guard(*this);
    pointerKnown_ = std::isfinite(x) && std::isfinite(y);
    pointerX_ = x;
    pointerY_ = y;
    const bool handled = router_.pointerMove(makePointer(x, y, events::Button::None, modifiers));
    if (pointerKnown_) tooltips_.onPointerMoved(x, y);
    return handled;
  });
}

bool UiContext::pointerDown(double x, double y, events::Button button, uint8_t modifiers) {
  return guarded([&] {
    DispatchGuard guard(*this);
    pointerKnown_ = std::isfinite(x) && std::isfinite(y);
    pointerX_ = x;
    pointerY_ = y;
    const WidgetId hit = router_.hitTest(x, y);
    const OverlayManager::PressOutcome outcome = overlays_.pressOutside(hit);
    tooltips_.onPointerPressed();
    if (!outcome.deliver) return true;
    return router_.pointerDown(makePointer(x, y, button, modifiers));
  });
}

bool UiContext::pointerUp(double x, double y, events::Button button, uint8_t modifiers) {
  return guarded([&] {
    DispatchGuard guard(*this);
    return router_.pointerUp(makePointer(x, y, button, modifiers));
  });
}

bool UiContext::wheel(double x, double y, double deltaX, double deltaY, uint8_t modifiers) {
  return guarded([&] {
    DispatchGuard guard(*this);
    events::PointerInput in = makePointer(x, y, events::Button::None, modifiers);
    in.wheelX = deltaX;
    in.wheelY = deltaY;
    tooltips_.hide();
    return router_.pointerWheel(in);
  });
}

void UiContext::pointerLeftWindow() {
  guarded([&] {
    DispatchGuard guard(*this);
    pointerKnown_ = false;
    router_.pointerLeftWindow();
    tooltips_.onPointerLeftWindow();
    return true;
  });
}

bool UiContext::keyDown(events::Key key, uint8_t modifiers, bool repeat) {
  return guarded([&] {
    DispatchGuard guard(*this);
    if (key == events::Key::Escape && overlays_.escape(false)) return true;
    const bool navMods = (modifiers & (events::Mod::kCtrl | events::Mod::kAlt | events::Mod::kMeta)) != 0;
    if (key == events::Key::Tab && !navMods && overlays_.trapTab((modifiers & events::Mod::kShift) != 0)) return true;
    return router_.keyDown(key, modifiers, repeat, nowMs_);
  });
}

bool UiContext::keyUp(events::Key key, uint8_t modifiers) {
  return guarded([&] {
    DispatchGuard guard(*this);
    return router_.keyUp(key, modifiers, nowMs_);
  });
}

bool UiContext::textInput(char32_t codePoint, uint8_t modifiers) {
  return guarded([&] {
    DispatchGuard guard(*this);
    return router_.textInput(codePoint, modifiers, nowMs_);
  });
}

bool UiContext::focusWidget(WidgetId id, events::FocusReason reason) {
  return guarded([&] {
    DispatchGuard guard(*this);
    return router_.focus(id, reason);
  });
}

void UiContext::clearFocus() {
  guarded([&] {
    DispatchGuard guard(*this);
    router_.clearFocus();
    return true;
  });
}

// A focused widget that takes typed text gets every unmodified letter, digit and space: they are
// text for it, never application shortcuts (typing "t" in a field must not switch the theme).
bool UiContext::onGlobalKey(const events::Event& event, events::Router& router) {
  if (event.key == events::Key::Escape && overlays_.escape(true)) return true;
  if (appKeys_ == nullptr) return false;
  // Modality isolates the keyboard too: delete, undo or a theme toggle must not reach the document
  // behind a dialog (a dialog that wants them sets allowGlobalShortcuts).
  if (overlays_.blocksGlobalShortcuts()) return false;
  const uint32_t key = static_cast<uint32_t>(event.key);
  const bool printable = key == 32 || (key >= 48 && key <= 57) || (key >= 65 && key <= 90);
  const bool commandMods = (event.modifiers & (events::Mod::kCtrl | events::Mod::kAlt | events::Mod::kMeta)) != 0;
  if (printable && !commandMods) {
    const WidgetObject* focused = object(router.focused());
    if (focused != nullptr && focused->wantsTextInput()) return false;
  }
  return appKeys_->onGlobalKey(event, router);
}

Cursor UiContext::cursor() const {
  WidgetId start = router_.capturer();
  if (!start.valid()) start = router_.hovered();
  for (WidgetId id = start; id.valid(); id = tree_.parent(id)) {
    const WidgetObject* o = object(id);
    if (o != nullptr && o->cursor() != Cursor::Default) return o->cursor();
  }
  return Cursor::Default;
}

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the Win32 window procedure, its exception barrier, and the handlers for size, move, DPI,
//   focus, close, keyboard, character and pointer messages. They fill the legacy per-kind queues
//   and the unified ordered event queue.
// Why: separates message handling from window lifecycle (Window.cpp) and frame/chrome handling
//   (WindowChrome.cpp).
// Callers: the OS, through the window class registered in Window.cpp.
// Invariants: dispatch() never throws; a handler that reports false leaves the message to
//   DefWindowProc. Pointer capture is held from the first button press to the last release and
//   every exit path (release, capture loss) resets buttonsDown and chromePressed.
#include <windowsx.h>

#include <utility>

#include "WindowImpl.h"

namespace r1ui::platform {

namespace {

constexpr UINT kMaxWheelDeltaUnits = 120;  // one notch
constexpr UINT_PTR kLiveTimerId = 1;       // drives the live callback during the OS move/size loop
constexpr UINT kLiveTimerMs = 16;

Modifiers currentModifiers() {
  const auto down = [](int vk) { return (GetKeyState(vk) & 0x8000) != 0; };
  return {down(VK_CONTROL), down(VK_SHIFT), down(VK_MENU), down(VK_LWIN) || down(VK_RWIN)};
}

EventType eventTypeFor(MouseEvent::Type type) {
  switch (type) {
    case MouseEvent::Type::Down: return EventType::MouseDown;
    case MouseEvent::Type::Up: return EventType::MouseUp;
    case MouseEvent::Type::Move: return EventType::MouseMove;
    case MouseEvent::Type::CaptureLost: return EventType::CaptureLost;
  }
  return EventType::MouseMove;
}

uint32_t scanCodeFrom(LPARAM lp) {
  const uint32_t code = static_cast<uint32_t>((lp >> 16) & 0xFF);
  return (lp & (1 << 24)) != 0 ? (code | 0xE000u) : code;
}

}  // namespace

// ---- Window procedure and exception barrier ----

LRESULT CALLBACK Window::Impl::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    auto* self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    if (self != nullptr) self->hwnd = hwnd;
  }
  auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self == nullptr) return DefWindowProcW(hwnd, msg, wp, lp);
  if (msg == WM_NCDESTROY) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    self->hwnd = nullptr;
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
  return self->dispatch(msg, wp, lp);
}

LRESULT Window::Impl::dispatch(UINT msg, WPARAM wp, LPARAM lp) noexcept {
  try {
    LRESULT result = 0;
    if (handleFrame(msg, wp, lp, result) || handleState(msg, wp, lp, result) ||
        handleKeyboard(msg, wp, lp, result) || handlePointer(msg, wp, lp, result)) {
      return result;
    }
  } catch (...) {
    // An allocation failure while recording an event must not unwind through the OS: the message
    // falls back to the default handling and the event is lost (counted only if it was queued).
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- Event recording ----

Event Window::Impl::makeEvent(EventType type) const {
  Event e;
  e.type = type;
  e.modifiers = currentModifiers();
  return e;
}

void Window::Impl::pushEvent(const Event& event) {
  if (event.type == EventType::MouseMove) {
    if (Event* last = events.back(); last != nullptr && last->type == EventType::MouseMove) {
      *last = event;
      return;
    }
  }
  events.push(event);
}

// Records a pointer event in both the legacy pointer queue (moves coalesce) and the unified queue.
void Window::Impl::pushMouse(MouseEvent::Type type, MouseButton button, float x, float y) {
  mouseX = x;
  mouseY = y;
  const MouseEvent legacy{type, button, x, y};
  MouseEvent* last = mouseQueue.back();
  if (type == MouseEvent::Type::Move && last != nullptr && last->type == MouseEvent::Type::Move) {
    *last = legacy;
  } else {
    mouseQueue.push(legacy);
  }
  Event e = makeEvent(eventTypeFor(type));
  e.button = button;
  e.x = x;
  e.y = y;
  pushEvent(e);
}

void Window::Impl::buttonChange(MouseButton button, bool down, float x, float y) {
  if (down) {
    if (buttonsDown++ == 0) SetCapture(hwnd);
  } else if (buttonsDown > 0 && --buttonsDown == 0) {
    ReleaseCapture();
  }
  pushMouse(down ? MouseEvent::Type::Down : MouseEvent::Type::Up, button, x, y);
  if (down && button == MouseButton::Left) clickQueue.push(MouseClick{mouseX, mouseY});
}

// Asks the OS for a leave notification (client or non-client) once per pointer entry.
void Window::Impl::armLeaveTracking(bool nonClient) {
  bool& armed = nonClient ? trackingNonClientLeave : trackingClientLeave;
  if (armed) return;
  TRACKMOUSEEVENT t{};
  t.cbSize = sizeof(t);
  t.dwFlags = TME_LEAVE | (nonClient ? TME_NONCLIENT : 0);
  t.hwndTrack = hwnd;
  armed = TrackMouseEvent(&t) != FALSE;
}

// True while the cursor is still over this window (client or frame area): moving between the
// client and a non-client zone raises a leave message that is not a real exit.
bool Window::Impl::pointerOverWindow() const {
  POINT p{};
  return GetCursorPos(&p) != FALSE && WindowFromPoint(p) == hwnd;
}

Point Window::Impl::screenToClient(LPARAM screenPoint) const {
  POINT p{GET_X_LPARAM(screenPoint), GET_Y_LPARAM(screenPoint)};
  ScreenToClient(hwnd, &p);
  return {p.x, p.y};
}

Size Window::Impl::clientSize() const { return {width, height}; }

// ---- Live callback during the OS move/size loop ----

// Runs the application's live callback once, never nested; its exceptions stop here because the
// caller is the window procedure.
void Window::Impl::runLive() {
  if (!live || inLive) return;
  inLive = true;
  try {
    live();
  } catch (...) {
  }
  inLive = false;
}

// ---- Size, position, DPI, focus, close ----

bool Window::Impl::handleState(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
  result = 0;
  switch (msg) {
    case WM_SIZE: {
      // The reported size of a minimized window is the iconic size; the contract is 0x0.
      const bool minimized = wp == SIZE_MINIMIZED;
      width = minimized ? 0 : LOWORD(lp);
      height = minimized ? 0 : HIWORD(lp);
      resized = true;
      Event e = makeEvent(EventType::Resized);
      e.rect = {0, 0, width, height};
      pushEvent(e);
      if (inSizeMove) runLive();
      return true;
    }
    case WM_ENTERSIZEMOVE:
      inSizeMove = true;
      SetTimer(hwnd, kLiveTimerId, kLiveTimerMs, nullptr);
      return true;
    case WM_EXITSIZEMOVE:
      inSizeMove = false;
      KillTimer(hwnd, kLiveTimerId);
      return true;
    case WM_TIMER:
      if (wp != kLiveTimerId) return false;
      runLive();
      return true;
    case WM_MOVE: {
      RECT r{};
      if (GetWindowRect(hwnd, &r) == FALSE) return true;
      Event e = makeEvent(EventType::Moved);
      e.rect = {r.left, r.top, r.right - r.left, r.bottom - r.top};
      pushEvent(e);
      return true;
    }
    case WM_DPICHANGED: {
      const auto* suggested = reinterpret_cast<const RECT*>(lp);
      if (suggested == nullptr) return true;
      dpi = HIWORD(wp);
      SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                   suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
      Event e = makeEvent(EventType::DpiChanged);
      e.dpiScale = dpiScaleFromDpi(dpi);
      e.rect = {suggested->left, suggested->top, suggested->right - suggested->left,
                suggested->bottom - suggested->top};
      pushEvent(e);
      return true;
    }
    case WM_SETFOCUS:
      pushEvent(makeEvent(EventType::FocusGained));
      return true;
    case WM_KILLFOCUS:
      pushEvent(makeEvent(EventType::FocusLost));
      return true;
    case WM_CLOSE:
      pushEvent(makeEvent(EventType::CloseRequested));
      if (!needsConfirm) closed = true;
      return true;
    default:
      return false;
  }
}

// ---- Keyboard and character input ----

bool Window::Impl::handleKeyboard(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
  result = 0;
  switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
      if (wp > 0xFF) return msg == WM_KEYDOWN || msg == WM_KEYUP;  // not a virtual key: drop
      const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
      Event e = makeEvent(down ? EventType::KeyDown : EventType::KeyUp);
      e.virtualKey = static_cast<uint32_t>(wp);
      e.scanCode = scanCodeFrom(lp);
      e.repeat = down && (lp & (1 << 30)) != 0;
      pushEvent(e);
      if (msg == WM_KEYDOWN) {
        if (wp == VK_ESCAPE) escape = true;
        keyQueue.push(KeyEvent{static_cast<uint32_t>(wp)});
      }
      // System keys (Alt combinations, F10) must still reach the default handler for Alt+F4 and
      // the system menu.
      return msg == WM_KEYDOWN || msg == WM_KEYUP;
    }
    case WM_CHAR: {
      if (wp > 0xFFFF) return true;  // not a UTF-16 unit: drop
      const std::optional<char32_t> cp = chars.feed(static_cast<char16_t>(wp));
      if (cp && isTextCodePoint(*cp)) {
        Event e = makeEvent(EventType::Char);
        e.codePoint = *cp;
        e.repeat = (lp & (1 << 30)) != 0;
        pushEvent(e);
      }
      return true;
    }
    default:
      return false;
  }
}

// ---- Pointer input, capture, cursor ----

const wchar_t* cursorResource(CursorShape shape) {
  switch (shape) {
    case CursorShape::Arrow: return IDC_ARROW;
    case CursorShape::ResizeHorizontal: return IDC_SIZEWE;
    case CursorShape::ResizeVertical: return IDC_SIZENS;
    case CursorShape::Text: return IDC_IBEAM;
    case CursorShape::Hand: return IDC_HAND;
    case CursorShape::ResizeNwSe: return IDC_SIZENWSE;
    case CursorShape::ResizeNeSw: return IDC_SIZENESW;
    case CursorShape::Move: return IDC_SIZEALL;
    case CursorShape::NotAllowed: return IDC_NO;
    case CursorShape::Wait: return IDC_WAIT;
  }
  return IDC_ARROW;
}

bool Window::Impl::handlePointer(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
  result = 0;
  const float x = static_cast<float>(GET_X_LPARAM(lp));
  const float y = static_cast<float>(GET_Y_LPARAM(lp));

  // Button messages share one shape: which button, press or release, and whether the OS flagged
  // the press as the second of a double click.
  MouseButton button = MouseButton::Left;
  bool isButton = true;
  bool down = false;
  bool doubleClick = false;
  switch (msg) {
    case WM_LBUTTONDOWN: down = true; break;
    case WM_LBUTTONDBLCLK: down = doubleClick = true; break;
    case WM_LBUTTONUP: break;
    case WM_MBUTTONDOWN: button = MouseButton::Middle; down = true; break;
    case WM_MBUTTONDBLCLK: button = MouseButton::Middle; down = doubleClick = true; break;
    case WM_MBUTTONUP: button = MouseButton::Middle; break;
    case WM_RBUTTONDOWN: button = MouseButton::Right; down = true; break;
    case WM_RBUTTONDBLCLK: button = MouseButton::Right; down = doubleClick = true; break;
    case WM_RBUTTONUP: button = MouseButton::Right; break;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
    case WM_XBUTTONUP:
      button = GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2;
      down = msg != WM_XBUTTONUP;
      doubleClick = msg == WM_XBUTTONDBLCLK;
      result = TRUE;  // documented return value for the X-button messages
      break;
    default: isButton = false; break;
  }
  if (isButton) {
    // Releasing the last button releases capture, which resets chromePressed, so read it first.
    const HitZone pressedChrome = chromePressed;
    buttonChange(button, down, x, y);
    if (doubleClick) {
      Event e = makeEvent(EventType::MouseDoubleClick);
      e.button = button;
      e.x = x;
      e.y = y;
      pushEvent(e);
    }
    if (!down && button == MouseButton::Left) finishChromePress(pressedChrome, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
    return true;
  }

  switch (msg) {
    case WM_MOUSEMOVE:
      armLeaveTracking(false);
      pushMouse(MouseEvent::Type::Move, MouseButton::Left, x, y);
      return true;
    case WM_MOUSELEAVE:
      trackingClientLeave = false;
      if (!pointerOverWindow()) pushEvent(makeEvent(EventType::MouseLeave));
      return true;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
      // Wheel coordinates are screen-space; deltas are in 1/120 notch units, possibly partial.
      const Point p = screenToClient(lp);
      const float notches = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / static_cast<float>(kMaxWheelDeltaUnits);
      Event e = makeEvent(EventType::Wheel);
      e.x = static_cast<float>(p.x);
      e.y = static_cast<float>(p.y);
      (msg == WM_MOUSEWHEEL ? e.wheelY : e.wheelX) = notches;
      pushEvent(e);
      return true;
    }
    case WM_CAPTURECHANGED:
      chromePressed = HitZone::Client;
      if (buttonsDown > 0) {  // capture taken by the OS while buttons were held
        buttonsDown = 0;
        pushMouse(MouseEvent::Type::CaptureLost, MouseButton::Left, mouseX, mouseY);
      }
      return true;
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        SetCursor(LoadCursorW(nullptr, cursorResource(cursor)));
        result = TRUE;
        return true;
      }
      return false;
    default:
      return false;
  }
}

}  // namespace r1ui::platform

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the platform-neutral window abstraction (create, pump events, size, mouse, native handle).
// Why: the rest of the toolkit never sees Win32 types; macOS/Linux backends implement this header.
// Callers: ui-render (surface creation via nativeHandle), examples/preview.
// Calls: the per-OS backend in win32/Window.cpp.
// Invariants: all calls happen on the thread that created the window (the UI thread).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace r1ui::platform {

// Windows virtual-key codes for the keys the toolkit reacts to (stable across backends: other
// backends translate their native codes to these values).
namespace keys {
inline constexpr uint32_t kTab = 0x09;
inline constexpr uint32_t kEscape = 0x1B;
inline constexpr uint32_t kLeft = 0x25;
inline constexpr uint32_t kRight = 0x27;
inline constexpr uint32_t kT = 0x54;
inline constexpr uint32_t kL = 0x4C;
inline constexpr uint32_t kR = 0x52;
inline constexpr uint32_t kS = 0x53;
}  // namespace keys

struct KeyEvent {
  uint32_t virtualKey = 0;  // key-down (including auto-repeat)
};

struct MouseClick {
  float x = 0.0f;  // client-space physical pixels at the time of the left-button press
  float y = 0.0f;
};

enum class MouseButton { Left, Middle, Right };

// One pointer event in arrival order. While any button is held the window captures the pointer,
// so Move and Up keep arriving (with coordinates outside the client area) after it leaves.
struct MouseEvent {
  enum class Type {
    Down,
    Up,
    Move,
    CaptureLost  // the OS took the capture away (focus loss): treat any drag as cancelled
  };
  Type type = Type::Move;
  MouseButton button = MouseButton::Left;  // Down/Up only
  float x = 0.0f;  // client-space physical pixels; negative or beyond the size while captured
  float y = 0.0f;
};

enum class CursorShape { Arrow, ResizeHorizontal, ResizeVertical };

struct WindowDesc {
  std::string title = "R1GUI";  // UTF-8
  int width = 1280;             // initial client width in physical pixels
  int height = 720;             // initial client height in physical pixels
};

// Opaque OS handles for the renderer's surface creation; meaning is backend-defined.
struct NativeHandle {
  void* window = nullptr;
  void* instance = nullptr;
};

inline constexpr size_t kMaxQueuedEvents = 256;

class Window {
 public:
  explicit Window(const WindowDesc& desc);
  ~Window();
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  // Processes pending OS messages. Returns false once the window has been closed.
  bool pumpEvents();

  int clientWidth() const;   // physical pixels; 0 when minimized
  int clientHeight() const;  // physical pixels; 0 when minimized
  bool consumeResized();     // true once after each size change
  float mouseX() const;      // client-space physical pixels
  float mouseY() const;
  bool escapePressed() const;

  // Hands over and clears the events received since the last call, oldest first. Each queue
  // holds at most kMaxQueuedEvents; the oldest events are dropped beyond that.
  std::vector<KeyEvent> takeKeyEvents();
  std::vector<MouseClick> takeMouseClicks();
  // Hands over and clears the pointer events (button down/up, moves, capture loss), oldest
  // first, bounded like the other queues; consecutive moves are coalesced into the latest.
  // Independent of takeMouseClicks: a left press appears in both queues.
  std::vector<MouseEvent> takeMouseEvents();
  // Cursor shown over the client area until changed again.
  void setCursor(CursorShape shape);
  void setTitle(const std::string& utf8Title);
  NativeHandle nativeHandle() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace r1ui::platform

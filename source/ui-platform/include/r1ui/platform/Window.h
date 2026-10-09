// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the platform-neutral window abstraction (create, pump events, size, DPI, input events,
//   cursors, clipboard, custom chrome hit-testing, native handle).
// Why: the rest of the toolkit never sees Win32 types; macOS/Linux backends implement this header.
// Callers: ui-render (surface creation via nativeHandle), ui-dock/ui-core (events, chrome),
//   examples/preview.
// Calls: the per-OS backend in win32/.
// Invariants: all calls happen on the thread that created the window (the UI thread). Several
//   Window objects may coexist on that thread; pumpEvents() on any of them dispatches the
//   thread's whole message queue and each window only records its own events.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/platform/ChromeHitTest.h"
#include "r1ui/platform/Clipboard.h"
#include "r1ui/platform/Dpi.h"
#include "r1ui/platform/Events.h"
#include "r1ui/platform/Geometry.h"

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

// The first three values keep their original meaning; the rest are platform cursor kinds.
enum class CursorShape {
  Arrow,
  ResizeHorizontal,
  ResizeVertical,
  Text,
  Hand,
  ResizeNwSe,  // diagonal, top-left to bottom-right
  ResizeNeSw,  // diagonal, top-right to bottom-left
  Move,
  NotAllowed,
  Wait
};

struct WindowDesc {
  std::string title = "R1GUI";  // UTF-8
  int width = 1280;             // initial client width (physical pixels unless sizesAreLogical)
  int height = 720;             // initial client height (same unit)
  // Borderless: no OS title bar or frame; the toolkit draws the header and reports its regions
  // through setChromeLayout. Edge resize, snapping, maximize/restore, the DWM shadow and
  // per-monitor DPI are kept. The default (false) is the native frame.
  bool borderless = false;
  bool resizable = true;
  std::optional<Point> position;  // outer top-left in screen physical pixels; default: OS choice
  std::optional<Size> minSize;    // minimum client size (same unit as width/height)
  bool toolWindow = false;        // no taskbar button (floating panels)
  // When true, width, height and minSize are logical pixels (1.0 = 96 dpi) and are scaled by the
  // dpi of the monitor the window opens on. Default false: physical pixels.
  bool sizesAreLogical = false;
};

// Opaque OS handles for the renderer's surface creation; meaning is backend-defined.
struct NativeHandle {
  void* window = nullptr;
  void* instance = nullptr;
};

inline constexpr size_t kMaxQueuedEvents = 256;
inline constexpr size_t kMaxQueuedWindowEvents = 1024;  // unified queue (takeEvents)

class Window {
 public:
  // Throws std::invalid_argument for an unusable descriptor (non-positive or > 32768 sizes,
  // invalid UTF-8 title) and std::runtime_error if the OS refuses to create the window; a
  // partially built window is destroyed.
  explicit Window(const WindowDesc& desc);
  ~Window();
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  // Processes pending OS messages for the whole thread (bounded per call so a message flood
  // cannot stall the caller). Returns false once this window has been closed.
  bool pumpEvents();

  int clientWidth() const;   // physical pixels; 0 when minimized
  int clientHeight() const;  // physical pixels; 0 when minimized
  bool consumeResized();     // true once after each size change
  float mouseX() const;      // client-space physical pixels
  float mouseY() const;
  bool escapePressed() const;

  // ---- DPI ----
  float dpiScale() const;  // 1.0 = 96 dpi; follows the monitor the window is on
  float logicalClientWidth() const { return physicalToLogical(clientWidth(), dpiScale()); }
  float logicalClientHeight() const { return physicalToLogical(clientHeight(), dpiScale()); }

  // ---- Position and size (physical pixels, screen coordinates) ----
  Rect windowRect() const;  // outer rectangle; includes the invisible resize border when maximized
  // Ignores an empty rectangle. Returns false if the OS call failed or the rectangle was empty.
  bool setWindowRect(const Rect& rect);

  // ---- Commands ----
  void minimize();
  void maximizeToggle();  // maximize, or restore if maximized; no-op for a non-resizable window
  bool isMaximized() const;
  bool isMinimized() const;
  void requestClose();  // posts a close request, exactly like the user closing the window
  // By default a close request closes the window (pumpEvents returns false). With confirmation
  // enabled the request only queues a CloseRequested event; call confirmClose() to proceed.
  void setCloseNeedsConfirmation(bool enabled);
  void confirmClose();

  // ---- Custom chrome (borderless windows) ----
  // Replaces the chrome description; returns false and keeps the previous one if invalid.
  bool setChromeLayout(const ChromeLayout& layout);
  // Zone of a client-space point under the current layout, size, dpi and maximize state.
  HitZone chromeZoneAt(Point clientPoint) const;

  // ---- Input ----
  // Hands over and clears all events since the last call in arrival order. The queue holds at
  // most kMaxQueuedWindowEvents; older events are dropped beyond that (see droppedEventCount).
  // Consecutive MouseMove events are coalesced into the latest.
  std::vector<Event> takeEvents();
  // Events discarded because a queue overflowed (all queues together), since creation.
  size_t droppedEventCount() const;

  // Legacy per-kind queues, kept working. Each holds at most kMaxQueuedEvents.
  std::vector<KeyEvent> takeKeyEvents();
  std::vector<MouseClick> takeMouseClicks();
  // Hands over and clears the pointer events (button down/up, moves, capture loss), oldest
  // first, bounded like the other queues; consecutive moves are coalesced into the latest.
  // Independent of takeMouseClicks: a left press appears in both queues.
  std::vector<MouseEvent> takeMouseEvents();

  // Cursor shown over the client area until changed again.
  void setCursor(CursorShape shape);
  void setTitle(const std::string& utf8Title);

  // ---- Clipboard (UTF-8 text) ----
  // Never throws for contention: a clipboard locked by another process is retried briefly and
  // then reported as Busy.
  ClipboardText getClipboardText();
  ClipboardStatus setClipboardText(std::string_view utf8);

  NativeHandle nativeHandle() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace r1ui::platform

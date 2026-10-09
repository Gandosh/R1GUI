// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the private state and message handlers of one Win32 window (Window::Impl).
// Why: shared by Window.cpp (lifecycle and public API), WindowProc.cpp (state and input
//   messages) and WindowChrome.cpp (frame, hit-test and custom-chrome messages) so no single
//   file mixes those responsibilities. Not a public header: Win32 types live here only.
// Callers: the three Window*.cpp files in this directory.
// Lifetime: Impl owns the HWND and destroys it in its destructor (so a Window whose constructor
//   threw halfway is still cleaned up); the window procedure reaches Impl through GWLP_USERDATA,
//   set in WM_NCCREATE and cleared before the HWND is destroyed.
// Invariants: UI thread only. Handlers may throw (allocation); dispatch() catches everything so
//   no exception crosses the OS boundary.
#pragma once

#include <windows.h>

#include "r1ui/platform/BoundedQueue.h"
#include "r1ui/platform/Utf.h"
#include "r1ui/platform/Window.h"

namespace r1ui::platform {

struct Window::Impl {
  HWND hwnd = nullptr;

  // Creation options (immutable after construction except needsConfirm).
  bool borderless = false;
  bool resizable = true;
  Size minClientSize{0, 0};  // 0 = none
  bool minSizeLogical = false;
  bool needsConfirm = false;
  bool altTapOpensSystemMenu = false;

  // State fed by messages.
  int width = 0;
  int height = 0;
  bool resized = true;  // first frame always sees a size
  bool closed = false;
  bool escape = false;
  float mouseX = 0.0f;
  float mouseY = 0.0f;
  UINT dpi = kBaseDpi;
  CursorShape cursor = CursorShape::Arrow;
  int buttonsDown = 0;  // buttons currently held; capture lasts until it reaches zero
  bool trackingClientLeave = false;
  bool trackingNonClientLeave = false;
  CodePointAssembler chars;
  ChromeLayout chrome;
  HitZone chromePressed = HitZone::Client;  // chrome button pressed and awaiting release
  std::function<void()> live;               // see Window::setLiveCallback
  bool inSizeMove = false;                  // the OS move/size loop is running
  bool inLive = false;                      // live callback active (never re-entered)
  bool keepCapture = false;                 // setVisible(false) is moving the capture: WM_CAPTURECHANGED is not a loss

  BoundedQueue<KeyEvent> keyQueue{kMaxQueuedEvents};
  BoundedQueue<MouseClick> clickQueue{kMaxQueuedEvents};
  BoundedQueue<MouseEvent> mouseQueue{kMaxQueuedEvents};
  BoundedQueue<Event> events{kMaxQueuedWindowEvents, &eventRank};

  Impl() = default;
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  ~Impl();

  static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  LRESULT dispatch(UINT msg, WPARAM wp, LPARAM lp) noexcept;

  // WindowProc.cpp: state, focus, keyboard and pointer messages.
  bool handleState(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result);
  bool handleKeyboard(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result);
  bool handlePointer(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result);
  Event makeEvent(EventType type) const;
  void pushEvent(const Event& event);
  void pushMouse(MouseEvent::Type type, MouseButton button, float x, float y);
  void buttonChange(MouseButton button, bool down, float x, float y);
  void armLeaveTracking(bool nonClient);
  bool pointerOverWindow() const;
  Point screenToClient(LPARAM screenPoint) const;
  Size clientSize() const;
  void runLive();

  // WindowChrome.cpp: frame, min size, hit-testing and chrome buttons.
  bool handleFrame(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result);
  HitZone zoneAt(Point clientPoint) const;
  void finishChromePress(HitZone pressed, Point clientPoint);
  void runChromeCommand(HitZone zone);
};

// System cursor resource for a shape (WindowProc.cpp).
const wchar_t* cursorResource(CursorShape shape);

// Dpi of a monitor (effective, per-monitor); kBaseDpi if the query fails.
UINT monitorDpi(HMONITOR monitor);

}  // namespace r1ui::platform

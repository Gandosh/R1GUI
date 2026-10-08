// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the platform-neutral window abstraction (create, pump events, size, mouse, native handle).
// Why: the rest of the toolkit never sees Win32 types; macOS/Linux backends implement this header.
// Callers: ui-render (surface creation via nativeHandle), examples/preview.
// Calls: the per-OS backend in win32/Window.cpp.
// Invariants: all calls happen on the thread that created the window (the UI thread).
#pragma once

#include <memory>
#include <string>

namespace r1ui::platform {

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
  void setTitle(const std::string& utf8Title);
  NativeHandle nativeHandle() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace r1ui::platform

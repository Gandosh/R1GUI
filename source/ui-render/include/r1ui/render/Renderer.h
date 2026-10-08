// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the public Vulkan renderer facade for one window (device, swapchain, frame loop).
// Why: keeps every Vulkan type out of public headers so other modules include only this surface.
// Callers: examples/preview and later the toolkit shell. Calls: Vulkan loader via Renderer.cpp.
// Invariants: single-threaded, owned by the UI thread; the Window must outlive the Renderer.
// Phase 0 scope: clears the window to a colour; UI drawing arrives in Phase 3.
#pragma once

#include <memory>

#include "r1ui/platform/Window.h"

namespace r1ui::render {

struct ClearColor {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
};

class Renderer {
 public:
  // Throws std::runtime_error with the failing Vulkan call if setup cannot complete.
  explicit Renderer(const platform::Window& window);
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  // Renders one frame at the window's current size. A zero-sized (minimized) window is a no-op.
  // Handles swapchain recreation on resize; throws on unrecoverable device errors.
  void drawFrame(const platform::Window& window, ClearColor color);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace r1ui::render

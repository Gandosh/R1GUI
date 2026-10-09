// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the presentation surface and swapchain of one OS window, plus its per-frame-in-flight
//   resources (command buffers, instance ring buffers, acquire semaphores).
// Why: every toolkit window (main window and floating panels) is one WindowTarget on the shared
//   RenderDevice; each recreates its own swapchain independently.
// Callers: application shell, Renderer facade, tests. Calls: Window::nativeHandle(),
//   clientWidth(), clientHeight() only.
// Lifetime: the Window and the RenderDevice must outlive the WindowTarget; destroy the target
//   before the window. The destructor waits for the GPU (it never throws).
// Resize: beginFrame compares the window's client size with the size the swapchain was built for
//   and recreates it on change; VK_ERROR_OUT_OF_DATE / SUBOPTIMAL also trigger a recreate. A
//   recreate stalls the GPU (vkDeviceWaitIdle) once; this is the documented cost of a resize.
//   A zero-sized (minimized) window makes beginFrame return false.
// Present mode: FIFO is always available and is the default. Mailbox / Immediate are used when
//   the surface offers them, otherwise FIFO; presentMode() reports the effective mode.
#pragma once

#include <memory>

#include "r1ui/platform/Window.h"
#include "r1ui/render/RenderDevice.h"
#include "r1ui/render/RenderTarget.h"

namespace r1ui::render {

enum class PresentMode { Fifo, Mailbox, Immediate };

struct WindowTargetOptions {
  PresentMode presentMode = PresentMode::Fifo;
};

class WindowTarget final : public RenderTarget {
 public:
  // Throws std::runtime_error if the surface cannot present on the device's queue or the
  // swapchain cannot be created (nothing is leaked in that case).
  WindowTarget(RenderDevice& device, const platform::Window& window, const WindowTargetOptions& options = {});
  ~WindowTarget() override;

  bool beginFrame(const Color& clear) override;
  bool endFrame() override;
  uint32_t width() const override;   // swapchain extent
  uint32_t height() const override;

  // Marks the swapchain stale so the next beginFrame rebuilds it (display or DPI changes).
  void invalidate();
  PresentMode presentMode() const;
  // Number of swapchains built so far (1 after construction).
  uint32_t swapchainGeneration() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace r1ui::render

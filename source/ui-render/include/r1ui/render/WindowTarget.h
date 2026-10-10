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
//   recreate waits (bounded) for this window's own last frames and presents only, never for other
//   windows; the first swapchain of a window waits for nothing.
//   A zero-sized (minimized) window makes beginFrame return false.
// Waits: no call blocks for good. Waiting for a free swapchain image, for the image's fence and for the
//   window's earlier GPU work is bounded (a few ms to one second); when the image does not come back in
//   time endFrame() returns false (the frame is dropped, nothing is presented) and the caller draws
//   again on its next step. After several drops in a row beginFrame() returns false for a short back-off
//   (retryDelayMs() says how long) instead of paying the waits every step; a window the compositor
//   stopped serving (hidden, occluded, not responding) therefore costs next to nothing and resumes by
//   itself. Closing a window waits at most about a second for its last presents and otherwise hands the
//   swapchain to the device, which frees it at its own teardown.
// Present mode: FIFO is always available and is the default. Mailbox / Immediate are used when
//   the surface offers them, otherwise FIFO; presentMode() reports the effective mode.
#pragma once

#include <memory>

#include "r1ui/platform/Window.h"
#include "r1ui/render/RenderDevice.h"
#include "r1ui/render/RenderTarget.h"

namespace r1ui::render {

enum class PresentMode { Fifo, Mailbox, Immediate };

// Where the CPU time of the last completed endFrame() went, in milliseconds: waiting for the GPU
// and the presentation engine (frame slot fence plus image acquire), recording and submitting the
// command buffer, and the present call itself.
struct FrameTimings {
  double waitMs = 0.0;
  double recordSubmitMs = 0.0;
  double presentMs = 0.0;
};

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
  FrameTimings lastFrameTimings() const;
  // Frames dropped since creation because no swapchain image came back in time (see "Waits").
  uint64_t droppedFrames() const;
  // Milliseconds until beginFrame() tries again after repeated drops; 0 when the window is not backing off.
  uint32_t retryDelayMs() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace r1ui::render

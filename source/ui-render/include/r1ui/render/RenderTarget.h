// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the common frame interface of anything the painter can draw into (a window swapchain or
//   an offscreen image): beginFrame -> draw through painter() -> endFrame.
// Why: widgets and tests draw the same way regardless of where the pixels end up.
// Callers: application shell, preview, tests. Implementations: WindowTarget, OffscreenTarget.
// Frame protocol:
//   beginFrame(clear): returns false when nothing can be drawn this frame (window minimized,
//     swapchain being rebuilt); the caller just skips drawing. Returns true and resets the
//     painter() for the current target size. Calling it again before endFrame() restarts the
//     frame (an abandoned frame costs nothing).
//   painter(): valid between a successful beginFrame and endFrame; sized to width() x height().
//   endFrame(): uploads the recorded draws, submits them and (windows) presents. Returns false
//     when the frame had to be skipped (swapchain went out of date between begin and end; the
//     next beginFrame recovers). Throws std::invalid_argument for a bad frame (nothing is
//     submitted, the target stays usable) and DeviceLostError if the GPU is gone. Too many draws
//     in one frame is not an error: see PaintStats::dropped.
#pragma once

#include <cstdint>

#include "r1ui/render/Painter.h"

namespace r1ui::render {

class RenderTarget {
 public:
  virtual ~RenderTarget() = default;
  RenderTarget(const RenderTarget&) = delete;
  RenderTarget& operator=(const RenderTarget&) = delete;

  virtual bool beginFrame(const Color& clear) = 0;
  virtual bool endFrame() = 0;
  Painter& painter() { return painter_; }

  virtual uint32_t width() const = 0;   // physical pixels of the current drawing surface
  virtual uint32_t height() const = 0;

 protected:
  RenderTarget() = default;
  Painter painter_;
};

}  // namespace r1ui::render

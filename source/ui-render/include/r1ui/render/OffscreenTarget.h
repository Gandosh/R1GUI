// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: a window-less render target (one B8G8R8A8 image) with CPU readback.
// Why: GPU tests, thumbnails and screenshots need real shader output without an OS window.
// Callers: tests/ui-render, tools. Same frame protocol as every RenderTarget; endFrame() is
//   synchronous: it returns after the GPU finished and the pixels are readable.
// Pixel order of readPixels(): R, G, B, A bytes, row 0 at the top, tightly packed; the values are
//   the sRGB-encoded framebuffer contents (no conversion), alpha as accumulated by blending.
// Lifetime: the RenderDevice must outlive the target.
#pragma once

#include <memory>
#include <vector>

#include "r1ui/render/RenderDevice.h"
#include "r1ui/render/RenderTarget.h"

namespace r1ui::render {

// Largest side of an offscreen target, bounding readback memory (16384^2 * 4 = 1 GiB is refused).
inline constexpr uint32_t kMaxOffscreenSide = 8192;

class OffscreenTarget final : public RenderTarget {
 public:
  // Throws std::invalid_argument for a zero side or a side above kMaxOffscreenSide.
  OffscreenTarget(RenderDevice& device, uint32_t width, uint32_t height);
  ~OffscreenTarget() override;

  bool beginFrame(const Color& clear) override;
  bool endFrame() override;
  uint32_t width() const override;
  uint32_t height() const override;

  // Contents of the last completed frame as RGBA8. Throws std::logic_error before the first
  // completed frame.
  std::vector<uint8_t> readPixels() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace r1ui::render

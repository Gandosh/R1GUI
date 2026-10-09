// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the Vulkan-backed TextureFactory (atlas textures on a shared RenderDevice) and the frame
//   helper that draws a UiContext into a RenderTarget.
// Why: the widget runtime is GPU-free (TextureFactory interface); this is the one place that binds
//   it to ui-render, so the shell and the visual harness do not repeat the protocol
//   beginFrame -> paint -> finishPaint -> endFrame -> repaint-on-atlas-overflow.
// Callers: the application shell, examples/preview, the visual harness. Calls: ui-render Texture,
//   RenderTarget. Lifetime: the RenderDevice must outlive the factory and every texture it made.
#pragma once

#include <memory>

#include "r1ui/render/RenderDevice.h"
#include "r1ui/render/RenderTarget.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/text/TextureFactory.h"

namespace r1ui::widgets {

class GpuTextureFactory final : public TextureFactory {
 public:
  explicit GpuTextureFactory(render::RenderDevice& device) : device_(device) {}
  std::unique_ptr<AtlasTexture> createCoverage(uint32_t width, uint32_t height) override;

 private:
  render::RenderDevice& device_;
};

// Lays out (when needed) and paints one frame of `ui` into `target` cleared with `clear`. When an
// atlas ran out of room the frame is painted a second time. Returns false when the target could not
// draw (minimised window, swapchain rebuilding): the caller just tries again later.
bool renderFrame(UiContext& ui, render::RenderTarget& target, const render::Color& clear, FrameInfo* info = nullptr);

}  // namespace r1ui::widgets

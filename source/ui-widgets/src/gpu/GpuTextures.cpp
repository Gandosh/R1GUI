// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of GpuTextures.h.
// Callers: the shell, preview, visual harness.
#include "r1ui/widgets/gpu/GpuTextures.h"

#include "r1ui/render/Texture.h"

namespace r1ui::widgets {

namespace {

class GpuAtlasTexture final : public AtlasTexture {
 public:
  GpuAtlasTexture(render::RenderDevice& device, uint32_t w, uint32_t h) : texture_(device, w, h, render::TextureFormat::R8Coverage) {}
  render::TextureRef ref() const override { return texture_.ref(); }
  uint32_t width() const override { return texture_.width(); }
  uint32_t height() const override { return texture_.height(); }
  void update(uint32_t x, uint32_t y, uint32_t w, uint32_t h, std::span<const uint8_t> pixels) override { texture_.update(x, y, w, h, pixels); }

 private:
  render::Texture texture_;
};

}  // namespace

std::unique_ptr<AtlasTexture> GpuTextureFactory::createCoverage(uint32_t width, uint32_t height) {
  return std::make_unique<GpuAtlasTexture>(device_, width, height);
}

bool renderFrame(UiContext& ui, render::RenderTarget& target, const render::Color& clear, FrameInfo* info) {
  FrameInfo frame = ui.frame();
  if (info != nullptr) *info = std::move(frame);
  for (int pass = 0; pass < 2; ++pass) {
    if (!target.beginFrame(clear)) return false;
    ui.paint(target.painter());
    ui.finishPaint();
    const bool presented = target.endFrame();
    if (!ui.consumeRepaint()) return presented;
  }
  return true;
}

}  // namespace r1ui::widgets

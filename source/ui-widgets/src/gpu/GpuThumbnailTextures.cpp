// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of GpuThumbnailTextures.h.
// Invariants: each ThumbnailTexture owns exactly one render::Texture; the pixels are copied by the
//   Texture constructor, so the caller's buffer may be released at once.
// Callers: the ui-widgets-gpu library users.
#include "r1ui/widgets/thumbnailgrid/GpuThumbnailTextures.h"

#include "r1ui/render/Texture.h"

namespace r1ui::widgets {

namespace {

class GpuThumbnail final : public thumbs::ThumbnailTexture {
 public:
  GpuThumbnail(render::RenderDevice& device, uint32_t w, uint32_t h, std::span<const uint8_t> rgba)
      : texture_(device, w, h, render::TextureFormat::Rgba8Srgb, rgba) {}
  render::TextureRef ref() const override { return texture_.ref(); }
  uint32_t width() const override { return texture_.width(); }
  uint32_t height() const override { return texture_.height(); }

 private:
  render::Texture texture_;
};

}  // namespace

std::unique_ptr<thumbs::ThumbnailTexture> GpuThumbnailTextures::create(uint32_t width, uint32_t height, std::span<const uint8_t> rgba) {
  return std::make_unique<GpuThumbnail>(device_, width, height, rgba);
}

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the Vulkan-backed ThumbnailTextureSink: RGBA8 sRGB textures on a shared RenderDevice, one per
//   thumbnail, destroyed through the device's deferred-destruction queue when the cache evicts them.
// Why: the thumbnail cache (ThumbnailCache.h) is GPU-free so it can be tested without Vulkan; this
//   is the one place that binds it to ui-render's Texture API.
// Callers: the application shell, examples/preview, the visual and GPU tests (ui-widgets-gpu library
//   only). Calls: render::Texture.
// Lifetime: the RenderDevice must outlive the sink and every texture it made. Evicting a texture
//   right before endFrame of a frame that drew it would make that endFrame throw (Texture.h): the grid
//   evicts at the start of paint, before it draws, so no frame in progress still references it.
// Failure behaviour: texture creation errors (invalid size, texture limit, Vulkan failure) propagate
//   as exceptions; the cache catches them and keeps the type icon.
#pragma once

#include <memory>

#include "r1ui/render/RenderDevice.h"
#include "r1ui/widgets/thumbnailgrid/ThumbnailCache.h"

namespace r1ui::widgets {

class GpuThumbnailTextures final : public thumbs::ThumbnailTextureSink {
 public:
  explicit GpuThumbnailTextures(render::RenderDevice& device) : device_(device) {}
  std::unique_ptr<thumbs::ThumbnailTexture> create(uint32_t width, uint32_t height, std::span<const uint8_t> rgba) override;

 private:
  render::RenderDevice& device_;
};

}  // namespace r1ui::widgets

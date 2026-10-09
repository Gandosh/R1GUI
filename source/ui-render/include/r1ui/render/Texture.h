// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: GPU textures the painter samples: R8 coverage (glyph atlases) and RGBA8 colour images
//   (icons, bitmaps), created, updated by sub-region through staging memory and destroyed through
//   the device's deferred-destruction queue.
// Why: widgets and the text module need to upload pixels without knowing any Vulkan type, and a
//   texture may be destroyed while the last submitted frame still reads it.
// Callers: ui-text (glyph atlas), the Renderer facade, tests. Produces: TextureRef for Painter.
// Lifetime: a Texture must be destroyed before its RenderDevice. Destroying it right after
//   endFrame() is safe; destroying it between painter.drawTexture() and endFrame() makes that
//   endFrame() throw std::invalid_argument (frame abandoned, target stays usable).
// Upload timing: create/update only record the pixels; they reach the GPU in the command buffer
//   of the next endFrame() (or flushUploads()), ordered before the draw that samples them, so an
//   update followed by a draw in the same frame is always visible. More than 64 MiB of pending
//   uploads forces a synchronous flush. Updates never tear a frame already submitted.
// Limits: at most kMaxTextures live textures per device, each at most 256 MiB.
#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "r1ui/render/Painter.h"
#include "r1ui/render/RenderDevice.h"

namespace r1ui::render {

enum class TextureFormat {
  R8Coverage,  // 1 byte per pixel, coverage 0..255
  Rgba8Srgb    // 4 bytes per pixel R,G,B,A; sRGB-encoded colour, straight alpha, stored UNORM
};

inline constexpr uint32_t kMaxTextures = 4096;

class Texture {
 public:
  // Creates a width x height texture. `pixels` is either empty (the texture starts zeroed) or
  // exactly width*height*bytesPerPixel tightly packed bytes, row 0 at the top. Throws
  // std::invalid_argument for a zero or device-exceeding size, a wrong pixel count or more than
  // kMaxTextures live textures; std::runtime_error on Vulkan failure (nothing is leaked).
  Texture(RenderDevice& device, uint32_t width, uint32_t height, TextureFormat format,
          std::span<const uint8_t> pixels = {});
  ~Texture();
  Texture(Texture&&) noexcept;
  Texture& operator=(Texture&&) noexcept;
  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;

  // Replaces the sub-rectangle (x, y, w, h) with tightly packed pixels. Throws
  // std::invalid_argument when the rectangle is empty, leaves the texture or `pixels` has the
  // wrong size; the texture is unchanged in that case.
  void update(uint32_t x, uint32_t y, uint32_t w, uint32_t h, std::span<const uint8_t> pixels);

  uint32_t width() const;
  uint32_t height() const;
  TextureFormat format() const;
  TextureRef ref() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace r1ui::render

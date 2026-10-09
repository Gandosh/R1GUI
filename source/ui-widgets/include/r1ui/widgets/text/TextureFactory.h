// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the narrow interface through which the text and icon services obtain GPU textures: create a
//   single-channel coverage texture, update a sub-rectangle, and name it to the Painter.
// Why: the widget runtime must build and be tested without Vulkan. The GPU backend
//   (widgets/gpu/GpuTextures.h) implements this with render::Texture; NullTextureFactory records
//   updates in memory so unit tests can run the full paint path (and inspect atlas contents).
// Callers: TextEngine, IconCache (create/update), Services (owns the factory reference).
// Lifetime: a texture must not outlive its factory's backing device. update() copies; it never
//   keeps the span.
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "r1ui/render/Painter.h"

namespace r1ui::widgets {

class AtlasTexture {
 public:
  virtual ~AtlasTexture() = default;
  // Handle for Painter::drawTexture. Constant for the life of the texture.
  virtual render::TextureRef ref() const = 0;
  virtual uint32_t width() const = 0;
  virtual uint32_t height() const = 0;
  // Replaces the sub-rectangle with tightly packed coverage bytes (w * h). Throws
  // std::invalid_argument for a rectangle outside the texture or a wrong byte count.
  virtual void update(uint32_t x, uint32_t y, uint32_t w, uint32_t h, std::span<const uint8_t> pixels) = 0;
};

class TextureFactory {
 public:
  virtual ~TextureFactory() = default;
  // R8 coverage texture of the given size, initially zero.
  virtual std::unique_ptr<AtlasTexture> createCoverage(uint32_t width, uint32_t height) = 0;
};

// In-memory implementation for tests and headless painting. Texture ids start at 0x1000 so they
// never collide with ids of a real device used in the same process.
class NullTextureFactory final : public TextureFactory {
 public:
  std::unique_ptr<AtlasTexture> createCoverage(uint32_t width, uint32_t height) override;
};

// The pixels of a texture created by NullTextureFactory (for tests); empty for any other texture.
std::span<const uint8_t> nullTexturePixels(const AtlasTexture& texture);

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: NullTextureFactory (see TextureFactory.h): CPU-memory coverage textures with unique ids.
// Callers: unit tests, headless runs.
#include "r1ui/widgets/text/TextureFactory.h"

#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace r1ui::widgets {

namespace {

class NullTexture final : public AtlasTexture {
 public:
  NullTexture(uint32_t w, uint32_t h, uint64_t id) : width_(w), height_(h), id_(id), pixels_(size_t{w} * h, 0) {}
  render::TextureRef ref() const override { return {id_, render::TextureKind::Coverage}; }
  uint32_t width() const override { return width_; }
  uint32_t height() const override { return height_; }
  void update(uint32_t x, uint32_t y, uint32_t w, uint32_t h, std::span<const uint8_t> pixels) override {
    if (w == 0 || h == 0 || uint64_t{x} + w > width_ || uint64_t{y} + h > height_ || pixels.size() != size_t{w} * h) {
      throw std::invalid_argument("NullTexture::update: bad rectangle or pixel count");
    }
    for (uint32_t row = 0; row < h; ++row) {
      std::copy_n(pixels.begin() + static_cast<std::ptrdiff_t>(size_t{row} * w), w,
                  pixels_.begin() + static_cast<std::ptrdiff_t>((size_t{y} + row) * width_ + x));
    }
  }
  std::span<const uint8_t> pixels() const { return pixels_; }

 private:
  uint32_t width_;
  uint32_t height_;
  uint64_t id_;
  std::vector<uint8_t> pixels_;
};

}  // namespace

std::unique_ptr<AtlasTexture> NullTextureFactory::createCoverage(uint32_t width, uint32_t height) {
  static std::atomic<uint64_t> nextId{0x1000};
  if (width == 0 || height == 0 || uint64_t{width} * height > (uint64_t{1} << 28)) throw std::invalid_argument("NullTextureFactory: bad size");
  return std::make_unique<NullTexture>(width, height, nextId++);
}

std::span<const uint8_t> nullTexturePixels(const AtlasTexture& texture) {
  const auto* null = dynamic_cast<const NullTexture*>(&texture);
  return null != nullptr ? null->pixels() : std::span<const uint8_t>{};
}

}  // namespace r1ui::widgets

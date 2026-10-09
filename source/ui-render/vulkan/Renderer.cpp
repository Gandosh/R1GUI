// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of the Renderer facade (Renderer.h) on top of RenderDevice, WindowTarget,
//   Texture and Painter.
// Callers: examples/preview. Calls: the public render API only (no Vulkan types here).
// Images: uploadImage swizzles the caller's BGRA pixels to RGBA and creates an Rgba8Srgb texture;
//   ids are slot index + generation, so a stale id is rejected even when its slot is reused.
// Validation: drawFrame checks the content before any GPU work, as it always did, and throws
//   std::invalid_argument for an unknown image id or more than kMaxRects rectangles.
#include "r1ui/render/Renderer.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

#include "r1ui/core/CheckedCast.h"
#include "r1ui/render/Painter.h"
#include "r1ui/render/RenderDevice.h"
#include "r1ui/render/Texture.h"
#include "r1ui/render/WindowTarget.h"

namespace r1ui::render {

namespace {

// One live uploaded image; `generation` changes whenever the slot is freed so stale ids fail.
struct ImageSlot {
  std::unique_ptr<Texture> texture;
  uint32_t generation = 1;
};

Color toColor(const Rgba8& c) { return Color::fromRgba8(c.r, c.g, c.b, c.a); }

}  // namespace

struct Renderer::Impl {
  explicit Impl(const platform::Window& windowRef)
      : window(&windowRef), device(), target(device, windowRef) {}

  const platform::Window* window;
  RenderDevice device;
  WindowTarget target;  // after device; images below are destroyed before both
  std::vector<ImageSlot> images;

  const ImageSlot* find(ImageId id) const {
    if (!id.valid() || id.index >= images.size()) return nullptr;
    const ImageSlot& slot = images[id.index];
    return slot.texture != nullptr && slot.generation == id.generation ? &slot : nullptr;
  }

  // Letterboxes the image into the target: scaled to fit, aspect preserved, centred.
  void drawImage(Painter& painter, const Texture& texture) const {
    const double scale = std::min(static_cast<double>(target.width()) / texture.width(),
                                  static_cast<double>(target.height()) / texture.height());
    const auto fitted = [scale](uint32_t size, uint32_t limit) {
      return std::clamp(static_cast<int32_t>(std::lround(size * scale)), 1, core::checkedCast<int32_t>(limit));
    };
    const int32_t w = fitted(texture.width(), target.width());
    const int32_t h = fitted(texture.height(), target.height());
    const int32_t x = (core::checkedCast<int32_t>(target.width()) - w) / 2;
    const int32_t y = (core::checkedCast<int32_t>(target.height()) - h) / 2;
    painter.drawTexture(texture.ref(), Rect{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)},
                        Rect{0.0f, 0.0f, 1.0f, 1.0f});
  }
};

Renderer::Renderer(const platform::Window& window) : impl_(std::make_unique<Impl>(window)) {}

Renderer::~Renderer() = default;

ImageId Renderer::uploadImage(uint32_t width, uint32_t height, const uint8_t* bgra) {
  if (bgra == nullptr) throw std::invalid_argument("uploadImage: null pixel pointer");
  if (width == 0 || height == 0) throw std::invalid_argument("uploadImage: image size is zero");
  Impl& s = *impl_;
  size_t slotIndex = s.images.size();
  for (size_t i = 0; i < s.images.size(); ++i) {
    if (s.images[i].texture == nullptr) {
      slotIndex = i;
      break;
    }
  }
  if (slotIndex == s.images.size() && s.images.size() >= kMaxImages) {
    throw std::invalid_argument("uploadImage: too many live images");
  }
  // 64-bit product: width and height are below 2^32 each, so this cannot wrap.
  const size_t pixelCount = core::checkedCast<size_t>(uint64_t{width} * height);
  std::vector<uint8_t> rgba(pixelCount * 4);
  for (size_t i = 0; i < pixelCount; ++i) {
    rgba[4 * i + 0] = bgra[4 * i + 2];
    rgba[4 * i + 1] = bgra[4 * i + 1];
    rgba[4 * i + 2] = bgra[4 * i + 0];
    rgba[4 * i + 3] = bgra[4 * i + 3];
  }
  auto texture = std::make_unique<Texture>(s.device, width, height, TextureFormat::Rgba8Srgb, rgba);
  // The slot is only added or filled once everything above has succeeded.
  if (slotIndex == s.images.size()) s.images.emplace_back();
  s.images[slotIndex].texture = std::move(texture);
  return ImageId{core::checkedCast<uint32_t>(slotIndex), s.images[slotIndex].generation};
}

void Renderer::destroyImage(ImageId image) {
  Impl& s = *impl_;
  if (s.find(image) == nullptr) throw std::invalid_argument("destroyImage: unknown or destroyed image");
  ImageSlot& slot = s.images[image.index];
  slot.texture.reset();  // GPU memory is released once the frames using it have completed
  ++slot.generation;
}

void Renderer::drawFrame(const platform::Window& window, const FrameContent& content) {
  Impl& s = *impl_;
  if (&window != s.window) throw std::invalid_argument("drawFrame: window differs from the one the renderer was created for");
  if (content.rects.size() > kMaxRects) throw std::invalid_argument("drawFrame: too many rectangles");
  const ImageSlot* image = nullptr;
  if (content.image.valid()) {
    image = s.find(content.image);
    if (image == nullptr) throw std::invalid_argument("drawFrame: unknown or destroyed image");
  }
  if (!s.target.beginFrame(Color{content.clear.r, content.clear.g, content.clear.b, 1.0f})) return;
  Painter& painter = s.target.painter();
  if (image != nullptr) s.drawImage(painter, *image->texture);
  for (const FillRect& r : content.rects) {
    painter.fillRect(Rect{static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.w), static_cast<float>(r.h)},
                     toColor(r.color));
  }
  s.target.endFrame();
}

}  // namespace r1ui::render

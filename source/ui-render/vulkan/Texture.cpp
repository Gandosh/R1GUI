// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Texture.h: validation, GPU image and descriptor creation, registration
//   with the device, queueing of pixel writes (staged by the device's upload queue) and deferred
//   destruction.
// Callers: ui-text, Renderer facade, tests. Calls: RenderDevice::Impl (DeviceImpl.h).
// Invariants: a texture's first queued upload is a whole-image clear or whole-image copy; the
//   device registry entry exists exactly as long as the Texture object; the GPU objects are
//   handed to the deferred queue (not destroyed inline) because the last frame may sample them.
#include "r1ui/render/Texture.h"

#include <memory>
#include <stdexcept>
#include <string>

#include "DeviceImpl.h"

namespace r1ui::render {

namespace {

constexpr uint64_t kMaxTextureBytes = uint64_t{256} * 1024 * 1024;

uint32_t bytesPerPixel(TextureFormat format) { return format == TextureFormat::R8Coverage ? 1u : 4u; }

VkFormat vkFormat(TextureFormat format) {
  return format == TextureFormat::R8Coverage ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
}

// Allocates the texture's descriptor set. Retired textures keep their sets until the GPU is done
// with them, so a loader that creates and destroys textures faster than frames complete can find
// the pool full while fewer than kMaxTextures textures are alive: reclaim finished sets, then wait
// for the GPU and reclaim again, and only report "too many live textures" if that still fails.
detail::PooledSet allocateSetReclaiming(RenderDevice::Impl& dev, VkImageView view) {
  try {
    return detail::PooledSet(*dev.pipelines, view);
  } catch (const detail::PoolExhausted&) {
  }
  dev.collect();
  try {
    return detail::PooledSet(*dev.pipelines, view);
  } catch (const detail::PoolExhausted&) {
  }
  dev.waitSerial(dev.submitted);
  dev.collect();
  try {
    return detail::PooledSet(*dev.pipelines, view);
  } catch (const detail::PoolExhausted&) {
    throw std::invalid_argument("Texture: too many live textures");
  }
}

}  // namespace

struct Texture::Impl {
  RenderDevice::Impl* device = nullptr;
  std::unique_ptr<detail::TextureState> state;  // stable address: the device registry points at it

  ~Impl() {
    if (state == nullptr) return;
    device->cancelUploads(state->id);
    device->textures.erase(state->id);
    device->retire(std::move(state->gpu));
  }
};

Texture::Texture(RenderDevice& device, uint32_t width, uint32_t height, TextureFormat format,
                 std::span<const uint8_t> pixels)
    : impl_(std::make_unique<Impl>()) {
  RenderDevice::Impl& dev = *device.impl_;
  dev.requireUsable();
  if (width == 0 || height == 0 || width > dev.limits.maxImageDimension2D || height > dev.limits.maxImageDimension2D) {
    throw std::invalid_argument("Texture: size is zero or exceeds the device limit");
  }
  const uint64_t bytes = uint64_t{width} * height * bytesPerPixel(format);  // both < 2^32 * 4: no overflow
  if (bytes > kMaxTextureBytes) throw std::invalid_argument("Texture: texture is too large");
  if (!pixels.empty() && pixels.size() != bytes) {
    throw std::invalid_argument("Texture: pixel data size does not match width * height * bytes per pixel");
  }
  if (dev.textures.size() >= kMaxTextures) throw std::invalid_argument("Texture: too many live textures");

  auto state = std::make_unique<detail::TextureState>();
  state->id = dev.nextTextureId;
  state->width = width;
  state->height = height;
  state->format = format;
  state->gpu.image = detail::GpuImage(dev.device, dev.memory, width, height, vkFormat(format),
                                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, true);
  state->gpu.set = allocateSetReclaiming(dev, state->gpu.image.view());

  detail::PendingUpload upload;
  upload.textureId = state->id;
  if (pixels.empty()) {
    upload.clear = true;
  } else {
    upload.w = width;
    upload.h = height;
  }
  // Everything that can fail has succeeded: register and queue (the state is owned from here on).
  ++dev.nextTextureId;
  dev.textures.emplace(state->id, state.get());
  impl_->device = &dev;
  impl_->state = std::move(state);
  dev.enqueueUpload(std::move(upload), pixels);
}

Texture::~Texture() = default;
Texture::Texture(Texture&&) noexcept = default;
Texture& Texture::operator=(Texture&&) noexcept = default;

void Texture::update(uint32_t x, uint32_t y, uint32_t w, uint32_t h, std::span<const uint8_t> pixels) {
  detail::TextureState& state = *impl_->state;
  RenderDevice::Impl& dev = *impl_->device;
  dev.requireUsable();
  const uint64_t right = uint64_t{x} + w;
  const uint64_t bottom = uint64_t{y} + h;
  if (w == 0 || h == 0 || right > state.width || bottom > state.height) {
    throw std::invalid_argument("Texture::update: rectangle is empty or outside the texture");
  }
  const uint64_t bytes = uint64_t{w} * h * bytesPerPixel(state.format);
  if (pixels.size() != bytes) throw std::invalid_argument("Texture::update: pixel data size does not match the rectangle");

  detail::PendingUpload upload;
  upload.textureId = state.id;
  upload.x = x;
  upload.y = y;
  upload.w = w;
  upload.h = h;
  dev.enqueueUpload(std::move(upload), pixels);
}

uint32_t Texture::width() const { return impl_->state->width; }
uint32_t Texture::height() const { return impl_->state->height; }
TextureFormat Texture::format() const { return impl_->state->format; }

TextureRef Texture::ref() const {
  const detail::TextureState& state = *impl_->state;
  return TextureRef{state.id, state.format == TextureFormat::R8Coverage ? TextureKind::Coverage : TextureKind::Color};
}

}  // namespace r1ui::render

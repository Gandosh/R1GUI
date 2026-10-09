// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of OffscreenTarget.h: one colour image, its framebuffer, a host-visible
//   readback buffer and the synchronous begin/end frame protocol.
// Callers: tests/ui-render, tools. Calls: RenderDevice::Impl and FrameRecorder.
// Sync: one frame slot; endFrame waits for the submission, so the readback buffer is stable as
//   soon as it returns and the next frame can reuse the same image safely.
#include "r1ui/render/OffscreenTarget.h"

#include <stdexcept>

#include "FrameRecorder.h"
#include "r1ui/core/CheckedCast.h"

namespace r1ui::render {

struct OffscreenTarget::Impl {
  explicit Impl(RenderDevice::Impl& device) : dev(device) {}
  ~Impl() {
    if (dev.device != VK_NULL_HANDLE) vkDeviceWaitIdle(dev.device);
    if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(dev.device, framebuffer, nullptr);
  }

  RenderDevice::Impl& dev;
  uint32_t width = 0;
  uint32_t height = 0;
  detail::GpuImage image;
  detail::GpuBuffer readback;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  std::unique_ptr<detail::FrameSlots> slots;
  Color clear;
  bool recording = false;
  bool hasFrame = false;

  void init(uint32_t w, uint32_t h) {
    width = w;
    height = h;
    image = detail::GpuImage(dev.device, dev.memory, w, h, detail::kTargetFormat,
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, true);
    const VkDeviceSize bytes = VkDeviceSize{w} * h * 4;
    try {
      readback = detail::GpuBuffer(dev.device, dev.memory, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                       VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                                   true);
    } catch (const DeviceLostError&) {
      throw;
    } catch (const std::runtime_error&) {  // no cached host memory type: plain coherent is fine
      readback = detail::GpuBuffer(dev.device, dev.memory, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    }
    VkImageView view = image.view();
    VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbInfo.renderPass = dev.pipelines->offscreenPass();
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &view;
    fbInfo.width = w;
    fbInfo.height = h;
    fbInfo.layers = 1;
    dev.vk(vkCreateFramebuffer(dev.device, &fbInfo, nullptr, &framebuffer), "vkCreateFramebuffer");
    slots = std::make_unique<detail::FrameSlots>(dev, 1);
  }
};

OffscreenTarget::OffscreenTarget(RenderDevice& device, uint32_t width, uint32_t height)
    : impl_(std::make_unique<Impl>(*device.impl_)) {
  if (width == 0 || height == 0 || width > kMaxOffscreenSide || height > kMaxOffscreenSide) {
    throw std::invalid_argument("OffscreenTarget: side is zero or above kMaxOffscreenSide");
  }
  impl_->dev.requireUsable();
  impl_->init(width, height);
}

OffscreenTarget::~OffscreenTarget() = default;

uint32_t OffscreenTarget::width() const { return impl_->width; }
uint32_t OffscreenTarget::height() const { return impl_->height; }

bool OffscreenTarget::beginFrame(const Color& clear) {
  impl_->dev.requireUsable();
  impl_->dev.collect();
  impl_->clear = clear;
  painter_.begin(impl_->width, impl_->height);
  impl_->recording = true;
  return true;
}

bool OffscreenTarget::endFrame() {
  Impl& s = *impl_;
  if (!s.recording) throw std::logic_error("OffscreenTarget::endFrame without beginFrame");
  s.recording = false;
  painter_.end();
  const std::vector<VkDescriptorSet> sets = detail::resolveTextures(s.dev, painter_.list());
  detail::FrameSlot& slot = s.slots->at(0);
  s.dev.waitSerial(slot.serial);

  detail::FrameTarget target;
  target.pass = s.dev.pipelines->offscreenPass();
  target.framebuffer = s.framebuffer;
  target.extent = {s.width, s.height};
  target.clear = s.clear;
  const auto copyOut = [&s](VkCommandBuffer cmd) {
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {s.width, s.height, 1};
    vkCmdCopyImageToBuffer(cmd, s.image.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s.readback.handle(), 1, &region);
  };
  const uint64_t serial = detail::renderFrame(s.dev, slot, painter_.list(), sets, target, copyOut, VK_NULL_HANDLE, VK_NULL_HANDLE);
  s.dev.waitSerial(serial);
  s.hasFrame = true;
  return true;
}

std::vector<uint8_t> OffscreenTarget::readPixels() const {
  const Impl& s = *impl_;
  if (!s.hasFrame) throw std::logic_error("OffscreenTarget::readPixels before a completed frame");
  const size_t count = core::checkedCast<size_t>(uint64_t{s.width} * s.height);
  std::vector<uint8_t> rgba(count * 4);
  const auto* src = static_cast<const uint8_t*>(s.readback.mapped());
  for (size_t i = 0; i < count; ++i) {  // B8G8R8A8 -> R8G8B8A8
    rgba[4 * i + 0] = src[4 * i + 2];
    rgba[4 * i + 1] = src[4 * i + 1];
    rgba[4 * i + 2] = src[4 * i + 0];
    rgba[4 * i + 3] = src[4 * i + 3];
  }
  return rgba;
}

}  // namespace r1ui::render

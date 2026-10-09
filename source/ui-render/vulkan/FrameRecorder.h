// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the per-frame-in-flight slots of a target (command buffer, instance ring buffers,
//   completion serial) and the recording/submission of one PaintList into one framebuffer.
// Why: WindowTarget and OffscreenTarget differ only in where the image comes from and what
//   happens after the render pass; everything else (ring upload, pending texture uploads,
//   batches -> draw calls) is shared here.
// Callers: vulkan/WindowTarget.cpp, vulkan/OffscreenTarget.cpp. Consumes: PaintList (painter
//   output). Calls: RenderDevice::Impl (DeviceImpl.h).
// Ring buffers: one pair (sdf instances, textured instances) per slot, host visible and mapped,
//   written linearly from offset 0 every frame. A slot's buffers are only touched after the
//   timeline reached the slot's serial, so the GPU is done with the previous contents. Buffers
//   grow in powers of two up to kMaxInstancesPerFrame, so memory is bounded by
//   2 slots * kMaxInstancesPerFrame * (sizeof(SdfInstance) + sizeof(TexInstance)) per target.
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <vector>

#include "DeviceImpl.h"
#include "r1ui/render/PaintList.h"
#include "r1ui/render/Painter.h"

namespace r1ui::render::detail {

struct FrameSlot {
  VkCommandBuffer commands = VK_NULL_HANDLE;
  uint64_t serial = 0;  // timeline value of the last submission that used this slot
  GpuBuffer sdfBuffer;
  GpuBuffer texBuffer;
  uint32_t sdfCapacity = 0;  // in instances
  uint32_t texCapacity = 0;
};

// Owns the command buffers of `count` slots. The device must be idle when it is destroyed.
class FrameSlots {
 public:
  FrameSlots(RenderDevice::Impl& device, uint32_t count);
  ~FrameSlots();
  FrameSlots(const FrameSlots&) = delete;
  FrameSlots& operator=(const FrameSlots&) = delete;

  FrameSlot& at(uint32_t index) { return slots_[index]; }
  uint32_t count() const { return static_cast<uint32_t>(slots_.size()); }

 private:
  RenderDevice::Impl& device_;
  std::vector<FrameSlot> slots_;
};

struct FrameTarget {
  VkRenderPass pass = VK_NULL_HANDLE;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  VkExtent2D extent{};
  Color clear;
};

// Throws std::invalid_argument when a batch samples a texture that no longer exists; otherwise
// returns the descriptor set of every batch (null for SDF batches), in batch order. No GPU side
// effects, so a target can call it before acquiring a swapchain image.
std::vector<VkDescriptorSet> resolveTextures(RenderDevice::Impl& device, const PaintList& list);

// Records and submits `list` into `target` using `slot`. The caller has waited for slot.serial.
// `afterPass` (optional) records commands after the render pass ends (readback copy). Optional
// binary semaphores are waited at colour-attachment-output / signalled on completion. Returns
// the serial of the submission and stores it in slot.serial.
uint64_t renderFrame(RenderDevice::Impl& device, FrameSlot& slot, const PaintList& list,
                     const std::vector<VkDescriptorSet>& sets, const FrameTarget& target,
                     const std::function<void(VkCommandBuffer)>& afterPass, VkSemaphore waitBinary,
                     VkSemaphore signalBinary);

}  // namespace r1ui::render::detail

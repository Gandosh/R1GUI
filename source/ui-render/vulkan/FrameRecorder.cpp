// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of FrameRecorder.h.
// Callers: WindowTarget.cpp, OffscreenTarget.cpp. Calls: the Vulkan loader.
// Draw model: per batch set the scissor, (re)bind the pipeline/vertex buffer/descriptor when the
//   kind or texture changes, and draw 6 vertices x batch.count instances starting at batch.first.
#include "FrameRecorder.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "r1ui/core/CheckedCast.h"

namespace r1ui::render::detail {

namespace {

constexpr uint32_t kMinRingInstances = 1024;

uint32_t roundUpPow2(uint32_t value) {
  uint32_t result = 1;
  while (result < value) result <<= 1;
  return result;
}

// Makes sure `buffer` can hold `needed` instances of `stride` bytes and copies them in.
void upload(RenderDevice::Impl& device, GpuBuffer& buffer, uint32_t& capacity, const void* data, uint32_t needed,
            size_t stride) {
  if (needed == 0) return;
  if (needed > capacity) {
    const uint32_t wanted = std::min(roundUpPow2(std::max(needed, kMinRingInstances)), kMaxInstancesPerFrame);
    // The slot's previous frame has completed, so the old buffer can be released immediately.
    buffer = GpuBuffer();
    buffer = GpuBuffer(device.device, device.memory, VkDeviceSize{wanted} * stride, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    capacity = wanted;
  }
  std::memcpy(buffer.mapped(), data, core::checkedCast<size_t>(uint64_t{needed} * stride));
}

}  // namespace

FrameSlots::FrameSlots(RenderDevice::Impl& device, uint32_t count) : device_(device), slots_(count) {
  std::vector<VkCommandBuffer> commands(count);
  VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  alloc.commandPool = device_.pool;
  alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc.commandBufferCount = count;
  device_.vk(vkAllocateCommandBuffers(device_.device, &alloc, commands.data()), "vkAllocateCommandBuffers");
  for (uint32_t i = 0; i < count; ++i) slots_[i].commands = commands[i];
}

FrameSlots::~FrameSlots() {
  for (FrameSlot& slot : slots_) {
    if (slot.commands != VK_NULL_HANDLE) vkFreeCommandBuffers(device_.device, device_.pool, 1, &slot.commands);
  }
}

std::vector<VkDescriptorSet> resolveTextures(RenderDevice::Impl& device, const PaintList& list) {
  std::vector<VkDescriptorSet> sets(list.batches.size(), VK_NULL_HANDLE);
  for (size_t i = 0; i < list.batches.size(); ++i) {
    const Batch& batch = list.batches[i];
    if (batch.kind != BatchKind::Textured) continue;
    const auto found = device.textures.find(batch.textureId);
    if (found == device.textures.end()) {
      throw std::invalid_argument("endFrame: a drawn texture was destroyed before the frame ended");
    }
    sets[i] = found->second->gpu.set.handle();
  }
  return sets;
}

uint64_t renderFrame(RenderDevice::Impl& device, FrameSlot& slot, const PaintList& list,
                     const std::vector<VkDescriptorSet>& sets, const FrameTarget& target,
                     const std::function<void(VkCommandBuffer)>& afterPass, VkSemaphore waitBinary,
                     VkSemaphore signalBinary) {
  device.requireUsable();
  if (list.width != target.extent.width || list.height != target.extent.height) {
    throw std::logic_error("renderFrame: the painter was begun for a different target size");
  }
  upload(device, slot.sdfBuffer, slot.sdfCapacity, list.sdf.data(), core::checkedCast<uint32_t>(list.sdf.size()),
         sizeof(SdfInstance));
  upload(device, slot.texBuffer, slot.texCapacity, list.tex.data(), core::checkedCast<uint32_t>(list.tex.size()),
         sizeof(TexInstance));

  std::vector<PendingUpload> uploads = device.takeUploads();
  VkCommandBuffer cmd = slot.commands;
  device.vk(vkResetCommandBuffer(cmd, 0), "vkResetCommandBuffer");
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  device.vk(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer");
  device.recordUploads(cmd, uploads);

  VkClearValue clear{};
  clear.color.float32[0] = target.clear.r;
  clear.color.float32[1] = target.clear.g;
  clear.color.float32[2] = target.clear.b;
  clear.color.float32[3] = target.clear.a;
  VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  pass.renderPass = target.pass;
  pass.framebuffer = target.framebuffer;
  pass.renderArea = {{0, 0}, target.extent};
  pass.clearValueCount = 1;
  pass.pClearValues = &clear;
  vkCmdBeginRenderPass(cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
  const VkViewport viewport{0.0f, 0.0f, static_cast<float>(target.extent.width), static_cast<float>(target.extent.height), 0.0f, 1.0f};
  vkCmdSetViewport(cmd, 0, 1, &viewport);
  const float size[2] = {static_cast<float>(target.extent.width), static_cast<float>(target.extent.height)};

  const IRect bounds{0, 0, core::checkedCast<int32_t>(target.extent.width), core::checkedCast<int32_t>(target.extent.height)};
  bool kindBound = false;
  BatchKind boundKind = BatchKind::Sdf;
  VkDescriptorSet boundSet = VK_NULL_HANDLE;
  const VkDeviceSize zeroOffset = 0;
  for (size_t i = 0; i < list.batches.size(); ++i) {
    const Batch& batch = list.batches[i];
    const IRect clipped = intersect(batch.scissor, bounds);
    if (clipped.empty()) continue;
    const VkRect2D scissor{{clipped.x, clipped.y}, {core::checkedCast<uint32_t>(clipped.w), core::checkedCast<uint32_t>(clipped.h)}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    if (!kindBound || boundKind != batch.kind) {
      const bool isSdf = batch.kind == BatchKind::Sdf;
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, isSdf ? device.pipelines->sdf() : device.pipelines->textured());
      const VkBuffer vertices = isSdf ? slot.sdfBuffer.handle() : slot.texBuffer.handle();
      vkCmdBindVertexBuffers(cmd, 0, 1, &vertices, &zeroOffset);
      vkCmdPushConstants(cmd, isSdf ? device.pipelines->sdfLayout() : device.pipelines->texturedLayout(),
                         VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(size), size);
      kindBound = true;
      boundKind = batch.kind;
      boundSet = VK_NULL_HANDLE;
    }
    if (batch.kind == BatchKind::Textured && sets[i] != boundSet) {
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, device.pipelines->texturedLayout(), 0, 1, &sets[i], 0, nullptr);
      boundSet = sets[i];
    }
    vkCmdDraw(cmd, 6, batch.count, 0, batch.first);
  }
  vkCmdEndRenderPass(cmd);
  if (afterPass) afterPass(cmd);
  device.vk(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");

  const uint64_t serial = device.submit(cmd, waitBinary, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, signalBinary);
  slot.serial = serial;
  device.retireUploads(std::move(uploads));
  return serial;
}

}  // namespace r1ui::render::detail

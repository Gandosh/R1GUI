// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the device's queue of pending texture writes: enqueue, cancel on texture destruction,
//   recording into a command buffer (layout transitions plus clear/copy) and the synchronous
//   flush used when too much is pending.
// Why: Texture::create/update only stage pixels; the copies ride the next frame's command buffer
//   so glyph atlas updates cost no extra submission and are ordered before the draws using them.
// Callers: Texture.cpp, FrameRecorder.cpp, RenderDevice::flushUploads. Calls: the Vulkan loader.
// Invariants: the first recorded upload of a texture is a whole-image clear or a whole-image copy
//   (Texture.cpp guarantees this), so UNDEFINED -> TRANSFER_DST never discards needed data; after
//   recording, every touched texture is in SHADER_READ_ONLY_OPTIMAL.
#include <algorithm>
#include <utility>

#include "DeviceImpl.h"
#include "r1ui/core/CheckedCast.h"

namespace r1ui::render {

namespace {

void transition(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to,
                VkPipelineStageFlags srcStage, VkAccessFlags srcAccess, VkPipelineStageFlags dstStage,
                VkAccessFlags dstAccess) {
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout = from;
  barrier.newLayout = to;
  barrier.srcAccessMask = srcAccess;
  barrier.dstAccessMask = dstAccess;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(commands, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

// Frees a one-time command buffer on every exit path.
struct OneTimeCommands {
  VkDevice device;
  VkCommandPool pool;
  VkCommandBuffer commands = VK_NULL_HANDLE;
  ~OneTimeCommands() {
    if (commands != VK_NULL_HANDLE) vkFreeCommandBuffers(device, pool, 1, &commands);
  }
};

}  // namespace

void RenderDevice::Impl::enqueueUpload(detail::PendingUpload&& upload) {
  const uint64_t bytes = upload.bytes;
  if (pendingBytes + bytes > kMaxPendingUploadBytes && !pending.empty()) flushUploads();
  pendingBytes += bytes;
  pending.push_back(std::move(upload));
}

void RenderDevice::Impl::cancelUploads(uint64_t textureId) {
  std::erase_if(pending, [textureId](const detail::PendingUpload& u) { return u.textureId == textureId; });
}

std::vector<detail::PendingUpload> RenderDevice::Impl::takeUploads() {
  pendingBytes = 0;
  return std::exchange(pending, {});
}

void RenderDevice::Impl::recordUploads(VkCommandBuffer commands, std::vector<detail::PendingUpload>& uploads) {
  for (detail::PendingUpload& upload : uploads) {
    const auto found = textures.find(upload.textureId);
    if (found == textures.end()) continue;  // destroyed after being queued; cancelUploads normally removes it
    detail::TextureState& texture = *found->second;
    VkImage image = texture.gpu.image.handle();
    if (texture.gpuInitialized) {
      transition(commands, image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_ACCESS_TRANSFER_WRITE_BIT);
    } else {
      transition(commands, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    }
    if (upload.clear) {
      const VkClearColorValue zero{};
      const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      vkCmdClearColorImage(commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
    } else {
      VkBufferImageCopy copy{};
      copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      copy.imageOffset = {core::checkedCast<int32_t>(upload.x), core::checkedCast<int32_t>(upload.y), 0};
      copy.imageExtent = {upload.w, upload.h, 1};
      vkCmdCopyBufferToImage(commands, upload.staging.handle(), image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    }
    transition(commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
               VK_ACCESS_SHADER_READ_BIT);
    texture.gpuInitialized = true;
  }
}

void RenderDevice::Impl::retireUploads(std::vector<detail::PendingUpload>&& uploads) {
  for (detail::PendingUpload& upload : uploads) {
    if (upload.staging.valid()) retire(std::move(upload.staging));
  }
  uploads.clear();
}

void RenderDevice::Impl::flushUploads() {
  requireUsable();
  std::vector<detail::PendingUpload> uploads = takeUploads();
  if (uploads.empty()) return;
  OneTimeCommands once{device, pool};
  VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  alloc.commandPool = pool;
  alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc.commandBufferCount = 1;
  vk(vkAllocateCommandBuffers(device, &alloc, &once.commands), "vkAllocateCommandBuffers");
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vk(vkBeginCommandBuffer(once.commands, &begin), "vkBeginCommandBuffer");
  recordUploads(once.commands, uploads);
  vk(vkEndCommandBuffer(once.commands), "vkEndCommandBuffer");
  const uint64_t serial = submit(once.commands, VK_NULL_HANDLE, 0, VK_NULL_HANDLE);
  waitSerial(serial);
  retireUploads(std::move(uploads));
  collect();
}

}  // namespace r1ui::render

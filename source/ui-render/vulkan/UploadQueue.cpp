// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the device's queue of pending texture writes: enqueue (staging the pixels), cancel on
//   texture destruction, recording into a command buffer (layout transitions plus clear/copy),
//   restoring a batch whose submission failed, and the synchronous flush used when too much is
//   pending.
// Why: Texture::create/update only stage pixels; the copies ride the next frame's command buffer
//   so glyph atlas updates cost no extra submission and are ordered before the draws using them.
// Callers: Texture.cpp, FrameRecorder.cpp, RenderDevice::flushUploads. Calls: the Vulkan loader.
// Staging: pixels of all queued writes are bump-allocated from shared 1 MiB host-visible chunks (a
//   bigger single write gets a chunk of its own). The chunks of a submitted batch go through the
//   deferred queue and come back to a small free list, so steady-state uploads allocate nothing.
// Invariants: the first recorded upload of a texture is a whole-image clear or a whole-image copy
//   (Texture.cpp guarantees this), so UNDEFINED -> TRANSFER_DST never discards needed data; after
//   recording, every touched texture is in SHADER_READ_ONLY_OPTIMAL. A batch that fails before its
//   submission is restored whole (writes, chunks and the "first write recorded" flags).
#include <algorithm>
#include <cstring>
#include <iterator>
#include <span>
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

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
  return (value + alignment - 1) / alignment * alignment;
}

// Owner of a submitted staging chunk inside the deferred queue: when the GPU is done and the queue
// drops it, the chunk goes back to the device's free list (or is freed if it is not worth keeping).
class ChunkLease {
 public:
  ChunkLease(RenderDevice::Impl& device, detail::StagingChunk&& chunk) : device_(&device), chunk_(std::move(chunk)) {}
  ChunkLease(ChunkLease&& other) noexcept : device_(std::exchange(other.device_, nullptr)), chunk_(std::move(other.chunk_)) {}
  ChunkLease(const ChunkLease&) = delete;
  ChunkLease& operator=(const ChunkLease&) = delete;
  ChunkLease& operator=(ChunkLease&&) = delete;
  ~ChunkLease() {
    if (device_ != nullptr) device_->recycleChunk(std::move(chunk_));
  }

 private:
  RenderDevice::Impl* device_;
  detail::StagingChunk chunk_;
};

}  // namespace

void RenderDevice::Impl::enqueueUpload(detail::PendingUpload&& upload, std::span<const uint8_t> pixels) {
  const uint64_t bytes = pixels.size();
  if ((pendingBytes + bytes > kMaxPendingUploadBytes || pending.size() >= kMaxPendingUploads) && !pending.empty()) {
    flushUploads();
  }
  if (!pixels.empty()) {
    // Find room in the current chunk, else take a recycled one or allocate a new one.
    const VkDeviceSize alignment = std::max<VkDeviceSize>(16, limits.optimalBufferCopyOffsetAlignment);
    detail::StagingChunk* chunk = activeChunks.empty() ? nullptr : &activeChunks.back();
    VkDeviceSize offset = chunk != nullptr ? alignUp(chunk->used, alignment) : 0;
    if (chunk == nullptr || offset + bytes > chunk->capacity) {
      detail::StagingChunk fresh;
      if (bytes <= kStagingChunkBytes && !freeChunks.empty()) {
        fresh = std::move(freeChunks.back());
        freeChunks.pop_back();
        fresh.used = 0;
      } else {
        fresh.capacity = std::max(kStagingChunkBytes, bytes);
        fresh.buffer = detail::GpuBuffer(device, memory, fresh.capacity, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
      }
      try {
        activeChunks.push_back(std::move(fresh));
      } catch (...) {
        recycleChunk(std::move(fresh));
        throw;
      }
      chunk = &activeChunks.back();
      offset = 0;
    }
    std::memcpy(static_cast<uint8_t*>(chunk->buffer.mapped()) + offset, pixels.data(), core::checkedCast<size_t>(bytes));
    chunk->used = offset + bytes;
    upload.stagingBuffer = chunk->buffer.handle();
    upload.stagingOffset = offset;
  }
  upload.bytes = bytes;
  pending.push_back(std::move(upload));
  pendingBytes += bytes;
}

void RenderDevice::Impl::recycleChunk(detail::StagingChunk&& chunk) noexcept {
  // freeChunks is reserved to kMaxFreeChunks, so the push cannot allocate.
  if (chunk.buffer.valid() && chunk.capacity == kStagingChunkBytes && freeChunks.size() < kMaxFreeChunks) {
    chunk.used = 0;
    freeChunks.push_back(std::move(chunk));
  }
}

void RenderDevice::Impl::cancelUploads(uint64_t textureId) {
  std::erase_if(pending, [textureId](const detail::PendingUpload& u) { return u.textureId == textureId; });
}

detail::UploadBatch RenderDevice::Impl::takeUploads() {
  detail::UploadBatch batch;
  batch.uploads = std::exchange(pending, {});
  batch.chunks = std::exchange(activeChunks, {});
  pendingBytes = 0;
  return batch;
}

// Puts a batch that was not submitted back in front of anything queued since, and forgets the
// "first write recorded" flags it set, so the retry starts from the real GPU state.
void RenderDevice::Impl::restoreUploads(detail::UploadBatch&& batch) noexcept {
  for (const uint64_t id : batch.newlyInitialized) {
    const auto found = textures.find(id);
    if (found != textures.end()) found->second->gpuInitialized = false;
  }
  batch.newlyInitialized.clear();
  uint64_t bytes = 0;
  for (const detail::PendingUpload& u : batch.uploads) bytes += u.bytes;
  try {
    if (pending.empty()) {
      pending.swap(batch.uploads);
    } else {
      pending.insert(pending.begin(), std::make_move_iterator(batch.uploads.begin()),
                     std::make_move_iterator(batch.uploads.end()));
    }
    if (activeChunks.empty()) {
      activeChunks.swap(batch.chunks);
    } else {
      activeChunks.insert(activeChunks.begin(), std::make_move_iterator(batch.chunks.begin()),
                          std::make_move_iterator(batch.chunks.end()));
    }
    pendingBytes += bytes;
  } catch (...) {
    // Out of memory while restoring: these writes are lost (their owners will see stale pixels).
    // Keep the device consistent rather than half-restored.
    pending.clear();
    activeChunks.clear();
    pendingBytes = 0;
  }
}

void RenderDevice::Impl::recordUploads(VkCommandBuffer commands, detail::UploadBatch& batch) {
  for (detail::PendingUpload& upload : batch.uploads) {
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
      copy.bufferOffset = upload.stagingOffset;
      copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      copy.imageOffset = {core::checkedCast<int32_t>(upload.x), core::checkedCast<int32_t>(upload.y), 0};
      copy.imageExtent = {upload.w, upload.h, 1};
      vkCmdCopyBufferToImage(commands, upload.stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    }
    transition(commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
               VK_ACCESS_SHADER_READ_BIT);
    if (!texture.gpuInitialized) {
      batch.newlyInitialized.push_back(upload.textureId);
      texture.gpuInitialized = true;
    }
  }
}

void RenderDevice::Impl::retireUploads(detail::UploadBatch&& batch) {
  for (detail::StagingChunk& chunk : batch.chunks) {
    if (chunk.buffer.valid()) retire(ChunkLease(*this, std::move(chunk)));
  }
  batch.chunks.clear();
  batch.uploads.clear();
}

void RenderDevice::Impl::flushUploads() {
  requireUsable();
  detail::UploadBatch batch = takeUploads();
  if (batch.uploads.empty()) {
    restoreUploads(std::move(batch));  // nothing to write; keeps any staged chunk
    return;
  }
  bool submittedWork = false;
  try {
    OneTimeCommands once{device, pool};
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    vk(vkAllocateCommandBuffers(device, &alloc, &once.commands), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk(vkBeginCommandBuffer(once.commands, &begin), "vkBeginCommandBuffer");
    recordUploads(once.commands, batch);
    vk(vkEndCommandBuffer(once.commands), "vkEndCommandBuffer");
    const uint64_t serial = submit(once.commands, VK_NULL_HANDLE, 0, VK_NULL_HANDLE);
    submittedWork = true;
    waitSerial(serial);
  } catch (...) {
    if (submittedWork) {
      retireUploads(std::move(batch));  // the GPU may still read the staging; free it only when done
    } else {
      restoreUploads(std::move(batch));
    }
    throw;
  }
  retireUploads(std::move(batch));
  collect();
}

}  // namespace r1ui::render

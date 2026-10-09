// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: RAII wrappers for a Vulkan buffer or 2D image (optionally with a colour view) together with
//   its dedicated device memory, memory-type selection, and the VkResult check helper shared by
//   the renderer sources.
// Why: every GPU allocation in ui-render goes through one place that validates sizes, picks a
//   memory type from the device's real properties and frees everything on any exit path.
// Callers: the vulkan/*.cpp files only (internal; never included from public headers).
// Lifetime: a wrapper must be destroyed (or reset) before the VkDevice it was created on, and
//   only after the GPU is done using it; the render device guarantees this by moving wrappers
//   into its deferred-destruction queue (RenderDevice.h) or by waiting for idle.
// Allocation count: one vkAllocateMemory per resource; the resource count is bounded by the
//   frame rings, texture limit and targets, far below maxMemoryAllocationCount (4096 minimum).
//   Texture writes are the exception to "one per resource": they are staged in shared 1 MiB
//   chunks (UploadQueue.cpp), never one allocation per write.
// Failure behavior: constructors throw std::runtime_error after releasing anything partly made.
#pragma once

#include <vulkan/vulkan.h>

#include <string>

namespace r1ui::render::detail {

// Converts a failed VkResult into an exception naming the call; no failure is ignored.
// VK_ERROR_DEVICE_LOST becomes DeviceLostError (RenderDevice.h).
void check(VkResult result, const char* call);

// Index of a memory type allowed by typeBits that has all the required property flags.
// Throws std::runtime_error when the device offers none.
uint32_t findMemoryType(const VkPhysicalDeviceMemoryProperties& properties, uint32_t typeBits,
                        VkMemoryPropertyFlags required);

class GpuBuffer {
 public:
  GpuBuffer() = default;
  // When `mapPersistently` is true the memory must be host visible and stays mapped.
  GpuBuffer(VkDevice device, const VkPhysicalDeviceMemoryProperties& properties, VkDeviceSize size,
            VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryFlags, bool mapPersistently);
  ~GpuBuffer();
  GpuBuffer(GpuBuffer&& other) noexcept;
  GpuBuffer& operator=(GpuBuffer&& other) noexcept;
  GpuBuffer(const GpuBuffer&) = delete;
  GpuBuffer& operator=(const GpuBuffer&) = delete;

  VkBuffer handle() const { return buffer_; }
  void* mapped() const { return mapped_; }
  bool valid() const { return buffer_ != VK_NULL_HANDLE; }

 private:
  void release();
  VkDevice device_ = VK_NULL_HANDLE;
  VkBuffer buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory memory_ = VK_NULL_HANDLE;
  void* mapped_ = nullptr;
};

class GpuImage {
 public:
  GpuImage() = default;
  // Creates a device-local, optimally tiled, single-mip 2D image; `withView` adds a colour view.
  GpuImage(VkDevice device, const VkPhysicalDeviceMemoryProperties& properties, uint32_t width,
           uint32_t height, VkFormat format, VkImageUsageFlags usage, bool withView = false);
  ~GpuImage();
  GpuImage(GpuImage&& other) noexcept;
  GpuImage& operator=(GpuImage&& other) noexcept;
  GpuImage(const GpuImage&) = delete;
  GpuImage& operator=(const GpuImage&) = delete;

  VkImage handle() const { return image_; }
  VkImageView view() const { return view_; }

 private:
  void release();
  VkDevice device_ = VK_NULL_HANDLE;
  VkImage image_ = VK_NULL_HANDLE;
  VkImageView view_ = VK_NULL_HANDLE;
  VkDeviceMemory memory_ = VK_NULL_HANDLE;
};

}  // namespace r1ui::render::detail

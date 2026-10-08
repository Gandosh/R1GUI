// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: RAII wrappers for a Vulkan buffer or 2D image together with its dedicated device memory,
//   memory-type selection, and the VkResult check helper shared by the renderer sources.
// Why: every GPU allocation in ui-render goes through one place that validates sizes, picks a
//   memory type from the device's real properties and frees everything on any exit path.
// Callers: vulkan/Renderer.cpp only (internal; never included from public headers).
// Lifetime: a wrapper must be destroyed (or reset) before the VkDevice it was created on, and
//   only after the GPU is done using it; the renderer guarantees this with fences/idle waits.
// Failure behavior: constructors throw std::runtime_error after releasing anything partly made.
#pragma once

#include <vulkan/vulkan.h>

#include <string>

namespace r1ui::render::detail {

// Converts a failed VkResult into an exception naming the call; no failure is ignored.
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
  // Creates a device-local, optimally tiled, single-mip 2D image.
  GpuImage(VkDevice device, const VkPhysicalDeviceMemoryProperties& properties, uint32_t width,
           uint32_t height, VkFormat format, VkImageUsageFlags usage);
  ~GpuImage();
  GpuImage(GpuImage&& other) noexcept;
  GpuImage& operator=(GpuImage&& other) noexcept;
  GpuImage(const GpuImage&) = delete;
  GpuImage& operator=(const GpuImage&) = delete;

  VkImage handle() const { return image_; }

 private:
  void release();
  VkDevice device_ = VK_NULL_HANDLE;
  VkImage image_ = VK_NULL_HANDLE;
  VkDeviceMemory memory_ = VK_NULL_HANDLE;
};

}  // namespace r1ui::render::detail

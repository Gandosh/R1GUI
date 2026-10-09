// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of GpuResources.h (buffer/image allocation, memory-type choice, release).
// Why: see GpuResources.h. One dedicated vkAllocateMemory per resource; the resource count in
//   this phase is small (a few images plus per-frame palette buffers), so no sub-allocator yet.
// Callers: every vulkan/*.cpp. Calls: the Vulkan loader.
#include "GpuResources.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "r1ui/render/RenderDevice.h"

namespace r1ui::render::detail {

void check(VkResult result, const char* call) {
  if (result == VK_ERROR_DEVICE_LOST) {
    throw DeviceLostError(std::string("Vulkan device lost during ") + call);
  }
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string("Vulkan call failed: ") + call + " (VkResult " +
                             std::to_string(static_cast<int>(result)) + ")");
  }
}

uint32_t findMemoryType(const VkPhysicalDeviceMemoryProperties& properties, uint32_t typeBits,
                        VkMemoryPropertyFlags required) {
  const uint32_t count = std::min(properties.memoryTypeCount, uint32_t{VK_MAX_MEMORY_TYPES});
  for (uint32_t i = 0; i < count; ++i) {
    const bool allowed = (typeBits & (1u << i)) != 0;
    if (allowed && (properties.memoryTypes[i].propertyFlags & required) == required) return i;
  }
  throw std::runtime_error("No suitable Vulkan memory type");
}

// ---- GpuBuffer ------------------------------------------------------------------------

GpuBuffer::GpuBuffer(VkDevice device, const VkPhysicalDeviceMemoryProperties& properties,
                     VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryFlags,
                     bool mapPersistently)
    : device_(device) {
  if (size == 0) throw std::runtime_error("GpuBuffer: zero size");
  try {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device_, &info, nullptr, &buffer_), "vkCreateBuffer");

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device_, buffer_, &requirements);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = findMemoryType(properties, requirements.memoryTypeBits, memoryFlags);
    check(vkAllocateMemory(device_, &alloc, nullptr, &memory_), "vkAllocateMemory");
    check(vkBindBufferMemory(device_, buffer_, memory_, 0), "vkBindBufferMemory");
    if (mapPersistently) {
      check(vkMapMemory(device_, memory_, 0, VK_WHOLE_SIZE, 0, &mapped_), "vkMapMemory");
    }
  } catch (...) {
    release();
    throw;
  }
}

void GpuBuffer::release() {
  if (device_ == VK_NULL_HANDLE) return;
  if (mapped_ != nullptr) vkUnmapMemory(device_, memory_);
  if (buffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, buffer_, nullptr);
  if (memory_ != VK_NULL_HANDLE) vkFreeMemory(device_, memory_, nullptr);
  device_ = VK_NULL_HANDLE;
  buffer_ = VK_NULL_HANDLE;
  memory_ = VK_NULL_HANDLE;
  mapped_ = nullptr;
}

GpuBuffer::~GpuBuffer() { release(); }

GpuBuffer::GpuBuffer(GpuBuffer&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      buffer_(std::exchange(other.buffer_, VK_NULL_HANDLE)),
      memory_(std::exchange(other.memory_, VK_NULL_HANDLE)),
      mapped_(std::exchange(other.mapped_, nullptr)) {}

GpuBuffer& GpuBuffer::operator=(GpuBuffer&& other) noexcept {
  if (this != &other) {
    release();
    device_ = std::exchange(other.device_, VK_NULL_HANDLE);
    buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
    memory_ = std::exchange(other.memory_, VK_NULL_HANDLE);
    mapped_ = std::exchange(other.mapped_, nullptr);
  }
  return *this;
}

// ---- GpuImage -------------------------------------------------------------------------

GpuImage::GpuImage(VkDevice device, const VkPhysicalDeviceMemoryProperties& properties,
                   uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
                   bool withView)
    : device_(device) {
  try {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(device_, &info, nullptr, &image_), "vkCreateImage");

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device_, image_, &requirements);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex =
        findMemoryType(properties, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device_, &alloc, nullptr, &memory_), "vkAllocateMemory");
    check(vkBindImageMemory(device_, image_, memory_, 0), "vkBindImageMemory");
    if (withView) {
      VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      view.image = image_;
      view.viewType = VK_IMAGE_VIEW_TYPE_2D;
      view.format = format;
      view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      check(vkCreateImageView(device_, &view, nullptr, &view_), "vkCreateImageView");
    }
  } catch (...) {
    release();
    throw;
  }
}

void GpuImage::release() {
  if (device_ == VK_NULL_HANDLE) return;
  if (view_ != VK_NULL_HANDLE) vkDestroyImageView(device_, view_, nullptr);
  if (image_ != VK_NULL_HANDLE) vkDestroyImage(device_, image_, nullptr);
  if (memory_ != VK_NULL_HANDLE) vkFreeMemory(device_, memory_, nullptr);
  device_ = VK_NULL_HANDLE;
  image_ = VK_NULL_HANDLE;
  view_ = VK_NULL_HANDLE;
  memory_ = VK_NULL_HANDLE;
}

GpuImage::~GpuImage() { release(); }

GpuImage::GpuImage(GpuImage&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      image_(std::exchange(other.image_, VK_NULL_HANDLE)),
      view_(std::exchange(other.view_, VK_NULL_HANDLE)),
      memory_(std::exchange(other.memory_, VK_NULL_HANDLE)) {}

GpuImage& GpuImage::operator=(GpuImage&& other) noexcept {
  if (this != &other) {
    release();
    device_ = std::exchange(other.device_, VK_NULL_HANDLE);
    image_ = std::exchange(other.image_, VK_NULL_HANDLE);
    view_ = std::exchange(other.view_, VK_NULL_HANDLE);
    memory_ = std::exchange(other.memory_, VK_NULL_HANDLE);
  }
  return *this;
}

}  // namespace r1ui::render::detail

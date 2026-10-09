// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the private state of RenderDevice (Vulkan handles, timeline semaphore, deferred
//   destruction queue, texture registry, pending texture uploads) and the helpers other vulkan/
//   sources use through it: result checking, memory-type choice, submission and retirement.
// Why: WindowTarget, OffscreenTarget, Texture and FrameRecorder all need the same device state but
//   the public headers must stay free of Vulkan types.
// Callers: vulkan/*.cpp only. Never include from public headers.
// Serials: every queue submission signals the timeline semaphore with the next integer. A
//   resource retired when `submitted == n` is freed once the semaphore reaches n, i.e. after all
//   work that could use it has finished (nothing is recorded across calls, so work submitted
//   later cannot reference it).
#pragma once

#include <vulkan/vulkan.h>

#include <concepts>
#include <cstdint>
#include <deque>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "GpuResources.h"
#include "Pipelines.h"
#include "r1ui/render/RenderDevice.h"
#include "r1ui/render/Texture.h"

namespace r1ui::render {

namespace detail {

// GPU side of a texture; destroyed (via retire) after the frames using it finished.
struct TextureGpu {
  GpuImage image;
  PooledSet set;  // declared after image: freed first
};

// Registered with the device while the Texture object lives; the painter refers to it by id.
struct TextureState {
  uint64_t id = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  TextureFormat format = TextureFormat::R8Coverage;
  TextureGpu gpu;
  bool gpuInitialized = false;  // false until its first upload was recorded (layout UNDEFINED)
};

// One recorded texture write waiting for the next command buffer.
struct PendingUpload {
  uint64_t textureId = 0;
  bool clear = false;  // true: zero the whole image; false: copy `staging` into (x, y, w, h)
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t w = 0;
  uint32_t h = 0;
  uint64_t bytes = 0;  // size of `staging`, for the pending-bytes cap
  GpuBuffer staging;
};

// Type-erased owner of a retired resource; destroying the holder frees the resource.
class Retired {
 public:
  template <class T>
    requires(!std::same_as<std::decay_t<T>, Retired>)
  explicit Retired(T&& resource) : holder_(std::make_unique<Holder<std::decay_t<T>>>(std::forward<T>(resource))) {}

 private:
  struct Base {
    virtual ~Base() = default;
  };
  template <class T>
  struct Holder final : Base {
    explicit Holder(T&& value) : value(std::move(value)) {}
    T value;
  };
  std::unique_ptr<Base> holder_;
};

}  // namespace detail

struct RenderDevice::Impl {
  static constexpr uint64_t kMaxPendingUploadBytes = uint64_t{64} * 1024 * 1024;

  Impl() = default;
  ~Impl();
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;

  // Creates everything; on failure the destructor releases whatever exists.
  void init(const DeviceOptions& options);

  // Vulkan state
  VkInstance instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;
  VkPhysicalDeviceMemoryProperties memory{};
  VkPhysicalDeviceLimits limits{};
  GpuInfo info;
  bool validation = false;
  uint32_t validationMessages = 0;
  bool lost = false;
  bool memoryBudget = false;  // VK_EXT_memory_budget enabled (RenderDevice::memoryUsage)
  VkCommandPool pool = VK_NULL_HANDLE;
  VkSemaphore timeline = VK_NULL_HANDLE;
  uint64_t submitted = 0;
  std::unique_ptr<detail::Pipelines> pipelines;

  std::deque<std::pair<uint64_t, detail::Retired>> deferred;
  std::unordered_map<uint64_t, detail::TextureState*> textures;
  uint64_t nextTextureId = 1;
  std::vector<detail::PendingUpload> pending;
  uint64_t pendingBytes = 0;

  // Throws DeviceLostError (and remembers it) or std::runtime_error for a failed result.
  void vk(VkResult result, const char* call);
  // Throws DeviceLostError when the device was lost earlier.
  void requireUsable() const;
  uint32_t memoryType(uint32_t typeBits, VkMemoryPropertyFlags required) const {
    return detail::findMemoryType(memory, typeBits, required);
  }

  // Submits one command buffer. Optional binary semaphores: wait at `waitStage`, signal on finish.
  // Returns the timeline serial of this submission.
  uint64_t submit(VkCommandBuffer commands, VkSemaphore waitBinary, VkPipelineStageFlags waitStage,
                  VkSemaphore signalBinary);
  void waitSerial(uint64_t serial);
  bool serialDone(uint64_t serial) const;

  // Frees `resource` once everything submitted so far has completed.
  template <class T>
  void retire(T&& resource) {
    deferred.emplace_back(submitted, detail::Retired(std::forward<T>(resource)));
  }
  size_t collect();

  // Texture upload queue (see Texture.h, "Upload timing").
  void enqueueUpload(detail::PendingUpload&& upload);
  void cancelUploads(uint64_t textureId);
  std::vector<detail::PendingUpload> takeUploads();
  // Records the barriers and copies for `uploads` into `commands`.
  void recordUploads(VkCommandBuffer commands, std::vector<detail::PendingUpload>& uploads);
  // Hands the staging buffers of submitted uploads to the deferred queue.
  void retireUploads(std::vector<detail::PendingUpload>&& uploads);
  void flushUploads();
};

}  // namespace r1ui::render

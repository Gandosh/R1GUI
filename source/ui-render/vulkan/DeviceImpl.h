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
#include <span>
#include <stdexcept>
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

// One host-visible, persistently mapped block that texture writes are staged in. Many small writes
// share one block (bump allocation), so a burst of glyph updates costs one allocation, not one each.
struct StagingChunk {
  GpuBuffer buffer;
  VkDeviceSize capacity = 0;
  VkDeviceSize used = 0;
};

// One recorded texture write waiting for the next command buffer. Its pixels live in a staging
// chunk owned by the device's current batch (see UploadBatch), not by the upload itself.
struct PendingUpload {
  uint64_t textureId = 0;
  bool clear = false;  // true: zero the whole image; false: copy the staged bytes into (x, y, w, h)
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t w = 0;
  uint32_t h = 0;
  uint64_t bytes = 0;  // staged size, for the pending-bytes cap
  VkBuffer stagingBuffer = VK_NULL_HANDLE;
  VkDeviceSize stagingOffset = 0;
};

// Everything taken from the device queue for one submission: the writes, the staging chunks they
// read from, and the textures whose first write this batch records (rolled back if it fails).
struct UploadBatch {
  std::vector<PendingUpload> uploads;
  std::vector<StagingChunk> chunks;
  std::vector<uint64_t> newlyInitialized;
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
  static constexpr size_t kMaxPendingUploads = 4096;  // a flush is forced beyond this many queued writes
  static constexpr VkDeviceSize kStagingChunkBytes = VkDeviceSize{1} << 20;
  static constexpr size_t kMaxFreeChunks = 4;  // default-sized chunks kept for reuse

  Impl() { freeChunks.reserve(kMaxFreeChunks); }  // reserved so recycling a chunk can never throw
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
  bool swapchainMaintenance1 = false;  // VK_EXT_swapchain_maintenance1 enabled: present fences (WindowTarget)
  VkCommandPool pool = VK_NULL_HANDLE;
  VkSemaphore timeline = VK_NULL_HANDLE;
  uint64_t submitted = 0;
  std::unique_ptr<detail::Pipelines> pipelines;

  std::deque<std::pair<uint64_t, detail::Retired>> deferred;
  // Swapchains whose presents did not finish in time (WindowTarget): kept until the device is torn down.
  std::vector<detail::Retired> abandoned;
  std::unordered_map<uint64_t, detail::TextureState*> textures;
  uint64_t nextTextureId = 1;
  std::vector<detail::PendingUpload> pending;
  std::vector<detail::StagingChunk> activeChunks;  // staging of `pending`; the last one has room left
  std::vector<detail::StagingChunk> freeChunks;    // default-sized chunks ready for reuse
  uint64_t pendingBytes = 0;
  uint64_t staleTextureDraws = 0;  // batches skipped because their texture was destroyed (see RenderDevice)

  // RenderDevice::injectFault state: armed when faultPasses >= 0.
  FaultSite faultSite = FaultSite::SwapchainObject;
  int64_t faultPasses = -1;
  void faultPoint(FaultSite site) {
    if (faultPasses < 0 || site != faultSite) return;
    if (faultPasses-- == 0) throw std::runtime_error("injected render fault");
  }

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
  // Waits at most `timeoutNs` for the serial; false when it was not reached in time.
  bool waitSerialFor(uint64_t serial, uint64_t timeoutNs);
  bool serialDone(uint64_t serial) const;

  // Frees `resource` once everything submitted so far has completed.
  template <class T>
  void retire(T&& resource) {
    deferred.emplace_back(submitted, detail::Retired(std::forward<T>(resource)));
  }
  size_t collect();

  // Texture upload queue (see Texture.h, "Upload timing"). enqueueUpload stages `pixels` (empty for
  // a clear) and queues the write; it throws without queueing anything when that is not possible.
  void enqueueUpload(detail::PendingUpload&& upload, std::span<const uint8_t> pixels);
  void cancelUploads(uint64_t textureId);
  // Takes everything pending. The batch must end in retireUploads (submitted) or restoreUploads
  // (anything failed before the submission), so a failed frame loses no texture write.
  detail::UploadBatch takeUploads();
  // Records the barriers and copies for the batch into `commands`.
  void recordUploads(VkCommandBuffer commands, detail::UploadBatch& batch);
  void restoreUploads(detail::UploadBatch&& batch) noexcept;
  // Hands the staging chunks of a submitted batch to the deferred queue (they are recycled).
  void retireUploads(detail::UploadBatch&& batch);
  void recycleChunk(detail::StagingChunk&& chunk) noexcept;
  void flushUploads();
};

}  // namespace r1ui::render

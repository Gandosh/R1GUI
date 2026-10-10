// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of WindowTarget.h: surface, swapchain (create/recreate/retire), image views,
//   framebuffers, acquire/present synchronisation and the begin/end frame protocol of one window.
// Callers: application shell, Renderer facade, tests. Calls: Window (size, native handle),
//   RenderDevice::Impl and FrameRecorder.
// Image acquisition (the rule this file exists to keep): an image is acquired with a FENCE and the CPU
//   waits for that fence before the frame is submitted, so no GPU batch ever waits for the presentation
//   engine. The device has one queue, executed in order, shared by every window; a batch that waited on
//   an acquire semaphore would block the frames of all other windows behind it, and the compositor, which
//   hands an image back only after it showed a newer frame of some window, could then wait for work that
//   sits behind that batch: a cycle that stopped every present of the process (docs/dev/native-windows.md,
//   "Waits"). Every wait here is bounded; a window whose image does not come back in time drops the frame
//   and backs off (WindowTarget.h).
// Sync: two frame slots (command buffer + rings + timeline serial) and one render-finished semaphore per
//   swapchain image (reused only after the image was acquired again, which means its present is done).
//   With VK_EXT_swapchain_maintenance1 every present also carries a fence of its image, so "this window's
//   last present is done" is known without waiting for the whole device; without the extension the
//   legacy device-wide idle wait is used when a chain is retired.
// Image acquisition happens in endFrame, right before recording, so an abandoned frame (begin without
//   end, or an exception while drawing) holds no swapchain image.
// Failure behavior: if recording/submitting fails after an image was acquired, the swapchain is marked
//   stale, so the next frame starts clean. A swapchain whose per-image objects could not all be built is
//   discarded whole and the target stays stale, so the next beginFrame retries; a half-built chain is
//   never used. A chain the presentation engine does not release in time is abandoned to the device
//   (freed at its teardown) instead of blocking the caller.
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include "FrameRecorder.h"
#include "r1ui/core/CheckedCast.h"
#include "r1ui/render/WindowTarget.h"

namespace r1ui::render {

using detail::kFramesInFlight;

namespace {

using Clock = std::chrono::steady_clock;

// How long one wait for a free swapchain image, for its fence or for this window's earlier GPU work may
// take before the frame is dropped. A healthy wait is one display refresh (a few ms).
constexpr uint64_t kImageWaitNs = 8'000'000;
constexpr uint64_t kGpuWaitNs = 1'000'000'000;
// After this many dropped frames in a row the window is left alone for kBackoff instead of paying the
// bounded waits again on every loop step.
constexpr uint32_t kDropsBeforeBackoff = 4;
constexpr std::chrono::milliseconds kBackoff{100};
// Time given to a chain's last presents to finish before it is abandoned instead of destroyed.
constexpr uint64_t kRetireWaitNs = 1'000'000'000;

// The per-image objects and the swapchain itself, built and released as a unit.
struct Chain {
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  std::vector<VkImage> images;
  std::vector<VkImageView> views;
  std::vector<VkFramebuffer> framebuffers;
  std::vector<VkSemaphore> renderFinished;
  std::vector<VkFence> presentFences;  // one per image, only with swapchain maintenance 1
  std::vector<char> presentArmed;      // the image's fence belongs to a present that may be unfinished

  bool empty() const { return swapchain == VK_NULL_HANDLE; }

  // Frees everything; the GPU must not be using the objects and the presents must be done. The
  // swapchain goes before the semaphores its presents waited on.
  void destroy(VkDevice device) {
    for (VkFramebuffer f : framebuffers) vkDestroyFramebuffer(device, f, nullptr);
    for (VkImageView v : views) vkDestroyImageView(device, v, nullptr);
    if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, swapchain, nullptr);
    for (VkSemaphore s : renderFinished) vkDestroySemaphore(device, s, nullptr);
    for (VkFence f : presentFences) vkDestroyFence(device, f, nullptr);
    *this = Chain();
  }
};

// A chain (and the surface it presents to, and an acquire fence that may still be signalled) whose
// presents did not finish in time. The device keeps it until its own teardown, so closing a window never
// blocks on a compositor that does not answer.
struct Abandoned {
  VkInstance instance = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkFence acquireFence = VK_NULL_HANDLE;
  Chain chain;

  Abandoned() = default;
  Abandoned(const Abandoned&) = delete;
  Abandoned& operator=(const Abandoned&) = delete;
  Abandoned(Abandoned&& o) noexcept
      : instance(o.instance), device(o.device), surface(o.surface), acquireFence(o.acquireFence), chain(std::move(o.chain)) {
    o.surface = VK_NULL_HANDLE;
    o.acquireFence = VK_NULL_HANDLE;
    o.chain = Chain();
  }
  Abandoned& operator=(Abandoned&&) = delete;
  ~Abandoned() {
    chain.destroy(device);
    if (acquireFence != VK_NULL_HANDLE) vkDestroyFence(device, acquireFence, nullptr);
    if (surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance, surface, nullptr);
  }
};

}  // namespace

struct WindowTarget::Impl {
  Impl(RenderDevice::Impl& device, const platform::Window& windowRef, const WindowTargetOptions& opts)
      : dev(device), window(windowRef), options(opts) {}
  ~Impl() { destroyAll(); }

  RenderDevice::Impl& dev;
  const platform::Window& window;
  WindowTargetOptions options;

  VkSurfaceKHR surface = VK_NULL_HANDLE;
  Chain chain;
  VkExtent2D extent{};
  uint32_t requestedWidth = 0;   // window size the current swapchain was built for
  uint32_t requestedHeight = 0;
  PresentMode effectiveMode = PresentMode::Fifo;
  std::unique_ptr<detail::FrameSlots> slots;
  uint32_t frame = 0;
  bool stale = false;
  bool recording = false;
  uint32_t generation = 0;
  Color clear;
  FrameTimings timings;

  // Acquire state: at most one image is acquired but not yet released by the presentation engine.
  VkFence acquireFence = VK_NULL_HANDLE;
  bool imagePending = false;
  uint32_t pendingIndex = 0;

  // Dropped-frame accounting and the back-off after repeated drops.
  uint32_t consecutiveDrops = 0;
  uint64_t totalDrops = 0;
  Clock::time_point retryAt{};

  void init() {
    const platform::NativeHandle handle = window.nativeHandle();
    VkWin32SurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    info.hwnd = static_cast<HWND>(handle.window);
    info.hinstance = static_cast<HINSTANCE>(handle.instance);
    dev.vk(vkCreateWin32SurfaceKHR(dev.instance, &info, nullptr, &surface), "vkCreateWin32SurfaceKHR");
    VkBool32 supported = VK_FALSE;
    dev.vk(vkGetPhysicalDeviceSurfaceSupportKHR(dev.physical, dev.queueFamily, surface, &supported),
           "vkGetPhysicalDeviceSurfaceSupportKHR");
    if (supported != VK_TRUE) throw std::runtime_error("The render device's queue cannot present to this window");

    slots = std::make_unique<detail::FrameSlots>(dev, kFramesInFlight);
    acquireFence = makeFence();

    const uint32_t w = windowWidth();
    const uint32_t h = windowHeight();
    if (w != 0 && h != 0) createSwapchain(w, h);
  }

  uint32_t windowWidth() const { return core::checkedCast<uint32_t>(std::max(window.clientWidth(), 0)); }
  uint32_t windowHeight() const { return core::checkedCast<uint32_t>(std::max(window.clientHeight(), 0)); }

  VkFence makeFence() {
    VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    dev.vk(vkCreateFence(dev.device, &info, nullptr, &fence), "vkCreateFence");
    return fence;
  }

  // ---- retiring a chain -------------------------------------------------------------------------

  // The last serial this window submitted.
  uint64_t lastSerial() {
    uint64_t last = 0;
    for (uint32_t i = 0; slots && i < slots->count(); ++i) last = std::max(last, slots->at(i).serial);
    return last;
  }

  // Waits (bounded) until nothing of `c` can still be in use: this window's own GPU work and its
  // unfinished presents. Without present fences the whole device is waited for, which is the legacy
  // cost; it is only reached when a chain is retired, never while frames are drawn.
  bool quiesce(const Chain& c) {
    if (!dev.waitSerialFor(lastSerial(), kRetireWaitNs)) return false;
    if (!dev.swapchainMaintenance1) {
      dev.vk(vkDeviceWaitIdle(dev.device), "vkDeviceWaitIdle");
      return true;
    }
    std::vector<VkFence> busy;
    for (size_t i = 0; i < c.presentFences.size(); ++i) {
      if (c.presentArmed[i] != 0) busy.push_back(c.presentFences[i]);
    }
    if (busy.empty()) return true;
    const VkResult result = vkWaitForFences(dev.device, core::checkedCast<uint32_t>(busy.size()), busy.data(), VK_TRUE, kRetireWaitNs);
    if (result == VK_TIMEOUT) return false;
    dev.vk(result, "vkWaitForFences");
    return true;
  }

  // Frees `c`, or hands it to the device when its presents do not finish in time.
  void retire(Chain&& c, bool withSurface) {
    if (c.empty()) return;
    if (quiesce(c)) {
      c.destroy(dev.device);
      return;
    }
    Abandoned gone;
    gone.instance = dev.instance;
    gone.device = dev.device;
    gone.chain = std::move(c);
    c = Chain();
    if (withSurface) {
      gone.surface = surface;
      surface = VK_NULL_HANDLE;
    }
    dev.abandoned.emplace_back(std::move(gone));
  }

  // An acquire that is still outstanding belongs to the chain being replaced or destroyed: its fence may
  // be signalled at any time, so it is handed to the device and a fresh one is made when next needed.
  void abandonAcquireFence() {
    if (!imagePending) return;
    Abandoned pending;
    pending.instance = dev.instance;
    pending.device = dev.device;
    pending.acquireFence = acquireFence;
    acquireFence = VK_NULL_HANDLE;
    imagePending = false;
    dev.abandoned.emplace_back(std::move(pending));
  }

  void destroyAll() noexcept {
    if (dev.device != VK_NULL_HANDLE) {
      try {
        abandonAcquireFence();
        retire(std::move(chain), true);
      } catch (...) {
        // The device was lost: what could not be waited for is left to the driver, which frees it with the device.
      }
      chain = Chain();
      slots.reset();
      if (acquireFence != VK_NULL_HANDLE) vkDestroyFence(dev.device, acquireFence, nullptr);
      acquireFence = VK_NULL_HANDLE;
    }
    if (surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(dev.instance, surface, nullptr);
    surface = VK_NULL_HANDLE;
  }

  static VkPresentModeKHR toVk(PresentMode mode) {
    switch (mode) {
      case PresentMode::Mailbox: return VK_PRESENT_MODE_MAILBOX_KHR;
      case PresentMode::Immediate: return VK_PRESENT_MODE_IMMEDIATE_KHR;
      case PresentMode::Fifo: break;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
  }

  // ---- (re)creating the swapchain ---------------------------------------------------------------

  // Fills the per-image objects of `fresh` (images already fetched); throws on failure with whatever
  // was built left in `fresh` for the caller to free.
  void buildImageObjects(Chain& fresh, VkExtent2D size) {
    for (VkImage image : fresh.images) {
      VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      viewInfo.image = image;
      viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format = detail::kTargetFormat;
      viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      VkImageView view = VK_NULL_HANDLE;
      dev.vk(vkCreateImageView(dev.device, &viewInfo, nullptr, &view), "vkCreateImageView");
      fresh.views.push_back(view);

      dev.faultPoint(FaultSite::SwapchainObject);
      VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
      fbInfo.renderPass = dev.pipelines->swapchainPass();
      fbInfo.attachmentCount = 1;
      fbInfo.pAttachments = &view;
      fbInfo.width = size.width;
      fbInfo.height = size.height;
      fbInfo.layers = 1;
      VkFramebuffer fb = VK_NULL_HANDLE;
      dev.vk(vkCreateFramebuffer(dev.device, &fbInfo, nullptr, &fb), "vkCreateFramebuffer");
      fresh.framebuffers.push_back(fb);

      VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
      VkSemaphore sem = VK_NULL_HANDLE;
      dev.vk(vkCreateSemaphore(dev.device, &semInfo, nullptr, &sem), "vkCreateSemaphore");
      fresh.renderFinished.push_back(sem);

      fresh.presentArmed.push_back(0);
      if (dev.swapchainMaintenance1) fresh.presentFences.push_back(makeFence());
    }
  }

  // (Re)creates the swapchain for a window of w x h. Returns false (keeping state stale) when the
  // surface reports a zero extent, which happens while a window is being minimized. The first chain of
  // a window needs no waiting at all; replacing one retires the old chain (see quiesce).
  bool createSwapchain(uint32_t w, uint32_t h) {
    VkSurfaceCapabilitiesKHR caps;
    dev.vk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev.physical, surface, &caps), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    VkExtent2D newExtent = caps.currentExtent;
    if (newExtent.width == 0xFFFFFFFFu) {
      newExtent.width = std::clamp(w, caps.minImageExtent.width, caps.maxImageExtent.width);
      newExtent.height = std::clamp(h, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (newExtent.width == 0 || newExtent.height == 0) {
      stale = true;
      return false;
    }

    uint32_t formatCount = 0;
    dev.vk(vkGetPhysicalDeviceSurfaceFormatsKHR(dev.physical, surface, &formatCount, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    dev.vk(vkGetPhysicalDeviceSurfaceFormatsKHR(dev.physical, surface, &formatCount, formats.data()), "vkGetPhysicalDeviceSurfaceFormatsKHR");
    const bool hasFormat = std::any_of(formats.begin(), formats.end(), [](const VkSurfaceFormatKHR& f) {
      return f.format == detail::kTargetFormat && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    if (!hasFormat) throw std::runtime_error("The surface does not offer B8G8R8A8_UNORM / SRGB_NONLINEAR");

    uint32_t modeCount = 0;
    dev.vk(vkGetPhysicalDeviceSurfacePresentModesKHR(dev.physical, surface, &modeCount, nullptr), "vkGetPhysicalDeviceSurfacePresentModesKHR");
    std::vector<VkPresentModeKHR> modes(modeCount);
    dev.vk(vkGetPhysicalDeviceSurfacePresentModesKHR(dev.physical, surface, &modeCount, modes.data()), "vkGetPhysicalDeviceSurfacePresentModesKHR");
    const bool modeAvailable = std::find(modes.begin(), modes.end(), toVk(options.presentMode)) != modes.end();
    const PresentMode mode = modeAvailable ? options.presentMode : PresentMode::Fifo;

    uint32_t imageCount = caps.minImageCount + 1;
    if (mode == PresentMode::Mailbox) imageCount = std::max(imageCount, 3u);
    if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface;
    info.minImageCount = imageCount;
    info.imageFormat = detail::kTargetFormat;
    info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    info.imageExtent = newExtent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = toVk(mode);
    info.clipped = VK_TRUE;
    info.oldSwapchain = chain.swapchain;
    Chain fresh;
    dev.vk(vkCreateSwapchainKHR(dev.device, &info, nullptr, &fresh.swapchain), "vkCreateSwapchainKHR");

    // Build everything that depends on the new chain into `fresh` first and commit it in one step: a
    // failure part way (out of memory) must not leave `framebuffers` or `renderFinished` shorter than
    // the image count, because endFrame indexes them by acquired image.
    try {
      uint32_t count = 0;
      dev.vk(vkGetSwapchainImagesKHR(dev.device, fresh.swapchain, &count, nullptr), "vkGetSwapchainImagesKHR");
      fresh.images.resize(count);
      dev.vk(vkGetSwapchainImagesKHR(dev.device, fresh.swapchain, &count, fresh.images.data()), "vkGetSwapchainImagesKHR");
      buildImageObjects(fresh, newExtent);
    } catch (...) {
      // Creating a swapchain with `oldSwapchain` retired the old one, so it cannot be used again
      // either: release both now, and the next beginFrame builds a chain from nothing instead of
      // drawing into a half-built or retired one. Nothing was drawn to `fresh`.
      fresh.destroy(dev.device);
      try {
        abandonAcquireFence();
        retire(std::move(chain), false);
      } catch (...) {
        // Device lost: nothing more can be done for the old chain.
      }
      chain = Chain();
      stale = true;
      throw;
    }

    abandonAcquireFence();
    retire(std::move(chain), false);
    chain = std::move(fresh);
    extent = newExtent;
    requestedWidth = w;
    requestedHeight = h;
    effectiveMode = mode;
    stale = false;
    ++generation;
    return true;
  }

  // ---- drawing ----------------------------------------------------------------------------------

  void dropFrame() {
    ++totalDrops;
    if (++consecutiveDrops >= kDropsBeforeBackoff) retryAt = Clock::now() + kBackoff;
  }

  bool backingOff() const { return consecutiveDrops >= kDropsBeforeBackoff && Clock::now() < retryAt; }

  // Returns the index of a swapchain image that may be rendered to now, or nullopt when none became
  // free within the bounded waits (the caller drops the frame). The image is acquired with a fence, never
  // with a semaphore (see the file header); an acquired image that is not usable yet stays pending and
  // is picked up by the next call.
  std::optional<uint32_t> acquireImage() {
    if (!imagePending) {
      if (acquireFence == VK_NULL_HANDLE) acquireFence = makeFence();
      dev.vk(vkResetFences(dev.device, 1, &acquireFence), "vkResetFences");
      uint32_t index = 0;
      const VkResult acquired = vkAcquireNextImageKHR(dev.device, chain.swapchain, kImageWaitNs, VK_NULL_HANDLE, acquireFence, &index);
      if (acquired == VK_TIMEOUT || acquired == VK_NOT_READY) return std::nullopt;
      if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        stale = true;
        return std::nullopt;
      }
      if (acquired == VK_SUBOPTIMAL_KHR) {
        stale = true;  // the image is usable; rebuild on the next frame
      } else {
        dev.vk(acquired, "vkAcquireNextImageKHR");
      }
      imagePending = true;
      pendingIndex = index;
    }
    const VkResult waited = vkWaitForFences(dev.device, 1, &acquireFence, VK_TRUE, kImageWaitNs);
    if (waited == VK_TIMEOUT) return std::nullopt;
    dev.vk(waited, "vkWaitForFences");
    // The image's previous present is over (the image was handed back): rearm its present fence.
    const uint32_t index = pendingIndex;
    if (chain.presentArmed[index] != 0) {
      const VkResult done = vkWaitForFences(dev.device, 1, &chain.presentFences[index], VK_TRUE, kImageWaitNs);
      if (done == VK_TIMEOUT) return std::nullopt;  // still pending: retried with the next frame
      dev.vk(done, "vkWaitForFences");
      dev.vk(vkResetFences(dev.device, 1, &chain.presentFences[index]), "vkResetFences");
      chain.presentArmed[index] = 0;
    }
    imagePending = false;
    return index;
  }
};

WindowTarget::WindowTarget(RenderDevice& device, const platform::Window& window, const WindowTargetOptions& options)
    : impl_(std::make_unique<Impl>(*device.impl_, window, options)) {
  impl_->init();  // a throw destroys impl_, whose destructor releases partial state
}

WindowTarget::~WindowTarget() = default;

uint32_t WindowTarget::width() const { return impl_->extent.width; }
uint32_t WindowTarget::height() const { return impl_->extent.height; }
void WindowTarget::invalidate() { impl_->stale = true; }
PresentMode WindowTarget::presentMode() const { return impl_->effectiveMode; }
uint32_t WindowTarget::swapchainGeneration() const { return impl_->generation; }
FrameTimings WindowTarget::lastFrameTimings() const { return impl_->timings; }
uint64_t WindowTarget::droppedFrames() const { return impl_->totalDrops; }
uint32_t WindowTarget::retryDelayMs() const {
  if (!impl_->backingOff()) return 0;
  const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(impl_->retryAt - Clock::now()).count();
  return static_cast<uint32_t>(std::max<int64_t>(left, 1));
}

bool WindowTarget::beginFrame(const Color& clear) {
  Impl& s = *impl_;
  s.dev.requireUsable();
  s.dev.collect();
  s.recording = false;
  const uint32_t w = s.windowWidth();
  const uint32_t h = s.windowHeight();
  if (w == 0 || h == 0 || s.backingOff()) return false;
  if (s.chain.empty() || s.stale || w != s.requestedWidth || h != s.requestedHeight) {
    if (!s.createSwapchain(w, h)) return false;
  }
  s.clear = clear;
  painter_.begin(s.extent.width, s.extent.height);
  s.recording = true;
  return true;
}

bool WindowTarget::endFrame() {
  Impl& s = *impl_;
  if (!s.recording) throw std::logic_error("WindowTarget::endFrame without a successful beginFrame");
  s.recording = false;
  painter_.end();
  s.dev.requireUsable();
  const auto millis = [](Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
  };
  const Clock::time_point start = Clock::now();
  const std::vector<VkDescriptorSet> sets = detail::resolveTextures(s.dev, painter_.list());

  detail::FrameSlot& slot = s.slots->at(s.frame);
  if (!s.dev.waitSerialFor(slot.serial, kGpuWaitNs)) {
    s.dropFrame();
    return false;
  }
  const std::optional<uint32_t> image = s.acquireImage();
  if (!image) {
    if (!s.stale) s.dropFrame();  // a stale chain is rebuilt next frame, which is not a stall
    return false;
  }
  const uint32_t imageIndex = *image;
  s.consecutiveDrops = 0;
  const Clock::time_point acquiredAt = Clock::now();

  try {
    detail::FrameTarget target;
    target.pass = s.dev.pipelines->swapchainPass();
    target.framebuffer = s.chain.framebuffers[imageIndex];
    target.extent = s.extent;
    target.clear = s.clear;
    detail::renderFrame(s.dev, slot, painter_.list(), sets, target, nullptr, VK_NULL_HANDLE, s.chain.renderFinished[imageIndex]);
  } catch (...) {
    s.stale = true;  // the acquired image is never presented: the chain is rebuilt
    throw;
  }

  const Clock::time_point recordedAt = Clock::now();
  VkSwapchainPresentFenceInfoEXT fenceInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT};
  VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  if (s.dev.swapchainMaintenance1) {
    fenceInfo.swapchainCount = 1;
    fenceInfo.pFences = &s.chain.presentFences[imageIndex];
    present.pNext = &fenceInfo;
  }
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &s.chain.renderFinished[imageIndex];
  present.swapchainCount = 1;
  present.pSwapchains = &s.chain.swapchain;
  present.pImageIndices = &imageIndex;
  const VkResult presented = vkQueuePresentKHR(s.dev.queue, &present);
  if (s.dev.swapchainMaintenance1 && (presented == VK_SUCCESS || presented == VK_SUBOPTIMAL_KHR)) s.chain.presentArmed[imageIndex] = 1;
  if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
    s.stale = true;
  } else {
    s.dev.vk(presented, "vkQueuePresentKHR");
  }
  s.frame = (s.frame + 1) % kFramesInFlight;
  const Clock::time_point end = Clock::now();
  s.timings = {millis(start, acquiredAt), millis(acquiredAt, recordedAt), millis(recordedAt, end)};
  return true;
}

}  // namespace r1ui::render

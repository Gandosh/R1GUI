// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of WindowTarget.h: surface, swapchain (create/recreate), image views,
//   framebuffers, acquire/present synchronisation and the begin/end frame protocol of one window.
// Callers: application shell, Renderer facade, tests. Calls: Window (size, native handle),
//   RenderDevice::Impl and FrameRecorder.
// Sync: two frame slots (command buffer + rings + timeline serial); one acquire semaphore per
//   slot (reused only after the slot's previous submission completed) and one render-finished
//   semaphore per swapchain image so a semaphore is never reused while presentation may wait on it.
// Image acquisition happens in endFrame, right before recording, so an abandoned frame (begin
//   without end, or an exception while drawing) holds no swapchain image.
// Failure behavior: if recording/submitting fails after an image was acquired, the acquire
//   semaphore is replaced and the swapchain marked stale, so the next frame starts clean.
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <vector>

#include "FrameRecorder.h"
#include "r1ui/core/CheckedCast.h"
#include "r1ui/render/WindowTarget.h"

namespace r1ui::render {

using detail::kFramesInFlight;

struct WindowTarget::Impl {
  Impl(RenderDevice::Impl& device, const platform::Window& windowRef, const WindowTargetOptions& opts)
      : dev(device), window(windowRef), options(opts) {}
  ~Impl() { destroyAll(); }

  RenderDevice::Impl& dev;
  const platform::Window& window;
  WindowTargetOptions options;

  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkExtent2D extent{};
  uint32_t requestedWidth = 0;   // window size the current swapchain was built for
  uint32_t requestedHeight = 0;
  PresentMode effectiveMode = PresentMode::Fifo;
  std::vector<VkImage> images;
  std::vector<VkImageView> views;
  std::vector<VkFramebuffer> framebuffers;
  std::vector<VkSemaphore> renderFinished;
  std::array<VkSemaphore, kFramesInFlight> imageAvailable{};
  std::unique_ptr<detail::FrameSlots> slots;
  uint32_t frame = 0;
  bool stale = false;
  bool recording = false;
  uint32_t generation = 0;
  Color clear;
  FrameTimings timings;

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
    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (VkSemaphore& s : imageAvailable) dev.vk(vkCreateSemaphore(dev.device, &semInfo, nullptr, &s), "vkCreateSemaphore");

    const uint32_t w = windowWidth();
    const uint32_t h = windowHeight();
    if (w != 0 && h != 0) createSwapchain(w, h);
  }

  uint32_t windowWidth() const { return core::checkedCast<uint32_t>(std::max(window.clientWidth(), 0)); }
  uint32_t windowHeight() const { return core::checkedCast<uint32_t>(std::max(window.clientHeight(), 0)); }

  // Frees views, framebuffers and per-image semaphores; the GPU must be idle.
  void destroyChainResources() {
    for (VkFramebuffer f : framebuffers) vkDestroyFramebuffer(dev.device, f, nullptr);
    for (VkImageView v : views) vkDestroyImageView(dev.device, v, nullptr);
    for (VkSemaphore s : renderFinished) vkDestroySemaphore(dev.device, s, nullptr);
    framebuffers.clear();
    views.clear();
    renderFinished.clear();
    images.clear();
  }

  void destroyAll() {
    if (dev.device != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(dev.device);
      slots.reset();
      destroyChainResources();
      for (VkSemaphore& s : imageAvailable) {
        if (s != VK_NULL_HANDLE) vkDestroySemaphore(dev.device, s, nullptr);
        s = VK_NULL_HANDLE;
      }
      if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(dev.device, swapchain, nullptr);
      swapchain = VK_NULL_HANDLE;
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

  // (Re)creates the swapchain for a window of w x h. Returns false (keeping state stale) when the
  // surface reports a zero extent, which happens while a window is being minimized.
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
    info.oldSwapchain = swapchain;
    VkSwapchainKHR created = VK_NULL_HANDLE;
    dev.vk(vkCreateSwapchainKHR(dev.device, &info, nullptr, &created), "vkCreateSwapchainKHR");

    // Everything that referenced the old chain must be idle before it is destroyed. A resize
    // therefore stalls the GPU once (documented in WindowTarget.h).
    dev.vk(vkDeviceWaitIdle(dev.device), "vkDeviceWaitIdle");
    destroyChainResources();
    if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(dev.device, swapchain, nullptr);
    swapchain = created;
    extent = newExtent;
    requestedWidth = w;
    requestedHeight = h;
    effectiveMode = mode;
    stale = false;
    ++generation;

    uint32_t count = 0;
    dev.vk(vkGetSwapchainImagesKHR(dev.device, swapchain, &count, nullptr), "vkGetSwapchainImagesKHR");
    images.resize(count);
    dev.vk(vkGetSwapchainImagesKHR(dev.device, swapchain, &count, images.data()), "vkGetSwapchainImagesKHR");
    for (VkImage image : images) {
      VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      viewInfo.image = image;
      viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format = detail::kTargetFormat;
      viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      VkImageView view = VK_NULL_HANDLE;
      dev.vk(vkCreateImageView(dev.device, &viewInfo, nullptr, &view), "vkCreateImageView");
      views.push_back(view);

      VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
      fbInfo.renderPass = dev.pipelines->swapchainPass();
      fbInfo.attachmentCount = 1;
      fbInfo.pAttachments = &view;
      fbInfo.width = extent.width;
      fbInfo.height = extent.height;
      fbInfo.layers = 1;
      VkFramebuffer fb = VK_NULL_HANDLE;
      dev.vk(vkCreateFramebuffer(dev.device, &fbInfo, nullptr, &fb), "vkCreateFramebuffer");
      framebuffers.push_back(fb);

      VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
      VkSemaphore sem = VK_NULL_HANDLE;
      dev.vk(vkCreateSemaphore(dev.device, &semInfo, nullptr, &sem), "vkCreateSemaphore");
      renderFinished.push_back(sem);
    }
    return true;
  }

  // After a failure past image acquisition the acquire semaphore may be signalled and unwaited:
  // replace it and rebuild the chain on the next frame.
  void recoverFromFailedFrame() noexcept {
    stale = true;
    vkDeviceWaitIdle(dev.device);
    vkDestroySemaphore(dev.device, imageAvailable[frame], nullptr);
    imageAvailable[frame] = VK_NULL_HANDLE;
    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(dev.device, &semInfo, nullptr, &imageAvailable[frame]);
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

bool WindowTarget::beginFrame(const Color& clear) {
  Impl& s = *impl_;
  s.dev.requireUsable();
  s.dev.collect();
  s.recording = false;
  const uint32_t w = s.windowWidth();
  const uint32_t h = s.windowHeight();
  if (w == 0 || h == 0) return false;
  if (s.swapchain == VK_NULL_HANDLE || s.stale || w != s.requestedWidth || h != s.requestedHeight) {
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
  using Clock = std::chrono::steady_clock;
  const auto millis = [](Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
  };
  const Clock::time_point start = Clock::now();
  const std::vector<VkDescriptorSet> sets = detail::resolveTextures(s.dev, painter_.list());

  detail::FrameSlot& slot = s.slots->at(s.frame);
  s.dev.waitSerial(slot.serial);
  uint32_t imageIndex = 0;
  const VkResult acquired = vkAcquireNextImageKHR(s.dev.device, s.swapchain, UINT64_MAX, s.imageAvailable[s.frame],
                                                  VK_NULL_HANDLE, &imageIndex);
  if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
    s.stale = true;
    return false;
  }
  if (acquired == VK_SUBOPTIMAL_KHR) {
    s.stale = true;  // the image is usable; rebuild on the next frame
  } else {
    s.dev.vk(acquired, "vkAcquireNextImageKHR");
  }
  const Clock::time_point acquiredAt = Clock::now();

  try {
    detail::FrameTarget target;
    target.pass = s.dev.pipelines->swapchainPass();
    target.framebuffer = s.framebuffers[imageIndex];
    target.extent = s.extent;
    target.clear = s.clear;
    detail::renderFrame(s.dev, slot, painter_.list(), sets, target, nullptr, s.imageAvailable[s.frame],
                        s.renderFinished[imageIndex]);
  } catch (...) {
    s.recoverFromFailedFrame();
    throw;
  }

  const Clock::time_point recordedAt = Clock::now();
  VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &s.renderFinished[imageIndex];
  present.swapchainCount = 1;
  present.pSwapchains = &s.swapchain;
  present.pImageIndices = &imageIndex;
  const VkResult presented = vkQueuePresentKHR(s.dev.queue, &present);
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

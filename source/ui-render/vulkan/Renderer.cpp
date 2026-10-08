// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: Vulkan instance, surface, device, swapchain, per-frame sync and the frame loop for one window.
// Why: Phase 0 proof that a Vulkan window opens, resizes and closes cleanly; the 2D batcher (3.3)
//   and text (3.4) build on this device and frame structure.
// Callers: r1ui::render::Renderer (Renderer.h). Calls: the Vulkan loader (vulkan-1.dll).
// Lifetime: Impl destroys everything it created in reverse order, including after a partial
//   constructor failure (null handles are skipped). All GPU work is idle before teardown.
// Sync: two frames in flight (fence per frame); one render-finished semaphore per swapchain image
//   so a semaphore is never reused while presentation may still wait on it.
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "r1ui/core/CheckedCast.h"
#include "r1ui/render/Renderer.h"

namespace r1ui::render {

namespace {

constexpr uint32_t kFramesInFlight = 2;

// Converts a failed VkResult into an exception naming the call; no failure is ignored.
void check(VkResult result, const char* call) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string("Vulkan call failed: ") + call + " (VkResult " +
                             std::to_string(static_cast<int>(result)) + ")");
  }
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data,
                                             void*) {
  if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    std::fprintf(stderr, "[vulkan] %s\n", data->pMessage);
  }
  return VK_FALSE;
}

}  // namespace

struct Renderer::Impl {
  VkInstance instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;

  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent{};
  std::vector<VkImage> images;
  std::vector<VkSemaphore> renderFinished;  // one per swapchain image

  VkCommandPool pool = VK_NULL_HANDLE;
  std::array<VkCommandBuffer, kFramesInFlight> commands{};
  std::array<VkSemaphore, kFramesInFlight> imageAvailable{};
  std::array<VkFence, kFramesInFlight> inFlight{};
  uint32_t frame = 0;
  bool swapchainStale = false;

  // Setup ------------------------------------------------------------------------------
  void createInstance() {
    std::vector<const char*> extensions = {VK_KHR_SURFACE_EXTENSION_NAME,
                                           VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    std::vector<const char*> layers;
#ifndef NDEBUG
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> available(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, available.data());
    for (const auto& layer : available) {
      if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
      }
    }
#endif
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "R1GUI";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = core::checkedCast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.enabledLayerCount = core::checkedCast<uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    check(vkCreateInstance(&info, nullptr, &instance), "vkCreateInstance");

    if (!layers.empty()) {
      auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
      if (create != nullptr) {
        VkDebugUtilsMessengerCreateInfoEXT m{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        m.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        m.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        m.pfnUserCallback = debugCallback;
        create(instance, &m, nullptr, &messenger);
      }
    }
  }

  void createSurface(const platform::NativeHandle& handle) {
    VkWin32SurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    info.hwnd = static_cast<HWND>(handle.window);
    info.hinstance = static_cast<HINSTANCE>(handle.instance);
    check(vkCreateWin32SurfaceKHR(instance, &info, nullptr, &surface), "vkCreateWin32SurfaceKHR");
  }

  // Picks the first device with a graphics queue that can present; prefers discrete GPUs.
  void pickDevice() {
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices");
    if (count == 0) throw std::runtime_error("No Vulkan physical device found");
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "vkEnumeratePhysicalDevices");

    int bestScore = -1;
    for (VkPhysicalDevice candidate : devices) {
      uint32_t familyCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
      std::vector<VkQueueFamilyProperties> families(familyCount);
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
      for (uint32_t i = 0; i < familyCount; ++i) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present);
        if (present == VK_TRUE && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
          VkPhysicalDeviceProperties props;
          vkGetPhysicalDeviceProperties(candidate, &props);
          const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
          if (score > bestScore) {
            bestScore = score;
            physical = candidate;
            queueFamily = i;
          }
          break;
        }
      }
    }
    if (physical == VK_NULL_HANDLE) throw std::runtime_error("No Vulkan device can present");

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    const char* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = &extension;
    check(vkCreateDevice(physical, &info, nullptr, &device), "vkCreateDevice");
    vkGetDeviceQueue(device, queueFamily, 0, &queue);
  }

  void createFrameResources() {
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily;
    check(vkCreateCommandPool(device, &poolInfo, nullptr, &pool), "vkCreateCommandPool");

    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = kFramesInFlight;
    check(vkAllocateCommandBuffers(device, &alloc, commands.data()), "vkAllocateCommandBuffers");

    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // first wait must not block
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
      check(vkCreateSemaphore(device, &semInfo, nullptr, &imageAvailable[i]), "vkCreateSemaphore");
      check(vkCreateFence(device, &fenceInfo, nullptr, &inFlight[i]), "vkCreateFence");
    }
  }

  // Swapchain --------------------------------------------------------------------------
  void destroyRenderFinished() {
    for (VkSemaphore s : renderFinished) vkDestroySemaphore(device, s, nullptr);
    renderFinished.clear();
  }

  // (Re)creates the swapchain at the given size; the old one is retired after the new exists.
  void createSwapchain(uint32_t width, uint32_t height) {
    VkSurfaceCapabilitiesKHR caps;
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps), "surface caps");

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, formats.data());
    if (formats.empty()) throw std::runtime_error("Surface reports no formats");
    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats) {
      if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        chosen = f;
      }
    }

    VkExtent2D newExtent = caps.currentExtent;
    if (newExtent.width == 0xFFFFFFFFu) {
      newExtent.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
      newExtent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (newExtent.width == 0 || newExtent.height == 0) return;  // minimized: keep the old chain

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface;
    info.minImageCount = imageCount;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = newExtent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // always available; vsync-paced
    info.clipped = VK_TRUE;
    info.oldSwapchain = swapchain;

    VkSwapchainKHR created = VK_NULL_HANDLE;
    check(vkCreateSwapchainKHR(device, &info, nullptr, &created), "vkCreateSwapchainKHR");
    vkDeviceWaitIdle(device);
    destroyRenderFinished();
    if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = created;
    format = chosen.format;
    extent = newExtent;

    uint32_t count = 0;
    check(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr), "vkGetSwapchainImagesKHR");
    images.resize(count);
    check(vkGetSwapchainImagesKHR(device, swapchain, &count, images.data()), "vkGetSwapchainImagesKHR");

    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    renderFinished.resize(count, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < count; ++i) {
      check(vkCreateSemaphore(device, &semInfo, nullptr, &renderFinished[i]), "vkCreateSemaphore");
    }
    swapchainStale = false;
  }

  // Frame ------------------------------------------------------------------------------
  void transition(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                  VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage,
                  VkPipelineStageFlags dstStage) const {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
  }

  void draw(uint32_t width, uint32_t height, ClearColor color) {
    if (width == 0 || height == 0) return;
    if (swapchain == VK_NULL_HANDLE || swapchainStale || width != extent.width ||
        height != extent.height) {
      createSwapchain(width, height);
      if (swapchain == VK_NULL_HANDLE) return;
    }

    check(vkWaitForFences(device, 1, &inFlight[frame], VK_TRUE, UINT64_MAX), "vkWaitForFences");
    uint32_t imageIndex = 0;
    const VkResult acquired = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                                    imageAvailable[frame], VK_NULL_HANDLE, &imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
      swapchainStale = true;
      return;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) check(acquired, "vkAcquireNextImageKHR");
    check(vkResetFences(device, 1, &inFlight[frame]), "vkResetFences");

    VkCommandBuffer cmd = commands[frame];
    check(vkResetCommandBuffer(cmd, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer");

    VkImage image = images[imageIndex];
    transition(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
               VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkClearColorValue value{};
    value.float32[0] = color.r;
    value.float32[1] = color.g;
    value.float32[2] = color.b;
    value.float32[3] = 1.0f;
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
    transition(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
               VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &imageAvailable[frame];
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &renderFinished[imageIndex];
    check(vkQueueSubmit(queue, 1, &submit, inFlight[frame]), "vkQueueSubmit");

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished[imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &imageIndex;
    const VkResult presented = vkQueuePresentKHR(queue, &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
      swapchainStale = true;
    } else {
      check(presented, "vkQueuePresentKHR");
    }
    frame = (frame + 1) % kFramesInFlight;
  }

  // Teardown ---------------------------------------------------------------------------
  ~Impl() {
    if (device != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(device);
      destroyRenderFinished();
      for (VkSemaphore s : imageAvailable) {
        if (s != VK_NULL_HANDLE) vkDestroySemaphore(device, s, nullptr);
      }
      for (VkFence f : inFlight) {
        if (f != VK_NULL_HANDLE) vkDestroyFence(device, f, nullptr);
      }
      if (pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, pool, nullptr);
      if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, swapchain, nullptr);
      vkDestroyDevice(device, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
      if (surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance, surface, nullptr);
      if (messenger != VK_NULL_HANDLE) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy != nullptr) destroy(instance, messenger, nullptr);
      }
      vkDestroyInstance(instance, nullptr);
    }
  }
};

Renderer::Renderer(const platform::Window& window) : impl_(std::make_unique<Impl>()) {
  impl_->createInstance();
  impl_->createSurface(window.nativeHandle());
  impl_->pickDevice();
  impl_->createFrameResources();
}

Renderer::~Renderer() = default;

void Renderer::drawFrame(const platform::Window& window, ClearColor color) {
  impl_->draw(core::checkedCast<uint32_t>(window.clientWidth()),
              core::checkedCast<uint32_t>(window.clientHeight()), color);
}

}  // namespace r1ui::render

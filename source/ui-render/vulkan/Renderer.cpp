// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: Vulkan instance, surface, device, swapchain, per-frame sync, the frame loop for one window,
//   image upload/destroy, and the transfer-only drawing of clear colour, solid rectangles and a
//   letterboxed image (see Renderer.h for the drawing model).
// Why: Phase 0 proved a Vulkan window opens, resizes and closes cleanly; Phase 1 adds just enough
//   drawing for the viewer. The 2D batcher (3.3) and text (3.4) build on this device and frame
//   structure and replace the blit path.
// Callers: r1ui::render::Renderer (Renderer.h). Calls: the Vulkan loader (vulkan-1.dll).
// Lifetime: Impl destroys everything it created in reverse order, including after a partial
//   constructor failure (null handles are skipped). All GPU work is idle before teardown.
// Rectangles: each frame the rectangle colours are written to a per-frame host-visible staging
//   buffer, copied to a per-frame 1-texel-high palette image, and each rectangle is one
//   vkCmdBlitImage of one palette texel onto the clipped destination (nearest filter).
// Images: uploaded through a staging buffer and a one-time command buffer that is fence-waited;
//   they then stay in TRANSFER_SRC_OPTIMAL. destroyImage waits for all in-flight frames first.
// Sync: two frames in flight (fence per frame); one render-finished semaphore per swapchain image
//   so a semaphore is never reused while presentation may still wait on it.
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "GpuResources.h"
#include "r1ui/core/CheckedCast.h"
#include "r1ui/render/Renderer.h"

namespace r1ui::render {

namespace {

constexpr uint32_t kFramesInFlight = 2;
constexpr VkFormat kImageFormat = VK_FORMAT_B8G8R8A8_UNORM;  // pixel layout of uploadImage
constexpr uint32_t kMaxRectsPerFrame = Renderer::kMaxRects;
constexpr uint64_t kMaxImageBytes = uint64_t{256} * 1024 * 1024;

using detail::check;
using detail::GpuBuffer;
using detail::GpuImage;

// One live uploaded image; `generation` changes whenever the slot is freed so stale ids fail.
struct ImageSlot {
  GpuImage image;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t generation = 1;
  bool used = false;
};

// Releases a one-time command buffer and its fence on every exit path of an upload.
struct OneTimeCommands {
  VkDevice device;
  VkCommandPool pool;
  VkCommandBuffer commands = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  ~OneTimeCommands() {
    if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, nullptr);
    if (commands != VK_NULL_HANDLE) vkFreeCommandBuffers(device, pool, 1, &commands);
  }
};

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
  VkPhysicalDeviceMemoryProperties memory{};
  uint32_t maxImageDimension = 0;
  VkFilter imageFilter = VK_FILTER_NEAREST;  // linear when the image format supports it

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

  // Per-frame rectangle palette (see file header) and the uploaded images.
  std::array<GpuBuffer, kFramesInFlight> paletteStaging;
  std::array<GpuImage, kFramesInFlight> palette;
  std::vector<ImageSlot> imageSlots;

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

    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical, &props);
    maxImageDimension = props.limits.maxImageDimension2D;
    // All drawing is blits between B8G8R8A8 images, so the format must support them.
    VkFormatProperties formatProps;
    vkGetPhysicalDeviceFormatProperties(physical, kImageFormat, &formatProps);
    const VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                                        VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if ((formatProps.optimalTilingFeatures & needed) != needed) {
      throw std::runtime_error("Device cannot blit B8G8R8A8_UNORM images");
    }
    if ((formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0) {
      imageFilter = VK_FILTER_LINEAR;
    }
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

    // The staging buffer starts zeroed so unused palette texels never expose stale memory.
    const VkDeviceSize paletteBytes = VkDeviceSize{kMaxRectsPerFrame} * 4;
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
      paletteStaging[i] = GpuBuffer(device, memory, paletteBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    true);
      std::memset(paletteStaging[i].mapped(), 0, core::checkedCast<size_t>(paletteBytes));
      palette[i] = GpuImage(device, memory, kMaxRectsPerFrame, 1, kImageFormat,
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
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

    VkFormatProperties chosenProps;
    vkGetPhysicalDeviceFormatProperties(physical, chosen.format, &chosenProps);
    if ((chosenProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) == 0) {
      throw std::runtime_error("Swapchain format cannot be a blit destination");
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

  // Orders consecutive transfer writes to the same image (later rectangles overwrite earlier).
  static void transferBarrier(VkCommandBuffer cmd) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1,
                         &barrier, 0, nullptr, 0, nullptr);
  }

  const ImageSlot* findImage(ImageId id) const {
    if (!id.valid() || id.index >= imageSlots.size()) return nullptr;
    const ImageSlot& slot = imageSlots[id.index];
    return slot.used && slot.generation == id.generation ? &slot : nullptr;
  }

  // Blits the image scaled to fit the swapchain extent, centred, aspect preserved.
  void recordImage(VkCommandBuffer cmd, VkImage target, const ImageSlot& slot) const {
    const double scale = std::min(static_cast<double>(extent.width) / slot.width,
                                  static_cast<double>(extent.height) / slot.height);
    const auto fitted = [scale](uint32_t size, uint32_t limit) {
      return std::clamp(static_cast<int32_t>(std::lround(size * scale)), 1,
                        core::checkedCast<int32_t>(limit));
    };
    const int32_t w = fitted(slot.width, extent.width);
    const int32_t h = fitted(slot.height, extent.height);
    const int32_t x = (core::checkedCast<int32_t>(extent.width) - w) / 2;
    const int32_t y = (core::checkedCast<int32_t>(extent.height) - h) / 2;

    VkImageBlit region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[1] = {core::checkedCast<int32_t>(slot.width),
                            core::checkedCast<int32_t>(slot.height), 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstOffsets[0] = {x, y, 0};
    region.dstOffsets[1] = {x + w, y + h, 1};
    vkCmdBlitImage(cmd, slot.image.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, imageFilter);
    transferBarrier(cmd);
  }

  // Writes the rectangle colours to this frame's palette, copies it to the palette image and
  // blits one texel per clipped rectangle. Rectangles fully outside the window are skipped.
  void recordRects(VkCommandBuffer cmd, VkImage target, const std::vector<FillRect>& rects) {
    if (rects.empty()) return;
    auto* texels = static_cast<uint8_t*>(paletteStaging[frame].mapped());
    for (size_t i = 0; i < rects.size(); ++i) {
      texels[4 * i + 0] = rects[i].color.b;
      texels[4 * i + 1] = rects[i].color.g;
      texels[4 * i + 2] = rects[i].color.r;
      texels[4 * i + 3] = 255;
    }
    VkImage paletteImage = palette[frame].handle();
    transition(cmd, paletteImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
               VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {core::checkedCast<uint32_t>(rects.size()), 1, 1};
    vkCmdCopyBufferToImage(cmd, paletteStaging[frame].handle(), paletteImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    transition(cmd, paletteImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT);

    const int64_t width = extent.width;
    const int64_t height = extent.height;
    for (size_t i = 0; i < rects.size(); ++i) {
      const FillRect& r = rects[i];
      if (r.w <= 0 || r.h <= 0) continue;
      const int64_t x0 = std::max<int64_t>(r.x, 0);
      const int64_t y0 = std::max<int64_t>(r.y, 0);
      const int64_t x1 = std::min<int64_t>(int64_t{r.x} + r.w, width);
      const int64_t y1 = std::min<int64_t>(int64_t{r.y} + r.h, height);
      if (x0 >= x1 || y0 >= y1) continue;
      const int32_t texel = core::checkedCast<int32_t>(i);
      VkImageBlit region{};
      region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      region.srcOffsets[0] = {texel, 0, 0};
      region.srcOffsets[1] = {texel + 1, 1, 1};
      region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      region.dstOffsets[0] = {core::checkedCast<int32_t>(x0), core::checkedCast<int32_t>(y0), 0};
      region.dstOffsets[1] = {core::checkedCast<int32_t>(x1), core::checkedCast<int32_t>(y1), 1};
      vkCmdBlitImage(cmd, paletteImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);
      transferBarrier(cmd);
    }
  }

  void draw(uint32_t width, uint32_t height, const FrameContent& content) {
    // Validate before touching any GPU state so a rejected frame cannot leave a fence unsignalled.
    if (content.rects.size() > Renderer::kMaxRects) {
      throw std::invalid_argument("drawFrame: too many rectangles");
    }
    const ImageSlot* imageSlot = nullptr;
    if (content.image.valid()) {
      imageSlot = findImage(content.image);
      if (imageSlot == nullptr) throw std::invalid_argument("drawFrame: unknown or destroyed image");
    }
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
    value.float32[0] = content.clear.r;
    value.float32[1] = content.clear.g;
    value.float32[2] = content.clear.b;
    value.float32[3] = 1.0f;
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
    transferBarrier(cmd);
    if (imageSlot != nullptr) recordImage(cmd, image, *imageSlot);
    recordRects(cmd, image, content.rects);
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

  // Images -----------------------------------------------------------------------------
  // Runs `record` on a one-time command buffer and blocks until the GPU has finished it.
  template <class Record>
  void submitAndWait(Record&& record) {
    OneTimeCommands once{device, pool};
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(device, &alloc, &once.commands), "vkAllocateCommandBuffers");
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    check(vkCreateFence(device, &fenceInfo, nullptr, &once.fence), "vkCreateFence");

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(once.commands, &begin), "vkBeginCommandBuffer");
    record(once.commands);
    check(vkEndCommandBuffer(once.commands), "vkEndCommandBuffer");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &once.commands;
    check(vkQueueSubmit(queue, 1, &submit, once.fence), "vkQueueSubmit");
    check(vkWaitForFences(device, 1, &once.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
  }

  ImageId upload(uint32_t width, uint32_t height, const uint8_t* bgra) {
    if (bgra == nullptr) throw std::invalid_argument("uploadImage: null pixel pointer");
    if (width == 0 || height == 0 || width > maxImageDimension || height > maxImageDimension) {
      throw std::invalid_argument("uploadImage: image size is zero or exceeds the device limit");
    }
    const uint64_t bytes = uint64_t{width} * height * 4;  // both < 2^32, so this cannot overflow
    if (bytes > kMaxImageBytes) throw std::invalid_argument("uploadImage: image is too large");

    size_t slotIndex = imageSlots.size();
    for (size_t i = 0; i < imageSlots.size(); ++i) {
      if (!imageSlots[i].used) {
        slotIndex = i;
        break;
      }
    }
    if (slotIndex == imageSlots.size() && imageSlots.size() >= Renderer::kMaxImages) {
      throw std::invalid_argument("uploadImage: too many live images");
    }

    GpuImage image(device, memory, width, height, kImageFormat,
                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    {
      // Staging memory lives only inside this scope: freed as soon as the copy has completed.
      GpuBuffer staging(device, memory, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        true);
      std::memcpy(staging.mapped(), bgra, core::checkedCast<size_t>(bytes));
      submitAndWait([&](VkCommandBuffer cmd) {
        transition(cmd, image.handle(), VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, staging.handle(), image.handle(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        transition(cmd, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);
      });
    }

    // The slot is only added or filled once everything above has succeeded.
    if (slotIndex == imageSlots.size()) imageSlots.emplace_back();
    ImageSlot& slot = imageSlots[slotIndex];
    slot.image = std::move(image);
    slot.width = width;
    slot.height = height;
    slot.used = true;
    return ImageId{core::checkedCast<uint32_t>(slotIndex), slot.generation};
  }

  void destroy(ImageId id) {
    if (findImage(id) == nullptr) throw std::invalid_argument("destroyImage: unknown or destroyed image");
    // Frames in flight may still read the image: wait for all of them before freeing it.
    check(vkWaitForFences(device, kFramesInFlight, inFlight.data(), VK_TRUE, UINT64_MAX),
          "vkWaitForFences");
    ImageSlot& slot = imageSlots[id.index];
    slot.image = GpuImage();
    slot.used = false;
    ++slot.generation;
  }

  // Teardown ---------------------------------------------------------------------------
  ~Impl() {
    if (device != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(device);
      imageSlots.clear();  // GPU allocations must go before the device they were made on
      for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        palette[i] = GpuImage();
        paletteStaging[i] = GpuBuffer();
      }
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

ImageId Renderer::uploadImage(uint32_t width, uint32_t height, const uint8_t* bgra) {
  return impl_->upload(width, height, bgra);
}

void Renderer::destroyImage(ImageId image) { impl_->destroy(image); }

void Renderer::drawFrame(const platform::Window& window, const FrameContent& content) {
  impl_->draw(core::checkedCast<uint32_t>(window.clientWidth()),
              core::checkedCast<uint32_t>(window.clientHeight()), content);
}

}  // namespace r1ui::render

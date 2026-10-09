// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: RenderDevice creation and teardown: instance and validation messenger, GPU enumeration
//   and selection, logical device, queue, command pool, timeline semaphore, pipelines, plus the
//   submission / deferred-destruction helpers declared in DeviceImpl.h.
// Why: see RenderDevice.h. Selection never needs a window: presentation support of a queue family
//   is asked from the platform (vkGetPhysicalDeviceWin32PresentationSupportKHR).
// Callers: every other vulkan/*.cpp through RenderDevice::Impl. Calls: the Vulkan loader.
// Teardown order: wait idle, run all deferred destruction, texture registry must already be
//   empty (textures are destroyed first by contract), pipelines, timeline, pool, device,
//   messenger, instance. A constructor failure unwinds the same way over whatever exists.
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

#include "DeviceImpl.h"
#include "r1ui/core/CheckedCast.h"

namespace r1ui::render {

namespace {

constexpr uint32_t kRequiredApi = VK_API_VERSION_1_2;

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data,
                                             void* user) {
  if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    ++*static_cast<uint32_t*>(user);
    std::fprintf(stderr, "[vulkan] %s\n", data->pMessage);
  }
  return VK_FALSE;
}

// True when the physical device offers the named device extension.
bool deviceSupportsExtension(VkPhysicalDevice device, const char* name) {
  uint32_t count = 0;
  if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS) return false;
  std::vector<VkExtensionProperties> extensions(count);
  if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()) != VK_SUCCESS) return false;
  return std::any_of(extensions.begin(), extensions.end(),
                     [name](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
}

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

// Value of an environment variable, empty when unset.
std::string readEnv(const char* name) {
#ifdef _MSC_VER
  char* value = nullptr;
  size_t length = 0;
  if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) return {};
  std::string result(value);
  std::free(value);
  return result;
#else
  const char* value = std::getenv(name);
  return value == nullptr ? std::string() : std::string(value);
#endif
}

bool layerAvailable(const char* name) {
  uint32_t count = 0;
  if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS) return false;
  std::vector<VkLayerProperties> layers(count);
  if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS) return false;
  return std::any_of(layers.begin(), layers.end(), [name](const VkLayerProperties& l) { return std::strcmp(l.layerName, name) == 0; });
}

// One enumerated GPU plus the data needed to create a device on it.
struct Candidate {
  VkPhysicalDevice handle = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;
  GpuInfo info;
};

std::vector<Candidate> enumerate(VkInstance instance) {
  uint32_t count = 0;
  if (vkEnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS || count == 0) return {};
  std::vector<VkPhysicalDevice> devices(count);
  if (vkEnumeratePhysicalDevices(instance, &count, devices.data()) < 0) return {};
  std::vector<Candidate> result;
  for (uint32_t i = 0; i < count; ++i) {
    Candidate c;
    c.handle = devices[i];
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(c.handle, &props);
    c.info.index = i;
    c.info.name = props.deviceName;
    c.info.vendorId = props.vendorID;
    c.info.discrete = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    c.info.apiVersion = props.apiVersion;
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(c.handle, &memory);
    for (uint32_t h = 0; h < memory.memoryHeapCount; ++h) {
      if ((memory.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
        c.info.deviceLocalBytes += memory.memoryHeaps[h].size;
      }
    }

    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &features12;
    const bool apiOk = props.apiVersion >= kRequiredApi;
    if (apiOk) vkGetPhysicalDeviceFeatures2(c.handle, &features);

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(c.handle, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(c.handle, &familyCount, families.data());
    bool hasQueue = false;
    for (uint32_t f = 0; f < familyCount && !hasQueue; ++f) {
      if ((families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 &&
          vkGetPhysicalDeviceWin32PresentationSupportKHR(c.handle, f) == VK_TRUE) {
        c.queueFamily = f;
        hasQueue = true;
      }
    }
    c.info.usable = apiOk && hasQueue && features12.timelineSemaphore == VK_TRUE;
    result.push_back(std::move(c));
  }
  return result;
}

std::string describe(const std::vector<Candidate>& candidates) {
  std::string text;
  for (const Candidate& c : candidates) {
    text += "\n  [" + std::to_string(c.info.index) + "] " + c.info.name + (c.info.usable ? "" : " (not usable)");
  }
  return text;
}

// Picks the GPU: explicit option, then R1UI_GPU, then best usable.
const Candidate& choose(const std::vector<Candidate>& candidates, const DeviceOptions& options) {
  if (candidates.empty()) throw std::runtime_error("No Vulkan physical device found");
  int index = options.gpuIndex;
  std::string name = options.gpuName;
  if (index < 0 && name.empty()) {
    const std::string env = readEnv("R1UI_GPU");
    if (!env.empty()) {
      if (std::all_of(env.begin(), env.end(), [](unsigned char c) { return std::isdigit(c) != 0; }) && env.size() < 6) {
        index = std::stoi(env);
      } else {
        name = env;
      }
    }
  }
  const Candidate* picked = nullptr;
  if (index >= 0) {
    if (static_cast<size_t>(index) < candidates.size()) picked = &candidates[static_cast<size_t>(index)];
  } else if (!name.empty()) {
    const std::string needle = lower(name);
    for (const Candidate& c : candidates) {
      if (lower(c.info.name).find(needle) != std::string::npos) {
        picked = &c;
        break;
      }
    }
  }
  if (index >= 0 || !name.empty()) {
    if (picked == nullptr || !picked->info.usable) {
      throw std::runtime_error("The requested GPU does not exist or cannot render and present; available:" + describe(candidates));
    }
    return *picked;
  }
  for (const Candidate& c : candidates) {
    if (!c.info.usable) continue;
    if (picked == nullptr || (c.info.discrete && !picked->info.discrete) ||
        (c.info.discrete == picked->info.discrete && c.info.deviceLocalBytes > picked->info.deviceLocalBytes)) {
      picked = &c;
    }
  }
  if (picked == nullptr) throw std::runtime_error("No Vulkan GPU can render and present; found:" + describe(candidates));
  return *picked;
}

// Creates an instance, with the validation layer and messenger when `validation` is true.
VkInstance createInstance(bool validation) {
  std::vector<const char*> extensions = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
  std::vector<const char*> layers;
  if (validation) {
    layers.push_back("VK_LAYER_KHRONOS_validation");
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "R1GUI";
  app.apiVersion = kRequiredApi;
  VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  info.pApplicationInfo = &app;
  info.enabledExtensionCount = core::checkedCast<uint32_t>(extensions.size());
  info.ppEnabledExtensionNames = extensions.data();
  info.enabledLayerCount = core::checkedCast<uint32_t>(layers.size());
  info.ppEnabledLayerNames = layers.data();
  VkInstance instance = VK_NULL_HANDLE;
  detail::check(vkCreateInstance(&info, nullptr, &instance), "vkCreateInstance");
  return instance;
}

bool wantValidation(ValidationMode mode) {
  if (mode == ValidationMode::Off) return false;
#ifdef NDEBUG
  if (mode == ValidationMode::Auto) return false;
#endif
  return layerAvailable("VK_LAYER_KHRONOS_validation");
}

}  // namespace

// ---- Impl ----------------------------------------------------------------------------------

void RenderDevice::Impl::init(const DeviceOptions& options) {
  {
    validation = wantValidation(options.validation);
    instance = createInstance(validation);
    if (validation) {
      auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
      if (create != nullptr) {
        VkDebugUtilsMessengerCreateInfoEXT m{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        m.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        m.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        m.pfnUserCallback = debugCallback;
        m.pUserData = &validationMessages;
        detail::check(create(instance, &m, nullptr, &messenger), "vkCreateDebugUtilsMessengerEXT");
      }
    }

    const std::vector<Candidate> candidates = enumerate(instance);
    const Candidate& chosen = choose(candidates, options);
    physical = chosen.handle;
    queueFamily = chosen.queueFamily;
    info = chosen.info;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    memoryBudget = deviceSupportsExtension(physical, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    if (memoryBudget) deviceExtensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.timelineSemaphore = VK_TRUE;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.pNext = &features12;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = core::checkedCast<uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
    vk(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "vkCreateDevice");
    vkGetDeviceQueue(device, queueFamily, 0, &queue);
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical, &props);
    limits = props.limits;

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily;
    vk(vkCreateCommandPool(device, &poolInfo, nullptr, &pool), "vkCreateCommandPool");

    VkSemaphoreTypeCreateInfo typeInfo{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    semInfo.pNext = &typeInfo;
    vk(vkCreateSemaphore(device, &semInfo, nullptr, &timeline), "vkCreateSemaphore(timeline)");

    pipelines = std::make_unique<detail::Pipelines>(device);

    std::fprintf(stderr, "[r1ui.render] GPU %u: %s (%s, %llu MiB device-local, Vulkan %u.%u.%u), validation %s\n",
                 info.index, info.name.c_str(), info.discrete ? "discrete" : "integrated/other",
                 static_cast<unsigned long long>(info.deviceLocalBytes >> 20), VK_API_VERSION_MAJOR(info.apiVersion),
                 VK_API_VERSION_MINOR(info.apiVersion), VK_API_VERSION_PATCH(info.apiVersion),
                 validation ? "on" : "off");
  }
}

RenderDevice::Impl::~Impl() {
  if (device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device);
    pending.clear();  // staging chunks and recycled chunks hold buffers: free them while the device lives
    activeChunks.clear();
    deferred.clear();  // may recycle chunks into freeChunks
    freeChunks.clear();
    pipelines.reset();
    if (timeline != VK_NULL_HANDLE) vkDestroySemaphore(device, timeline, nullptr);
    if (pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, pool, nullptr);
    vkDestroyDevice(device, nullptr);
  }
  if (instance != VK_NULL_HANDLE) {
    if (messenger != VK_NULL_HANDLE) {
      auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
      if (destroy != nullptr) destroy(instance, messenger, nullptr);
    }
    vkDestroyInstance(instance, nullptr);
  }
}

void RenderDevice::Impl::vk(VkResult result, const char* call) {
  try {
    detail::check(result, call);
  } catch (const DeviceLostError&) {
    lost = true;
    throw;
  }
}

void RenderDevice::Impl::requireUsable() const {
  if (lost) throw DeviceLostError("The GPU device was lost; destroy all render objects and create a new RenderDevice");
}

uint64_t RenderDevice::Impl::submit(VkCommandBuffer commands, VkSemaphore waitBinary, VkPipelineStageFlags waitStage,
                                    VkSemaphore signalBinary) {
  requireUsable();
  const uint64_t serial = submitted + 1;
  std::array<VkSemaphore, 2> signals{signalBinary, timeline};
  std::array<uint64_t, 2> signalValues{0, serial};
  const uint32_t signalOffset = signalBinary == VK_NULL_HANDLE ? 1u : 0u;
  const uint64_t waitValue = 0;  // ignored for a binary semaphore

  VkTimelineSemaphoreSubmitInfo timelineInfo{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
  timelineInfo.waitSemaphoreValueCount = waitBinary == VK_NULL_HANDLE ? 0u : 1u;
  timelineInfo.pWaitSemaphoreValues = &waitValue;
  timelineInfo.signalSemaphoreValueCount = 2 - signalOffset;
  timelineInfo.pSignalSemaphoreValues = signalValues.data() + signalOffset;

  VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submitInfo.pNext = &timelineInfo;
  submitInfo.waitSemaphoreCount = waitBinary == VK_NULL_HANDLE ? 0u : 1u;
  submitInfo.pWaitSemaphores = &waitBinary;
  submitInfo.pWaitDstStageMask = &waitStage;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commands;
  submitInfo.signalSemaphoreCount = 2 - signalOffset;
  submitInfo.pSignalSemaphores = signals.data() + signalOffset;
  vk(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit");
  submitted = serial;
  return serial;
}

void RenderDevice::Impl::waitSerial(uint64_t serial) {
  if (serial == 0) return;
  VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
  wait.semaphoreCount = 1;
  wait.pSemaphores = &timeline;
  wait.pValues = &serial;
  vk(vkWaitSemaphores(device, &wait, UINT64_MAX), "vkWaitSemaphores");
}

bool RenderDevice::Impl::serialDone(uint64_t serial) const {
  uint64_t value = 0;
  return vkGetSemaphoreCounterValue(device, timeline, &value) == VK_SUCCESS && value >= serial;
}

size_t RenderDevice::Impl::collect() {
  uint64_t value = 0;
  if (!lost && vkGetSemaphoreCounterValue(device, timeline, &value) == VK_SUCCESS) {
    while (!deferred.empty() && deferred.front().first <= value) deferred.pop_front();
  }
  return deferred.size();
}

// ---- RenderDevice --------------------------------------------------------------------------

RenderDevice::RenderDevice(const DeviceOptions& options) : impl_(std::make_unique<Impl>()) {
  impl_->init(options);  // a throw here destroys impl_, whose destructor releases partial state
}

RenderDevice::~RenderDevice() = default;

std::vector<GpuInfo> RenderDevice::listGpus() {
  const VkInstance instance = createInstance(false);
  std::vector<GpuInfo> result;
  for (const Candidate& c : enumerate(instance)) result.push_back(c.info);
  vkDestroyInstance(instance, nullptr);
  return result;
}

const GpuInfo& RenderDevice::gpu() const { return impl_->info; }
bool RenderDevice::validationActive() const { return impl_->validation; }
uint32_t RenderDevice::validationMessageCount() const { return impl_->validationMessages; }
bool RenderDevice::lost() const { return impl_->lost; }
uint64_t RenderDevice::staleTextureDraws() const { return impl_->staleTextureDraws; }

GpuMemoryUsage RenderDevice::memoryUsage() const {
  GpuMemoryUsage usage;
  if (!impl_->memoryBudget) return usage;
  VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
  VkPhysicalDeviceMemoryProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
  properties.pNext = &budget;
  vkGetPhysicalDeviceMemoryProperties2(impl_->physical, &properties);
  usage.available = true;
  for (uint32_t i = 0; i < properties.memoryProperties.memoryHeapCount; ++i) {
    if ((properties.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0) continue;
    usage.deviceLocalUsageBytes += budget.heapUsage[i];
    usage.deviceLocalBudgetBytes += budget.heapBudget[i];
  }
  return usage;
}

void RenderDevice::injectFault(FaultSite site, uint32_t afterPasses) {
  impl_->faultSite = site;
  impl_->faultPasses = afterPasses;
}

void RenderDevice::waitIdle() {
  impl_->requireUsable();
  impl_->vk(vkDeviceWaitIdle(impl_->device), "vkDeviceWaitIdle");
  impl_->collect();
}

size_t RenderDevice::collectGarbage() {
  impl_->requireUsable();
  return impl_->collect();
}

void RenderDevice::flushUploads() { impl_->flushUploads(); }

}  // namespace r1ui::render

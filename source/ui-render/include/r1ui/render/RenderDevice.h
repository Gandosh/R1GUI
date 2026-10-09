// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the shared GPU device: Vulkan instance (validation layer in Debug), physical-device
//   choice, logical device, the single graphics queue, memory-type selection, command pool, the
//   deferred-destruction queue and device-lost reporting. Several WindowTargets, OffscreenTargets
//   and Textures share one RenderDevice (floating panels are separate OS windows).
// Why: one place decides which GPU is used and guarantees that no GPU object is freed while a
//   submitted frame may still read it.
// Callers: the application shell, the Renderer facade, tests. No Vulkan type appears here.
// Threading: single-threaded; every call must come from the UI thread that created the device.
//
// GPU selection: DeviceOptions::gpuIndex (index in listGpus()) or gpuName (case-insensitive
//   substring) pick a GPU explicitly; if neither is set the environment variable R1UI_GPU (an
//   index or a name substring) is consulted; otherwise the best usable GPU is chosen (discrete
//   first, then most device-local memory). An explicit choice that does not exist or cannot
//   present throws std::runtime_error listing the usable GPUs. The chosen GPU is logged to stderr
//   as "[r1ui.render] GPU <index> ..." when the device is created.
//
// Colour: tokens are sRGB values. The swapchain and offscreen images are B8G8R8A8_UNORM and every
//   shader works on sRGB-encoded values directly: colours are written as given and blended in
//   the encoded space, which is what browsers do for CSS colours, so translucent shadows and
//   borders match the reference screenshots. Textures hold encoded bytes too (TextureFormat in
//   Texture.h), so no GPU sRGB conversion is involved anywhere and linear filtering also
//   happens on encoded values. Alpha is premultiplied after coverage is applied, never stored.
//
// Deferred destruction: resources used by submitted frames are handed to retire(); the callback
//   runs once the GPU has finished every submission made before the retire call's next submit
//   (a timeline semaphore counts submissions). collectGarbage() runs the due callbacks; it is
//   called at every beginFrame and at destruction.
//
// Device loss: when the driver reports VK_ERROR_DEVICE_LOST the failing call throws
//   DeviceLostError, lost() becomes true and every further frame call throws DeviceLostError.
//   Recovery is explicit: destroy all targets and textures, destroy the device, construct a new
//   RenderDevice, rebuild targets and textures. Destructors never throw.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace r1ui::render {

// Thrown when the GPU device was lost (driver reset, GPU removed, TDR).
class DeviceLostError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

enum class ValidationMode {
  Auto,  // on in Debug builds when the Khronos validation layer is installed, otherwise off
  On,    // requested explicitly; silently unavailable if the layer is not installed
  Off
};

struct DeviceOptions {
  int gpuIndex = -1;            // index in RenderDevice::listGpus(); -1 = not specified
  std::string gpuName;          // case-insensitive substring of the device name; empty = not specified
  ValidationMode validation = ValidationMode::Auto;
};

struct GpuInfo {
  uint32_t index = 0;            // enumeration order of vkEnumeratePhysicalDevices
  std::string name;
  uint32_t vendorId = 0;
  bool discrete = false;
  uint32_t apiVersion = 0;       // VK_MAKE_API_VERSION encoding
  uint64_t deviceLocalBytes = 0;
  bool usable = false;           // graphics queue that can present, Vulkan 1.2 timeline semaphores
};

// Device-local GPU memory used by this process (VK_EXT_memory_budget). `available` is false when
// the GPU lacks the extension; the byte counts are then 0.
struct GpuMemoryUsage {
  bool available = false;
  uint64_t deviceLocalUsageBytes = 0;   // allocated by this process in device-local heaps
  uint64_t deviceLocalBudgetBytes = 0;  // what the OS lets this process use
};

class RenderDevice {
 public:
  explicit RenderDevice(const DeviceOptions& options = {});
  ~RenderDevice();
  RenderDevice(const RenderDevice&) = delete;
  RenderDevice& operator=(const RenderDevice&) = delete;

  // All Vulkan GPUs on this machine, in enumeration order (creates a throw-away instance).
  static std::vector<GpuInfo> listGpus();

  const GpuInfo& gpu() const;
  bool validationActive() const;
  // Validation-layer messages of severity warning or error seen so far (they are also printed to
  // stderr as "[vulkan] ..."). Always 0 when validation is not active.
  uint32_t validationMessageCount() const;
  bool lost() const;
  GpuMemoryUsage memoryUsage() const;

  // Blocks until the GPU is idle, then runs all deferred destruction.
  void waitIdle();
  // Runs deferred-destruction callbacks whose frames have completed; returns how many remain.
  size_t collectGarbage();
  // Submits pending texture uploads now and waits for them (otherwise they ride the next frame).
  void flushUploads();

 // Private state; defined in vulkan/DeviceImpl.h and only used by the render library itself.
  struct Impl;

 private:
  friend class Texture;
  friend class WindowTarget;
  friend class OffscreenTarget;
  std::unique_ptr<Impl> impl_;
};

}  // namespace r1ui::render

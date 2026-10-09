// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the device-wide pipeline objects: the two render passes (swapchain and offscreen), the
//   instanced SDF pipeline, the textured-quad pipeline, the texture descriptor layout/pool and
//   the shared sampler.
// Why: every target draws with the same pipelines; they depend only on the colour format, so one
//   set serves all windows (render passes differing only in final layout are compatible).
// Callers: RenderDevice.cpp creates it; FrameRecorder.cpp, WindowTarget.cpp, OffscreenTarget.cpp
//   and Texture.cpp read it. Lifetime: destroyed before the VkDevice; PooledSet objects must be
//   released before it.
// Blending: premultiplied output, ONE / ONE_MINUS_SRC_ALPHA on colour and alpha, in the encoded
//   (sRGB) value space (see RenderDevice.h, "Colour").
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <stdexcept>


namespace r1ui::render::detail {

inline constexpr VkFormat kTargetFormat = VK_FORMAT_B8G8R8A8_UNORM;
inline constexpr uint32_t kFramesInFlight = 2;

// Thrown by Pipelines::allocateSet when the descriptor pool has no free set (live textures plus
// retired sets the GPU may still use); the caller can reclaim retired sets and retry.
class PoolExhausted : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class Pipelines {
 public:
  explicit Pipelines(VkDevice device);  // throws std::runtime_error; releases partial state
  ~Pipelines();
  Pipelines(const Pipelines&) = delete;
  Pipelines& operator=(const Pipelines&) = delete;

  VkRenderPass swapchainPass() const { return swapchainPass_; }
  VkRenderPass offscreenPass() const { return offscreenPass_; }
  VkPipeline sdf() const { return sdf_; }
  VkPipeline textured() const { return textured_; }
  VkPipelineLayout sdfLayout() const { return sdfLayout_; }
  VkPipelineLayout texturedLayout() const { return texturedLayout_; }

  // Allocates a descriptor set sampling `view` (expected layout SHADER_READ_ONLY_OPTIMAL).
  // Throws PoolExhausted when the pool is full.
  VkDescriptorSet allocateSet(VkImageView view);
  void freeSet(VkDescriptorSet set);

 private:
  void release();
  VkDevice device_;
  VkRenderPass swapchainPass_ = VK_NULL_HANDLE;
  VkRenderPass offscreenPass_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
  VkPipelineLayout sdfLayout_ = VK_NULL_HANDLE;
  VkPipelineLayout texturedLayout_ = VK_NULL_HANDLE;
  VkPipeline sdf_ = VK_NULL_HANDLE;
  VkPipeline textured_ = VK_NULL_HANDLE;
  VkSampler sampler_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
};

// RAII owner of one texture descriptor set; frees it back to the pool.
class PooledSet {
 public:
  PooledSet() = default;
  PooledSet(Pipelines& pipelines, VkImageView view)
      : pipelines_(&pipelines), set_(pipelines.allocateSet(view)) {}
  ~PooledSet() { release(); }
  PooledSet(PooledSet&& other) noexcept : pipelines_(other.pipelines_), set_(other.set_) {
    other.set_ = VK_NULL_HANDLE;
  }
  PooledSet& operator=(PooledSet&& other) noexcept {
    if (this != &other) {
      release();
      pipelines_ = other.pipelines_;
      set_ = other.set_;
      other.set_ = VK_NULL_HANDLE;
    }
    return *this;
  }
  PooledSet(const PooledSet&) = delete;
  PooledSet& operator=(const PooledSet&) = delete;
  VkDescriptorSet handle() const { return set_; }

 private:
  void release() {
    if (set_ != VK_NULL_HANDLE) pipelines_->freeSet(set_);
    set_ = VK_NULL_HANDLE;
  }
  Pipelines* pipelines_ = nullptr;
  VkDescriptorSet set_ = VK_NULL_HANDLE;
};

}  // namespace r1ui::render::detail

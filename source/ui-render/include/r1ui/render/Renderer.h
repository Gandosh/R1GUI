// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the public Vulkan renderer facade for one window (device, swapchain, frame loop) and the
//   generic frame description: clear colour, solid rectangles and one letterboxed image.
// Why: keeps every Vulkan type out of public headers so other modules include only this surface.
// Callers: examples/preview and later the toolkit shell. Calls: Vulkan loader via Renderer.cpp.
// Invariants: single-threaded, owned by the UI thread; the Window must outlive the Renderer.
// Drawing model (Phase 1): everything is transfer work (clear, blit); there are no shaders or
//   pipelines yet. Consequently rectangle alpha is ignored (rectangles are drawn opaque) and
//   rectangles are not anti-aliased; callers pre-composite translucent colours. The 2D batcher
//   (slice 3.3) replaces this path.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "r1ui/platform/Window.h"

namespace r1ui::render {

struct ClearColor {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
};

struct Rgba8 {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;  // ignored by the Phase 1 blit path (see header comment)
};

// A solid rectangle in physical client pixels; clipped to the window, drawn in list order.
struct FillRect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  Rgba8 color;
};

// Handle to an uploaded image. A default-constructed id is invalid; a destroyed id stays invalid
// (generation-checked) even if its slot is reused.
struct ImageId {
  uint32_t index = 0xFFFFFFFFu;
  uint32_t generation = 0;
  bool valid() const { return index != 0xFFFFFFFFu; }
};

struct FrameContent {
  ClearColor clear;
  std::vector<FillRect> rects;  // at most Renderer::kMaxRects; drawn after the image
  ImageId image;                // optional: scaled to fit the window, aspect preserved, centred
};

class Renderer {
 public:
  static constexpr uint32_t kMaxRects = 4096;
  static constexpr uint32_t kMaxImages = 256;

  // Throws std::runtime_error with the failing Vulkan call if setup cannot complete.
  explicit Renderer(const platform::Window& window);
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  // Copies width*height BGRA8 pixels (tightly packed, row 0 at the top) to GPU memory. The staging
  // memory is released before returning. Throws std::invalid_argument for a null pointer, a zero
  // or device-exceeding size or more than kMaxImages live images; std::runtime_error on Vulkan
  // failure (nothing is leaked in either case).
  ImageId uploadImage(uint32_t width, uint32_t height, const uint8_t* bgra);
  // Waits for in-flight frames, then frees the image. Throws std::invalid_argument for an id that
  // is invalid or already destroyed.
  void destroyImage(ImageId image);

  // Renders one frame at the window's current size. A zero-sized (minimized) window is a no-op.
  // Handles swapchain recreation on resize; throws on unrecoverable device errors, and
  // std::invalid_argument (before any GPU work) for an unknown image id or too many rectangles.
  void drawFrame(const platform::Window& window, const FrameContent& content);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace r1ui::render

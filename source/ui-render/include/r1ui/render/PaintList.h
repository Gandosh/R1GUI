// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the recorded output of a Painter for one frame: packed instance records for the two GPU
//   pipelines and the batch list that orders them, plus the integer rectangle type they use.
// Why: separates "what to draw" (CPU, testable without a GPU) from "how to draw it" (Vulkan
//   backend in vulkan/FrameRecorder.cpp). The structs are the exact bytes uploaded to the GPU, so
//   their sizes and offsets are asserted here and the shaders mirror them.
// Callers: paint/Painter.cpp fills it; vulkan/FrameRecorder.cpp consumes it; unit tests inspect it.
// Invariants: batches appear in draw order; sdf instances of the batches are contiguous slices of
//   `sdf` in batch order (likewise `tex`); no instance is shared between batches.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace r1ui::render {

// Integer pixel rectangle (physical pixels, y down). Empty when w <= 0 or h <= 0.
struct IRect {
  int32_t x = 0;
  int32_t y = 0;
  int32_t w = 0;
  int32_t h = 0;

  bool empty() const { return w <= 0 || h <= 0; }
  friend bool operator==(const IRect& a, const IRect& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
  }
};

// Which shader family a batch uses.
enum class BatchKind : uint32_t { Sdf, Textured };

// Values of SdfInstance::params[0]; the shader switches on them.
enum class SdfKind : uint32_t { Fill = 0, Border = 1, Shadow = 2, Line = 3 };

// One analytic shape. Layout is the vertex-attribute layout of sdf.vert (6 x vec4, 96 bytes).
//   Fill:   rect = box (x, y, w, h); radii = tl, tr, br, bl.
//   Border: rect/radii = outer box; params[1] = border width (drawn inside the outer box).
//   Shadow: rect/radii = already offset and spread shadow box; params[1] = gaussian sigma;
//           knockRect/knockRadii = the source box, which the shadow never paints inside (CSS).
//   Line:   rect = x0, y0, x1, y1; params[1] = width.
// color is straight (non-premultiplied) sRGB-encoded RGBA with the opacity stack already applied.
struct SdfInstance {
  float rect[4];
  float radii[4];
  float color[4];
  float params[4];  // [0] = SdfKind, [1] = width or sigma, [2..3] reserved (zero)
  float knockRect[4];
  float knockRadii[4];
};
static_assert(sizeof(SdfInstance) == 96, "SdfInstance must match the sdf.vert attribute layout");
static_assert(offsetof(SdfInstance, params) == 48, "SdfInstance field order is part of the shader ABI");
static_assert(offsetof(SdfInstance, knockRadii) == 80, "SdfInstance field order is part of the shader ABI");

// Values of TexInstance::params[0].
enum class TexMode : uint32_t { Coverage = 0, Color = 1 };

// One textured quad. Layout is the vertex-attribute layout of textured.vert (4 x vec4, 64 bytes).
//   dst = x, y, w, h in pixels; uv = u0, v0, u1, v1; tint = straight sRGB-encoded RGBA.
struct TexInstance {
  float dst[4];
  float uv[4];
  float tint[4];
  float params[4];  // [0] = TexMode, [1..3] reserved (zero)
};
static_assert(sizeof(TexInstance) == 64, "TexInstance must match the textured.vert attribute layout");

// A run of consecutive instances that share pipeline, scissor and texture: one GPU draw call.
struct Batch {
  BatchKind kind = BatchKind::Sdf;
  IRect scissor;           // pixels, inside the target; never empty
  uint64_t textureId = 0;  // 0 for Sdf batches
  uint32_t first = 0;      // index into PaintList::sdf or ::tex depending on kind
  uint32_t count = 0;
};

struct PaintList {
  uint32_t width = 0;   // target size in physical pixels the list was recorded for
  uint32_t height = 0;
  std::vector<SdfInstance> sdf;
  std::vector<TexInstance> tex;
  std::vector<Batch> batches;
};

}  // namespace r1ui::render

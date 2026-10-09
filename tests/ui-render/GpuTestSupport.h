// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: helpers shared by the GPU test cases: the process-wide RenderDevice, offscreen rendering
//   with RGBA readback, pixel access, analytic comparison and optional PPM dumps.
// Why: each case should read as "draw this, expect that" without repeating device and readback
//   plumbing.
// Callers: tests/ui-render/gpu_test.cpp only.
// GPU choice: the shared device uses the default selection, so R1UI_GPU (index or name) picks
//   the GPU; the chosen one is printed by the device when it is created.
// Dumps: when the environment variable R1UI_TEST_DUMP_DIR names a folder, dumpImage writes
//   <name>.ppm there so a human can look at the rendered pixels.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/render/OffscreenTarget.h"
#include "r1ui/render/Painter.h"
#include "r1ui/render/RenderDevice.h"

namespace gpu_test {

using namespace r1ui::render;
using render_test::expect;

inline std::unique_ptr<RenderDevice>& deviceSlot() {
  static std::unique_ptr<RenderDevice> device;
  return device;
}

// The process-wide device, created on first use.
inline RenderDevice& device() {
  auto& slot = deviceSlot();
  if (!slot) slot = std::make_unique<RenderDevice>();
  return *slot;
}

struct Image {
  uint32_t w = 0;
  uint32_t h = 0;
  std::vector<uint8_t> rgba;

  const uint8_t* px(uint32_t x, uint32_t y) const { return &rgba[4 * (size_t{y} * w + x)]; }
  bool is(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) const {
    const uint8_t* p = px(x, y);
    return p[0] == r && p[1] == g && p[2] == b;
  }
  bool near(uint32_t x, uint32_t y, double r, double g, double b, double tolerance) const {
    const uint8_t* p = px(x, y);
    return std::fabs(p[0] - r) <= tolerance && std::fabs(p[1] - g) <= tolerance && std::fabs(p[2] - b) <= tolerance;
  }
};

// Renders one frame into a fresh offscreen target and reads it back.
inline Image render(uint32_t w, uint32_t h, const Color& clear, const std::function<void(Painter&)>& draw) {
  OffscreenTarget target(device(), w, h);
  expect(target.beginFrame(clear), "offscreen beginFrame");
  draw(target.painter());
  expect(target.endFrame(), "offscreen endFrame");
  return Image{w, h, target.readPixels()};
}

inline Color rgb8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { return Color::fromRgba8(r, g, b, a); }

// dst * (1 - a) + src * a, the straight-alpha "over" in encoded space, in 0..255 units.
inline double over(double dst, double src, double a) { return dst * (1.0 - a) + src * a; }

struct Difference {
  double maxAbs = 0.0;
  double meanAbs = 0.0;
  uint32_t atX = 0;
  uint32_t atY = 0;
};

// Compares every pixel's RGB with expected(x, y) (values in 0..255) and reports the worst/mean error.
inline Difference compare(const Image& image, const std::function<std::array<double, 3>(uint32_t, uint32_t)>& expected) {
  Difference d;
  double sum = 0.0;
  for (uint32_t y = 0; y < image.h; ++y) {
    for (uint32_t x = 0; x < image.w; ++x) {
      const std::array<double, 3> want = expected(x, y);
      const uint8_t* p = image.px(x, y);
      for (size_t c = 0; c < 3; ++c) {
        const double e = std::fabs(p[c] - want[c]);
        sum += e;
        if (e > d.maxAbs) {
          d.maxAbs = e;
          d.atX = x;
          d.atY = y;
        }
      }
    }
  }
  d.meanAbs = sum / (3.0 * image.w * image.h);
  return d;
}

inline std::string describe(const Difference& d) {
  char text[128];
  std::snprintf(text, sizeof(text), "max error %.2f at (%u,%u), mean error %.4f", d.maxAbs, d.atX, d.atY, d.meanAbs);
  return text;
}

inline void dumpImage(const char* name, const Image& image) {
  char* dir = nullptr;
  size_t length = 0;
  if (_dupenv_s(&dir, &length, "R1UI_TEST_DUMP_DIR") != 0 || dir == nullptr) return;
  const std::string path = std::string(dir) + "/" + name + ".ppm";
  std::free(dir);
  std::FILE* file = nullptr;
  if (fopen_s(&file, path.c_str(), "wb") != 0 || file == nullptr) return;
  std::fprintf(file, "P6\n%u %u\n255\n", image.w, image.h);
  for (size_t i = 0; i < size_t{image.w} * image.h; ++i) std::fwrite(&image.rgba[4 * i], 1, 3, file);
  std::fclose(file);
}

}  // namespace gpu_test

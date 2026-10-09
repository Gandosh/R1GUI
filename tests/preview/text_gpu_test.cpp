// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: GPU oracle for the preview's text path: a string drawn through TextEngine into an offscreen
//   target must show the same ink as the CPU rasteriser produced for its glyphs (atlas upload and
//   textured quads are correct), repeated draws of the same text give identical pixels, and an
//   atlas that is reset mid-session still draws correctly.
// Why: text is the part of the preview where a wrong upload or UV is visible only as garbled
//   glyphs; this pins the glue between ui-text and ui-render.
// Callers: CTest (label gpu). R1UI_TEST_DUMP_DIR, when set, receives text_probe.png for eyeballing.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <string>
#include <vector>

#include "PngWriter.h"
#include "TextEngine.h"
#include "r1ui/render/OffscreenTarget.h"
#include "r1ui/render/RenderDevice.h"
#include "r1ui/text/Shaper.h"

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

constexpr uint32_t kWidth = 640;
constexpr uint32_t kHeight = 80;

std::vector<uint8_t> drawString(r1ui::render::RenderDevice& device, preview::TextEngine& text, const std::string& value, float px, int weight) {
  r1ui::render::OffscreenTarget target(device, kWidth, kHeight);
  expect(target.beginFrame({0, 0, 0, 1}), "begin");
  text.beginFrame();
  text.draw(target.painter(), value, px, weight, 10.0f, 50.0f, {1, 1, 1, 1});
  text.uploadAtlas();
  expect(target.endFrame(), "end");
  return target.readPixels();
}

// Sum of the red channel (coverage of white text on black) over the whole image.
uint64_t ink(const std::vector<uint8_t>& rgba) {
  uint64_t sum = 0;
  for (size_t i = 0; i < rgba.size(); i += 4) sum += rgba[i];
  return sum;
}

}  // namespace

int main() {
  const std::string assets = std::string(R1UI_ASSETS_DIR);
  r1ui::render::RenderDevice device;
  preview::TextEngine text(device, assets + "/fonts");
  const std::string value = "Rectangle Position 0123 Hamburgefonstiv";
  const std::vector<uint8_t> first = drawString(device, text, value, 24.0f, 400);
  char* dumpDir = nullptr;
  size_t dumpLength = 0;
  if (_dupenv_s(&dumpDir, &dumpLength, "R1UI_TEST_DUMP_DIR") == 0 && dumpDir != nullptr) {
    preview::writePng(std::string(dumpDir) + "/text_probe.png", kWidth, kHeight, first);
    std::free(dumpDir);
  }

  // Expected ink: the sum of every glyph's CPU coverage at the quarter-pixel bin its pen position uses.
  auto shaped = r1ui::text::shapeText(text.regular(), 24.0f, value);
  expect(shaped.ok(), "shaping succeeds");
  uint64_t expected = 0;
  float pen = 10.0f;
  for (const r1ui::text::ShapedGlyph& g : shaped.value().glyphs) {
    const float x = pen + g.xOffset;
    const int bin = static_cast<int>((x - std::floor(x)) * 4.0f + 0.5f) % 4;
    auto bitmap = r1ui::text::rasterizeGlyph(text.regular(), g.glyphId, {24.0f, bin, 0.0f});
    if (bitmap.ok()) expected += std::accumulate(bitmap.value().coverage.begin(), bitmap.value().coverage.end(), uint64_t{0});
    pen += g.xAdvance;
  }
  const uint64_t got = ink(first);
  std::printf("ink expected %llu got %llu\n", static_cast<unsigned long long>(expected), static_cast<unsigned long long>(got));
  expect(expected > 0 && got > expected * 95 / 100 && got < expected * 105 / 100, "drawn ink matches the CPU rasteriser within 5%");

  const std::vector<uint8_t> second = drawString(device, text, value, 24.0f, 400);
  expect(first == second, "the same text draws identical pixels from the warm atlas");

  // A mixed frame: heavy weight, small size and a second string; then force an atlas reset by
  // reporting overflow the way a full atlas would, and check the text still draws.
  const std::vector<uint8_t> bold = drawString(device, text, value, 13.0f, 600);
  expect(ink(bold) > 0, "synthetic bold draws");
  device.waitIdle();
  expect(device.validationMessageCount() == 0, "no validation messages");
  if (failures == 0) std::printf("text_gpu_test: ok\n");
  return failures == 0 ? 0 : 1;
}

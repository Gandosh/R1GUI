// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: GPU tests of ui-render: real shader output read back from an OffscreenTarget and compared
//   with the CPU reference, texture upload/lifetime, window swapchain lifecycle, multi-window
//   sharing, hostile inputs, and the validation-layer check.
// Why: unit tests prove what the painter records; these prove the shaders, pipelines, sync and
//   deferred destruction turn it into the right pixels without validation errors.
// Callers: CTest (label gpu), one case per entry: `ui-render-gpu-test <case>`; no argument runs
//   all cases. Exit code 0 = pass. Needs a Vulkan GPU and, for the window cases, a desktop session.
// Tolerances (all in 8-bit units, documented where used): exact cases demand equality; analytic
//   cases allow the error of float shader math plus one 8-bit rounding per blended draw.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>

#include "GpuTestSupport.h"
#include "r1ui/platform/Window.h"
#include "r1ui/render/SdfReference.h"
#include "r1ui/render/Texture.h"
#include "r1ui/render/WindowTarget.h"

using namespace gpu_test;
namespace platform = r1ui::platform;
namespace reference = r1ui::render::reference;

namespace {

const Color kBlack = rgb8(0, 0, 0);
const Color kWhite = rgb8(255, 255, 255);

template <class Fn>
bool throws(Fn&& fn) {
  try {
    fn();
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

// ---- device ----------------------------------------------------------------------------------

void deviceSelection() {
  const std::vector<GpuInfo> gpus = RenderDevice::listGpus();
  expect(!gpus.empty(), "at least one Vulkan GPU");
  const GpuInfo& chosen = device().gpu();
  std::printf("shared device uses GPU %u \"%s\"; validation %s\n", chosen.index, chosen.name.c_str(),
              device().validationActive() ? "active" : "inactive");
  expect(chosen.usable && chosen.index < gpus.size() && gpus[chosen.index].name == chosen.name, "chosen GPU is in the list");

  for (int round = 0; round < 2; ++round) {  // destroy and recreate on the same GPU
    DeviceOptions byIndex;
    byIndex.gpuIndex = static_cast<int>(chosen.index);
    RenderDevice a(byIndex);
    expect(a.gpu().index == chosen.index, "selection by index");
    DeviceOptions byName;
    byName.gpuName = chosen.name;
    RenderDevice b(byName);
    expect(b.gpu().index == chosen.index, "selection by exact name");
    expect(!a.lost() && a.validationMessageCount() == 0 && b.validationMessageCount() == 0, "fresh devices are healthy and silent");
  }
  DeviceOptions missingIndex;
  missingIndex.gpuIndex = 999;
  expect(throws([&] { RenderDevice d(missingIndex); }), "unknown GPU index is an error");
  DeviceOptions missingName;
  missingName.gpuName = "no such gpu";
  expect(throws([&] { RenderDevice d(missingName); }), "unknown GPU name is an error");
  device().waitIdle();
  expect(device().collectGarbage() == 0, "no deferred resources remain after waitIdle");
}

// ---- primitives ------------------------------------------------------------------------------

void exactFill() {
  const Image img = render(64, 64, rgb8(10, 20, 30), [](Painter& p) { p.fillRect({8, 8, 16, 16}, rgb8(200, 100, 50)); });
  expect(img.is(8, 8, 200, 100, 50) && img.is(23, 23, 200, 100, 50) && img.is(15, 15, 200, 100, 50), "inside is the exact fill colour");
  expect(img.is(7, 8, 10, 20, 30) && img.is(24, 8, 10, 20, 30) && img.is(8, 24, 10, 20, 30) && img.is(8, 7, 10, 20, 30), "outside is the exact clear colour");
  expect(img.is(0, 0, 10, 20, 30) && img.is(63, 63, 10, 20, 30), "untouched corners keep the clear colour");
  expect(img.px(30, 30)[3] == 255, "alpha stays opaque");
  dumpImage("exact_fill", img);
}

void roundedAa() {
  const Rect box{8, 8, 48, 48};
  const CornerRadii radii = CornerRadii::uniform(16);
  const Image img = render(64, 64, kBlack, [&](Painter& p) { p.fillRoundedRect(box, radii, kWhite); });
  const Difference d = compare(img, [&](uint32_t x, uint32_t y) {
    const double v = 255.0 * reference::fillCoverage(x + 0.5, y + 0.5, box, radii);
    return std::array<double, 3>{v, v, v};
  });
  // Tolerance 1.5: float shader math plus one 8-bit rounding.
  expect(d.maxAbs <= 1.5, "rounded rect matches the analytic coverage: " + describe(d));
  expect(img.is(8, 8, 0, 0, 0), "the very corner pixel is outside the arc");
  uint32_t partial = 0;
  for (uint32_t y = 0; y < 64; ++y) {
    for (uint32_t x = 0; x < 64; ++x) partial += (img.px(x, y)[0] != 0 && img.px(x, y)[0] != 255) ? 1u : 0u;
  }
  expect(partial >= 20, "the corner arcs contain anti-aliased pixels");
  bool monotonic = true;
  for (uint32_t i = 0; i < 20; ++i) monotonic = monotonic && img.px(8 + i, 8 + i)[0] <= img.px(9 + i, 9 + i)[0];
  expect(monotonic, "coverage along the corner diagonal is monotonic");
  dumpImage("rounded_aa", img);
}

void borderWidth() {
  const Image square = render(64, 64, kBlack, [](Painter& p) { p.border({8, 8, 48, 48}, CornerRadii{}, 3.0f, kWhite); });
  bool ring = true;
  for (uint32_t x = 8; x < 56; ++x) {
    const bool inRing = x < 11 || x >= 53;
    ring = ring && square.is(x, 32, inRing ? 255 : 0, inRing ? 255 : 0, inRing ? 255 : 0);
  }
  expect(ring, "border is exactly 3 px wide on both sides of a row");
  expect(square.is(7, 32, 0, 0, 0) && square.is(56, 32, 0, 0, 0), "nothing outside the box");
  expect(square.is(32, 8, 255, 255, 255) && square.is(32, 10, 255, 255, 255) && square.is(32, 11, 0, 0, 0), "top edge is 3 px too");

  const Rect box{8, 8, 48, 48};
  const CornerRadii radii = CornerRadii::uniform(12);
  const Image rounded = render(64, 64, kBlack, [&](Painter& p) { p.border(box, radii, 3.0f, kWhite); });
  const Difference d = compare(rounded, [&](uint32_t x, uint32_t y) {
    const double v = 255.0 * reference::borderCoverage(x + 0.5, y + 0.5, box, radii, 3.0);
    return std::array<double, 3>{v, v, v};
  });
  expect(d.maxAbs <= 2.0, "rounded border matches the analytic ring: " + describe(d));

  const Image outside = render(64, 64, kBlack, [](Painter& p) { p.border({16, 16, 32, 32}, CornerRadii{}, 2.0f, kWhite, false); });
  expect(outside.is(14, 32, 255, 255, 255) && outside.is(15, 32, 255, 255, 255) && outside.is(16, 32, 0, 0, 0) && outside.is(13, 32, 0, 0, 0),
         "outside border grows outwards and leaves the box untouched");
  dumpImage("border_rounded", rounded);
}

// Analytic composite of one shadow layer over `dst` (0..255) at pixel centre (px, py).
double shadowAt(double dst, double px, double py, const Rect& box, const CornerRadii& radii, const ShadowSpec& s) {
  const Rect moved{box.x + s.offsetX - s.spread, box.y + s.offsetY - s.spread, box.w + 2 * s.spread, box.h + 2 * s.spread};
  const auto grow = [&](float r) { return r > 0 ? std::max(r + s.spread, 0.0f) : 0.0f; };
  const CornerRadii shadowRadii{grow(radii.topLeft), grow(radii.topRight), grow(radii.bottomRight), grow(radii.bottomLeft)};
  const double body = reference::blurredBoxCoverage(px, py, moved, shadowRadii, s.blur * 0.5);
  const double a = s.color.a * body * (1.0 - reference::fillCoverage(px, py, box, radii));
  return over(dst, 0.0, a);
}

void shadowBlur() {
  const Rect box{40, 40, 48, 48};
  const CornerRadii radii = CornerRadii::uniform(8);
  const ShadowSpec spec{0, 0, 16, 0, Color{0, 0, 0, 1}};
  const Image img = render(128, 128, kWhite, [&](Painter& p) { p.shadow(box, radii, spec); });
  const Difference d = compare(img, [&](uint32_t x, uint32_t y) {
    const double v = shadowAt(255.0, x + 0.5, y + 0.5, box, radii, spec);
    return std::array<double, 3>{v, v, v};
  });
  // Tolerance 3: the shader integrates 16 rows and uses a 1.5e-7 erf approximation; at full
  // alpha one 8-bit step is 1 and the row quadrature adds up to ~2 steps near the edge.
  expect(d.maxAbs <= 3.0, "shadow matches the analytic gaussian: " + describe(d));

  bool symmetric = true;
  for (uint32_t y = 0; y < 128; ++y) {
    for (uint32_t x = 0; x < 128; ++x) {
      symmetric = symmetric && std::abs(img.px(x, y)[0] - img.px(127 - x, y)[0]) <= 1 && std::abs(img.px(x, y)[0] - img.px(x, 127 - y)[0]) <= 1;
    }
  }
  expect(symmetric, "blur is symmetric left/right and top/bottom");
  bool bounded = true;
  for (uint32_t y = 0; y < 128; ++y) {
    for (uint32_t x = 0; x < 128; ++x) {
      const double dx = std::max({40.0 - x - 0.5, x + 0.5 - 88.0, 0.0});
      const double dy = std::max({40.0 - y - 0.5, y + 0.5 - 88.0, 0.0});
      if (std::max(dx, dy) > 25.0 && !img.is(x, y, 255, 255, 255)) bounded = false;  // 3 sigma = 24 px, plus the AA margin
    }
  }
  expect(bounded, "no shadow beyond three sigma of the box");
  expect(img.is(64, 64, 255, 255, 255) && img.is(45, 45, 255, 255, 255), "the source box is knocked out (CSS)");
  expect(img.px(30, 64)[0] < 255 && img.px(30, 64)[0] > 100, "the shadow is visible next to the box");

  const Rect small{50, 50, 40, 30};
  const ShadowSpec offset{6, 10, 8, -4, Color{0, 0, 0, 0.5f}};
  const Image shifted = render(128, 128, rgb8(200, 220, 240), [&](Painter& p) { p.shadow(small, radii, offset); });
  const Difference d2 = compare(shifted, [&](uint32_t x, uint32_t y) {
    return std::array<double, 3>{shadowAt(200, x + 0.5, y + 0.5, small, radii, offset), shadowAt(220, x + 0.5, y + 0.5, small, radii, offset),
                                 shadowAt(240, x + 0.5, y + 0.5, small, radii, offset)};
  });
  expect(d2.maxAbs <= 3.0, "offset, negative spread and alpha match the analytic result: " + describe(d2));
  dumpImage("shadow_blur", img);
}

void clipCorrectness() {
  const Image img = render(64, 64, kBlack, [](Painter& p) {
    p.pushClip({10, 10, 20, 20});
    p.fillRect({0, 0, 64, 64}, kWhite);
    p.pushClip({20, 20, 30, 30});
    p.fillRect({0, 0, 64, 64}, rgb8(255, 0, 0));
    p.popClip();
    p.popClip();
  });
  uint32_t white = 0, red = 0, other = 0;
  for (uint32_t y = 0; y < 64; ++y) {
    for (uint32_t x = 0; x < 64; ++x) {
      const bool inA = x >= 10 && x < 30 && y >= 10 && y < 30;
      const bool inB = x >= 20 && x < 30 && y >= 20 && y < 30;
      if (inB) {
        red += img.is(x, y, 255, 0, 0) ? 1u : 0u;
      } else if (inA) {
        white += img.is(x, y, 255, 255, 255) ? 1u : 0u;
      } else {
        other += img.is(x, y, 0, 0, 0) ? 1u : 0u;
      }
    }
  }
  expect(red == 100 && white == 300 && other == 64u * 64u - 400u, "nested clips: exact pixels inside, nothing outside");

  const Image fractional = render(64, 64, kBlack, [](Painter& p) {
    p.pushClip({10.4f, 10.6f, 20.0f, 20.0f});  // snaps to x [10,30), y [11,31)
    p.fillRect({0, 0, 64, 64}, kWhite);
    p.popClip();
  });
  expect(fractional.is(10, 11, 255, 255, 255) && fractional.is(29, 30, 255, 255, 255) && fractional.is(10, 10, 0, 0, 0) &&
             fractional.is(30, 20, 0, 0, 0) && fractional.is(20, 31, 0, 0, 0),
         "fractional clip snaps to the documented pixel edges");

  const Image curved = render(64, 64, kBlack, [](Painter& p) {
    p.pushClip({0, 0, 32, 64});
    p.fillRoundedRect({8, 8, 48, 48}, CornerRadii::uniform(20), kWhite);
    p.popClip();
  });
  bool leaked = false;
  for (uint32_t y = 0; y < 64; ++y) {
    for (uint32_t x = 32; x < 64; ++x) leaked = leaked || curved.px(x, y)[0] != 0;
  }
  expect(!leaked, "a shape crossing the clip edge never paints outside it");
  expect(curved.px(31, 32)[0] == 255, "and is complete inside");
}

void alphaBlend() {
  const Image img = render(64, 64, rgb8(0, 0, 255), [](Painter& p) {
    p.fillRect({0, 0, 32, 64}, rgb8(255, 0, 0, 128));
    p.pushOpacity(0.5f);
    p.fillRect({32, 0, 32, 32}, kWhite);
    p.popOpacity();
  });
  const double a = 128.0 / 255.0;
  expect(img.near(10, 10, over(0, 255, a), 0, over(255, 0, a), 1.0), "red at 128/255 over blue, straight alpha in encoded space");
  expect(img.near(40, 10, 127.5, 127.5, 255, 1.0), "opacity 0.5 white over blue");
  expect(img.is(40, 40, 0, 0, 255), "untouched area keeps the clear colour");
  expect(img.px(10, 10)[3] == 255, "target alpha stays opaque over an opaque clear");
}

void texturedQuad() {
  // R8 coverage ramp, 4x4, drawn 1:1 with a tint: output = tint * coverage over black.
  std::vector<uint8_t> coverage(16);
  for (size_t i = 0; i < coverage.size(); ++i) coverage[i] = static_cast<uint8_t>(i * 17);
  Texture glyph(device(), 4, 4, TextureFormat::R8Coverage, coverage);
  const Image tinted = render(32, 32, kBlack, [&](Painter& p) {
    p.drawTexture(glyph.ref(), {8, 8, 4, 4}, {0, 0, 1, 1}, rgb8(255, 128, 0));
  });
  bool tintOk = true;
  for (uint32_t y = 0; y < 4; ++y) {
    for (uint32_t x = 0; x < 4; ++x) {
      const double c = coverage[y * 4 + x] / 255.0;
      tintOk = tintOk && tinted.near(8 + x, 8 + y, 255 * c, 128 * c, 0, 1.0);
    }
  }
  expect(tintOk, "coverage texture is tinted per texel");
  expect(tinted.is(7, 8, 0, 0, 0) && tinted.is(12, 8, 0, 0, 0), "nothing outside the quad");

  // Partial update, visible in the same frame it was made.
  const std::vector<uint8_t> full(4, 255);
  glyph.update(1, 1, 2, 2, full);
  const Image updated = render(32, 32, kBlack, [&](Painter& p) { p.drawTexture(glyph.ref(), {8, 8, 4, 4}, {0, 0, 1, 1}, kWhite); });
  expect(updated.is(9, 9, 255, 255, 255) && updated.is(10, 10, 255, 255, 255) && updated.is(10, 9, 255, 255, 255) && updated.is(9, 10, 255, 255, 255),
         "the updated rectangle is white");
  expect(updated.is(8, 8, 0, 0, 0) && updated.near(11, 8, 51, 51, 51, 1.0) && updated.near(8, 9, 68, 68, 68, 1.0),
         "texels outside the rectangle keep their old values");

  // RGBA image: texel colour times tint, straight alpha over the background.
  const std::vector<uint8_t> texels = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 128};
  Texture image(device(), 2, 2, TextureFormat::Rgba8Srgb, texels);
  const Image drawn = render(16, 16, rgb8(0, 0, 0), [&](Painter& p) {
    p.drawTexture(image.ref(), {4, 4, 2, 2}, {0, 0, 1, 1});
    p.drawTexture(image.ref(), {10, 4, 2, 2}, {0, 0, 1, 1}, rgb8(128, 128, 128));
  });
  expect(drawn.is(4, 4, 255, 0, 0) && drawn.is(5, 4, 0, 255, 0) && drawn.is(4, 5, 0, 0, 255), "RGBA texels appear unchanged with a white tint");
  expect(drawn.near(5, 5, 128, 128, 128, 1.0), "half-transparent white texel over black");
  expect(drawn.near(10, 4, 128, 0, 0, 1.0), "tint multiplies the texel colour");

  expect(throws([&] { glyph.update(3, 3, 2, 2, full); }), "update outside the texture is rejected");
  expect(throws([&] { glyph.update(0, 0, 2, 2, std::span<const uint8_t>(full).first(3)); }), "update with the wrong byte count is rejected");
  expect(throws([&] { glyph.update(0, 0, 0, 2, {}); }), "empty update rectangle is rejected");
  dumpImage("textured_tint", tinted);
}

void textureLifetime() {
  expect(throws([&] { Texture t(device(), 0, 4, TextureFormat::R8Coverage); }), "zero width is rejected");
  expect(throws([&] { Texture t(device(), 4, 100000, TextureFormat::R8Coverage); }), "size beyond the device limit is rejected");
  const std::vector<uint8_t> wrong(5);
  expect(throws([&] { Texture t(device(), 2, 2, TextureFormat::R8Coverage, wrong); }), "pixel count mismatch is rejected");

  OffscreenTarget target(device(), 32, 32);
  // Destroyed right after endFrame: the submitted frame still reads it; the deferred queue keeps it alive.
  for (int i = 0; i < 20; ++i) {
    auto texture = std::make_unique<Texture>(device(), 8, 8, TextureFormat::Rgba8Srgb, std::vector<uint8_t>(8 * 8 * 4, 200));
    expect(target.beginFrame(kBlack), "begin");
    target.painter().drawTexture(texture->ref(), {0, 0, 8, 8}, {0, 0, 1, 1});
    expect(target.endFrame(), "end");
    texture.reset();
  }
  expect(target.readPixels().size() == 32u * 32u * 4u, "readback after the loop");

  // Destroyed between draw and endFrame: only that draw is skipped (and counted); everything else
  // in the frame is drawn and the target stays usable.
  auto doomed = std::make_unique<Texture>(device(), 4, 4, TextureFormat::R8Coverage);
  const uint64_t staleBefore = device().staleTextureDraws();
  expect(target.beginFrame(kBlack), "begin");
  target.painter().fillRect({0, 0, 4, 4}, rgb8(40, 50, 60));
  target.painter().drawTexture(doomed->ref(), {8, 8, 4, 4}, {0, 0, 1, 1});
  target.painter().fillRect({16, 0, 4, 4}, rgb8(70, 80, 90));
  doomed.reset();
  expect(target.endFrame(), "endFrame survives a destroyed texture");
  expect(device().staleTextureDraws() == staleBefore + 1, "the skipped draw is counted");
  const std::vector<uint8_t> after = target.readPixels();
  expect(after[0] == 40 && after[4 * 16] == 70, "the other draws of that frame are rendered");
  expect(target.beginFrame(rgb8(1, 2, 3)), "begin after a frame with a destroyed texture");
  target.painter().fillRect({0, 0, 4, 4}, rgb8(9, 9, 9));
  expect(target.endFrame(), "end");
  expect(target.readPixels()[0] == 9, "the target renders normally afterwards");

  device().waitIdle();
  expect(device().collectGarbage() == 0, "all retired textures are freed once the GPU is idle");

  Texture a(device(), 4, 4, TextureFormat::R8Coverage);
  Texture b(std::move(a));
  Texture c(device(), 2, 2, TextureFormat::R8Coverage);
  c = std::move(b);
  expect(c.width() == 4, "move construction and assignment keep the texture");
}

void manyGlyphQuads() {
  // 200 000 glyph quads in one frame (a long text view) used to exceed the 65 536-instance cap and
  // end the application. They must render, with the right pixels, through grown ring buffers.
  std::vector<uint8_t> coverage(16, 255);
  Texture glyph(device(), 4, 4, TextureFormat::R8Coverage, coverage);
  OffscreenTarget target(device(), 512, 512);
  for (int frame = 0; frame < 3; ++frame) {  // repeated: the grown buffers are reused by the next frames of the slot
    expect(target.beginFrame(kBlack), "begin");
    Painter& p = target.painter();
    for (uint32_t i = 0; i < 200000; ++i) {
      const float x = static_cast<float>((i % 128) * 4);
      const float y = static_cast<float>(((i / 128) % 128) * 4);
      p.drawTexture(glyph.ref(), {x, y, 4, 4}, {0, 0, 1, 1}, rgb8(255, 255, 255));
    }
    p.fillRect({500, 500, 4, 4}, rgb8(10, 20, 30));  // after the glyphs: draw order is kept
    expect(p.stats().texInstances == 200000 && p.stats().dropped == 0, "all glyph quads are recorded");
    expect(target.endFrame(), "end");
    const std::vector<uint8_t> px = target.readPixels();
    const auto at = [&](uint32_t x, uint32_t y) { return &px[4 * (size_t{y} * 512 + x)]; };
    expect(at(1, 1)[0] == 255 && at(100, 300)[1] == 255 && at(511, 511)[2] == 255, "the white quads cover the target");
    expect(at(501, 501)[0] == 10 && at(501, 501)[1] == 20 && at(501, 501)[2] == 30, "a later draw lands on top of the glyphs");
  }
}

// ---- failure handling ------------------------------------------------------------------------

// A frame that fails after the texture writes were taken (here: right before the submission) must
// not lose them, and must not leave a texture believing its first write already ran.
void failedFrameKeepsUploads() {
  const uint32_t before = device().validationMessageCount();
  const std::vector<uint8_t> texels = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255};
  Texture image(device(), 2, 2, TextureFormat::Rgba8Srgb, texels);
  OffscreenTarget target(device(), 16, 16);
  expect(target.beginFrame(kBlack), "begin");
  target.painter().drawTexture(image.ref(), {0, 0, 2, 2}, {0, 0, 1, 1});
  device().injectFault(FaultSite::BeforeSubmit);
  expect(throws([&] { target.endFrame(); }), "the injected failure surfaces from endFrame");
  expect(target.beginFrame(kBlack), "begin after the failed frame");
  target.painter().drawTexture(image.ref(), {0, 0, 2, 2}, {0, 0, 1, 1});
  expect(target.endFrame(), "end after the failed frame");
  std::vector<uint8_t> px = target.readPixels();
  const auto at = [&](uint32_t x, uint32_t y) { return &px[4 * (size_t{y} * 16 + x)]; };
  expect(at(0, 0)[0] == 255 && at(0, 0)[1] == 0 && at(1, 0)[1] == 255 && at(0, 1)[2] == 255,
         "the texture writes taken by the failed frame were restored and ran with the next frame");

  // The same for a partial update queued behind the creation write, and for a texture whose
  // creation write already ran (so the retry must transition from the shader-read layout).
  const std::vector<uint8_t> white(4, 255);
  Texture coverage(device(), 4, 4, TextureFormat::R8Coverage, std::vector<uint8_t>(16, 0));
  expect(target.beginFrame(kBlack), "begin");
  target.painter().drawTexture(coverage.ref(), {8, 8, 4, 4}, {0, 0, 1, 1});
  expect(target.endFrame(), "creation write executed");
  coverage.update(1, 1, 2, 2, white);
  expect(target.beginFrame(kBlack), "begin");
  target.painter().drawTexture(coverage.ref(), {8, 8, 4, 4}, {0, 0, 1, 1});
  device().injectFault(FaultSite::BeforeSubmit);
  expect(throws([&] { target.endFrame(); }), "second injected failure");
  expect(target.beginFrame(kBlack), "begin");
  target.painter().drawTexture(coverage.ref(), {8, 8, 4, 4}, {0, 0, 1, 1});
  expect(target.endFrame(), "end");
  px = target.readPixels();
  expect(at(9, 9)[0] == 255 && at(10, 10)[0] == 255 && at(8, 8)[0] == 0, "the partial update survived the failed frame");
  device().waitIdle();
  expect(device().validationMessageCount() == before, "no validation messages (layouts stayed consistent)");
}

// A failed allocation of a frame slot's instance buffer leaves an empty buffer with capacity 0, so
// the next frame allocates again instead of writing through a null mapping.
void ringRegrowFailure() {
  const uint32_t before = device().validationMessageCount();
  OffscreenTarget target(device(), 64, 64);
  const auto draw = [&](uint32_t count) {
    expect(target.beginFrame(kBlack), "begin");
    for (uint32_t i = 0; i < count; ++i) target.painter().fillRect({0, 0, 64, 64}, kWhite);
    return target.endFrame();
  };
  expect(draw(10), "small frame");
  device().injectFault(FaultSite::RingBuffer);
  expect(throws([&] { draw(5000); }), "growing the ring buffer failed");
  // Both frame slots alternate; several frames must recover and render.
  for (int i = 0; i < 4; ++i) expect(draw(5000), "a following large frame renders");
  expect(target.readPixels()[0] == 255, "and shows the content");
  device().waitIdle();
  expect(device().validationMessageCount() == before, "no validation messages");
}

// The swapchain is rebuilt from scratch after a failure part way: no half-built chain is used.
void swapchainBuildFailure() {
  platform::Window window({.title = "r1ui render test fault", .width = 320, .height = 200});
  window.pumpEvents();
  const uint32_t before = device().validationMessageCount();
  WindowTarget target(device(), window);
  expect(target.beginFrame(kBlack), "first frame");
  target.painter().fillRect({0, 0, 40, 40}, kWhite);
  expect(target.endFrame(), "first frame ends");
  for (uint32_t after : {0u, 1u, 2u}) {
    target.invalidate();
    device().injectFault(FaultSite::SwapchainObject, after);
    expect(throws([&] { target.beginFrame(kBlack); }), "building the new swapchain failed");
    // The target must not draw into the half-built chain: the next begin rebuilds it.
    expect(target.beginFrame(kBlack), "the next beginFrame retries and succeeds");
    target.painter().fillRect({0, 0, 50, 50}, rgb8(10, 200, 10));
    expect(target.endFrame(), "and the frame presents");
  }
  expect(target.width() == static_cast<uint32_t>(window.clientWidth()), "the swapchain matches the window");
  device().waitIdle();
  expect(device().validationMessageCount() == before, "no validation messages (nothing leaked, nothing half-built)");
}

// Textures created and destroyed faster than frames complete keep their descriptor sets until the
// GPU is done; the pool must still serve every live texture up to the documented limit, and the
// limit is reported with the documented exception type.
void descriptorPoolChurn() {
  const uint32_t before = device().validationMessageCount();
  for (int i = 0; i < 3 * static_cast<int>(kMaxTextures); ++i) {
    Texture churn(device(), 4, 4, TextureFormat::R8Coverage);  // created and destroyed with no frame in between
  }
  std::vector<std::unique_ptr<Texture>> live;
  bool failed = false;
  try {
    for (uint32_t i = 0; i < kMaxTextures; ++i) live.push_back(std::make_unique<Texture>(device(), 4, 4, TextureFormat::R8Coverage));
  } catch (const std::exception& e) {
    failed = true;
    std::fprintf(stderr, "unexpected failure at texture %zu: %s\n", live.size(), e.what());
  }
  expect(!failed && live.size() == kMaxTextures, "kMaxTextures live textures fit after heavy churn");
  bool invalid = false;
  bool other = false;
  try {
    Texture one_too_many(device(), 4, 4, TextureFormat::R8Coverage);
  } catch (const std::invalid_argument&) {
    invalid = true;
  } catch (...) {
    other = true;
  }
  expect(invalid && !other, "one texture too many is std::invalid_argument, as documented");
  live.clear();
  device().waitIdle();
  expect(device().validationMessageCount() == before, "no validation messages");
}

// Thousands of small writes (a glyph atlas filling up) share staging blocks instead of one GPU
// allocation each: before this took about 0.3 ms per update (1.5 s for 5000).
void manySmallUpdates() {
  const uint32_t before = device().validationMessageCount();
  Texture atlas(device(), 1024, 1024, TextureFormat::R8Coverage);
  OffscreenTarget target(device(), 1024, 8);
  device().flushUploads();
  using Clock = std::chrono::steady_clock;
  const std::vector<uint8_t> tile(8 * 8, 200);
  const auto t0 = Clock::now();
  constexpr uint32_t kUpdates = 5000;
  for (uint32_t i = 0; i < kUpdates; ++i) atlas.update((i % 128) * 8, (i / 128) % 128 * 8, 8, 8, tile);
  expect(target.beginFrame(kBlack), "begin");
  target.painter().drawTexture(atlas.ref(), {0, 0, 1024, 8}, {0, 0, 1, 8.0f / 1024.0f}, kWhite);
  expect(target.endFrame(), "end");
  const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
  std::printf("%u 8x8 atlas updates + one frame: %.1f ms\n", kUpdates, ms);
  expect(ms < 400.0, "5000 small updates stay fast (was 1500 ms with one allocation per update)");
  const std::vector<uint8_t> px = target.readPixels();
  expect(px[4 * (3 * 1024 + 9)] == 200 && px[4 * (3 * 1024 + 9) + 1] == 200, "the updates landed in the texture");
  device().waitIdle();
  expect(device().validationMessageCount() == before, "no validation messages");
}

void batchingStats() {
  OffscreenTarget target(device(), 256, 32);
  expect(target.beginFrame(kBlack), "begin");
  Painter& p = target.painter();
  for (int i = 0; i < 200; ++i) p.fillRect({static_cast<float>(i), 0, 1, 16}, rgb8(static_cast<uint8_t>(i), 255, 0));
  expect(p.stats().instances == 200 && p.stats().drawCalls == 1, "200 adjacent rects merge into one draw call");
  expect(target.endFrame(), "end");
  const std::vector<uint8_t> merged = target.readPixels();
  expect(merged[4 * 100] == 100 && merged[4 * 199 + 1] == 255 && merged[4 * 200] == 0, "merged draws are still correct");

  expect(target.beginFrame(kBlack), "begin");
  for (int i = 0; i < 50; ++i) {
    p.pushClip({static_cast<float>(i), 0, 100, 100});
    p.fillRect({0, 0, 256, 16}, kWhite);
    p.popClip();
  }
  expect(p.stats().drawCalls == 50, "every distinct clip is its own draw call");
  expect(target.endFrame(), "end");
}

void hostileInputs() {
  const Image nonsense = render(32, 32, kBlack, [](Painter& p) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    p.fillRect({nan, 0, 10, 10}, kWhite);
    p.fillRect({0, 0, -10, 10}, kWhite);
    p.fillRect({0, 0, 0, 0}, kWhite);
    p.border({0, 0, 10, 10}, CornerRadii{}, nan, kWhite);
    p.shadow({8, 8, 8, 8}, CornerRadii{}, {0, 0, std::numeric_limits<float>::infinity(), 0, kWhite});
    p.line(0, 0, nan, 5, 1, kWhite);
  });
  bool clean = true;
  for (uint32_t i = 0; i < 32 * 32; ++i) clean = clean && nonsense.rgba[4 * i] == 0;
  expect(clean, "NaN, negative and empty draws leave the frame untouched");

  const Image huge = render(32, 32, kBlack, [](Painter& p) { p.fillRect({-1e9f, -1e9f, 2e9f, 2e9f}, kWhite); });
  expect(huge.is(0, 0, 255, 255, 255) && huge.is(31, 31, 255, 255, 255) && huge.is(16, 16, 255, 255, 255), "a huge rect covers the target");

  const Rect pill{4, 4, 40, 20};
  const Image oversized = render(48, 28, kBlack, [&](Painter& p) { p.fillRoundedRect(pill, CornerRadii::uniform(100), kWhite); });
  const CornerRadii normal = reference::normalizeRadii(pill, CornerRadii::uniform(100));
  const Difference d = compare(oversized, [&](uint32_t x, uint32_t y) {
    const double v = 255.0 * reference::fillCoverage(x + 0.5, y + 0.5, pill, normal);
    return std::array<double, 3>{v, v, v};
  });
  expect(d.maxAbs <= 1.5, "a radius larger than the rect gives a pill: " + describe(d));

  OffscreenTarget target(device(), 16, 16);
  expect(target.beginFrame(kBlack), "begin");
  for (uint32_t i = 0; i < kMaxInstancesPerFrame + 10; ++i) target.painter().fillRect({0, 0, 1, 1}, kWhite);
  expect(target.painter().stats().dropped == 10, "draws beyond the documented maximum are counted, not thrown");
  expect(target.endFrame() && target.readPixels()[0] == 255, "the frame with the maximum number of instances still renders");

  expect(throws([&] { OffscreenTarget t(device(), 0, 8); }), "zero-sized offscreen target is rejected");
  expect(throws([&] { OffscreenTarget t(device(), kMaxOffscreenSide + 1, 8); }), "oversized offscreen target is rejected");
  OffscreenTarget fresh(device(), 8, 8);
  expect(throws([&] { (void)fresh.readPixels(); }), "readPixels before a frame is an error");
  expect(throws([&] { fresh.endFrame(); }), "endFrame without beginFrame is an error");
}

// ---- reference panel -------------------------------------------------------------------------

void panelReference() {
  // Panel from the design tokens: radius 8 (radius.lg), 1 px border #3a3a3a, shadow.lg, fill #2c2c2c
  // on a #1e1e1e canvas.
  const Rect box{32, 24, 160, 100};
  const CornerRadii radii = CornerRadii::uniform(8);
  const Color fill = Color::fromHex(0x2c2c2cff);
  const Color border = Color::fromHex(0x3a3a3aff);
  const ShadowSpec lg1{0, 10, 15, -3, Color::fromHex(0x0000001a)};
  const ShadowSpec lg2{0, 4, 6, -4, Color::fromHex(0x0000001a)};
  const Image img = render(224, 168, Color::fromHex(0x1e1e1eff), [&](Painter& p) {
    p.shadow(box, radii, lg2);  // CSS lists the first shadow on top, so it is painted last
    p.shadow(box, radii, lg1);
    p.fillRoundedRect(box, radii, fill);
    p.border(box, radii, 1.0f, border);
  });
  const Difference d = compare(img, [&](uint32_t x, uint32_t y) {
    const double px = x + 0.5;
    const double py = y + 0.5;
    double v = 0x1e;
    v = shadowAt(v, px, py, box, radii, lg2);
    v = shadowAt(v, px, py, box, radii, lg1);
    v = over(v, 0x2c, reference::fillCoverage(px, py, box, radii));
    v = over(v, 0x3a, reference::borderCoverage(px, py, box, radii, 1.0));
    return std::array<double, 3>{v, v, v};
  });
  // Tolerance: the reference composites in double precision, the GPU quantises to 8 bits after each
  // of the four draws and uses 16-row shadow quadrature: allow max 3 steps and mean 0.35 steps.
  expect(d.maxAbs <= 3.0, "panel matches the analytic composite: " + describe(d));
  expect(d.meanAbs <= 0.35, "panel mean error is small: " + describe(d));
  std::printf("panel reference: %s\n", describe(d).c_str());
  dumpImage("panel_reference", img);
}

// ---- windows ---------------------------------------------------------------------------------

void windowLifecycle() {
  platform::Window window({.title = "r1ui render test", .width = 320, .height = 200});
  window.pumpEvents();
  const uint32_t before = device().validationMessageCount();
  for (int i = 0; i < 200; ++i) {
    WindowTarget target(device(), window);
    expect(target.beginFrame(rgb8(30, 30, 30)), "begin on a fresh target");
    target.painter().fillRoundedRect({10, 10, 100, 60}, CornerRadii::uniform(8), rgb8(200, 80, 40));
    expect(target.endFrame(), "end on a fresh target");
    if (i % 20 == 0) window.pumpEvents();
  }
  {
    WindowTarget target(device(), window);
    const uint32_t start = target.swapchainGeneration();
    for (int i = 0; i < 200; ++i) {
      target.invalidate();
      expect(target.beginFrame(rgb8(30, 30, 30)), "begin after invalidate rebuilds the swapchain");
      target.painter().fillRect({0, 0, 50, 50}, rgb8(10, 200, 10));
      expect(target.endFrame(), "end after rebuild");
    }
    expect(target.swapchainGeneration() == start + 200, "each invalidate produced exactly one new swapchain");
    expect(target.width() == static_cast<uint32_t>(window.clientWidth()) && target.height() == static_cast<uint32_t>(window.clientHeight()),
           "swapchain matches the client size");
  }
  // A window can have only one swapchain at a time, so each present mode gets its own turn.
  for (PresentMode mode : {PresentMode::Mailbox, PresentMode::Immediate, PresentMode::Fifo}) {
    WindowTarget other(device(), window, WindowTargetOptions{mode});
    expect(other.beginFrame(kBlack) && (other.painter().fillRect({0, 0, 4, 4}, kWhite), other.endFrame()), "frame in each present mode");
    expect(other.presentMode() == mode || other.presentMode() == PresentMode::Fifo, "effective present mode is the request or FIFO");
  }
  device().waitIdle();
  expect(device().validationMessageCount() == before, "no validation messages during 400+ swapchain builds");
}

// Per-frame CPU timings of a window target add up to something positive, and the memory budget
// extension reports growing device-local usage when a large texture is created.
void timingsAndMemory() {
  platform::Window window({.title = "r1ui render test timings", .width = 320, .height = 200});
  window.pumpEvents();
  WindowTarget target(device(), window);
  expect(target.beginFrame(kBlack), "begin");
  target.painter().fillRect({0, 0, 40, 40}, kWhite);
  expect(target.endFrame(), "end");
  const FrameTimings t = target.lastFrameTimings();
  expect(t.waitMs >= 0.0 && t.recordSubmitMs > 0.0 && t.presentMs >= 0.0, "timings are measured");
  const GpuMemoryUsage before = device().memoryUsage();
  if (!before.available) {
    std::printf("memory budget extension unavailable on this GPU\n");
    return;
  }
  expect(before.deviceLocalBudgetBytes >= before.deviceLocalUsageBytes && before.deviceLocalUsageBytes > 0, "budget exceeds usage");
  Texture big(device(), 4096, 4096, TextureFormat::Rgba8Srgb);
  device().flushUploads();
  const GpuMemoryUsage after = device().memoryUsage();
  expect(after.deviceLocalUsageBytes >= before.deviceLocalUsageBytes + (uint64_t{32} << 20), "a 64 MiB texture raises the reported usage");
}

void multiWindow() {
  platform::Window first({.title = "r1ui render test A", .width = 300, .height = 180});
  platform::Window second({.title = "r1ui render test B", .width = 220, .height = 260});
  WindowTarget a(device(), first);
  WindowTarget b(device(), second);
  std::vector<uint8_t> shared(16, 255);
  Texture texture(device(), 4, 4, TextureFormat::R8Coverage, shared);
  for (int i = 0; i < 60; ++i) {
    first.pumpEvents();
    second.pumpEvents();
    expect(a.beginFrame(rgb8(20, 20, 60)), "A begin");
    a.painter().drawTexture(texture.ref(), {10, 10, 40, 40}, {0, 0, 1, 1}, rgb8(255, 200, 0));
    expect(a.endFrame(), "A end");
    expect(b.beginFrame(rgb8(60, 20, 20)), "B begin");
    b.painter().drawTexture(texture.ref(), {20, 20, 40, 40}, {0, 0, 1, 1}, rgb8(0, 200, 255));
    b.painter().fillRoundedRect({80, 80, 100, 60}, CornerRadii::uniform(10), rgb8(255, 255, 255, 128));
    expect(b.endFrame(), "B end");
  }
  expect(a.width() == static_cast<uint32_t>(first.clientWidth()) && b.width() == static_cast<uint32_t>(second.clientWidth()),
         "each target follows its own window");
  expect(a.width() != b.width(), "the two windows have different sizes");
  device().waitIdle();
}

// Several windows drawn back to back while another, owned window is created and destroyed on every
// round, as fast as the loop can go and for a few seconds: the pattern that stopped every present of the
// process when a frame's GPU work waited for the presentation engine in the shared queue. Here no call may
// take more than a couple of seconds (a stall is then a failure instead of a hung test), every window must
// keep presenting, and the closing windows must not leak or raise validation messages.
void windowWaitsBounded() {
  using Clock = std::chrono::steady_clock;
  constexpr double kSlowMs = 2000.0;
  const auto since = [](Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); };
  const uint32_t messages = device().validationMessageCount();

  std::vector<std::unique_ptr<platform::Window>> windows;
  std::vector<std::unique_ptr<WindowTarget>> targets;
  for (int i = 0; i < 3; ++i) {
    platform::WindowDesc desc;
    desc.title = "r1ui render test wait " + std::to_string(i);
    desc.width = 260 + 40 * i;
    desc.height = 180;
    desc.position = platform::Point{80 + 60 * i, 80 + 40 * i};
    windows.push_back(std::make_unique<platform::Window>(desc));
    targets.push_back(std::make_unique<WindowTarget>(device(), *windows.back()));
  }
  std::vector<uint64_t> presented(targets.size(), 0);
  double slowest = 0.0;
  const auto frame = [&](WindowTarget& target, platform::Window& window, int round) {
    window.pumpEvents();
    const auto t0 = Clock::now();
    bool shown = false;
    if (target.beginFrame(rgb8(20, 20, 40))) {
      target.painter().fillRect({static_cast<float>(round % 100), 10, 60, 40}, rgb8(200, 120, 20));
      shown = target.endFrame();
    }
    slowest = std::max(slowest, since(t0));
    return shown;
  };

  const auto deadline = Clock::now() + std::chrono::seconds(4);
  int rounds = 0;
  while (Clock::now() < deadline) {
    for (size_t i = 0; i < targets.size(); ++i) presented[i] += frame(*targets[i], *windows[i], rounds) ? 1u : 0u;
    platform::WindowDesc owned;
    owned.title = "r1ui render test owned";
    owned.width = 200;
    owned.height = 140;
    owned.borderless = true;
    owned.toolWindow = true;
    owned.owner = windows.front().get();
    const auto t0 = Clock::now();
    {
      platform::Window transient(owned);
      WindowTarget target(device(), transient);
      for (int k = 0; k < 3; ++k) frame(target, transient, k);
      for (size_t i = 0; i < targets.size(); ++i) presented[i] += frame(*targets[i], *windows[i], rounds) ? 1u : 0u;
    }
    slowest = std::max(slowest, since(t0));
    ++rounds;
  }
  std::printf("window waits: %d rounds, slowest call %.1f ms, frames presented per window %llu/%llu/%llu\n", rounds, slowest,
              static_cast<unsigned long long>(presented[0]), static_cast<unsigned long long>(presented[1]),
              static_cast<unsigned long long>(presented[2]));
  expect(slowest < kSlowMs, "no frame, window creation or window destruction waited more than " + std::to_string(kSlowMs) + " ms");
  for (size_t i = 0; i < presented.size(); ++i) expect(presented[i] > 10, "window " + std::to_string(i) + " keeps presenting");
  targets.clear();
  device().waitIdle();
  expect(device().validationMessageCount() == messages, "no validation messages while windows come and go");
}

// ---- cost ------------------------------------------------------------------------------------

void frameCost() {
  using Clock = std::chrono::steady_clock;
  OffscreenTarget target(device(), 1280, 720);
  double recordMs = 0.0;
  double submitMs = 0.0;
  PaintStats stats;
  constexpr int kFrames = 30;
  for (int f = 0; f < kFrames + 2; ++f) {
    const auto t0 = Clock::now();
    target.beginFrame(rgb8(30, 30, 30));
    Painter& p = target.painter();
    for (int i = 0; i < 2000; ++i) {
      const float x = static_cast<float>((i % 50) * 25);
      const float y = static_cast<float>((i / 50) * 17);
      p.fillRoundedRect({x, y, 22, 14}, CornerRadii::uniform(4), rgb8(static_cast<uint8_t>(i), 120, 200));
    }
    stats = p.stats();
    const auto t1 = Clock::now();
    target.endFrame();
    const auto t2 = Clock::now();
    if (f >= 2) {  // the first frames include pipeline warm-up
      recordMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
      submitMs += std::chrono::duration<double, std::milli>(t2 - t1).count();
    }
  }
  std::printf("2000 rounded rects: %u instances, %u draw calls; CPU painter %.3f ms, endFrame (record+submit+GPU wait) %.3f ms per frame\n",
              stats.instances, stats.drawCalls, recordMs / kFrames, submitMs / kFrames);
  expect(stats.instances == 2000 && stats.drawCalls == 1, "2000 rects are one draw call");
}

}  // namespace

int main(int argc, char** argv) {
  int result = render_test::runCases(
      {{"device_selection", deviceSelection}, {"exact_fill", exactFill},           {"rounded_aa", roundedAa},
       {"border_width", borderWidth},         {"shadow_blur", shadowBlur},         {"clip_correctness", clipCorrectness},
       {"alpha_blend", alphaBlend},           {"textured_quad", texturedQuad},     {"texture_lifetime", textureLifetime},
       {"many_glyph_quads", manyGlyphQuads}, {"failed_frame_keeps_uploads", failedFrameKeepsUploads},
       {"ring_regrow_failure", ringRegrowFailure}, {"swapchain_build_failure", swapchainBuildFailure},
       {"descriptor_pool_churn", descriptorPoolChurn}, {"many_small_updates", manySmallUpdates},
       {"batching_stats", batchingStats},     {"hostile_inputs", hostileInputs},   {"panel_reference", panelReference},
       {"window_lifecycle", windowLifecycle}, {"multi_window", multiWindow},       {"window_waits_bounded", windowWaitsBounded}, {"frame_cost", frameCost},
       {"timings_and_memory", timingsAndMemory}},
      argc, argv);
  if (deviceSlot()) {
    const RenderDevice& d = *deviceSlot();
    std::printf("GPU: %s; validation layer %s; messages: %u\n", d.gpu().name.c_str(), d.validationActive() ? "active" : "inactive",
                d.validationMessageCount());
    if (d.validationMessageCount() != 0) result = 1;
  }
  deviceSlot().reset();
  return result;
}

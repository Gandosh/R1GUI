// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: GPU-free tests of the 2D painter: clip stack math, SDF reference formulas, instance
//   packing, batching rules, limits and hostile numeric input.
// Why: everything the shaders consume is decided on the CPU; these cases pin that contract so the
//   GPU tests only have to prove the shaders agree with it.
// Callers: CTest (label fast). Exit code 0 = pass.
#include <cmath>
#include <limits>
#include <stdexcept>

#include "TestSupport.h"
#include "r1ui/render/ClipStack.h"
#include "r1ui/render/Painter.h"
#include "r1ui/render/SdfReference.h"

using namespace r1ui::render;
using render_test::expect;
using render_test::near;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();
const Color kRed{1, 0, 0, 1};

template <class Fn>
bool throwsLogic(Fn&& fn) {
  try {
    fn();
  } catch (const std::logic_error&) {
    return true;
  }
  return false;
}

// ---- clip stack ------------------------------------------------------------------------------

void clipIntersection() {
  expect(intersect({0, 0, 10, 10}, {5, 5, 10, 10}) == IRect{5, 5, 5, 5}, "overlap");
  expect(intersect({0, 0, 10, 10}, {10, 0, 5, 5}).empty(), "touching edges do not overlap");
  expect(intersect({0, 0, 10, 10}, {20, 20, 5, 5}).empty(), "disjoint");
  expect(intersect({0, 0, 10, 10}, {2, 2, 3, 3}) == IRect{2, 2, 3, 3}, "contained");
  const int32_t big = std::numeric_limits<int32_t>::max();
  expect(intersect({big - 5, 0, 100, 10}, {0, 0, 10, 10}).empty(), "no overflow near int32 max");
  expect(intersect({-100, -100, 50, 50}, {0, 0, 10, 10}).empty(), "negative origin disjoint");
}

void clipSnapping() {
  expect(snapToPixels({1.4f, 2.5f, 10.0f, 10.0f}) == IRect{1, 3, 10, 10}, "halves round up per edge");
  expect(snapToPixels({0.1f, 0.1f, 0.1f, 0.1f}).empty(), "sub-pixel clip collapses");
  expect(snapToPixels({kNaN, 0, 1, 1}).empty(), "NaN clip is empty (fail closed)");
  expect(snapToPixels({0, 0, kInf, 1}).empty(), "inf clip is empty");
  expect(snapToPixels({0, 0, -5, 5}).empty(), "negative width is empty");
  const IRect huge = snapToPixels({-3e9f, -3e9f, 6e9f, 6e9f});
  expect(!huge.empty() && huge.w > 0 && huge.h > 0, "huge clip is clamped, not wrapped");
}

void clipStackNesting() {
  ClipStack stack;
  stack.reset({0, 0, 100, 100});
  expect(stack.current() == IRect{0, 0, 100, 100}, "base clip");
  stack.push({10, 10, 50, 50});
  stack.push({30, 30, 100, 100});
  expect(stack.current() == IRect{30, 30, 30, 30}, "nested push intersects");
  expect(stack.depth() == 2, "depth");
  stack.pop();
  expect(stack.current() == IRect{10, 10, 50, 50}, "pop restores");
  stack.push({200, 200, 5, 5});
  expect(stack.current().empty(), "disjoint push is empty");
  stack.push({0, 0, 100, 100});
  expect(stack.current().empty(), "empty stays empty for nested pushes");
  stack.pop();
  stack.pop();
  stack.pop();
  expect(throwsLogic([&] { stack.pop(); }), "pop of the base clip throws");
  stack.reset({0, 0, 0, 0});
  expect(stack.current().empty(), "empty bounds");
}

// ---- SDF reference ---------------------------------------------------------------------------

void sdfPixelAlignedEdges() {
  const Rect box{8, 8, 16, 16};
  const CornerRadii square{};
  // Pixel (8, 8) has its centre 0.5 inside both edges: exactly covered. Pixel (7, 8) is 0.5 outside.
  expect(reference::fillCoverage(8.5, 8.5, box, square) == 1.0, "edge pixel inside is exactly 1");
  expect(reference::fillCoverage(7.5, 8.5, box, square) == 0.0, "edge pixel outside is exactly 0");
  expect(reference::fillCoverage(23.5, 23.5, box, square) == 1.0, "far edge pixel inside is exactly 1");
  expect(reference::fillCoverage(24.5, 23.5, box, square) == 0.0, "far edge pixel outside is exactly 0");
  expect(near(reference::fillCoverage(8.0, 16.0, box, square), 0.5, 1e-12), "on the edge: half covered");
}

void sdfRoundedCorner() {
  const Rect box{0, 0, 40, 40};
  const CornerRadii radii = CornerRadii::uniform(10);
  expect(reference::fillCoverage(0.5, 0.5, box, radii) == 0.0, "corner pixel is outside the arc");
  const double mid = reference::fillCoverage(2.5, 3.5, box, radii);
  expect(mid > 0.0 && mid < 1.0, "diagonal pixel near the corner is partial");
  // Coverage along the diagonal from the corner inward never decreases.
  double previous = 0.0;
  bool monotonic = true;
  for (int i = 0; i < 12; ++i) {
    const double c = reference::fillCoverage(i + 0.5, i + 0.5, box, radii);
    if (c + 1e-12 < previous) monotonic = false;
    previous = c;
  }
  expect(monotonic, "coverage is monotonic towards the interior");
  expect(near(reference::signedDistance(10.0, 0.0, box, radii), 0.0, 1e-9), "arc start lies on the edge");
  const double arc = 10.0 - 10.0 / std::sqrt(2.0);
  expect(near(reference::signedDistance(arc, arc, box, radii), 0.0, 1e-9), "arc midpoint lies on the edge");
}

void sdfPerCornerRadii() {
  const Rect box{0, 0, 40, 40};
  const CornerRadii radii{12, 0, 0, 0};
  expect(reference::fillCoverage(0.5, 0.5, box, radii) == 0.0, "rounded top-left cuts the corner");
  expect(reference::fillCoverage(39.5, 0.5, box, radii) == 1.0, "square top-right keeps the corner");
  expect(reference::fillCoverage(0.5, 39.5, box, radii) == 1.0, "square bottom-left keeps the corner");
}

void radiusNormalisation() {
  const Rect box{0, 0, 40, 20};
  const CornerRadii pill = reference::normalizeRadii(box, CornerRadii::uniform(100));
  expect(near(pill.topLeft, 10.0, 1e-6) && near(pill.bottomRight, 10.0, 1e-6), "oversized radius becomes half the short side");
  const CornerRadii skew = reference::normalizeRadii(box, CornerRadii{30, 30, 0, 0});
  expect(near(skew.topLeft, 20.0, 1e-6) && near(skew.topRight, 20.0, 1e-6) && skew.bottomLeft == 0.0f,
         "the tightest side scales every radius equally");
  const CornerRadii negative = reference::normalizeRadii(box, CornerRadii{-5, 4, -1, 2});
  expect(negative.topLeft == 0.0f && negative.bottomRight == 0.0f && negative.topRight == 4.0f, "negative radii become 0");
  const CornerRadii fits = reference::normalizeRadii(box, CornerRadii{4, 4, 4, 4});
  expect(fits.topLeft == 4.0f, "radii that fit are untouched");
}

void sdfBorderRing() {
  const Rect box{0, 0, 40, 40};
  const CornerRadii square{};
  expect(reference::borderCoverage(0.5, 20.5, box, square, 2.0) == 1.0, "outer pixel of a 2 px border");
  expect(reference::borderCoverage(1.5, 20.5, box, square, 2.0) == 1.0, "second pixel of a 2 px border");
  expect(reference::borderCoverage(2.5, 20.5, box, square, 2.0) == 0.0, "interior is empty");
  expect(reference::borderCoverage(20.5, 20.5, box, square, 2.0) == 0.0, "centre is empty");
  expect(reference::borderCoverage(20.5, 20.5, box, square, 25.0) == 1.0, "a border wider than the box fills it");
}

void sdfBlurredBox() {
  const Rect box{40, 40, 40, 40};
  const CornerRadii square{};
  const double sigma = 4.0;
  expect(reference::blurredBoxCoverage(60, 60, box, square, sigma) > 0.999, "deep inside is solid");
  expect(reference::blurredBoxCoverage(0, 0, box, square, sigma) < 1e-6, "far outside is empty");
  expect(near(reference::blurredBoxCoverage(40, 60, box, square, sigma), 0.5, 2e-3), "half way on a straight edge");
  expect(near(reference::blurredBoxCoverage(40, 40, box, square, sigma), 0.25, 2e-3), "a square corner is a quarter");
  const double left = reference::blurredBoxCoverage(34, 60, box, square, sigma);
  const double right = reference::blurredBoxCoverage(86, 60, box, square, sigma);
  expect(near(left, right, 1e-3), "symmetric around the box");
  // Matches the closed form on a straight edge: 0.5 * erfc(d / (sigma * sqrt 2)).
  expect(near(reference::blurredBoxCoverage(34, 60, box, square, sigma), 0.5 * std::erfc(6.0 / (sigma * std::sqrt(2.0))), 2e-3),
         "straight edge matches the erfc closed form");
  expect(reference::blurredBoxCoverage(60, 60, box, square, 0.0) == 1.0, "zero sigma degenerates to the hard shape");
  const CornerRadii round = CornerRadii::uniform(20);
  expect(reference::blurredBoxCoverage(40.5, 40.5, box, round, sigma) < reference::blurredBoxCoverage(40.5, 40.5, box, square, sigma),
         "a rounded corner casts less shadow than a square one");
}

void sdfLine() {
  expect(reference::lineCoverage(10.5, 5.5, 0, 5.5, 20, 5.5, 1.0) == 1.0, "1 px horizontal line on a pixel row");
  expect(reference::lineCoverage(10.5, 6.5, 0, 5.5, 20, 5.5, 1.0) == 0.0, "next row is empty");
  expect(reference::lineCoverage(25.0, 5.5, 0, 5.5, 20, 5.5, 1.0) == 0.0, "butt cap ends at the endpoint");
  expect(reference::lineCoverage(0, 0, 3, 3, 3, 3, 2.0) == 0.0, "zero length line covers nothing");
}

// ---- painter: packing ------------------------------------------------------------------------

void packingFill() {
  Painter p;
  p.begin(100, 100);
  p.fillRoundedRect({10, 20, 30, 40}, {1, 2, 3, 4}, {0.5f, 0.25f, 1.0f, 0.5f});
  p.end();
  const PaintList& list = p.list();
  expect(list.sdf.size() == 1 && list.tex.empty() && list.batches.size() == 1, "one sdf instance, one batch");
  const SdfInstance& i = list.sdf[0];
  expect(i.rect[0] == 10 && i.rect[1] == 20 && i.rect[2] == 30 && i.rect[3] == 40, "rect packed as x y w h");
  expect(i.radii[0] == 1 && i.radii[1] == 2 && i.radii[2] == 3 && i.radii[3] == 4, "radii packed tl tr br bl");
  expect(i.color[0] == 0.5f && i.color[1] == 0.25f && i.color[2] == 1.0f && i.color[3] == 0.5f, "straight colour");
  expect(i.params[0] == static_cast<float>(SdfKind::Fill), "fill kind");
  expect(list.batches[0].scissor == IRect{0, 0, 100, 100}, "scissor is the target");
}

void packingBorder() {
  Painter p;
  p.begin(100, 100);
  p.border({10, 10, 40, 30}, CornerRadii::uniform(6), 2.0f, kRed);
  p.border({10, 10, 40, 30}, CornerRadii::uniform(6), 2.0f, kRed, false);
  p.border({10, 10, 4, 30}, CornerRadii{}, 50.0f, kRed);
  p.end();
  const auto& sdf = p.list().sdf;
  expect(sdf.size() == 3, "three borders");
  expect(sdf[0].params[0] == static_cast<float>(SdfKind::Border) && sdf[0].params[1] == 2.0f, "inside border keeps its box");
  expect(sdf[0].rect[0] == 10 && sdf[0].rect[2] == 40, "inside border outer box is the rect");
  expect(sdf[1].rect[0] == 8 && sdf[1].rect[2] == 44 && sdf[1].radii[0] == 8.0f, "outside border grows box and radius");
  expect(sdf[2].params[1] == 2.0f, "border wider than half the box is limited to half the smaller side");
}

void packingShadow() {
  Painter p;
  p.begin(200, 200);
  // tokens.json shadow "lg", first layer: [0, 10, 15, -3, #0000001a].
  p.shadow({50, 50, 100, 80}, CornerRadii::uniform(8), {0, 10, 15, -3, Color::fromHex(0x0000001a)});
  p.shadow({50, 50, 100, 80}, CornerRadii{}, {2, 3, 0, 4, Color{0, 0, 0, 1}});
  p.end();
  const auto& sdf = p.list().sdf;
  expect(sdf.size() == 2, "two shadows");
  const SdfInstance& a = sdf[0];
  expect(a.params[0] == static_cast<float>(SdfKind::Shadow), "shadow kind");
  expect(a.rect[0] == 53 && a.rect[1] == 63 && a.rect[2] == 94 && a.rect[3] == 74, "offset then negative spread shrinks the box");
  expect(a.radii[0] == 5.0f, "rounded corner shrinks with the spread");
  expect(near(a.params[1], 7.5, 1e-6), "sigma is half the CSS blur radius");
  expect(near(a.color[3], 0x1a / 255.0, 1e-6), "alpha from #0000001a");
  expect(a.knockRect[0] == 50 && a.knockRect[2] == 100 && a.knockRadii[0] == 8.0f, "knock-out is the source box");
  const SdfInstance& b = sdf[1];
  expect(b.rect[0] == 48 && b.rect[2] == 108 && b.radii[0] == 0.0f, "positive spread grows; square corner stays square");
  expect(b.params[1] == 0.0f, "zero blur means a hard shadow (sigma 0)");
}

void packingTexture() {
  Painter p;
  p.begin(100, 100);
  p.drawTexture({7, TextureKind::Coverage}, {10, 10, 8, 8}, {0.25f, 0.5f, 0.5f, 0.25f}, {1, 0.5f, 0, 1});
  p.drawTexture({9, TextureKind::Color}, {20, 10, 8, 8}, {0, 0, 1, 1});
  p.end();
  const auto& tex = p.list().tex;
  expect(tex.size() == 2 && p.list().batches.size() == 2, "two textures, two batches");
  expect(tex[0].uv[0] == 0.25f && tex[0].uv[1] == 0.5f && tex[0].uv[2] == 0.75f && tex[0].uv[3] == 0.75f, "uv rect packed as u0 v0 u1 v1");
  expect(tex[0].params[0] == static_cast<float>(TexMode::Coverage) && tex[1].params[0] == static_cast<float>(TexMode::Color), "mode follows kind");
  expect(p.list().batches[0].textureId == 7 && p.list().batches[1].textureId == 9, "batches carry texture ids");
}

void textureCropFollowsClampedDestination() {
  // A destination beyond the coordinate limit is clamped; the texture window is cropped with it
  // instead of being stretched over the clamped rectangle.
  Painter p;
  p.begin(100, 100);
  p.drawTexture({7, TextureKind::Color}, {-1.0e6f, 0.0f, 2.0e6f, 10.0f}, {0.0f, 0.0f, 1.0f, 1.0f});
  p.drawTexture({7, TextureKind::Color}, {10.0f, 0.0f, 20.0f, 10.0f}, {0.0f, 0.0f, 1.0f, 1.0f});
  p.end();
  const auto& tex = p.list().tex;
  expect(tex.size() == 2, "both quads recorded");
  // x range [-65536, 65536] of [-1e6, 1e6]: u from (1e6-65536)/2e6 to (1e6+65536)/2e6.
  expect(near(tex[0].uv[0], 0.467232f, 1e-5f) && near(tex[0].uv[2], 0.532768f, 1e-5f), "u window cropped with the destination");
  expect(tex[0].uv[1] == 0.0f && tex[0].uv[3] == 1.0f, "v unchanged when only x was clamped");
  expect(tex[1].uv[0] == 0.0f && tex[1].uv[2] == 1.0f, "an unclamped quad keeps its uv exactly");
}

void packingLine() {
  Painter p;
  p.begin(100, 100);
  p.line(5, 5.5f, 60, 5.5f, 1.0f, kRed);
  p.end();
  const SdfInstance& i = p.list().sdf[0];
  expect(i.params[0] == static_cast<float>(SdfKind::Line) && i.params[1] == 1.0f, "line kind and width");
  expect(i.rect[0] == 5 && i.rect[2] == 60 && i.rect[1] == 5.5f, "endpoints packed");
}

void opacityAndColour() {
  Painter p;
  p.begin(100, 100);
  p.pushOpacity(0.5f);
  p.pushOpacity(0.5f);
  p.fillRect({0, 0, 10, 10}, {2.0f, -1.0f, 0.5f, 1.0f});
  p.popOpacity();
  p.fillRect({0, 0, 10, 10}, kRed);
  p.popOpacity();
  p.pushOpacity(kNaN);
  p.fillRect({0, 0, 10, 10}, kRed);
  p.popOpacity();
  p.end();
  const auto& sdf = p.list().sdf;
  expect(sdf.size() == 2, "NaN opacity draws nothing");
  expect(near(sdf[0].color[3], 0.25, 1e-6), "nested opacity multiplies");
  expect(sdf[0].color[0] == 1.0f && sdf[0].color[1] == 0.0f, "colour channels clamp to 0..1");
  expect(near(sdf[1].color[3], 0.5, 1e-6), "popped opacity returns to the outer value");
  expect(p.stats().culled == 1, "invisible draw counted as culled");
}

// ---- painter: batching -----------------------------------------------------------------------

void batchingMergesAdjacent() {
  Painter p;
  p.begin(1000, 100);
  for (int i = 0; i < 100; ++i) p.fillRect({static_cast<float>(i * 10), 0, 10, 10}, kRed);
  p.end();
  const PaintStats s = p.stats();
  expect(s.instances == 100 && s.drawCalls == 1, "100 adjacent rects share one draw call");
  expect(p.list().batches[0].count == 100 && p.list().batches[0].first == 0, "batch spans all instances");
}

void batchingSplits() {
  Painter p;
  p.begin(200, 200);
  p.fillRect({0, 0, 10, 10}, kRed);
  p.pushClip({0, 0, 50, 50});
  p.fillRect({0, 0, 10, 10}, kRed);
  p.fillRect({20, 0, 10, 10}, kRed);
  p.popClip();
  p.fillRect({0, 0, 10, 10}, kRed);
  p.drawTexture({3, TextureKind::Color}, {0, 0, 10, 10}, {0, 0, 1, 1});
  p.drawTexture({3, TextureKind::Color}, {10, 0, 10, 10}, {0, 0, 1, 1});
  p.drawTexture({4, TextureKind::Color}, {20, 0, 10, 10}, {0, 0, 1, 1});
  p.fillRect({0, 0, 10, 10}, kRed);
  p.end();
  const auto& b = p.list().batches;
  expect(b.size() == 6, "clip change, texture change and kind change each start a batch");
  expect(b[1].count == 2 && b[1].scissor == IRect{0, 0, 50, 50}, "clipped run merges under one scissor");
  expect(b[3].count == 2 && b[3].textureId == 3, "same texture merges");
  expect(p.stats().drawCalls == 6 && p.stats().sdfInstances == 5 && p.stats().texInstances == 3, "stats");
}

void batchingClipCulling() {
  Painter p;
  p.begin(100, 100);
  p.pushClip({10, 10, 20, 20});
  p.fillRect({50, 50, 10, 10}, kRed);
  p.fillRect({0, 0, 10, 10}, kRed);
  p.fillRect({5, 5, 10, 10}, kRed);
  p.popClip();
  p.fillRect({200, 200, 10, 10}, kRed);
  p.end();
  expect(p.stats().instances == 1 && p.stats().culled == 3, "fully clipped and off-target draws are culled");
}

// ---- painter: limits and hostile input -------------------------------------------------------

void limitDegradesInsteadOfThrowing() {
  Painter p(8);
  p.begin(100, 100);
  for (int i = 0; i < 8; ++i) p.fillRect({0, 0, 5, 5}, kRed);
  bool threw = false;
  try {
    p.fillRect({0, 0, 5, 5}, kRed);
    p.border({0, 0, 5, 5}, {}, 1.0f, kRed);
    p.shadow({0, 0, 5, 5}, {}, ShadowSpec{0, 0, 4, 0, kRed});
    p.line(0, 0, 9, 9, 1.0f, kRed);
    p.drawTexture(TextureRef{7, TextureKind::Coverage}, {0, 0, 5, 5}, {0, 0, 1, 1});
  } catch (...) {
    threw = true;
  }
  expect(!threw, "draws past the limit do not throw");
  expect(p.stats().instances == 8 && p.stats().dropped == 5, "earlier instances stay valid and the rest is counted");
  p.end();
  p.begin(100, 100);
  expect(p.stats().dropped == 0, "the drop counter is per frame");
  p.fillRect({0, 0, 5, 5}, kRed);
  p.end();

  // 200 000 glyph quads (a long text view) fit in one frame: the old 65 536 cap ended the app.
  Painter glyphs;
  glyphs.begin(2000, 2000);
  const TextureRef atlas{3, TextureKind::Coverage};
  for (uint32_t i = 0; i < 200000; ++i) {
    glyphs.drawTexture(atlas, {static_cast<float>(i % 1900), static_cast<float>((i / 1900) % 1900), 6, 10}, {0, 0, 0.01f, 0.01f});
  }
  glyphs.end();
  expect(glyphs.stats().texInstances == 200000 && glyphs.stats().dropped == 0, "200 000 glyph quads are recorded");
  expect(glyphs.stats().drawCalls == 1, "and stay one draw call");

  // The documented maximum is enforced by dropping, never by throwing.
  Painter big(kMaxInstancesPerFrame * 4);  // the constructor clamps the larger request
  big.begin(10, 10);
  for (uint32_t i = 0; i < kMaxInstancesPerFrame + 10; ++i) big.fillRect({0, 0, 5, 5}, kRed);
  big.end();
  expect(big.stats().instances == kMaxInstancesPerFrame && big.stats().dropped == 10, "the documented maximum is enforced");
}

void hostileNumbers() {
  Painter p;
  p.begin(100, 100);
  p.fillRect({kNaN, 0, 10, 10}, kRed);
  p.fillRect({0, 0, kInf, 10}, kRed);
  p.fillRoundedRect({0, 0, 10, 10}, {kNaN, 0, 0, 0}, kRed);
  p.fillRect({0, 0, 10, 10}, {kNaN, 0, 0, 1});
  p.border({0, 0, 10, 10}, {}, kNaN, kRed);
  p.shadow({0, 0, 10, 10}, {}, {0, 0, kInf, 0, kRed});
  p.line(0, 0, kNaN, 5, 1, kRed);
  p.drawTexture({1, TextureKind::Color}, {0, 0, 10, 10}, {kNaN, 0, 1, 1});
  expect(p.stats().rejected == 8 && p.stats().instances == 0, "every NaN/inf draw is rejected and counted");
  p.fillRect({0, 0, 0, 10}, kRed);
  p.fillRect({0, 0, -5, 10}, kRed);
  p.fillRect({0, 0, 10, -1}, kRed);
  p.border({0, 0, 10, 10}, {}, 0.0f, kRed);
  p.border({0, 0, 10, 10}, {}, -2.0f, kRed);
  p.line(1, 1, 1, 1, 2, kRed);
  p.shadow({0, 0, 10, 10}, {}, {0, 0, 4, -20, kRed});
  expect(p.stats().instances == 0 && p.stats().culled == 7, "zero, negative and collapsed sizes are culled");
  p.fillRect({-3e9f, -3e9f, 6e9f, 6e9f}, kRed);
  p.fillRect({1e30f, 0, 5, 5}, kRed);
  p.end();
  expect(p.stats().instances == 1, "a huge rect covering the target is kept (the other is off-target)");
  const SdfInstance& i = p.list().sdf[0];
  bool finite = true;
  for (float v : {i.rect[0], i.rect[1], i.rect[0] + i.rect[2], i.rect[1] + i.rect[3]}) {
    finite = finite && std::isfinite(v) && std::fabs(v) <= 65536.0f;
  }
  expect(finite, "huge coordinates are clamped to a finite range");
}

void apiMisuse() {
  Painter p;
  expect(throwsLogic([&] { p.fillRect({0, 0, 1, 1}, kRed); }), "drawing before begin throws");
  p.begin(10, 10);
  expect(throwsLogic([&] { p.popClip(); }), "popClip without push throws");
  expect(throwsLogic([&] { p.popOpacity(); }), "popOpacity without push throws");
  bool invalid = false;
  try {
    p.drawTexture({}, {0, 0, 1, 1}, {0, 0, 1, 1});
  } catch (const std::invalid_argument&) {
    invalid = true;
  }
  expect(invalid, "invalid texture ref throws");
  p.pushClip({0, 0, 5, 5});
  expect(throwsLogic([&] { p.end(); }), "end with an open clip throws");
  expect(throwsLogic([&] { p.fillRect({0, 0, 1, 1}, kRed); }), "drawing after end throws");
  p.begin(0, 0);
  p.fillRect({0, 0, 10, 10}, kRed);
  p.end();
  expect(p.stats().instances == 0 && p.stats().culled == 1, "a zero-sized target culls everything");
}

void displayScale() {
  const DisplayScale s{1.5f};
  expect(s.toPhysical(10) == 15.0f, "toPhysical multiplies");
  expect(s.snapped(1) == 2.0f && s.snapped(0.1f) == 1.0f, "snapped rounds and keeps thin lines visible");
  expect(s.snapped(0) == 0.0f && s.snapped(-3) == 0.0f && s.snapped(kNaN) == 0.0f, "non-positive and NaN snap to 0");
}

}  // namespace

int main(int argc, char** argv) {
  return render_test::runCases(
      {{"clipIntersection", clipIntersection},     {"clipSnapping", clipSnapping},
       {"clipStackNesting", clipStackNesting},     {"sdfPixelAlignedEdges", sdfPixelAlignedEdges},
       {"sdfRoundedCorner", sdfRoundedCorner},     {"sdfPerCornerRadii", sdfPerCornerRadii},
       {"radiusNormalisation", radiusNormalisation}, {"sdfBorderRing", sdfBorderRing},
       {"sdfBlurredBox", sdfBlurredBox},           {"sdfLine", sdfLine},
       {"packingFill", packingFill},               {"packingBorder", packingBorder},
       {"packingShadow", packingShadow},           {"packingTexture", packingTexture},
       {"packingLine", packingLine}, {"textureCropFollowsClampedDestination", textureCropFollowsClampedDestination},               {"opacityAndColour", opacityAndColour},
       {"batchingMergesAdjacent", batchingMergesAdjacent}, {"batchingSplits", batchingSplits},
       {"batchingClipCulling", batchingClipCulling}, {"limitDegradesInsteadOfThrowing", limitDegradesInsteadOfThrowing},
       {"hostileNumbers", hostileNumbers},         {"apiMisuse", apiMisuse},
       {"displayScale", displayScale}},
      argc, argv);
}

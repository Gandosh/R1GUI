// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for glyph rasterization: bitmap bounds and coverage for "Rectangle" at 12 px,
//   bit-identical repeat runs, 1/4 px subpixel shifts, synthetic bold growth, and bad parameters.
// Callers: CTest (label fast). Exit code 0 = pass.
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

#include "TestSupport.h"
#include "r1ui/text/Rasterizer.h"
#include "r1ui/text/Shaper.h"

using namespace r1ui::text;
using namespace r1ui::text::testing;

namespace {

long long coverageSum(const GlyphBitmap& b) {
  return std::accumulate(b.coverage.begin(), b.coverage.end(), 0LL);
}

// Horizontal centre of mass of the ink, in pixels relative to the pen (bearing included).
double centroidX(const GlyphBitmap& b) {
  double weighted = 0;
  double total = 0;
  for (int y = 0; y < b.height; ++y) {
    for (int x = 0; x < b.width; ++x) {
      const double c = b.coverage[static_cast<std::size_t>(y) * static_cast<std::size_t>(b.width) + static_cast<std::size_t>(x)];
      weighted += c * (b.left + x + 0.5);
      total += c;
    }
  }
  return total > 0 ? weighted / total : 0.0;
}

std::vector<GlyphBitmap> renderAll(const Font& font, const ShapedRun& run, const RasterParams& p) {
  std::vector<GlyphBitmap> out;
  for (const ShapedGlyph& g : run.glyphs) {
    auto r = rasterizeGlyph(font, g.glyphId, p);
    expect(r.ok(), "rasterize glyph");
    if (r.ok()) out.push_back(r.value());
  }
  return out;
}

void testRectangle(const Font& font) {
  const ShapedRun run = shapeText(font, 12, "Rectangle").value();
  RasterParams p;
  p.pixelSize = 12;
  const std::vector<GlyphBitmap> bitmaps = renderAll(font, run, p);
  expect(bitmaps.size() == 9, "9 bitmaps");
  if (bitmaps.size() != 9) return;

  const GlyphBitmap& capR = bitmaps[0];
  expect(capR.width >= 6 && capR.width <= 9, "R width about 7-8 px at 12 px");
  expect(capR.height >= 9 && capR.height <= 10, "R height about the 8.7 px cap height");
  expect(capR.top >= 9 && capR.top <= 10, "R sits on the baseline (top = cap height rounded up)");
  expect(capR.left >= 0 && capR.left <= 2, "R left bearing small and non-negative");
  expect(capR.coverage.size() == static_cast<std::size_t>(capR.width * capR.height), "tight packing");

  int maxCoverage = 0;
  for (const std::uint8_t c : capR.coverage) maxCoverage = std::max<int>(maxCoverage, c);
  expect(maxCoverage >= 200, "near-full coverage reached inside the stem");
  expect(coverageSum(capR) > 800, "R has substantial ink");

  const GlyphBitmap& e = bitmaps[1];
  expect(e.height >= 6 && e.height <= 8, "x-height glyph is about 6.5 px tall");
  const GlyphBitmap& l = bitmaps[7];
  expect(l.height >= 9 && l.height <= 10, "ascender l is taller than x-height");
  for (const GlyphBitmap& b : bitmaps) expect(b.width > 0 && b.height > 0 && coverageSum(b) > 0, "every letter has ink");

  // Two runs are bit-identical, also from a fresh library.
  const std::vector<GlyphBitmap> again = renderAll(font, run, p);
  bool same = again.size() == bitmaps.size();
  for (std::size_t i = 0; same && i < bitmaps.size(); ++i) {
    same = again[i].width == bitmaps[i].width && again[i].height == bitmaps[i].height &&
           again[i].left == bitmaps[i].left && again[i].top == bitmaps[i].top && again[i].coverage == bitmaps[i].coverage;
  }
  expect(same, "two rasterizations are bit-identical");

  FontLibrary fresh;
  const FontHandle other = loadInter(fresh, "Inter-Regular.ttf", 400);
  const ShapedRun run2 = shapeText(*other, 12, "Rectangle").value();
  const std::vector<GlyphBitmap> third = renderAll(*other, run2, p);
  bool same2 = third.size() == bitmaps.size();
  for (std::size_t i = 0; same2 && i < bitmaps.size(); ++i) same2 = third[i].coverage == bitmaps[i].coverage;
  expect(same2, "a fresh library gives the same pixels");

  // A space has no ink.
  const ShapedRun space = shapeText(font, 12, " ").value();
  auto sp = rasterizeGlyph(font, space.glyphs[0].glyphId, p);
  expect(sp.ok() && sp.value().width == 0 && sp.value().height == 0 && sp.value().coverage.empty(), "space is empty");
}

void testSubpixelAndBold(const Font& font) {
  // A large, wide glyph keeps the centroid estimate (coverage sampled at pixel centres) accurate.
  const std::uint32_t glyph = shapeText(font, 14, "H").value().glyphs[0].glyphId;
  RasterParams p;
  p.pixelSize = 48;
  const GlyphBitmap base = rasterizeGlyph(font, glyph, p).value();
  const double c0 = centroidX(base);
  for (int bin = 1; bin < kSubpixelBins; ++bin) {
    p.subpixelBin = bin;
    const GlyphBitmap shifted = rasterizeGlyph(font, glyph, p).value();
    expect(std::abs((centroidX(shifted) - c0) - bin / 4.0) < 0.03, "bin shifts the ink by a quarter pixel per step");
    expect(coverageSum(shifted) > coverageSum(base) * 95 / 100 && coverageSum(shifted) < coverageSum(base) * 105 / 100,
           "subpixel shift preserves total ink");
  }

  p.subpixelBin = 0;
  p.emboldenPx = 0.0f;
  expect(rasterizeGlyph(font, glyph, p).value().coverage == base.coverage, "embolden 0 equals plain");
  p.emboldenPx = 48.0f / 24.0f;
  const GlyphBitmap bold = rasterizeGlyph(font, glyph, p).value();
  expect(coverageSum(bold) > coverageSum(base) * 112 / 100, "bold has clearly more ink");
  expect(coverageSum(bold) < coverageSum(base) * 2, "bold is not absurdly heavy");
  expect(bold.width >= base.width + 1 && bold.width <= base.width + 4, "bold grows the box by about the strength (2 px)");
  expect(bold.height >= base.height + 1 && bold.height <= base.height + 4, "bold grows the height by about the strength");

  p.emboldenPx = 48.0f;
  expect(rasterizeGlyph(font, glyph, p).ok(), "embolden equal to the size is accepted");
}

void testBadParameters(const Font& font) {
  RasterParams p;
  for (const float size : {0.0f, -4.0f, std::nanf(""), 5000.0f}) {
    p.pixelSize = size;
    expect(!rasterizeGlyph(font, 1, p).ok(), "bad size rejected");
  }
  p = RasterParams{};
  p.subpixelBin = -1;
  expect(!rasterizeGlyph(font, 1, p).ok(), "bin -1");
  p.subpixelBin = kSubpixelBins;
  expect(!rasterizeGlyph(font, 1, p).ok(), "bin 4");
  p = RasterParams{};
  p.emboldenPx = -1.0f;
  expect(!rasterizeGlyph(font, 1, p).ok(), "negative embolden");
  p.emboldenPx = std::nanf("");
  expect(!rasterizeGlyph(font, 1, p).ok(), "NaN embolden");
  p.emboldenPx = 99.0f;
  expect(!rasterizeGlyph(font, 1, p).ok(), "embolden above size");
  p = RasterParams{};
  expect(!rasterizeGlyph(font, font.glyphCount(), p).ok(), "glyph id past the end");
  expect(!rasterizeGlyph(font, 0xFFFFFFFFu, p).ok(), "huge glyph id");

  // The largest supported size still renders (bitmap bounded by the 4096 px guard).
  p.pixelSize = 1024.0f;
  const std::uint32_t w = shapeText(font, 12, "W").value().glyphs[0].glyphId;
  auto big = rasterizeGlyph(font, w, p);
  expect(big.ok() && big.value().width > 500 && big.value().width <= 1100, "1024 px glyph renders");

  // Every glyph in the font renders at 12 px without failure (also exercises composite glyphs).
  p = RasterParams{};
  int failed = 0;
  for (std::uint32_t g = 0; g < font.glyphCount(); g += 7) failed += rasterizeGlyph(font, g, p).ok() ? 0 : 1;
  expect(failed == 0, "sampled glyphs of the whole font rasterize");
}

}  // namespace

int main() {
  FontLibrary lib;
  const FontHandle font = loadInter(lib, "Inter-Regular.ttf", 400);
  testRectangle(*font);
  testSubpixelAndBold(*font);
  testBadParameters(*font);
  return finish("raster");
}

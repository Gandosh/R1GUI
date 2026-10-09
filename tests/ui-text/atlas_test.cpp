// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for ShelfPacker (no overlap, bounds, reuse, hostile sizes), GlyphAtlas (cache,
//   padding, dirty rectangle, LRU eviction, "atlas full", frame pinning) and buildGlyphQuads.
// Callers: CTest (label fast). Exit code 0 = pass.
#include <cmath>
#include <cstdint>
#include <vector>

#include "TestSupport.h"
#include "r1ui/text/GlyphAtlas.h"
#include "r1ui/text/GlyphQuads.h"
#include "r1ui/text/Shaper.h"
#include "r1ui/text/ShelfPacker.h"

using namespace r1ui::text;
using namespace r1ui::text::testing;

namespace {

// ---- ShelfPacker ----------------------------------------------------------------------------

// Occupancy grid mirror of the live rectangles; verifies bounds and absence of overlap.
class Occupancy {
 public:
  Occupancy(int w, int h) : w_(w), h_(h), cells_(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0) {}
  bool mark(const PackedRect& r, int delta) {
    if (r.x < 0 || r.y < 0 || r.x + r.w > w_ || r.y + r.h > h_) return false;
    for (int y = r.y; y < r.y + r.h; ++y) {
      for (int x = r.x; x < r.x + r.w; ++x) {
        int& c = cells_[static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) + static_cast<std::size_t>(x)];
        c += delta;
        if (c < 0 || c > 1) return false;
      }
    }
    return true;
  }

 private:
  int w_;
  int h_;
  std::vector<int> cells_;
};

void testPackerBasics() {
  ShelfPacker packer(128, 128);
  Occupancy occ(128, 128);
  const auto a = packer.allocate(30, 10);
  const auto b = packer.allocate(30, 10);
  expect(a && b && occ.mark(*a, 1) && occ.mark(*b, 1), "two rectangles fit without overlap");
  expect(a->y == b->y && a->x != b->x, "same-height rectangles share a shelf");
  expect(packer.liveCount() == 2 && packer.liveArea() == 600, "accounting");

  expect(!packer.allocate(0, 5) && !packer.allocate(5, 0) && !packer.allocate(-1, 5) && !packer.allocate(5, -1),
         "zero or negative sizes");
  expect(!packer.allocate(129, 5) && !packer.allocate(5, 129), "larger than the atlas");
  expect(packer.allocate(128, 128).has_value() == false, "no room for a full-size rectangle while others live");

  expect(packer.release(*a) && occ.mark(*a, -1), "release");
  expect(!packer.release(*a), "double release rejected");
  expect(!packer.release(PackedRect{0, 77, 5, 5}), "unknown rectangle rejected");
  expect(!packer.release(PackedRect{-3, 0, 5, 5}), "out-of-bounds rectangle rejected");
  const auto c = packer.allocate(30, 10);
  expect(c && occ.mark(*c, 1), "freed span is reused");
  expect(packer.release(*b) && packer.release(*c), "release everything");
  expect(packer.liveCount() == 0 && packer.liveArea() == 0, "empty again");
  expect(packer.allocate(128, 128).has_value(), "after freeing, a full-size rectangle fits again");

  ShelfPacker empty(0, 0);
  expect(!empty.allocate(1, 1), "empty packer is always full");
  ShelfPacker negative(-5, 10);
  expect(!negative.allocate(1, 1), "negative packer is always full");
}

void testPackerFuzz() {
  std::uint32_t state = 99;
  const auto rnd = [&](int n) {
    state = state * 1664525u + 1013904223u;
    return static_cast<int>((state >> 8) % static_cast<std::uint32_t>(n));
  };
  ShelfPacker packer(200, 160);
  Occupancy occ(200, 160);
  std::vector<PackedRect> live;
  long long area = 0;
  for (int i = 0; i < 20000; ++i) {
    if (!live.empty() && (rnd(3) == 0 || live.size() > 80)) {
      const std::size_t idx = static_cast<std::size_t>(rnd(static_cast<int>(live.size())));
      const PackedRect r = live[idx];
      expect(packer.release(r) && occ.mark(r, -1), "fuzz release of a live rectangle");
      area -= static_cast<long long>(r.w) * r.h;
      live.erase(live.begin() + static_cast<std::ptrdiff_t>(idx));
    } else {
      const int w = 1 + rnd(40);
      const int h = 1 + rnd(30);
      if (auto r = packer.allocate(w, h)) {
        expect(r->w == w && r->h == h, "allocated size matches the request");
        expect(occ.mark(*r, 1), "fuzz allocation in bounds and disjoint");
        live.push_back(*r);
        area += static_cast<long long>(w) * h;
      }
    }
    expect(packer.liveCount() == live.size() && packer.liveArea() == area, "fuzz accounting");
  }
  for (const PackedRect& r : live) expect(packer.release(r), "final release");
  expect(packer.allocate(200, 160).has_value(), "fully recovered after the fuzz run");
}

// ---- GlyphAtlas -----------------------------------------------------------------------------

std::uint8_t pixelAt(const GlyphAtlas& a, int x, int y) {
  return a.pixels()[static_cast<std::size_t>(y) * static_cast<std::size_t>(a.width()) + static_cast<std::size_t>(x)];
}

bool glyphMatchesRaster(const GlyphAtlas& atlas, const Font& font, std::uint32_t glyph, const RasterParams& p,
                        const AtlasGlyph& g) {
  const GlyphBitmap ref = rasterizeGlyph(font, glyph, p).value();
  if (ref.width != g.w || ref.height != g.h || ref.left != g.left || ref.top != g.top) return false;
  for (int y = 0; y < g.h; ++y) {
    for (int x = 0; x < g.w; ++x) {
      if (pixelAt(atlas, g.x + x, g.y + y) != ref.coverage[static_cast<std::size_t>(y * g.w + x)]) return false;
    }
  }
  return true;
}

void testConfig() {
  AtlasConfig c;
  c.width = 32;
  expect(!GlyphAtlas::create(c).ok(), "width below the minimum");
  c = AtlasConfig{};
  c.height = kMaxAtlasDimension + 1;
  expect(!GlyphAtlas::create(c).ok(), "height above the maximum");
  c = AtlasConfig{};
  c.maxEvictionPasses = 0;
  expect(!GlyphAtlas::create(c).ok(), "zero eviction passes");
  c = AtlasConfig{};
  c.maxEntries = 3;
  expect(!GlyphAtlas::create(c).ok(), "too few entries");
  auto def = GlyphAtlas::create();
  expect(def.ok() && def.value().width() == 2048 && def.value().height() == 2048, "default is 2048x2048");
}

void testCacheAndDirty(const Font& font) {
  GlyphAtlas atlas = GlyphAtlas::create().value();
  expect(!atlas.takeDirtyRect().has_value(), "fresh atlas is clean");
  const std::uint32_t glyph = shapeText(font, 12, "R").value().glyphs[0].glyphId;
  RasterParams p;
  p.pixelSize = 12;
  const AtlasLookup first = atlas.get(font, glyph, p);
  expect(first.status == LookupStatus::Ok && first.glyph.visible, "first lookup rasterizes");
  expect(glyphMatchesRaster(atlas, font, glyph, p, first.glyph), "atlas pixels equal the rasterizer output");
  expect(first.glyph.u0 >= 0 && first.glyph.u1 <= 1 && first.glyph.u0 < first.glyph.u1 && first.glyph.v0 < first.glyph.v1,
         "UVs inside the image");
  expect(std::abs(first.glyph.u0 * 2048 - first.glyph.x) < 1e-3f, "UV matches pixel position");

  const auto dirty = atlas.takeDirtyRect();
  expect(dirty && dirty->x <= first.glyph.x - 1 && dirty->y <= first.glyph.y - 1 &&
             dirty->x + dirty->w >= first.glyph.x + first.glyph.w + 1 && dirty->y + dirty->h >= first.glyph.y + first.glyph.h + 1,
         "dirty rectangle covers the padded glyph");
  expect(!atlas.takeDirtyRect().has_value(), "dirty state is consumed");

  // 1 px zero padding on every side.
  bool padded = true;
  for (int x = first.glyph.x - 1; x <= first.glyph.x + first.glyph.w; ++x) {
    padded = padded && pixelAt(atlas, x, first.glyph.y - 1) == 0 && pixelAt(atlas, x, first.glyph.y + first.glyph.h) == 0;
  }
  for (int y = first.glyph.y - 1; y <= first.glyph.y + first.glyph.h; ++y) {
    padded = padded && pixelAt(atlas, first.glyph.x - 1, y) == 0 && pixelAt(atlas, first.glyph.x + first.glyph.w, y) == 0;
  }
  expect(padded, "glyph is surrounded by a zeroed border");

  const AtlasLookup second = atlas.get(font, glyph, p);
  expect(second.status == LookupStatus::Ok && second.glyph.x == first.glyph.x && second.glyph.y == first.glyph.y &&
             atlas.stats().entries == 1,
         "second lookup is a cache hit");
  expect(!atlas.takeDirtyRect().has_value(), "a hit does not dirty the image");

  // Every key component makes a distinct entry.
  RasterParams q = p;
  q.subpixelBin = 1;
  const AtlasLookup bin1 = atlas.get(font, glyph, q);
  q = p;
  q.emboldenPx = 0.5f;
  const AtlasLookup bold = atlas.get(font, glyph, q);
  q = p;
  q.pixelSize = 13;
  const AtlasLookup bigger = atlas.get(font, glyph, q);
  expect(atlas.stats().entries == 4 && bin1.glyph.x != first.glyph.x && bold.glyph.x != first.glyph.x &&
             bigger.glyph.x != first.glyph.x,
         "bin, embolden and size are part of the key");

  // Space: Ok, invisible, cached.
  const std::uint32_t space = shapeText(font, 12, " ").value().glyphs[0].glyphId;
  const AtlasLookup sp = atlas.get(font, space, p);
  expect(sp.status == LookupStatus::Ok && !sp.glyph.visible, "space has no ink");

  RasterParams bad = p;
  bad.pixelSize = std::nanf("");
  expect(atlas.get(font, glyph, bad).status == LookupStatus::InvalidArgument, "NaN size rejected");
  expect(atlas.get(font, font.glyphCount() + 5, p).status == LookupStatus::InvalidArgument, "bad glyph id rejected");

  atlas.clear();
  expect(atlas.stats().entries == 0 && atlas.takeDirtyRect().has_value() && pixelAt(atlas, first.glyph.x, first.glyph.y) == 0,
         "clear empties the cache and zeroes the image");
}

void testEvictionAndFull(const Font& font) {
  AtlasConfig c;
  c.width = 96;
  c.height = 96;
  GlyphAtlas atlas = GlyphAtlas::create(c).value();

  // Cycle through many distinct glyphs, one frame per glyph: eviction must keep it working and
  // every returned rectangle must hold exactly the right pixels.
  RasterParams p;
  p.pixelSize = 16;
  int mismatches = 0;
  int notOk = 0;
  for (std::uint32_t g = 3; g < 600; ++g) {
    atlas.beginFrame();
    const AtlasLookup r = atlas.get(font, g, p);
    if (r.status != LookupStatus::Ok) {
      ++notOk;
      continue;
    }
    if (r.glyph.visible && !glyphMatchesRaster(atlas, font, g, p, r.glyph)) ++mismatches;
  }
  expect(notOk == 0, "per-frame lookups always succeed on a small atlas thanks to eviction");
  expect(mismatches == 0, "stored glyph pixels are intact after reuse of evicted space");
  expect(atlas.stats().evictedTotal > 100, "eviction happened");
  expect(atlas.stats().entries < 200, "cache stays bounded");

  // Within one frame nothing can be evicted, so the atlas fills up and reports it.
  atlas.beginFrame();
  int ok = 0;
  int full = 0;
  std::vector<std::pair<std::uint32_t, AtlasGlyph>> kept;
  for (std::uint32_t g = 700; g < 900; ++g) {
    const AtlasLookup r = atlas.get(font, g, p);
    if (r.status == LookupStatus::Ok) {
      ++ok;
      kept.emplace_back(g, r.glyph);
    } else if (r.status == LookupStatus::AtlasFull) {
      ++full;
    }
  }
  expect(ok > 5 && full > 0, "single frame: some glyphs fit, then AtlasFull is reported");
  bool intact = true;
  for (const auto& [g, glyph] : kept) intact = intact && (!glyph.visible || glyphMatchesRaster(atlas, font, g, p, glyph));
  expect(intact, "failed lookups did not disturb pinned glyphs");
  atlas.beginFrame();
  expect(atlas.get(font, 899, p).status == LookupStatus::Ok, "after beginFrame the atlas accepts glyphs again");

  // A glyph that can never fit.
  RasterParams huge;
  huge.pixelSize = 200;
  const std::uint32_t w = shapeText(font, 12, "W").value().glyphs[0].glyphId;
  expect(atlas.get(font, w, huge).status == LookupStatus::GlyphTooLarge, "glyph larger than the atlas");

  // Entry-count bound, including ink-less glyphs.
  AtlasConfig few;
  few.maxEntries = 16;
  GlyphAtlas small = GlyphAtlas::create(few).value();
  const std::uint32_t space = shapeText(font, 12, " ").value().glyphs[0].glyphId;
  for (int i = 0; i < 100; ++i) {
    small.beginFrame();
    RasterParams sp;
    sp.pixelSize = 10.0f + static_cast<float>(i) * 0.25f;
    (void)small.get(font, space, sp);
  }
  expect(small.stats().entries <= 16, "entry table is bounded");
}

// ---- Quads ----------------------------------------------------------------------------------

void testQuads(const Font& font) {
  GlyphAtlas atlas = GlyphAtlas::create().value();
  atlas.beginFrame();
  const ShapedRun run = shapeText(font, 12, "Hello a b").value();
  QuadParams qp;
  qp.pixelSize = 12;
  std::vector<GlyphQuad> quads;
  auto stats = buildGlyphQuads(atlas, font, run, qp, 10.0f, 20.0f, quads);
  expect(stats.ok() && stats.value().quads == 7 && quads.size() == 7 && stats.value().skipped == 0,
         "spaces produce no quads (7 letters)");
  bool ordered = true;
  bool uvOk = true;
  for (std::size_t i = 0; i < quads.size(); ++i) {
    ordered = ordered && (i == 0 || quads[i].x > quads[i - 1].x);
    uvOk = uvOk && quads[i].u0 >= 0 && quads[i].u1 <= 1 && quads[i].v0 >= 0 && quads[i].v1 <= 1 && quads[i].w > 0 &&
           quads[i].h > 0 && std::abs((quads[i].u1 - quads[i].u0) * 2048.0f - quads[i].w) < 1e-2f;
  }
  expect(ordered && uvOk, "quads left to right with consistent UVs");
  expect(quads[0].y + quads[0].h > 19.0f && quads[0].y + quads[0].h < 21.0f, "H sits on the baseline at y = 20");
  expect(quads[0].x >= 10.0f && quads[0].x < 13.0f, "quad starts near the pen");

  // Pen fraction selects a different atlas bitmap; whole-pixel shifts reuse the same one.
  const ShapedRun one = shapeText(font, 12, "l").value();
  std::vector<GlyphQuad> a;
  std::vector<GlyphQuad> b;
  std::vector<GlyphQuad> c;
  std::vector<GlyphQuad> d;
  (void)buildGlyphQuads(atlas, font, one, qp, 5.0f, 20.0f, a);
  (void)buildGlyphQuads(atlas, font, one, qp, 5.5f, 20.0f, b);
  (void)buildGlyphQuads(atlas, font, one, qp, 6.0f, 20.0f, c);
  (void)buildGlyphQuads(atlas, font, one, qp, 5.1f, 20.0f, d);
  expect(a[0].u0 != b[0].u0, "half-pixel pen uses another subpixel bitmap");
  expect(a[0].u0 == c[0].u0 && std::abs(c[0].x - a[0].x - 1.0f) < 1e-4f, "whole-pixel shift reuses the bitmap");
  expect(a[0].u0 == d[0].u0, "5.1 rounds to the same quarter as 5.0");

  // Baseline snapping.
  std::vector<GlyphQuad> y1;
  std::vector<GlyphQuad> y2;
  (void)buildGlyphQuads(atlas, font, one, qp, 0.0f, 20.4f, y1);
  (void)buildGlyphQuads(atlas, font, one, qp, 0.0f, 20.6f, y2);
  expect(std::abs(y2[0].y - y1[0].y - 1.0f) < 1e-4f, "baseline snaps to whole pixels");
  qp.snapBaselineY = false;
  std::vector<GlyphQuad> y3;
  (void)buildGlyphQuads(atlas, font, one, qp, 0.0f, 20.4f, y3);
  expect(std::abs(y3[0].y - (y1[0].y + 0.4f)) < 1e-3f, "unsnapped baseline keeps the fraction");
  qp.snapBaselineY = true;

  // Hostile inputs.
  std::vector<GlyphQuad> none;
  expect(!buildGlyphQuads(atlas, font, one, qp, std::nanf(""), 0, none).ok(), "NaN pen");
  expect(!buildGlyphQuads(atlas, font, one, qp, 0, INFINITY, none).ok(), "infinite baseline");
  expect(!buildGlyphQuads(atlas, font, one, qp, 1e9f, 0, none).ok(), "absurd pen");
  QuadParams badParams = qp;
  badParams.pixelSize = 0;
  expect(!buildGlyphQuads(atlas, font, one, badParams, 0, 0, none).ok(), "bad size");
  expect(none.empty(), "failed builds leave the output untouched");
  ShapedRun empty;
  expect(buildGlyphQuads(atlas, font, empty, qp, 0, 0, none).ok() && none.empty(), "empty run");
  ShapedRun badGlyph;
  badGlyph.glyphs.push_back(ShapedGlyph{0xFFFFFFF0u, 0, 5, 0, 0, false});
  auto bg = buildGlyphQuads(atlas, font, badGlyph, qp, 0, 0, none);
  expect(bg.ok() && bg.value().skipped == 1 && none.empty(), "an unknown glyph id is skipped, not fatal");
  std::vector<GlyphQuad> negative;
  expect(buildGlyphQuads(atlas, font, one, qp, -3.3f, -4.0f, negative).ok() && negative.size() == 1, "negative coordinates");

  // 100k glyph run: cache hits make this cheap; quad count equals glyph count.
  std::string many;
  for (int i = 0; i < 10000; ++i) many += "abcdefghij";
  const ShapedRun big = shapeText(font, 12, many).value();
  std::vector<GlyphQuad> bigQuads;
  atlas.beginFrame();
  auto bs = buildGlyphQuads(atlas, font, big, qp, 0, 12, bigQuads);
  expect(bs.ok() && bigQuads.size() == 100000 && bs.value().skipped == 0, "100k glyph run builds 100k quads");

  // Atlas full is reported but does not abort the label.
  AtlasConfig tiny;
  tiny.width = 64;
  tiny.height = 64;
  GlyphAtlas tinyAtlas = GlyphAtlas::create(tiny).value();
  tinyAtlas.beginFrame();
  QuadParams large;
  large.pixelSize = 28;
  const ShapedRun text = shapeText(font, 28, "Rectangle Pass through").value();
  std::vector<GlyphQuad> partial;
  auto ps = buildGlyphQuads(tinyAtlas, font, text, large, 0, 30, partial);
  expect(ps.ok() && ps.value().atlasFull && ps.value().skipped > 0 && ps.value().quads > 0 &&
             ps.value().quads == partial.size(),
         "atlas-full glyphs are skipped and reported");
}

}  // namespace

int main() {
  FontLibrary lib;
  const FontHandle font = loadInter(lib, "Inter-Regular.ttf", 400);
  testPackerBasics();
  testPackerFuzz();
  testConfig();
  testCacheAndDirty(*font);
  testEvictionAndFull(*font);
  testQuads(*font);
  return finish("atlas");
}

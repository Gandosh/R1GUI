// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for font loading, per-size metrics (against assets/fonts/metrics.json), family
//   resolution and the hostile-file cases (missing, empty, garbage, truncated, oversized).
// Callers: CTest (label fast). Exit code 0 = pass.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "TestSupport.h"
#include "r1ui/core/Json.h"
#include "r1ui/text/Font.h"
#include "r1ui/text/Rasterizer.h"
#include "r1ui/text/Shaper.h"

using namespace r1ui::text;
using namespace r1ui::text::testing;

namespace {

bool near(float a, double b, double eps = 1e-3) { return std::abs(static_cast<double>(a) - b) <= eps; }

struct FaceFile {
  const char* file;
  int weight;
};
constexpr FaceFile kFaces[] = {{"Inter-Regular.ttf", 400}, {"Inter-Medium.ttf", 500}, {"Inter-SemiBold.ttf", 600},
                               {"Inter-Bold.ttf", 700}, {"Inter-ExtraBold.ttf", 800}};

void testMetricsAgainstJson(FontLibrary& lib) {
  const auto bytes = readFileBytes(assetPath("fonts/metrics.json"));
  const auto parsed = r1ui::core::parseJson(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
  expect(parsed.ok(), "metrics.json parses");
  if (!parsed.ok()) return;
  const r1ui::core::JsonValue* faces = parsed.value->find("faces");
  expect(faces != nullptr && faces->size() == 5, "metrics.json lists 5 faces");
  if (faces == nullptr) return;

  for (std::size_t i = 0; i < faces->size(); ++i) {
    const r1ui::core::JsonValue& f = faces->child(i);
    const std::string file = f.find("file")->stringValue();
    const FontHandle font = loadInter(lib, file.c_str(), static_cast<int>(f.find("weight")->numberValue()));
    const double upem = f.find("unitsPerEm")->numberValue();
    expect(font->unitsPerEm() == static_cast<int>(upem), "units per em matches metrics.json");
    // metrics.json counts mapped glyphs; the font's own maxp count is larger.
    expect(font->glyphCount() >= static_cast<std::uint32_t>(f.find("glyphCount")->numberValue()), "glyph count");
    const double asc = f.find("hhea")->find("ascender")->numberValue();
    const double desc = f.find("hhea")->find("descender")->numberValue();
    const double gap = f.find("hhea")->find("lineGap")->numberValue();
    const double xh = f.find("xHeight")->numberValue();
    const double cap = f.find("capHeight")->numberValue();
    for (const float size : {11.0f, 12.0f, 13.0f, 14.0f, 16.0f}) {
      const FontMetrics m = font->metricsAt(size);
      const double k = size / upem;
      expect(near(m.ascent, asc * k), "ascent");
      expect(near(m.descent, -desc * k), "descent");
      expect(near(m.lineGap, gap * k), "line gap");
      expect(near(m.xHeight, xh * k), "x-height");
      expect(near(m.capHeight, cap * k), "cap height");
      expect(near(m.naturalLineHeight, (asc - desc + gap) * k), "natural line height");
    }
  }
}

void testLineBoxes(FontLibrary& lib) {
  // Token line heights: 12px text in a 16px box, 14px in 20px, 13px in 20px (body), 16px in 24px.
  const FontHandle font = loadInter(lib, "Inter-Regular.ttf", 400);
  struct Case {
    float size;
    float box;
  };
  for (const Case c : {Case{11, 16}, Case{12, 16}, Case{13, 20}, Case{14, 20}, Case{16, 24}}) {
    const FontMetrics m = font->metricsAt(c.size);
    const float baseline = m.baselineInBox(c.box);
    expect(baseline > m.capHeight && baseline < c.box, "baseline lies inside the line box below the cap height");
    expect(near(baseline - m.ascent, (c.box - (m.ascent + m.descent)) / 2.0, 1e-4), "half-leading is symmetric");
  }
  expect(font->metricsAt(std::nanf("")).ascent == 0.0f, "invalid size yields empty metrics");
}

void testFamily(FontLibrary& lib) {
  FontFamily family;
  expect(family.resolve(400, 12, BoldMode::Synthetic).font == nullptr, "empty family resolves to null");
  for (const FaceFile& f : kFaces) expect(family.addFace(loadInter(lib, f.file, f.weight)).ok(), "addFace");
  expect(!family.addFace(nullptr).ok(), "null face rejected");

  const ResolvedFace regular = family.resolve(400, 12, BoldMode::Synthetic);
  expect(regular.font && regular.font->weight() == 400 && regular.emboldenPx == 0.0f, "400 is plain Regular");
  for (const int w : {500, 600, 700}) {
    const ResolvedFace r = family.resolve(w, 12, BoldMode::Synthetic);
    expect(r.font && r.font->weight() == 400, "synthetic bold uses the Regular face");
    expect(r.emboldenPx > 0.4f && r.emboldenPx < 0.5f, "default strength about size/24..size/32 at 12 px");
  }
  expect(family.resolve(600, 12, BoldMode::Synthetic, 0.25f).emboldenPx == 0.25f, "strength override");
  expect(family.resolve(600, 12, BoldMode::Synthetic, 99.0f).emboldenPx == 12.0f, "override is capped at the size");
  expect(family.resolve(600, 12, BoldMode::RealFaces).font->weight() == 600, "real face by weight");
  expect(family.resolve(600, 12, BoldMode::RealFaces).emboldenPx == 0.0f, "real faces are not emboldened");
  expect(family.resolve(650, 12, BoldMode::RealFaces).font->weight() == 600, "ties go to the lighter face");
  expect(family.resolve(900, 12, BoldMode::RealFaces).font->weight() == 800, "nearest available weight");
  expect(family.resolve(400, 0, BoldMode::Synthetic).font == nullptr, "invalid size resolves to null");

  expect(near(static_cast<float>(defaultEmboldenPx(9)), 9.0 / 24.0, 1e-5), "strength at 9 px");
  expect(near(static_cast<float>(defaultEmboldenPx(36)), 36.0 / 32.0, 1e-5), "strength at 36 px");
  expect(defaultEmboldenPx(std::nanf("")) == 0.0f && defaultEmboldenPx(-3) == 0.0f, "strength for invalid sizes");
}

void testLoading(FontLibrary& lib) {
  expect(lib.ready(), "FreeType initialized");
  const std::string good = assetPath("fonts/Inter-Regular.ttf");

  auto r = lib.loadFromFile(good, 400);
  expect(r.ok(), "load from file");
  auto bytes = readFileBytes(good);
  expect(bytes.size() > 100000, "font file read");
  auto m = lib.loadFromMemory(bytes, 400);
  expect(m.ok() && m.value()->id() != r.value()->id(), "memory load works; ids are unique");

  // Fonts keep the FreeType library alive: destroy the library object first.
  FontHandle survivor;
  {
    FontLibrary scoped;
    survivor = loadInter(scoped, "Inter-Regular.ttf", 400);
  }
  expect(measureWidth(*survivor, 12, "Rectangle").ok(), "font outlives its FontLibrary");

  expect(lib.loadFromFile("", 400).error().code == ErrorCode::InvalidArgument, "empty path");
  expect(lib.loadFromFile(std::string("a\0b", 3), 400).error().code == ErrorCode::InvalidArgument, "NUL in path");
  expect(lib.loadFromFile(assetPath("fonts/NoSuchFont.ttf"), 400).error().code == ErrorCode::FileNotFound, "missing file");
  expect(lib.loadFromFile(assetPath("fonts"), 400).error().code == ErrorCode::FileNotFound, "directory is not a font");
  expect(lib.loadFromFile(good, 0).error().code == ErrorCode::InvalidArgument, "weight 0");
  expect(lib.loadFromFile(good, 1001).error().code == ErrorCode::InvalidArgument, "weight 1001");
  expect(lib.loadFromMemory({}, 400).error().code == ErrorCode::InvalidArgument, "empty memory");
  expect(lib.loadFromFile(assetPath("fonts/OFL.txt"), 400).error().code == ErrorCode::CorruptFont, "text file is not a font");

  std::vector<std::uint8_t> garbage(4096);
  std::uint32_t state = 7;
  for (auto& b : garbage) {
    state = state * 1664525u + 1013904223u;
    b = static_cast<std::uint8_t>(state >> 24);
  }
  expect(lib.loadFromMemory(garbage, 400).error().code == ErrorCode::CorruptFont, "random bytes");
  std::vector<std::uint8_t> zeros(1000, 0);
  expect(lib.loadFromMemory(zeros, 400).error().code == ErrorCode::CorruptFont, "zero bytes");

  const std::vector<std::uint8_t> huge(kMaxFontFileBytes + 1, 0);
  expect(lib.loadFromMemory(huge, 400).error().code == ErrorCode::FileTooLarge, "oversized font data");

  // Truncated real fonts: every cut either fails cleanly or yields a font that survives use.
  const std::size_t cuts[] = {1, 4, 11, 12, 100, 500, 2000, 20000, bytes.size() / 2, bytes.size() - 1};
  for (const std::size_t cut : cuts) {
    std::vector<std::uint8_t> part(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut));
    auto t = lib.loadFromMemory(part, 400);
    if (t.ok()) {
      const FontHandle font = t.value();
      (void)measureWidth(*font, 12, "Rectangle");
      for (std::uint32_t g = 0; g < 40 && g < font->glyphCount(); ++g) (void)rasterizeGlyph(*font, g, RasterParams{});
      expect(true, "truncated font survived use");
    } else {
      expect(t.error().code == ErrorCode::CorruptFont, "truncated font reports CorruptFont");
    }
  }

  // Single-byte corruption sweep over the table directory and the start of the first tables.
  for (std::size_t pos = 0; pos < 400; pos += 3) {
    std::vector<std::uint8_t> bad = bytes;
    bad[pos] = static_cast<std::uint8_t>(bad[pos] ^ 0xFF);
    auto t = lib.loadFromMemory(bad, 400);
    if (t.ok()) {
      (void)measureWidth(*t.value(), 12, "Rectangle");
      (void)rasterizeGlyph(*t.value(), 5, RasterParams{});
    }
  }
}

}  // namespace

int main() {
  FontLibrary lib;
  testLoading(lib);
  testMetricsAgainstJson(lib);
  testLineBoxes(lib);
  testFamily(lib);
  return finish("font");
}

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for shaping, measurement, CaretMap and ellipsis truncation, including hostile
//   inputs (empty, 10 MB, invalid UTF-8, NUL bytes, mark storms, 100k glyphs, bad sizes, RTL).
// Callers: CTest (label fast). Exit code 0 = pass.
#include <cmath>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/text/CaretMap.h"
#include "r1ui/text/Grapheme.h"
#include "r1ui/text/Shaper.h"
#include "r1ui/text/Utf8.h"

using namespace r1ui::text;
using namespace r1ui::text::testing;

namespace {

void testBasics(const Font& font) {
  auto run = shapeText(font, 12, "Rectangle");
  expect(run.ok(), "shape Rectangle");
  if (!run.ok()) return;
  const ShapedRun& r = run.value();
  expect(r.glyphs.size() == 9, "one glyph per letter");
  double sum = 0;
  std::uint32_t last = 0;
  bool ascending = true;
  for (const ShapedGlyph& g : r.glyphs) {
    sum += g.xAdvance;
    ascending = ascending && g.cluster >= last;
    last = g.cluster;
    expect(g.glyphId != 0, "no .notdef in plain Latin");
  }
  expect(ascending && r.glyphs.front().cluster == 0 && r.glyphs.back().cluster == 8, "clusters are byte offsets");
  expect(std::abs(sum - r.width) < 1e-3, "width is the sum of advances");
  expect(!r.wasSanitized, "valid input is not repaired");
  expect(measureWidth(font, 12, "Rectangle").value() == r.width, "measureWidth matches shapeText");
  expect(measureWidth(font, 24, "Rectangle").value() > 1.99f * r.width, "width scales with size");

  const float kerned = measureWidth(font, 16, "AVATAR To").value();
  ShapeOptions off;
  off.kerning = false;
  const float plain = measureWidth(font, 16, "AVATAR To", off).value();
  expect(kerned < plain, "kerning tightens AV, AT, To pairs");

  auto empty = shapeText(font, 12, "");
  expect(empty.ok() && empty.value().glyphs.empty() && empty.value().width == 0.0f, "empty string");
}

void testBadSizes(const Font& font) {
  for (const float size : {0.0f, -1.0f, 0.5f, 1025.0f, std::nanf(""), INFINITY, -INFINITY}) {
    auto r = shapeText(font, size, "abc");
    expect(!r.ok() && r.error().code == ErrorCode::InvalidArgument, "invalid size rejected");
  }
  expect(shapeText(font, 1.0f, "abc").ok() && shapeText(font, 1024.0f, "abc").ok(), "range limits accepted");
}

void testHostileText(const Font& font) {
  // Invalid UTF-8 is repaired; clusters refer to the repaired text.
  auto bad = shapeText(font, 12, "a\xFF" "b");
  expect(bad.ok() && bad.value().wasSanitized, "invalid byte repaired");
  if (bad.ok()) {
    expect(isValidUtf8(bad.value().sanitizedText) && bad.value().sanitizedText == utf8({'a', 0xFFFD, 'b'}),
           "sanitized text exposed");
    expect(bad.value().glyphs.size() == 3 && bad.value().glyphs[2].cluster == 4, "clusters index the sanitized text");
  }
  auto surrogate = shapeText(font, 12, "\xED\xA0\x80");
  expect(surrogate.ok() && surrogate.value().glyphs.size() == 3, "lone surrogate: three replacements");

  const std::string nul("a\0b\0", 4);
  auto n = shapeText(font, 12, nul);
  expect(n.ok() && n.value().glyphs.size() == 4, "NUL bytes shaped, not terminating the string");

  std::string tooBig(10 * 1024 * 1024, 'a');
  auto big = shapeText(font, 12, tooBig);
  expect(!big.ok() && big.error().code == ErrorCode::InputTooLarge, "10 MB string rejected, not truncated");
  expect(truncateWithEllipsis(font, 12, tooBig, 100).error().code == ErrorCode::InputTooLarge, "ellipsis on 10 MB");

  const std::string limit(kMaxShapeBytes, 'a');
  auto atLimit = shapeText(font, 12, limit);
  expect(atLimit.ok() && atLimit.value().glyphs.size() == kMaxShapeBytes, "exactly the limit shapes (one glyph per byte)");

  // Combining mark storm: one cluster, caret map has just two stops.
  std::string storm = "a";
  for (int i = 0; i < 100000; ++i) appendUtf8(storm, 0x301);
  auto s = shapeText(font, 12, storm);
  expect(s.ok(), "mark storm shapes");
  if (s.ok()) {
    const CaretMap map = CaretMap::build(s.value(), storm);
    expect(map.stops().size() == 2 && map.stops().back().offset == storm.size(), "mark storm is one caret cell");
  }

  // 100k glyphs.
  std::string many;
  for (int i = 0; i < 10000; ++i) many += "abcdefghij";
  auto m = shapeText(font, 12, many);
  expect(m.ok() && m.value().glyphs.size() == 100000, "100k glyph run");
  if (m.ok()) {
    const CaretMap map = CaretMap::build(m.value(), many);
    expect(map.stops().size() == 100001, "caret stop per grapheme");
    expect(std::abs(map.stops().back().x - m.value().width) < 1e-2, "last stop at the run width");
  }

  // Scripts the font does not cover, RTL and mixed text must not crash.
  const std::vector<std::string> exotic = {
      utf8({0x5E9, 0x5DC, 0x5D5, 0x5DD}),                                   // Hebrew
      utf8({0x645, 0x631, 0x62D, 0x628, 0x627}),                            // Arabic
      "abc " + utf8({0x5E9, 0x5DC, 0x5D5, 0x5DD}) + " def " + utf8({0x645, 0x631}),
      utf8({0x4F60, 0x597D, 0x4E16, 0x754C}),                               // CJK
      utf8({0x0915, 0x094D, 0x0937, 0x093F}),                               // Devanagari conjunct
      utf8({0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467, 0x1F1FA, 0x1F1F8}),  // emoji
      utf8({0x200E, 0x200F, 0x202E, 0x2066, 0xFEFF, 0xFFFF, 0x10FFFF})};     // bidi/format/noncharacters
  for (const std::string& t : exotic) {
    auto r = shapeText(font, 14, t);
    expect(r.ok() && std::isfinite(r.value().width), "exotic script shapes with a finite width");
    if (r.ok()) (void)CaretMap::build(r.value(), t);
  }
  auto rtl = shapeText(font, 14, utf8({0x645, 0x631, 0x62D, 0x628, 0x627}));
  expect(rtl.ok() && !rtl.value().glyphs.empty() && rtl.value().glyphs.front().rtl, "Arabic run flagged right-to-left");
}

void testCaretMap(const Font& font) {
  // Real text: one stop per grapheme, monotone, ending at the width.
  const std::string text = "Rectangle";
  const ShapedRun run = shapeText(font, 12, text).value();
  const CaretMap map = CaretMap::build(run, text);
  expect(map.stops().size() == 10, "stop per boundary");
  expect(map.xForOffset(0) == 0.0f && std::abs(map.xForOffset(9) - run.width) < 1e-3f, "ends of the run");
  bool monotone = true;
  for (std::size_t i = 1; i < map.stops().size(); ++i) monotone = monotone && map.stops()[i].x > map.stops()[i - 1].x;
  expect(monotone, "stops increase left to right");
  expect(map.offsetForX(-100) == 0 && map.offsetForX(1e9f) == 9, "hit test clamps to the ends");
  expect(map.offsetForX(std::nanf("")) == 0, "NaN hit test is safe");
  expect(map.offsetForX(map.stops()[4].x + 0.1f) == 4, "hit test picks the nearest boundary");

  // Combining mark: caret cannot sit between the base and the mark.
  const std::string accent = utf8({'e', 0x301, 'x'});
  const CaretMap am = CaretMap::build(shapeText(font, 12, accent).value(), accent);
  bool noInside = true;
  for (const auto& st : am.stops()) noInside = noInside && st.offset != 1;
  expect(noInside && am.stops().size() == 3, "no stop inside e + acute");

  // Ligature-like cluster: one glyph covering two graphemes splits evenly.
  ShapedRun lig;
  lig.width = 10;
  lig.glyphs.push_back(ShapedGlyph{1, 0, 10, 0, 0, false});
  const CaretMap lm = CaretMap::build(lig, "ab");
  expect(lm.stops().size() == 3 && std::abs(lm.xForOffset(1) - 5.0f) < 1e-4f, "ligature caret in the middle");

  // Right-to-left clusters run right to left.
  ShapedRun rl;
  rl.width = 10;
  rl.glyphs.push_back(ShapedGlyph{1, 1, 5, 0, 0, true});
  rl.glyphs.push_back(ShapedGlyph{2, 0, 5, 0, 0, true});
  const CaretMap rm = CaretMap::build(rl, "ab");
  expect(rm.xForOffset(0) == 10.0f && rm.xForOffset(1) == 5.0f && rm.xForOffset(2) == 0.0f, "RTL stops descend");

  // Malformed runs (clusters beyond the text) must not break the map.
  ShapedRun junk;
  junk.glyphs.push_back(ShapedGlyph{1, 9999, 3, 0, 0, false});
  const CaretMap jm = CaretMap::build(junk, "ab");
  expect(!jm.stops().empty() && jm.stops().front().offset == 0, "out-of-range clusters tolerated");
  expect(CaretMap().stops().size() == 1, "default map has one stop");
}

void testEllipsis(const Font& font) {
  const std::string text = "The quick brown fox jumps over the lazy dog";
  const float full = measureWidth(font, 12, text).value();

  auto fits = truncateWithEllipsis(font, 12, text, full + 1);
  expect(fits.ok() && !fits.value().truncated && fits.value().text == text, "fits: unchanged");

  const std::string ellipsis = utf8({0x2026});
  for (const float limit : {20.0f, 50.0f, 100.0f, 150.0f}) {
    auto r = truncateWithEllipsis(font, 12, text, limit);
    expect(r.ok() && r.value().truncated, "truncated");
    if (!r.ok()) continue;
    const std::string& t = r.value().text;
    expect(t.size() > ellipsis.size() && t.substr(t.size() - ellipsis.size()) == ellipsis, "ends with an ellipsis");
    const std::string prefix = t.substr(0, t.size() - ellipsis.size());
    expect(text.compare(0, prefix.size(), prefix) == 0, "prefix of the original");
    expect(r.value().width <= limit, "fits the limit");
    expect(std::abs(measureWidth(font, 12, t).value() - r.value().width) < 1e-3f, "reported width is measured");
    // One more grapheme would not have fit.
    const std::size_t next = nextGraphemeBoundary(text, prefix.size());
    expect(next >= text.size() || measureWidth(font, 12, text.substr(0, next) + ellipsis).value() > limit,
           "longest fitting prefix");
  }

  auto tiny = truncateWithEllipsis(font, 12, text, 0);
  expect(tiny.ok() && tiny.value().text == ellipsis && tiny.value().truncated, "no room: bare ellipsis");
  auto negative = truncateWithEllipsis(font, 12, text, -5);
  expect(negative.ok() && negative.value().text == ellipsis, "negative width: bare ellipsis");
  expect(!truncateWithEllipsis(font, 12, text, std::nanf("")).ok(), "NaN width rejected");
  expect(!truncateWithEllipsis(font, 0, text, 50).ok(), "bad size rejected");

  // Never cut inside a cluster.
  std::string marks;
  for (int i = 0; i < 40; ++i) marks += utf8({'e', 0x301, 0x302});
  std::string flags;
  for (int i = 0; i < 20; ++i) flags += utf8({0x1F1FA, 0x1F1F8});
  std::string family;
  for (int i = 0; i < 10; ++i) family += utf8({0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467});
  for (const std::string& s : {marks, flags, family}) {
    for (float limit = 10; limit < 200; limit += 7) {
      auto r = truncateWithEllipsis(font, 12, s, limit);
      expect(r.ok(), "cluster-heavy text truncates");
      if (!r.ok() || !r.value().truncated) continue;
      const std::string prefix = r.value().text.substr(0, r.value().text.size() - ellipsis.size());
      expect(isGraphemeBoundary(s, prefix.size()) && s.compare(0, prefix.size(), prefix) == 0,
             "truncation lands on a grapheme boundary");
    }
  }

  auto repaired = truncateWithEllipsis(font, 12, std::string("ab\xFF") + "cd", 1000);
  expect(repaired.ok() && repaired.value().text == utf8({'a', 'b', 0xFFFD, 'c', 'd'}), "invalid UTF-8 repaired first");
}

}  // namespace

int main() {
  FontLibrary lib;
  const FontHandle font = loadInter(lib, "Inter-Regular.ttf", 400);
  testBasics(*font);
  testBadSizes(*font);
  testHostileText(*font);
  testCaretMap(*font);
  testEllipsis(*font);
  return finish("shaper");
}

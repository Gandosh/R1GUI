// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for the colour model (ColorModel.h): known HSV/HSL values, exact 8-bit round
//   trips over a lattice of the whole RGB cube, hue wrapping, quantisation, hex parsing and
//   formatting (including hostile text), premultiplied mixing and the hue memory of ColorState.
// Why: the picker's three views must agree to the last digit, and clipboard text and numeric
//   entries reach these functions unfiltered.
// Callers: CTest (colorpicker, fast tier, no GPU).
#include <cmath>
#include <limits>
#include <string>

#include "TestSupport.h"
#include "r1ui/widgets/colorpicker/ColorModel.h"

namespace {

using namespace r1ui::widgets::color;

bool near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }

void testKnownValues() {
  const Hsv red = toHsv({1, 0, 0});
  R1_EXPECT(near(red.h, 0) && near(red.s, 1) && near(red.v, 1));
  R1_EXPECT(near(toHsv({1, 1, 0}).h, 60) && near(toHsv({0, 1, 0}).h, 120) && near(toHsv({0, 1, 1}).h, 180));
  R1_EXPECT(near(toHsv({0, 0, 1}).h, 240) && near(toHsv({1, 0, 1}).h, 300));
  R1_EXPECT(near(toHsv({1, 0, 0.5}).h, 330));
  const Hsv grey = toHsv({0.5, 0.5, 0.5});
  R1_EXPECT(grey.s == 0 && near(grey.v, 0.5) && grey.h == 0);
  const Hsl hsl = toHsl({1, 0, 0});
  R1_EXPECT(near(hsl.h, 0) && near(hsl.s, 1) && near(hsl.l, 0.5));
  const Hsl pastel = toHsl({0.75, 0.5, 0.5});  // 0.625 lightness, hue 0
  R1_EXPECT(near(pastel.l, 0.625) && near(pastel.s, 0.25 / (1 - std::fabs(2 * 0.625 - 1))));
  const Rgb back = toRgb(Hsv{120, 1, 1});
  R1_EXPECT(near(back.r, 0) && near(back.g, 1) && near(back.b, 0));
  const Rgb fromHsl = toRgb(Hsl{240, 1, 0.5});
  R1_EXPECT(near(fromHsl.r, 0) && near(fromHsl.g, 0) && near(fromHsl.b, 1));
  R1_EXPECT(toHsl({0, 0, 0}).s == 0 && toHsl({1, 1, 1}).s == 0);  // no division by zero at the poles
}

void testRoundTrips() {
  int bad = 0;
  for (int r = 0; r < 256; r += 5) {
    for (int g = 0; g < 256; g += 5) {
      for (int b = 0; b < 256; b += 5) {
        const Rgb c{from8(r), from8(g), from8(b)};
        const Rgb viaHsv = toRgb(toHsv(c));
        const Rgb viaHsl = toRgb(toHsl(c));
        if (to8(viaHsv.r) != r || to8(viaHsv.g) != g || to8(viaHsv.b) != b) ++bad;
        if (to8(viaHsl.r) != r || to8(viaHsl.g) != g || to8(viaHsl.b) != b) ++bad;
      }
    }
  }
  R1_EXPECT(bad == 0);
  // The corners and the grey axis, every value.
  for (int v = 0; v < 256; ++v) {
    const Rgb grey{from8(v), from8(v), from8(v)};
    R1_EXPECT(to8(toRgb(toHsv(grey)).g) == v && to8(toRgb(toHsl(grey)).b) == v);
  }
}

void testHostileNumbers() {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  R1_EXPECT(clamp01(nan) == 0 && clamp01(inf) == 1 && clamp01(-inf) == 0 && clamp01(2) == 1 && clamp01(-1) == 0);
  R1_EXPECT(wrapHue(nan) == 0 && wrapHue(inf) == 0 && wrapHue(-inf) == 0);
  R1_EXPECT(near(wrapHue(-30), 330) && near(wrapHue(725), 5) && wrapHue(360) == 0 && wrapHue(-1e-30) < 360);
  R1_EXPECT(wrapHue(1e300) >= 0 && wrapHue(1e300) < 360 && wrapHue(-1e300) >= 0 && wrapHue(-1e300) < 360);
  const Rgb weird = toRgb(Hsv{nan, inf, -5});
  R1_EXPECT(std::isfinite(weird.r) && std::isfinite(weird.g) && std::isfinite(weird.b));
  const Hsv fromWeird = toHsv({nan, inf, -inf});
  R1_EXPECT(std::isfinite(fromWeird.h) && fromWeird.s >= 0 && fromWeird.v >= 0 && fromWeird.s <= 1 && fromWeird.v <= 1);
  const Hsl hs = toHsl({1e300, -1e300, nan});
  R1_EXPECT(std::isfinite(hs.h) && hs.s >= 0 && hs.s <= 1 && hs.l >= 0 && hs.l <= 1);
  R1_EXPECT(to8(nan) == 0 && to8(inf) == 255 && to8(-1) == 0 && to8(0.5) == 128 && to8(1.0 / 255.0) == 1);
  R1_EXPECT(from8(-5) == 0 && from8(1000) == 1 && from8(std::numeric_limits<int>::min()) == 0 && from8(std::numeric_limits<int>::max()) == 1);
  const Rgba s = sanitized(Rgba{{nan, 2, -1}, inf});
  R1_EXPECT(s.rgb.r == 0 && s.rgb.g == 1 && s.rgb.b == 0 && s.a == 1);
}

void testHex() {
  R1_EXPECT(formatHex({{1, 0.5, 0}, 1}) == "FF8000");
  R1_EXPECT(formatHex({{0.8313725490196079, 0.8313725490196079, 0.8313725490196079}, 1}) == "D4D4D4");
  R1_EXPECT(formatHex({{0, 0, 1}, 0.5}, true) == "0000FF80");
  R1_EXPECT(formatHex({{std::numeric_limits<double>::quiet_NaN(), 9, -9}, 7}, true) == "00FF00FF");
  const auto a = parseHex("#D4d4D4");
  R1_EXPECT(a && to8(a->rgb.r) == 212 && a->a == 1);
  const auto b = parseHex("  f80 ");
  R1_EXPECT(b && to8(b->rgb.r) == 255 && to8(b->rgb.g) == 136 && to8(b->rgb.b) == 0);
  const auto c = parseHex("#11223344");
  R1_EXPECT(c && to8(c->rgb.g) == 0x22 && to8(c->a) == 0x44);
  const auto d = parseHex("fab8");
  R1_EXPECT(d && to8(d->a) == 0x88);
  // Round trip through text for every grey and a few colours.
  for (int v = 0; v < 256; v += 3) {
    const Rgba g{{from8(v), from8(255 - v), from8(v / 2)}, from8(v)};
    const auto back = parseHex(formatHex(g, true));
    R1_EXPECT(back && *back == g);
  }
  const char* rejects[] = {"", "#", "12", "12345", "1234567", "123456789", "gg0000", "#12 456", "0xFFFFFF", "+ff0000", "-12345", "\xC3\xA9\xC3\xA9\xC3\xA9", "ff00ff\xFF", "##ffffff", "#ff\0ff"};
  for (const char* t : rejects) R1_EXPECT(!parseHex(t));
  R1_EXPECT(!parseHex(std::string("ff00f\0f", 7)));
  R1_EXPECT(!parseHex(std::string(1'000'000, 'a')));
  R1_EXPECT(!parseHex(std::string(1'000'000, ' ')));
}

void testMix() {
  const Rgba red{{1, 0, 0}, 1};
  const Rgba blue{{0, 0, 1}, 1};
  const Rgba mid = mix(red, blue, 0.5);
  R1_EXPECT(near(mid.rgb.r, 0.5) && near(mid.rgb.b, 0.5) && near(mid.a, 1));
  R1_EXPECT(mix(red, blue, -5) == red && mix(red, blue, 7) == blue && mix(red, blue, std::numeric_limits<double>::quiet_NaN()) == red);
  // Premultiplied: a fully transparent end contributes no colour.
  const Rgba clear{{0, 1, 0}, 0};
  const Rgba half = mix(red, clear, 0.5);
  R1_EXPECT(near(half.a, 0.5) && near(half.rgb.r, 1) && near(half.rgb.g, 0));
  const Rgba both = mix(clear, Rgba{{0, 0, 1}, 0}, 0.5);
  R1_EXPECT(both.a == 0 && std::isfinite(both.rgb.r));
  const Rgb comp = over({{1, 0, 0}, 0.5}, {0, 0, 1});
  R1_EXPECT(near(comp.r, 0.5) && near(comp.b, 0.5));
}

void testColorStateHueMemory() {
  ColorState s;
  s.setHue(200);
  s.setSaturationValue(0, 0.83);  // grey: the hue stays
  R1_EXPECT(s.hsv.h == 200);
  const Rgba grey = s.rgba();
  s.adopt(grey);  // the picker re-applies its own output
  R1_EXPECT(s.hsv.h == 200 && s.hsv.s == 0);
  s.adopt({{0.5, 0.5, 0.5}, 0.25});  // a different grey: hue still kept, alpha taken
  R1_EXPECT(s.hsv.h == 200 && s.alpha == 0.25);
  s.adopt({{0, 0, 0}, 1});  // black
  R1_EXPECT(s.hsv.h == 200 && s.hsv.v == 0);
  s.adopt({{1, 0, 0}, 1});  // a colour: hue follows
  R1_EXPECT(s.hsv.h == 0 && s.hsv.s == 1 && s.hsv.v == 1);
  s.setHue(120);
  s.setSaturationValue(1, 0);  // black with a stored saturation
  const Rgba black = s.rgba();
  s.adopt(black);
  R1_EXPECT(s.hsv.h == 120 && s.hsv.s == 1);  // adopting its own output never moves the handle
  R1_EXPECT(!s.setHue(120) && !s.setAlpha(s.alpha) && !s.setSaturationValue(1, 0));
  R1_EXPECT(s.setHue(-30) && s.hsv.h == 330);
  R1_EXPECT(s.setSaturationValue(0.5, 0.5) && s.setSaturationValue(5, -5) && s.hsv.s == 1 && s.hsv.v == 0);
  R1_EXPECT(ColorState::from({{0, 1, 0}, 0.5}).hsv.h == 120);
}

}  // namespace

int main() {
  testKnownValues();
  testRoundTrips();
  testHostileNumbers();
  testHex();
  testMix();
  testColorStateHueMemory();
  return r1test::finish();
}

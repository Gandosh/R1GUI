// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of ColorModel.h (conversions, quantisation, hex text, ColorState).
// Invariants: every public function returns finite values inside the documented ranges whatever it
//   is given; no function throws or allocates except the hex formatter / parser result strings.
// Callers: the colour picker, the gradient editor and their tests.
#include "r1ui/widgets/colorpicker/ColorModel.h"

#include <algorithm>
#include <cmath>

namespace r1ui::widgets::color {

namespace {

constexpr double kEps = 1e-12;

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

// Chroma-based hue to RGB for HSV and HSL (h in degrees, c chroma, m the lightness offset).
Rgb fromHueChroma(double h, double chroma, double m) {
  const double hp = wrapHue(h) / 60.0;
  const double x = chroma * (1.0 - std::fabs(std::fmod(hp, 2.0) - 1.0));
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  if (hp < 1.0) { r = chroma; g = x; }
  else if (hp < 2.0) { r = x; g = chroma; }
  else if (hp < 3.0) { g = chroma; b = x; }
  else if (hp < 4.0) { g = x; b = chroma; }
  else if (hp < 5.0) { r = x; b = chroma; }
  else { r = chroma; b = x; }
  return {clamp01(r + m), clamp01(g + m), clamp01(b + m)};
}

double hueOf(const Rgb& c, double maxv, double delta) {
  if (delta < kEps) return 0.0;
  double h;
  if (maxv == c.r) h = std::fmod((c.g - c.b) / delta, 6.0);
  else if (maxv == c.g) h = (c.b - c.r) / delta + 2.0;
  else h = (c.r - c.g) / delta + 4.0;
  return wrapHue(h * 60.0);
}

}  // namespace

double clamp01(double v) {
  if (std::isnan(v)) return 0.0;
  return std::clamp(v, 0.0, 1.0);
}

double wrapHue(double degrees) {
  if (!std::isfinite(degrees)) return 0.0;
  double h = std::fmod(degrees, 360.0);
  if (h < 0.0) h += 360.0;
  // fmod of a tiny negative number can round up to exactly 360.
  return h >= 360.0 ? 0.0 : h;
}

Rgb sanitized(const Rgb& c) { return {clamp01(c.r), clamp01(c.g), clamp01(c.b)}; }
Rgba sanitized(const Rgba& c) { return {sanitized(c.rgb), clamp01(c.a)}; }

Hsv toHsv(const Rgb& in) {
  const Rgb c = sanitized(in);
  const double maxv = std::max({c.r, c.g, c.b});
  const double minv = std::min({c.r, c.g, c.b});
  const double delta = maxv - minv;
  return {hueOf(c, maxv, delta), maxv < kEps ? 0.0 : delta / maxv, maxv};
}

Rgb toRgb(const Hsv& in) {
  const double s = clamp01(in.s);
  const double v = clamp01(in.v);
  const double chroma = v * s;
  return fromHueChroma(in.h, chroma, v - chroma);
}

Hsl toHsl(const Rgb& in) {
  const Rgb c = sanitized(in);
  const double maxv = std::max({c.r, c.g, c.b});
  const double minv = std::min({c.r, c.g, c.b});
  const double delta = maxv - minv;
  const double l = (maxv + minv) * 0.5;
  const double s = delta < kEps ? 0.0 : delta / (1.0 - std::fabs(2.0 * l - 1.0));
  return {hueOf(c, maxv, delta), clamp01(s), l};
}

Rgb toRgb(const Hsl& in) {
  const double s = clamp01(in.s);
  const double l = clamp01(in.l);
  const double chroma = (1.0 - std::fabs(2.0 * l - 1.0)) * s;
  return fromHueChroma(in.h, chroma, l - chroma * 0.5);
}

int to8(double unit) { return static_cast<int>(std::lround(clamp01(unit) * 255.0)); }

double from8(int value) { return static_cast<double>(std::clamp(value, 0, 255)) / 255.0; }

Rgb over(const Rgba& c, const Rgb& background) {
  const Rgba s = sanitized(c);
  const Rgb bg = sanitized(background);
  return {s.rgb.r * s.a + bg.r * (1.0 - s.a), s.rgb.g * s.a + bg.g * (1.0 - s.a), s.rgb.b * s.a + bg.b * (1.0 - s.a)};
}

Rgba mix(const Rgba& a, const Rgba& b, double t) {
  const Rgba x = sanitized(a);
  const Rgba y = sanitized(b);
  const double u = clamp01(t);
  const double alpha = x.a + (y.a - x.a) * u;
  if (alpha < kEps) return {{x.rgb.r + (y.rgb.r - x.rgb.r) * u, x.rgb.g + (y.rgb.g - x.rgb.g) * u, x.rgb.b + (y.rgb.b - x.rgb.b) * u}, 0.0};
  const auto channel = [&](double ca, double cb) { return clamp01((ca * x.a * (1.0 - u) + cb * y.a * u) / alpha); };
  return {{channel(x.rgb.r, y.rgb.r), channel(x.rgb.g, y.rgb.g), channel(x.rgb.b, y.rgb.b)}, alpha};
}

std::string formatHex(const Rgba& in, bool includeAlpha) {
  static constexpr char kDigits[] = "0123456789ABCDEF";
  const Rgba c = sanitized(in);
  const int values[4] = {to8(c.rgb.r), to8(c.rgb.g), to8(c.rgb.b), to8(c.a)};
  std::string out;
  const int count = includeAlpha ? 4 : 3;
  out.reserve(static_cast<size_t>(count) * 2);
  for (int i = 0; i < count; ++i) {
    out.push_back(kDigits[values[i] >> 4]);
    out.push_back(kDigits[values[i] & 15]);
  }
  return out;
}

std::optional<Rgba> parseHex(std::string_view text) {
  while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
  while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
  if (!text.empty() && text.front() == '#') text.remove_prefix(1);
  const size_t n = text.size();
  if (n != 3 && n != 4 && n != 6 && n != 8) return std::nullopt;
  int digits[8] = {};
  for (size_t i = 0; i < n; ++i) {
    digits[i] = hexDigit(text[i]);
    if (digits[i] < 0) return std::nullopt;
  }
  int v[4] = {0, 0, 0, 255};
  if (n <= 4) {
    for (size_t i = 0; i < n; ++i) v[i] = digits[i] * 17;
  } else {
    for (size_t i = 0; i < n / 2; ++i) v[i] = digits[2 * i] * 16 + digits[2 * i + 1];
  }
  return Rgba{{from8(v[0]), from8(v[1]), from8(v[2])}, from8(v[3])};
}

Rgba ColorState::rgba() const { return {toRgb(hsv), clamp01(alpha)}; }

ColorState ColorState::from(const Rgba& c) {
  ColorState s;
  s.adopt(c);
  return s;
}

void ColorState::adopt(const Rgba& in) {
  const Rgba c = sanitized(in);
  const Rgb current = toRgb(hsv);
  const bool sameColor = to8(current.r) == to8(c.rgb.r) && to8(current.g) == to8(c.rgb.g) && to8(current.b) == to8(c.rgb.b);
  if (!sameColor) {
    const Hsv next = toHsv(c.rgb);
    // A grey or black colour carries no hue: keep the one on display.
    hsv = {next.s < kEps || next.v < kEps ? hsv.h : next.h, next.s, next.v};
  }
  alpha = c.a;
}

bool ColorState::setHue(double degrees) {
  const double h = wrapHue(degrees);
  if (h == hsv.h) return false;
  hsv.h = h;
  return true;
}

bool ColorState::setSaturationValue(double s, double v) {
  const double ns = clamp01(s);
  const double nv = clamp01(v);
  if (ns == hsv.s && nv == hsv.v) return false;
  hsv.s = ns;
  hsv.v = nv;
  return true;
}

bool ColorState::setAlpha(double a) {
  const double na = clamp01(a);
  if (na == alpha) return false;
  alpha = na;
  return true;
}

}  // namespace r1ui::widgets::color

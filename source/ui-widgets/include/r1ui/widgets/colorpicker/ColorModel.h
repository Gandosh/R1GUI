// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the colour model of the colour picker and the gradient editor: sRGB, HSV and HSL value
//   types, the conversions between them, 8-bit quantisation and hex text parsing / formatting.
// Why: the picker edits a colour through three views (the saturation/value square, the sliders and
//   the numeric entries) that must agree to the last digit and survive hostile input (NaN, values
//   out of range, hex text from the clipboard); one pure module keeps that logic testable without a
//   UI context.
// Callers: ColorPicker and its parts, the gradient editor, tests. Calls: nothing (pure maths).
// Conventions: channels of Rgb and s, v, l, alpha are in 0..1; hue is in degrees [0, 360). Every
//   function accepts any double: NaN is treated as 0, infinities and out-of-range values are
//   clamped, hue wraps. Rgb is sRGB-encoded (the same space the Painter blends in), alpha is
//   straight (not premultiplied).
// Hue memory: an Hsv with s == 0 or v == 0 has no meaningful hue, yet the picker must keep the hue
//   the user chose (the square keeps showing it). ColorState therefore stores HSV + alpha as the
//   source of truth and only derives RGB; adopting an externally supplied RGB keeps the stored hue
//   when that RGB is what the stored state already produces.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace r1ui::widgets::color {

struct Rgb {
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  friend bool operator==(const Rgb&, const Rgb&) = default;
};

struct Hsv {
  double h = 0.0;  // degrees [0, 360)
  double s = 0.0;
  double v = 0.0;
  friend bool operator==(const Hsv&, const Hsv&) = default;
};

struct Hsl {
  double h = 0.0;
  double s = 0.0;
  double l = 0.0;
  friend bool operator==(const Hsl&, const Hsl&) = default;
};

struct Rgba {
  Rgb rgb;
  double a = 1.0;
  friend bool operator==(const Rgba&, const Rgba&) = default;
};

// ---- sanitising ----
double clamp01(double v);  // NaN -> 0
double wrapHue(double degrees);  // NaN or infinite -> 0, otherwise into [0, 360)
Rgb sanitized(const Rgb& c);
Rgba sanitized(const Rgba& c);

// ---- conversions (round trips are exact to 1e-9 away from the hue singularities) ----
Hsv toHsv(const Rgb& c);
Rgb toRgb(const Hsv& c);
Hsl toHsl(const Rgb& c);
Rgb toRgb(const Hsl& c);

// ---- 8-bit quantisation ----
int to8(double unit);       // round(clamp01(unit) * 255)
double from8(int value);    // value clamped to 0..255
// Straight-alpha blend of `c` over an opaque `background`.
Rgb over(const Rgba& c, const Rgb& background);
// Linear interpolation of two straight-alpha colours in premultiplied space (CSS gradients).
Rgba mix(const Rgba& a, const Rgba& b, double t);

// ---- hex text ----
// "RRGGBB" (uppercase, no '#'); with includeAlpha "RRGGBBAA".
std::string formatHex(const Rgba& c, bool includeAlpha = false);
// Accepts an optional leading '#', then 3, 4, 6 or 8 hex digits (#rgb, #rgba, #rrggbb, #rrggbbaa),
// any case, surrounding ASCII whitespace ignored. Anything else (including non-ASCII, signs, "0x")
// gives nullopt. Without an alpha digit pair the alpha is 1.
std::optional<Rgba> parseHex(std::string_view text);

// ---- the picker's editing state ----
struct ColorState {
  Hsv hsv;          // source of truth for the hue / saturation / value controls
  double alpha = 1.0;

  Rgba rgba() const;
  Rgb rgb() const { return toRgb(hsv); }
  // Adopts `c`. If `c` quantises to the same 8-bit colour as the stored state only the alpha is
  // taken (re-applying a colour the picker itself produced never moves the controls); otherwise the
  // HSV is derived from `c`, keeping the stored hue when `c` is grey or black.
  void adopt(const Rgba& c);
  static ColorState from(const Rgba& c);
  // Setters clamp / wrap; return true when the stored state changed.
  bool setHue(double degrees);
  bool setSaturationValue(double s, double v);
  bool setAlpha(double a);
};

}  // namespace r1ui::widgets::color

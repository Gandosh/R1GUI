// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the text <-> number conversions of the picker and editor entry fields: strict decimal
//   parsing (what the user may type or paste) and compact formatting (fixed digits, trailing zeros
//   dropped).
// Why: every numeric entry (hue, alpha %, RGB, stop position, key time and value) must reject
//   hostile text (NaN, inf, hex, locale separators, 1 MB of digits) the same way and print values
//   the same way, so one tested module serves them all.
// Callers: ColorPicker, GradientEditor, CurveEditor and their tests. Calls: <charconv>.
// Rules: parseNumber trims ASCII whitespace and accepts [+-]digits[.digits][(e|E)[+-]digits]
//   (also ".5" and "5."), at most 64 characters, finite results only; it never throws.
//   formatNumber rounds to maxFractionDigits (0..9), drops trailing zeros and the point, prints
//   "-0" as "0" and non-finite input as "0".
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace r1ui::widgets {

std::optional<double> parseNumber(std::string_view text);
std::string formatNumber(double value, int maxFractionDigits);

}  // namespace r1ui::widgets

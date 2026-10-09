// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the CSS cubic-bezier timing function used for 150 ms colour transitions.
// Why: tokens.json carries the easing as control points (0.4, 0, 0.2, 1); animations must follow it
//   rather than a hard-coded curve, and the function is pure so it is unit tested directly.
// Callers: UiContext animation, tests. Input t outside 0..1 is clamped; non-finite t counts as 1.
#pragma once

#include <array>

namespace r1ui::widgets {

// CSS cubic-bezier(x1, y1, x2, y2) evaluated at time fraction t (monotonic x assumed: control
// x values are clamped to 0..1).
double cubicBezierEase(const std::array<double, 4>& points, double t);

}  // namespace r1ui::widgets

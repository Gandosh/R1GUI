// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Easing.h (bisection on the bezier x(s) = t, then y(s)).
// Callers: UiContext animation, tests.
#include "r1ui/widgets/runtime/Easing.h"

#include <algorithm>
#include <cmath>

namespace r1ui::widgets {

namespace {

double bezier(double a, double b, double s) {
  // Cubic with end points 0 and 1 and control values a, b.
  const double u = 1.0 - s;
  return 3.0 * u * u * s * a + 3.0 * u * s * s * b + s * s * s;
}

}  // namespace

double cubicBezierEase(const std::array<double, 4>& p, double t) {
  if (!std::isfinite(t) || t >= 1.0) return 1.0;
  if (t <= 0.0) return 0.0;
  const double x1 = std::clamp(p[0], 0.0, 1.0);
  const double x2 = std::clamp(p[2], 0.0, 1.0);
  double lo = 0.0;
  double hi = 1.0;
  for (int i = 0; i < 40; ++i) {
    const double mid = (lo + hi) * 0.5;
    if (bezier(x1, x2, mid) < t) lo = mid;
    else hi = mid;
  }
  return bezier(p[1], p[3], (lo + hi) * 0.5);
}

}  // namespace r1ui::widgets

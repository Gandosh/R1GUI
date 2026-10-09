// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of SdfReference.h in double precision.
// Callers: paint/Painter.cpp (normalizeRadii) and tests. The formulas mirror shaders/sdf.frag; if
//   one changes the other must change with it (the GPU tests compare them).
#include "r1ui/render/SdfReference.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace r1ui::render::reference {

namespace {
// Horizontal inset of a corner arc at height y inside a box of half height hy: 0 on the straight
// part of the side, r at the very top/bottom row.
double cornerInset(double y, double radius, double halfHeight) {
  const double t = radius - (halfHeight - std::fabs(y));
  if (radius <= 0.0 || t <= 0.0) return 0.0;
  return radius - std::sqrt(std::max(radius * radius - t * t, 0.0));
}
}  // namespace

CornerRadii normalizeRadii(const Rect& rect, const CornerRadii& radii) {
  CornerRadii r{std::max(radii.topLeft, 0.0f), std::max(radii.topRight, 0.0f),
                std::max(radii.bottomRight, 0.0f), std::max(radii.bottomLeft, 0.0f)};
  double scale = 1.0;
  const auto limit = [&scale](double side, double sum) {
    if (sum > side && sum > 0.0) scale = std::min(scale, side / sum);
  };
  limit(rect.w, double{r.topLeft} + r.topRight);
  limit(rect.w, double{r.bottomLeft} + r.bottomRight);
  limit(rect.h, double{r.topLeft} + r.bottomLeft);
  limit(rect.h, double{r.topRight} + r.bottomRight);
  if (scale < 1.0) {
    r.topLeft = static_cast<float>(r.topLeft * scale);
    r.topRight = static_cast<float>(r.topRight * scale);
    r.bottomRight = static_cast<float>(r.bottomRight * scale);
    r.bottomLeft = static_cast<float>(r.bottomLeft * scale);
  }
  return r;
}

double signedDistance(double px, double py, const Rect& rect, const CornerRadii& radii) {
  const double hx = rect.w * 0.5;
  const double hy = rect.h * 0.5;
  const double qx = px - (rect.x + hx);
  const double qy = py - (rect.y + hy);
  const double r = qx < 0.0 ? (qy < 0.0 ? radii.topLeft : radii.bottomLeft)
                            : (qy < 0.0 ? radii.topRight : radii.bottomRight);
  const double dx = std::fabs(qx) - hx + r;
  const double dy = std::fabs(qy) - hy + r;
  return std::hypot(std::max(dx, 0.0), std::max(dy, 0.0)) + std::min(std::max(dx, dy), 0.0) - r;
}

double coverageFromDistance(double d) { return std::clamp(0.5 - d, 0.0, 1.0); }

double fillCoverage(double px, double py, const Rect& rect, const CornerRadii& radii) {
  return coverageFromDistance(signedDistance(px, py, rect, radii));
}

double borderCoverage(double px, double py, const Rect& outer, const CornerRadii& radii, double width) {
  const double outerCoverage = fillCoverage(px, py, outer, radii);
  const double w = static_cast<double>(width);
  const double innerW = outer.w - 2.0 * w;
  const double innerH = outer.h - 2.0 * w;
  if (innerW <= 0.0 || innerH <= 0.0) return outerCoverage;
  const Rect inner{static_cast<float>(outer.x + w), static_cast<float>(outer.y + w),
                   static_cast<float>(innerW), static_cast<float>(innerH)};
  const CornerRadii innerRadii{
      static_cast<float>(std::max(radii.topLeft - w, 0.0)), static_cast<float>(std::max(radii.topRight - w, 0.0)),
      static_cast<float>(std::max(radii.bottomRight - w, 0.0)), static_cast<float>(std::max(radii.bottomLeft - w, 0.0))};
  return outerCoverage * (1.0 - fillCoverage(px, py, inner, innerRadii));
}

double blurredBoxCoverage(double px, double py, const Rect& rect, const CornerRadii& radii, double sigma) {
  if (sigma <= 0.0) return fillCoverage(px, py, rect, radii);
  const double hx = rect.w * 0.5;
  const double hy = rect.h * 0.5;
  const double qx = px - (rect.x + hx);
  const double qy = py - (rect.y + hy);
  const double lo = std::max(qy - 6.0 * sigma, -hy);
  const double hi = std::min(qy + 6.0 * sigma, hy);
  if (hi <= lo) return 0.0;
  constexpr int kSamples = 1500;
  const double dy = (hi - lo) / kSamples;
  const double invSigmaRoot2 = 1.0 / (sigma * std::numbers::sqrt2);
  double sum = 0.0;
  for (int i = 0; i < kSamples; ++i) {
    const double y = lo + (i + 0.5) * dy;
    const double left = -hx + cornerInset(y, y < 0.0 ? radii.topLeft : radii.bottomLeft, hy);
    const double right = hx - cornerInset(y, y < 0.0 ? radii.topRight : radii.bottomRight, hy);
    const double row = 0.5 * (std::erf((right - qx) * invSigmaRoot2) - std::erf((left - qx) * invSigmaRoot2));
    const double weight = std::exp(-0.5 * (qy - y) * (qy - y) / (sigma * sigma));
    sum += row * weight * dy;
  }
  return std::clamp(sum / (sigma * std::sqrt(2.0 * std::numbers::pi)), 0.0, 1.0);
}

double lineCoverage(double px, double py, double x0, double y0, double x1, double y1, double width) {
  const double dx = x1 - x0;
  const double dy = y1 - y0;
  const double length = std::hypot(dx, dy);
  if (length <= 0.0 || width <= 0.0) return 0.0;
  const double ux = dx / length;
  const double uy = dy / length;
  const double qx = px - (x0 + x1) * 0.5;
  const double qy = py - (y0 + y1) * 0.5;
  const double along = std::fabs(qx * ux + qy * uy) - length * 0.5;
  const double across = std::fabs(-qx * uy + qy * ux) - width * 0.5;
  const double outside = std::hypot(std::max(along, 0.0), std::max(across, 0.0));
  return coverageFromDistance(outside + std::min(std::max(along, across), 0.0));
}

}  // namespace r1ui::render::reference

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: pixel and exact rectangles plus the coordinate limits shared by tree, layout, events
//   and invalidation.
// Why: every module measures in logical pixels; one definition of "the largest coordinate" keeps
//   32-bit arithmetic overflow-free by construction (see kMaxExtent).
// Callers: tree::Widget (stores both rectangle kinds), layout::*, events::Router (hit testing),
//   invalidation::DamageList. Calls: nothing.
// Invariants: Rect coordinates and sizes are clamped by the producer to +-kMaxExtent, so
//   x + w always fits int32 and areas fit int64.
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace r1ui::core::layout {

// Largest magnitude (in pixels) of any coordinate or size the layout engine produces.
// 2 * 1e9 < 2^31, so a sum of two clamped values never overflows int32.
inline constexpr double kMaxExtent = 1.0e9;

// A whole-pixel rectangle; w and h are never negative when produced by the layout engine.
struct Rect {
  int32_t x = 0;
  int32_t y = 0;
  int32_t w = 0;
  int32_t h = 0;

  friend bool operator==(const Rect&, const Rect&) = default;
  bool empty() const { return w <= 0 || h <= 0; }
  int64_t right() const { return int64_t{x} + w; }
  int64_t bottom() const { return int64_t{y} + h; }
  int64_t area() const { return empty() ? 0 : int64_t{w} * h; }
};

// An exact (unrounded) rectangle in layout space; the rounding pass converts it to a Rect.
struct RectD {
  double x = 0;
  double y = 0;
  double w = 0;
  double h = 0;
};

// Intersection; an empty Rect when the inputs do not overlap.
inline Rect intersect(const Rect& a, const Rect& b) {
  const int64_t l = std::max<int64_t>(a.x, b.x);
  const int64_t t = std::max<int64_t>(a.y, b.y);
  const int64_t r = std::min(a.right(), b.right());
  const int64_t btm = std::min(a.bottom(), b.bottom());
  if (r <= l || btm <= t) return Rect{};
  // The extent can exceed int32 for rectangles near the +-1e9 limits: saturate instead of wrapping.
  constexpr int64_t kMaxExtent32 = std::numeric_limits<int32_t>::max();
  return Rect{static_cast<int32_t>(l), static_cast<int32_t>(t), static_cast<int32_t>(std::min(r - l, kMaxExtent32)),
              static_cast<int32_t>(std::min(btm - t, kMaxExtent32))};
}

// Smallest rectangle containing both; an empty input contributes nothing.
inline Rect unite(const Rect& a, const Rect& b) {
  if (a.empty()) return b.empty() ? Rect{} : b;
  if (b.empty()) return a;
  const int64_t l = std::min<int64_t>(a.x, b.x);
  const int64_t t = std::min<int64_t>(a.y, b.y);
  const int64_t r = std::max(a.right(), b.right());
  const int64_t btm = std::max(a.bottom(), b.bottom());
  // The extent can exceed int32 for rectangles near the +-1e9 limits: saturate instead of wrapping.
  constexpr int64_t kMaxExtent32 = std::numeric_limits<int32_t>::max();
  return Rect{static_cast<int32_t>(l), static_cast<int32_t>(t), static_cast<int32_t>(std::min(r - l, kMaxExtent32)),
              static_cast<int32_t>(std::min(btm - t, kMaxExtent32))};
}

// True when b lies completely inside a (an empty b is contained in anything).
inline bool containsRect(const Rect& a, const Rect& b) {
  if (b.empty()) return true;
  if (a.empty()) return false;
  return b.x >= a.x && b.y >= a.y && b.right() <= a.right() && b.bottom() <= a.bottom();
}

// Half-open point containment: [x, x+w) x [y, y+h).
inline bool containsPoint(const Rect& r, double px, double py) {
  return !r.empty() && px >= r.x && py >= r.y && px < static_cast<double>(r.right()) &&
         py < static_cast<double>(r.bottom());
}

}  // namespace r1ui::core::layout

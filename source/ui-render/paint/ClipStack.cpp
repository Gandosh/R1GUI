// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of ClipStack.h (intersection, pixel snapping, stack discipline).
// Callers: paint/Painter.cpp, unit tests. Pure integer/float math; no allocation beyond the vector.
#include "r1ui/render/ClipStack.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

#include "r1ui/core/CheckedCast.h"

namespace r1ui::render {

namespace {
constexpr double kSnapLimit = 536870912.0;  // 2^29: any width or height (up to 2^30) fits int32

// Clamps then rounds to the nearest integer, halves up.
int32_t snapEdge(double value) {
  const double clamped = std::clamp(value, -kSnapLimit, kSnapLimit);
  return core::checkedCast<int32_t>(static_cast<int64_t>(std::floor(clamped + 0.5)));
}
}  // namespace

IRect intersect(const IRect& a, const IRect& b) {
  // 64-bit edges: a.x + a.w may exceed int32 for hostile inputs.
  const int64_t x0 = std::max<int64_t>(a.x, b.x);
  const int64_t y0 = std::max<int64_t>(a.y, b.y);
  const int64_t x1 = std::min<int64_t>(int64_t{a.x} + a.w, int64_t{b.x} + b.w);
  const int64_t y1 = std::min<int64_t>(int64_t{a.y} + a.h, int64_t{b.y} + b.h);
  if (x1 <= x0 || y1 <= y0) return IRect{};
  return IRect{core::checkedCast<int32_t>(x0), core::checkedCast<int32_t>(y0),
               core::checkedCast<int32_t>(x1 - x0), core::checkedCast<int32_t>(y1 - y0)};
}

IRect snapToPixels(const Rect& rect) {
  const double x = rect.x;
  const double y = rect.y;
  const double w = rect.w;
  const double h = rect.h;
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h)) return IRect{};
  const int32_t x0 = snapEdge(x);
  const int32_t y0 = snapEdge(y);
  const int32_t x1 = snapEdge(x + w);
  const int32_t y1 = snapEdge(y + h);
  if (x1 <= x0 || y1 <= y0) return IRect{};
  return IRect{x0, y0, core::checkedCast<int32_t>(int64_t{x1} - x0),
               core::checkedCast<int32_t>(int64_t{y1} - y0)};
}

void ClipStack::reset(const IRect& bounds) {
  stack_.clear();
  stack_.push_back(bounds.empty() ? IRect{} : bounds);
}

void ClipStack::push(const Rect& rect) { stack_.push_back(intersect(stack_.back(), snapToPixels(rect))); }

void ClipStack::pop() {
  if (stack_.size() <= 1) throw std::logic_error("ClipStack::pop without a matching push");
  stack_.pop_back();
}

}  // namespace r1ui::render

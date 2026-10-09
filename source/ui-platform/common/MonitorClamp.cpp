// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: clampToVisible (Monitors.h): the rule that a window overlapping no monitor work area by
//   the visible margin is moved fully onto the nearest work area.
// Why: restored or dragged floating windows must stay reachable after monitor changes; kept free
//   of OS calls so synthetic monitor layouts can be tested.
// Callers: window placement code, tests.
// Invariants: 64-bit intermediate arithmetic (no overflow for any int rectangles); results are
//   saturated to the int range; never throws.
#include <algorithm>
#include <cstdint>
#include <limits>

#include "r1ui/platform/Dpi.h"
#include "r1ui/platform/Monitors.h"

namespace r1ui::platform {

namespace {

int64_t right(const Rect& r) { return static_cast<int64_t>(r.x) + r.width; }
int64_t bottom(const Rect& r) { return static_cast<int64_t>(r.y) + r.height; }

int64_t overlap(int64_t a0, int64_t a1, int64_t b0, int64_t b1) {
  return std::max<int64_t>(0, std::min(a1, b1) - std::max(a0, b0));
}

int saturateToInt(int64_t v) {
  return static_cast<int>(std::clamp<int64_t>(v, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
}

// Gap between two rectangles along one axis (0 when they overlap or touch).
int64_t gap(int64_t a0, int64_t a1, int64_t b0, int64_t b1) {
  return std::max<int64_t>({0, b0 - a1, a0 - b1});
}

// Euclidean distance between rectangles as double (squared sums of 33-bit gaps exceed int64).
double distance(const Rect& a, const Rect& b) {
  const double dx = static_cast<double>(gap(a.x, right(a), b.x, right(b)));
  const double dy = static_cast<double>(gap(a.y, bottom(a), b.y, bottom(b)));
  return dx * dx + dy * dy;
}

}  // namespace

Rect clampToVisible(const Rect& window, std::span<const MonitorInfo> monitors, int marginLogical) {
  if (window.empty()) return window;

  const MonitorInfo* nearest = nullptr;
  double nearestDistance = 0.0;
  for (const MonitorInfo& m : monitors) {
    if (m.workArea.empty()) continue;
    const int margin = std::max(0, logicalToPhysical(static_cast<float>(std::max(0, marginLogical)), m.dpiScale));
    const int64_t needX = std::min(margin, window.width);
    const int64_t needY = std::min(margin, window.height);
    const int64_t ox = overlap(window.x, right(window), m.workArea.x, right(m.workArea));
    const int64_t oy = overlap(window.y, bottom(window), m.workArea.y, bottom(m.workArea));
    if (ox > 0 && oy > 0 && ox >= needX && oy >= needY) return window;
    const double d = distance(window, m.workArea);
    if (nearest == nullptr || d < nearestDistance) {
      nearest = &m;
      nearestDistance = d;
    }
  }
  if (nearest == nullptr) return window;

  const Rect& work = nearest->workArea;
  Rect out;
  out.width = std::min(window.width, work.width);
  out.height = std::min(window.height, work.height);
  out.x = saturateToInt(std::clamp<int64_t>(window.x, work.x, right(work) - out.width));
  out.y = saturateToInt(std::clamp<int64_t>(window.y, work.y, bottom(work) - out.height));
  return out;
}

}  // namespace r1ui::platform

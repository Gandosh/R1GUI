// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: fitWindowRect and hasUsableMonitor (see DockMonitors.h).
// Invariants: output sizes are finite, within [minSize, kMaxCoordinate] and positions within
//   +-kMaxCoordinate; the function is idempotent (fitting a fitted rectangle changes nothing).
#include "r1ui/dock/DockMonitors.h"

#include <algorithm>
#include <cmath>

namespace r1ui::dock {

namespace {

bool finiteRect(const Rect& r) {
  return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.w) && std::isfinite(r.h);
}

bool usable(const MonitorInfo& m) {
  return finiteRect(m.workArea) && finiteRect(m.bounds) && m.workArea.w > 0.0 && m.workArea.h > 0.0;
}

// Length of the overlap of [a0, a0 + alen) and [b0, b0 + blen); 0 when disjoint.
double overlap(double a0, double alen, double b0, double blen) {
  return std::max(0.0, std::min(a0 + alen, b0 + blen) - std::max(a0, b0));
}

}  // namespace

bool hasUsableMonitor(const MonitorSet& monitors) {
  return std::any_of(monitors.monitors.begin(), monitors.monitors.end(), usable);
}

FitResult fitWindowRect(const Rect& window, const MonitorSet& monitors, double margin, double minSize) {
  const double floorSize = std::isfinite(minSize) && minSize > 0.0 ? minSize : 1.0;
  const double safeMargin = std::isfinite(margin) && margin > 0.0 ? margin : 0.0;
  const auto sane = [](double v, double lo, double hi, double fallback) {
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
  };
  Rect fitted{sane(window.x, -kMaxCoordinate, kMaxCoordinate, 0.0), sane(window.y, -kMaxCoordinate, kMaxCoordinate, 0.0),
              sane(window.w, floorSize, kMaxCoordinate, floorSize), sane(window.h, floorSize, kMaxCoordinate, floorSize)};
  FitResult result;
  result.rect = fitted;
  result.resized = fitted.w != window.w || fitted.h != window.h;
  result.moved = fitted.x != window.x || fitted.y != window.y;
  if (!hasUsableMonitor(monitors)) return result;

  const double needW = std::min(safeMargin, fitted.w);
  const double needH = std::min(safeMargin, fitted.h);
  for (const MonitorInfo& m : monitors.monitors) {
    if (!usable(m)) continue;
    if (overlap(fitted.x, fitted.w, m.bounds.x, m.bounds.w) >= needW &&
        overlap(fitted.y, fitted.h, m.bounds.y, m.bounds.h) >= needH) {
      return result;
    }
  }

  // Not visible enough: centre on the preferred work area, reducing the size to fit it.
  const size_t preferred = monitors.primary < monitors.monitors.size() && usable(monitors.monitors[monitors.primary])
                               ? monitors.primary
                               : static_cast<size_t>(std::find_if(monitors.monitors.begin(), monitors.monitors.end(), usable) -
                                                     monitors.monitors.begin());
  const Rect& area = monitors.monitors[preferred].workArea;
  Rect placed;
  placed.w = std::max(std::min(fitted.w, area.w), std::min(floorSize, area.w));
  placed.h = std::max(std::min(fitted.h, area.h), std::min(floorSize, area.h));
  placed.x = area.x + (area.w - placed.w) / 2.0;
  placed.y = area.y + (area.h - placed.h) / 2.0;
  result.moved = placed.x != window.x || placed.y != window.y;
  result.resized = placed.w != window.w || placed.h != window.h;
  result.rect = placed;
  return result;
}

}  // namespace r1ui::dock

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the implementation of NativeCoords.h (see the header for the mapping rules).
// Invariants: every function is pure; scale values are sanitised (non-finite or non-positive -> 1) so a
//   misreporting monitor cannot produce infinities; monitor lists are small (a handful), so the linear
//   scans are the right data structure.
// Callers: NativeFloatingBackend, tests.
#include "r1ui/widgets/dock/native/NativeCoords.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "r1ui/platform/Dpi.h"

namespace r1ui::widgets::native {

namespace {

// Distance from a point to a rectangle (0 inside).
double distanceTo(const dock::Rect& r, double x, double y) {
  const double dx = std::max({r.x - x, 0.0, x - r.right()});
  const double dy = std::max({r.y - y, 0.0, y - r.bottom()});
  return std::hypot(dx, dy);
}

dock::Rect physicalRect(const platform::Rect& r) {
  return {static_cast<double>(r.x), static_cast<double>(r.y), static_cast<double>(r.width), static_cast<double>(r.height)};
}

}  // namespace

ScreenSpace::ScreenSpace(std::vector<platform::MonitorInfo> monitors) : monitors_(std::move(monitors)) {
  for (platform::MonitorInfo& m : monitors_) m.dpiScale = platform::sanitizeScale(m.dpiScale);
}

double ScreenSpace::scaleOf(int monitor) const {
  if (monitor < 0 || static_cast<size_t>(monitor) >= monitors_.size()) return 1.0;
  return static_cast<double>(monitors_[static_cast<size_t>(monitor)].dpiScale);
}

dock::Rect ScreenSpace::logicalBounds(int monitor) const {
  if (monitor < 0 || static_cast<size_t>(monitor) >= monitors_.size()) return {};
  const dock::Rect p = physicalRect(monitors_[static_cast<size_t>(monitor)].bounds);
  const double s = scaleOf(monitor);
  return {p.x / s, p.y / s, p.w / s, p.h / s};
}

dock::Rect ScreenSpace::logicalWorkArea(int monitor) const {
  if (monitor < 0 || static_cast<size_t>(monitor) >= monitors_.size()) return {};
  const dock::Rect p = physicalRect(monitors_[static_cast<size_t>(monitor)].workArea);
  const double s = scaleOf(monitor);
  return {p.x / s, p.y / s, p.w / s, p.h / s};
}

int ScreenSpace::monitorOfPhysical(double x, double y) const {
  int best = -1;
  double bestDistance = std::numeric_limits<double>::infinity();
  for (size_t i = 0; i < monitors_.size(); ++i) {
    const double d = distanceTo(physicalRect(monitors_[i].bounds), x, y);
    if (d < bestDistance) {  // ties keep the earlier monitor (the primary comes first)
      bestDistance = d;
      best = static_cast<int>(i);
    }
  }
  return best;
}

int ScreenSpace::monitorOfLogical(dock::Point logical, int hint) const {
  if (monitors_.empty()) return -1;
  if (hint >= 0 && static_cast<size_t>(hint) < monitors_.size() && distanceTo(logicalBounds(hint), logical.x, logical.y) == 0.0) return hint;
  int best = -1;
  double bestDistance = std::numeric_limits<double>::infinity();
  for (size_t i = 0; i < monitors_.size(); ++i) {
    const double d = distanceTo(logicalBounds(static_cast<int>(i)), logical.x, logical.y);
    if (d < bestDistance) {
      bestDistance = d;
      best = static_cast<int>(i);
    }
  }
  return best;
}

dock::Point ScreenSpace::toLogical(double physicalX, double physicalY) const {
  const double s = scaleOf(monitorOfPhysical(physicalX, physicalY));
  return {physicalX / s, physicalY / s};
}

dock::Point ScreenSpace::toPhysical(dock::Point logical, int hint) const {
  const double s = scaleOf(monitorOfLogical(logical, hint));
  return {logical.x * s, logical.y * s};
}

std::string ScreenSpace::nameAt(dock::Point logical, int hint) const {
  const int m = monitorOfLogical(logical, hint);
  return m < 0 ? std::string() : monitors_[static_cast<size_t>(m)].name;
}

dock::Point ScreenSpace::largestWorkAreaSize() const {
  dock::Point largest{0.0, 0.0};
  for (size_t i = 0; i < monitors_.size(); ++i) {
    const dock::Rect w = logicalWorkArea(static_cast<int>(i));
    largest.x = std::max(largest.x, w.w);
    largest.y = std::max(largest.y, w.h);
  }
  return largest;
}

// ---- frame arithmetic -----------------------------------------------------------------------------

dock::Rect outerOfContent(const dock::Rect& content, const FrameMetrics& frame) {
  return {content.x - frame.border, content.y - frame.titleHeight, content.w + 2.0 * frame.border, content.h + frame.titleHeight + frame.border};
}

dock::Rect limitContentSize(dock::Rect content, dock::Point minimum, dock::Point maximum) {
  const double maxW = std::clamp(maximum.x, 1.0, dock::kMaxCoordinate);
  const double maxH = std::clamp(maximum.y, 1.0, dock::kMaxCoordinate);
  // The minimum wins over the maximum on a tiny screen: a window below its minimum is unusable.
  content.w = std::max(std::min(content.w, maxW), std::clamp(minimum.x, 1.0, dock::kMaxCoordinate));
  content.h = std::max(std::min(content.h, maxH), std::clamp(minimum.y, 1.0, dock::kMaxCoordinate));
  return content;
}

// ---- drop target query ----------------------------------------------------------------------------

std::optional<FloatId> topmostWindow(std::span<const StackedWindow> bottomToTop, const dock::Rect& main, bool mainVisible, dock::Point screen,
                                     std::span<const FloatId> exclude) {
  if (!std::isfinite(screen.x) || !std::isfinite(screen.y)) return std::nullopt;
  const auto excluded = [&](FloatId id) { return std::find(exclude.begin(), exclude.end(), id) != exclude.end(); };
  for (auto it = bottomToTop.rbegin(); it != bottomToTop.rend(); ++it) {
    if (it->visible && !excluded(it->id) && it->outer.contains(screen)) return it->id;
  }
  if (mainVisible && !excluded(kMainWindow) && main.contains(screen)) return kMainWindow;
  return std::nullopt;
}

}  // namespace r1ui::widgets::native

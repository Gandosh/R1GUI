// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for clampToVisible on synthetic monitor layouts: visible windows stay put, stranded
//   windows land fully on the nearest work area, DPI-scaled margin, and hostile extremes.
// Callers: CTest (label fast). Exit code 0 = pass.
#include <climits>
#include <vector>

#include "TestSupport.h"
#include "r1ui/platform/Monitors.h"

using namespace r1ui::platform;
using platform_test::expect;
using platform_test::runCase;

namespace {

MonitorInfo monitor(Rect bounds, Rect work, float scale, bool primary) {
  MonitorInfo m;
  m.bounds = bounds;
  m.workArea = work;
  m.dpiScale = scale;
  m.primary = primary;
  return m;
}

// 1920x1080 primary with a 40 px taskbar at the bottom.
std::vector<MonitorInfo> single() { return {monitor({0, 0, 1920, 1080}, {0, 0, 1920, 1040}, 1.0f, true)}; }

// Primary plus a 2560x1440 monitor at 200% to its right.
std::vector<MonitorInfo> dual() {
  return {monitor({0, 0, 1920, 1080}, {0, 0, 1920, 1040}, 1.0f, true),
          monitor({1920, 0, 2560, 1440}, {1920, 0, 2560, 1400}, 2.0f, false)};
}

bool insideWork(const Rect& r, const Rect& work) {
  return r.x >= work.x && r.y >= work.y && static_cast<long long>(r.x) + r.width <= static_cast<long long>(work.x) + work.width &&
         static_cast<long long>(r.y) + r.height <= static_cast<long long>(work.y) + work.height;
}

void visibleWindowsStay() {
  const auto m = single();
  expect(clampToVisible({100, 100, 800, 600}, m) == Rect{100, 100, 800, 600}, "fully inside");
  expect(clampToVisible({1800, 500, 400, 300}, m) == Rect{1800, 500, 400, 300}, "120 px overlap is enough");
  expect(clampToVisible({1820, 500, 400, 300}, m) == Rect{1820, 500, 400, 300}, "exactly 100 px overlap is enough");
  expect(clampToVisible({-300, 100, 400, 300}, m) == Rect{-300, 100, 400, 300}, "100 px at the left edge is enough");
  expect(clampToVisible({-10, -10, 5000, 4000}, m) == Rect{-10, -10, 5000, 4000}, "huge window overlapping is visible");
}

void strandedWindowsMove() {
  const auto m = single();
  expect(clampToVisible({1821, 500, 400, 300}, m) == Rect{1520, 500, 400, 300}, "99 px overlap: moved fully in");
  expect(clampToVisible({-5000, 100, 800, 600}, m) == Rect{0, 100, 800, 600}, "far left");
  expect(clampToVisible({9000, 100, 800, 600}, m) == Rect{1120, 100, 800, 600}, "far right");
  expect(clampToVisible({100, 3000, 800, 600}, m) == Rect{100, 440, 800, 600}, "far below lands above the taskbar");
  expect(clampToVisible({100, -3000, 800, 600}, m) == Rect{100, 0, 800, 600}, "far above");
  expect(clampToVisible({100, 1000, 800, 600}, m) == Rect{100, 440, 800, 600}, "mostly under the taskbar: 40 px overlap");
  expect(clampToVisible({5000, 5000, 3000, 2000}, m) == Rect{0, 0, 1920, 1040}, "oversized stranded window shrinks to the work area");
}

void marginScalesWithMonitorDpi() {
  const auto m = dual();
  // On the 200% monitor the margin is 200 physical px: an 80 px overlap is not enough.
  const Rect r = clampToVisible({4400, 100, 600, 400}, m);
  expect(r == Rect{3880, 100, 600, 400}, "80 px overlap on a 200% monitor is stranded and moved in");
  expect(clampToVisible({4280, 100, 600, 400}, m) == Rect{4280, 100, 600, 400}, "200 px overlap on a 200% monitor is visible");
  expect(clampToVisible({1750, 100, 600, 400}, m) == Rect{1750, 100, 600, 400}, "spanning both monitors is visible");
}

void nearestMonitorWins() {
  const auto m = dual();
  const Rect farRight = clampToVisible({8000, 50, 700, 500}, m);
  expect(farRight == Rect{3780, 50, 700, 500}, "right of everything: nearest is the second monitor");
  const Rect farLeft = clampToVisible({-4000, 50, 700, 500}, m);
  expect(farLeft == Rect{0, 50, 700, 500}, "left of everything: nearest is the primary");
  expect(insideWork(farRight, m[1].workArea) && insideWork(farLeft, m[0].workArea), "results lie inside the work areas");
  // Equidistant between two monitors: the earlier entry wins.
  const std::vector<MonitorInfo> two = {monitor({0, 0, 1000, 1000}, {0, 0, 1000, 1000}, 1.0f, true),
                                        monitor({2000, 0, 1000, 1000}, {2000, 0, 1000, 1000}, 1.0f, false)};
  expect(clampToVisible({1450, 0, 100, 100}, two).x == 900, "tie goes to the earlier monitor");
}

void smallWindowsUseTheirOwnSize() {
  const auto m = single();
  expect(clampToVisible({1900, 500, 50, 50}, m) == Rect{1870, 500, 50, 50}, "50 px window with 20 px overlap is stranded");
  expect(clampToVisible({1870, 500, 50, 50}, m) == Rect{1870, 500, 50, 50}, "a window smaller than the margin must be fully visible");
}

void gridPropertyStrandedAlwaysEndsInside() {
  const auto m = dual();
  int checked = 0;
  for (int x = -6000; x <= 9000; x += 500) {
    for (int y = -4000; y <= 5000; y += 500) {
      const Rect w{x, y, 640, 480};
      const Rect r = clampToVisible(w, m);
      const bool changed = !(r == w);
      const bool inside = insideWork(r, m[0].workArea) || insideWork(r, m[1].workArea);
      if (changed && !inside) {
        expect(false, "moved window must be fully inside one work area");
        return;
      }
      if (changed && (r.width != w.width || r.height != w.height)) {
        expect(false, "a window that fits keeps its size");
        return;
      }
      // Idempotent: a clamped window is visible, so clamping it again changes nothing.
      if (!(clampToVisible(r, m) == r)) {
        expect(false, "clamping is idempotent");
        return;
      }
      ++checked;
    }
  }
  expect(checked > 500, "grid actually ran");
}

void hostileInputs() {
  const std::vector<MonitorInfo> none;
  expect(clampToVisible({5000, 5000, 100, 100}, none) == Rect{5000, 5000, 100, 100}, "no monitors: unchanged");
  expect(clampToVisible({5000, 5000, 0, 100}, single()) == Rect{5000, 5000, 0, 100}, "empty window: unchanged");
  expect(clampToVisible({5000, 5000, -50, -50}, single()) == Rect{5000, 5000, -50, -50}, "negative size: unchanged");
  const std::vector<MonitorInfo> degenerate = {monitor({0, 0, 0, 0}, {0, 0, 0, 0}, 1.0f, true)};
  expect(clampToVisible({5000, 5000, 100, 100}, degenerate) == Rect{5000, 5000, 100, 100}, "only empty work areas: unchanged");
  const auto m = single();
  const Rect hugeRight = clampToVisible({INT_MAX - 10, INT_MAX - 10, INT_MAX, INT_MAX}, m);
  expect(insideWork(hugeRight, m[0].workArea), "INT_MAX extents do not overflow and end inside");
  const Rect hugeLeft = clampToVisible({INT_MIN, INT_MIN, 800, 600}, m);
  expect(hugeLeft == Rect{0, 0, 800, 600}, "INT_MIN origin ends at the corner");
  expect(clampToVisible({5000, 100, 800, 600}, m, -50) == Rect{1120, 100, 800, 600}, "negative margin behaves as zero: any overlap is enough, none is stranded");
  expect(clampToVisible({100, 100, 800, 600}, m, INT_MAX) == Rect{100, 100, 800, 600}, "huge margin is capped by the window size");
  std::vector<MonitorInfo> badScale = {monitor({0, 0, 1920, 1080}, {0, 0, 1920, 1040}, 0.0f, true)};
  expect(clampToVisible({100, 100, 800, 600}, badScale) == Rect{100, 100, 800, 600}, "zero dpi scale treated as 1.0");
}

}  // namespace

int main() {
  runCase("visible_windows_stay", visibleWindowsStay);
  runCase("stranded_windows_move", strandedWindowsMove);
  runCase("margin_scales_with_dpi", marginScalesWithMonitorDpi);
  runCase("nearest_monitor_wins", nearestMonitorWins);
  runCase("small_windows", smallWindowsUseTheirOwnSize);
  runCase("grid_property", gridPropertyStrandedAlwaysEndsInside);
  runCase("hostile_inputs", hostileInputs);
  return platform_test::finish("ui-platform monitor clamp test");
}

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of the pure geometry of the native floating backend (NativeCoords.h): the mapping of
//   virtual-desktop physical pixels to the dock's screen logical pixels and back for several monitor
//   layouts and DPI scales (one monitor, this machine's two, side by side with different scales, a
//   monitor left of the primary, three monitors, none), the frame arithmetic, the size limits and the
//   drop-target query over a stack of windows (stacking, hidden, excluded, frames, minimized main).
// Why: the contract defines screen space piecewise; the rules for the places where two monitors'
//   logical ranges overlap must be pinned by tests, and no OS or window is needed for that.
// Callers: CTest. The file name ends in _gpu_test only because NativeCoords lives in the gpu library
//   (its folder, src/gpu/dock); the test itself needs neither a GPU nor a desktop.
#include <cmath>
#include <limits>

#include "ExpectWithMessage.h"
#include "r1ui/widgets/dock/native/NativeCoords.h"

using namespace r1ui::widgets;
using namespace r1ui::widgets::native;
namespace dock = r1ui::dock;
namespace platform = r1ui::platform;

namespace {

platform::MonitorInfo monitor(int x, int y, int w, int h, float scale, const char* name, bool primary = false, int taskbar = 0) {
  platform::MonitorInfo m;
  m.bounds = {x, y, w, h};
  m.workArea = {x, y, w, h - taskbar};
  m.dpiScale = scale;
  m.name = name;
  m.primary = primary;
  return m;
}

bool near(dock::Point a, dock::Point b, double eps = 1e-9) { return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps; }

void single_monitor() {
  const ScreenSpace s({monitor(0, 0, 1920, 1080, 1.0f, "A", true, 40)});
  R1_EXPECT(near(s.toLogical(100, 200), {100, 200}) && near(s.toPhysical({100, 200}), {100, 200}), "scale 1 is the identity");
  R1_EXPECT(s.nameAt({10, 10}) == "A");
  R1_EXPECT(s.largestWorkAreaSize().x == 1920 && s.largestWorkAreaSize().y == 1040, "the work area excludes the taskbar");
  const ScreenSpace scaled({monitor(0, 0, 2880, 1620, 1.5f, "S", true)});
  R1_EXPECT(near(scaled.toLogical(300, 150), {200, 100}) && near(scaled.toPhysical({200, 100}), {300, 150}));
  R1_EXPECT(near({scaled.logicalBounds(0).w, scaled.logicalBounds(0).h}, {1920, 1080}), "2880x1620 at 150 percent is 1920x1080 logical");
  // Off the desktop the nearest monitor decides.
  R1_EXPECT(near(scaled.toLogical(-3000, -150), {-2000, -100}) && near(scaled.toPhysical({-2000, -100}), {-3000, -150}));
}

void this_machines_two_monitors() {
  // 3440x1440 primary at the origin and a 1920x1080 monitor below it, both at 100 percent.
  const ScreenSpace s({monitor(0, 0, 3440, 1440, 1.0f, "DISPLAY1", true), monitor(795, 1440, 1920, 1080, 1.0f, "DISPLAY2")});
  R1_EXPECT(s.monitorOfPhysical(100, 100) == 0 && s.monitorOfPhysical(800, 1500) == 1);
  R1_EXPECT(s.nameAt({100, 100}) == "DISPLAY1" && s.nameAt({1000, 1600}) == "DISPLAY2");
  R1_EXPECT(near(s.toLogical(1000, 1600), {1000, 1600}) && near(s.toPhysical({1000, 1600}), {1000, 1600}));
  R1_EXPECT(s.monitorOfPhysical(-50, 100) == 0 && s.monitorOfPhysical(2800, 2000) == 1, "outside: the nearest monitor");
}

void different_scales_overlap() {
  // A 1920x1080 monitor at 100 percent and a 3840x2160 monitor at 200 percent on its right: B shows
  // logical x 960..2880, which overlaps A's 0..1920.
  const ScreenSpace s({monitor(0, 0, 1920, 1080, 1.0f, "A", true), monitor(1920, 0, 3840, 2160, 2.0f, "B")});
  R1_EXPECT(near(s.toLogical(2000, 400), {1000, 200}), "a physical point on B divides by 2");
  R1_EXPECT(near(s.toLogical(1500, 400), {1500, 400}), "a physical point on A divides by 1");
  const dock::Point overlap{1500, 500};  // inside both logical ranges
  R1_EXPECT(near(s.toPhysical(overlap), {1500, 500}), "no hint: the first monitor (the primary)");
  R1_EXPECT(near(s.toPhysical(overlap, 0), {1500, 500}) && near(s.toPhysical(overlap, 1), {3000, 1000}), "the hint monitor wins where both show the point");
  R1_EXPECT(near(s.toPhysical({2500, 500}), {5000, 1000}), "beyond A's range only B shows it");
  R1_EXPECT(near(s.toPhysical({2500, 500}, 0), {5000, 1000}), "a hint that does not show the point is ignored");
  // A window of 300x200 logical keeps its logical size on either monitor: physical = logical * scale.
  R1_EXPECT(s.scaleOf(0) * 300 == 300 && s.scaleOf(1) * 300 == 600);
  // Round trips on both monitors.
  for (const dock::Point p : {dock::Point{10, 10}, dock::Point{1900, 1000}}) R1_EXPECT(near(s.toLogical(s.toPhysical(p, 0).x, s.toPhysical(p, 0).y), p));
  for (const dock::Point p : {dock::Point{2000, 5}, dock::Point{2800, 1000}}) R1_EXPECT(near(s.toLogical(s.toPhysical(p, 1).x, s.toPhysical(p, 1).y), p));
}

void monitor_left_of_primary_and_three() {
  const ScreenSpace s({monitor(0, 0, 2560, 1440, 1.0f, "P", true), monitor(-1920, 100, 1920, 1080, 1.5f, "L"), monitor(2560, -200, 1920, 1080, 1.25f, "R")});
  R1_EXPECT(near(s.toLogical(-960, 400), {-640, 266.6666666666667}, 1e-9), "negative coordinates divide by the left monitor's 1.5");
  R1_EXPECT(s.nameAt(s.toLogical(-960, 400)) == "L");
  R1_EXPECT(near(s.toLogical(3200, 0), {2560, 0}), "the right monitor divides by 1.25");
  R1_EXPECT(s.nameAt({2600, 100}) == "R");
  const dock::Point p = s.toPhysical({-640, 266.6666666666667});
  R1_EXPECT(near(p, {-960, 400}, 1e-6));
  R1_EXPECT(s.largestWorkAreaSize().x > 0);
}

void no_monitors_and_bad_input() {
  const ScreenSpace none;
  R1_EXPECT(none.empty() && near(none.toLogical(5, 6), {5, 6}) && near(none.toPhysical({5, 6}), {5, 6}) && none.nameAt({1, 1}).empty());
  R1_EXPECT(none.largestWorkAreaSize().x == 0.0);
  const ScreenSpace bad({monitor(0, 0, 100, 100, 0.0f, "Z", true), monitor(100, 0, 100, 100, std::numeric_limits<float>::quiet_NaN(), "N")});
  R1_EXPECT(bad.scaleOf(0) == 1.0 && bad.scaleOf(1) == 1.0, "a scale that is zero or NaN is treated as 1");
  const ScreenSpace one({monitor(0, 0, 100, 100, 1.0f, "A", true)});
  R1_EXPECT(std::isnan(one.toLogical(std::nan(""), 1).x) && std::isnan(one.toPhysical({std::nan(""), 1}).x), "NaN in, NaN out: callers validate, nothing crashes");
  R1_EXPECT(one.scaleOf(7) == 1.0 && one.scaleOf(-1) == 1.0, "an invalid monitor index is scale 1");
}

void frame_arithmetic() {
  const FrameMetrics f;
  const dock::Rect content{100, 200, 300, 150};
  const dock::Rect outer = outerOfContent(content, f);
  R1_EXPECT(outer == dock::Rect({99, 166, 302, 185}), "1 px border, 34 px title bar");
  R1_EXPECT(contentOfOuter(outer, f) == content);
  R1_EXPECT(f.insets().top == 34 && f.insets().left == 1 && f.insets().bottom == 1 && f.insets().right == 1);
  R1_EXPECT(limitContentSize({0, 0, 3, 4}, {120, 90}, {1000, 800}) == dock::Rect({0, 0, 120, 90}), "raised to the minimum");
  R1_EXPECT(limitContentSize({5, 6, 5000, 4000}, {120, 90}, {1000, 800}) == dock::Rect({5, 6, 1000, 800}), "limited to the screen");
  R1_EXPECT(limitContentSize({0, 0, 50, 50}, {400, 300}, {200, 100}).w == 400, "the minimum wins on a tiny screen");
  R1_EXPECT(limitContentSize({0, 0, 1e12, 1e12}, {1, 1}, {1e12, 1e12}).w <= dock::kMaxCoordinate, "never above the coordinate limit");
}

void drop_target_stacking() {
  const FrameMetrics f;
  const dock::Rect main{0, 0, 800, 600};
  const StackedWindow a{1, outerOfContent({100, 100, 300, 200}, f), true};
  const StackedWindow b{2, outerOfContent({250, 150, 300, 200}, f), true};
  const StackedWindow stack[] = {a, b};
  const dock::Point inBoth{300, 200};
  R1_EXPECT(topmostWindow(stack, main, true, inBoth, {}) == std::optional<FloatId>(2), "the later window is on top");
  const FloatId skipB[] = {2};
  R1_EXPECT(topmostWindow(stack, main, true, inBoth, skipB) == std::optional<FloatId>(1), "excluded windows are skipped");
  const FloatId skipBoth[] = {1, 2};
  R1_EXPECT(topmostWindow(stack, main, true, inBoth, skipBoth) == std::optional<FloatId>(kMainWindow), "then the main window shows it");
  R1_EXPECT(topmostWindow(stack, main, true, {120, 120}, {}) == std::optional<FloatId>(1), "only a shows this point");
  // The frame counts as the window: the title bar is above the content rectangle.
  R1_EXPECT(topmostWindow(stack, main, true, {120, 100 - 20}, {}) == std::optional<FloatId>(1), "the title bar belongs to the window");
  R1_EXPECT(topmostWindow(stack, main, true, {99, 150}, {}) == std::optional<FloatId>(1), "so does the 1 px border");
  R1_EXPECT(topmostWindow(stack, main, true, {98.5, 150}, {}) == std::optional<FloatId>(kMainWindow), "beyond the border it is the main window");
  StackedWindow hiddenB = b;
  hiddenB.visible = false;
  const StackedWindow withHidden[] = {a, hiddenB};
  R1_EXPECT(topmostWindow(withHidden, main, true, inBoth, {}) == std::optional<FloatId>(1), "a hidden window takes no part");
  R1_EXPECT(!topmostWindow(stack, main, true, {-1.0e7, -1.0e7}, {}).has_value(), "outside every window");
  R1_EXPECT(!topmostWindow(stack, main, true, {900, 700}, {}).has_value(), "outside the main window and every float: a drop on the desktop");
  R1_EXPECT(!topmostWindow(stack, main, true, {std::nan(""), 5}, {}).has_value(), "NaN is shown by nothing");
  R1_EXPECT(topmostWindow(stack, main, false, {700, 500}, {}) != std::optional<FloatId>(kMainWindow), "a minimized main window shows nothing");
  R1_EXPECT(topmostWindow({}, main, true, {10, 10}, {}) == std::optional<FloatId>(kMainWindow));
  const FloatId skipMain[] = {kMainWindow};
  R1_EXPECT(!topmostWindow({}, main, true, {10, 10}, skipMain).has_value());
}

}  // namespace

int main() {
  single_monitor();
  this_machines_two_monitors();
  different_scales_overlap();
  monitor_left_of_primary_and_three();
  no_monitors_and_bad_input();
  frame_arithmetic();
  drop_target_stacking();
  return r1test::finish();
}

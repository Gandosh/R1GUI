// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for the view maths of the curve editor (CurveView.h) against the numbers of
//   spec 11: wheel zoom 90 % per notch and its multiplier clamp, acceptance scenario 1, zoom limits,
//   panning, fitting with the 50 px margin and the 0.00001 floor, the value grid (1, 2, 5 steps),
//   the time grid (seconds, minutes, whole frames; acceptance scenario 12), labels and snapping, and
//   hostile input (NaN, infinities, zero-size plots, 1e300 ranges).
// Callers: CTest (curveeditor, fast tier, no GPU).
#include <cmath>
#include <limits>

#include "TestSupport.h"
#include "r1ui/widgets/colorpicker/NumberText.h"
#include "r1ui/widgets/curveeditor/CurveView.h"

namespace {

using namespace r1ui::widgets::curve;

bool near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }

void testWheelFactor() {
  R1_EXPECT(near(wheelZoomFactor(1, 1), 0.9) && near(wheelZoomFactor(-1, 1), 1.0 / 0.9) && near(wheelZoomFactor(2, 1), 0.81));
  R1_EXPECT(near(wheelZoomFactor(1, 2), 0.8) && near(wheelZoomFactor(1, 0.05), 0.995));
  R1_EXPECT(near(wheelZoomFactor(1, 32), 0.1));  // a notch never changes the range by more than 90 %
  R1_EXPECT(near(wheelZoomFactor(1, 1000), 0.1) && near(wheelZoomFactor(1, 0), 0.995) && near(wheelZoomFactor(1, std::numeric_limits<double>::quiet_NaN()), 0.9));
  R1_EXPECT(wheelZoomFactor(std::numeric_limits<double>::quiet_NaN(), 1) == 1.0 && wheelZoomFactor(1e300, 1) >= 0.0 && wheelZoomFactor(-1e300, 1) > 1.0);
}

void testZoomAbout() {
  // Acceptance 1: view 0..10, one notch up with the pointer at the centre.
  const View v{0.0, 10.0, 0.0, 4.0};
  const View z = zoomAbout(v, 5.0, 2.0, 0.9, 0.9);
  R1_EXPECT(near(z.tMin, 0.5) && near(z.tMax, 9.5) && near(z.vMin, 0.2) && near(z.vMax, 3.8));
  // The anchor keeps its relative position.
  const View a = zoomAbout(v, 2.0, 1.0, 0.5, 0.5);
  R1_EXPECT(near((2.0 - a.tMin) / a.timeSpan(), 0.2) && near((1.0 - a.vMin) / a.valueSpan(), 0.25));
  // Anchors outside or NaN fall back inside the view / its centre.
  const View b = zoomAbout(v, 1e9, std::numeric_limits<double>::quiet_NaN(), 0.5, 0.5);
  R1_EXPECT(validView(b) && near(b.tMax, 10.0) && near(b.vMin + b.valueSpan() / 2, 2.0));
  // Bad factors do nothing.
  R1_EXPECT(zoomAbout(v, 5, 2, std::numeric_limits<double>::quiet_NaN(), -3) == v || validView(zoomAbout(v, 5, 2, std::numeric_limits<double>::quiet_NaN(), -3)));
  // Zooming in stops at the floating point limit; zooming out at one billion either way.
  View tiny = v;
  for (int i = 0; i < 400; ++i) tiny = zoomAbout(tiny, 5.0, 2.0, 0.5, 0.5);
  R1_EXPECT(validView(tiny) && tiny.timeSpan() >= kMinSpan && tiny.timeSpan() < 1e-6);
  const View stuck = zoomAbout(tiny, 5.0, 2.0, 0.5, 0.5);
  R1_EXPECT(stuck == tiny || stuck.timeSpan() >= kMinSpan);
  View wide = v;
  for (int i = 0; i < 200; ++i) wide = zoomAbout(wide, 5.0, 2.0, 2.0, 2.0);
  R1_EXPECT(validView(wide) && wide.tMin >= -kMaxCoordinate && wide.tMax <= kMaxCoordinate && wide.vMin >= -kMaxCoordinate && wide.vMax <= kMaxCoordinate);
  // The optional zoom-out limit (10000 units) refuses further zoom out.
  ZoomLimits limits;
  limits.limitTime = limits.limitValue = true;
  View limited = v;
  for (int i = 0; i < 40; ++i) limited = zoomAbout(limited, 5.0, 2.0, 2.0, 2.0, limits);
  R1_EXPECT(limited.timeSpan() <= 2.0 * limits.maxSpan && limited.valueSpan() <= 2.0 * limits.maxSpan);
  R1_EXPECT(zoomAbout(limited, 5, 2, 0.5, 0.5, limits).timeSpan() < limited.timeSpan());  // zooming in still works
}

void testPanAndMapping() {
  const View v{0.0, 10.0, 0.0, 4.0};
  const View p = panBy(v, 50.0, -100.0, 500.0, 400.0);  // content right 50 px, up 100 px
  R1_EXPECT(near(p.tMin, -1.0) && near(p.tMax, 9.0) && near(p.vMin, -1.0) && near(p.vMax, 3.0));
  R1_EXPECT(panBy(v, std::numeric_limits<double>::quiet_NaN(), 0, 500, 400) == v && panBy(v, 1, 1, 0, 400) == v && panBy(v, 1, 1, 500, -3) == v);
  const View far = panBy(v, -1e30, 1e30, 500, 400);
  R1_EXPECT(validView(far) && far.tMax <= kMaxCoordinate && far.vMin >= -kMaxCoordinate);
  Mapping m;
  m.x = 10;
  m.y = 20;
  m.w = 500;
  m.h = 400;
  m.view = v;
  R1_EXPECT(near(m.toX(0), 10) && near(m.toX(10), 510) && near(m.toY(4), 20) && near(m.toY(0), 420));
  R1_EXPECT(near(m.toTime(m.toX(3.3)), 3.3) && near(m.toValue(m.toY(1.7)), 1.7) && near(m.timePerPixel(), 0.02) && near(m.valuePerPixel(), 0.01));
}

void testFitRange() {
  // 50 px of a 500 px panel = 10 % of the range on each side.
  Range r = fitRange(0.0, 10.0, 500.0, 0.0, 99.0);
  R1_EXPECT(near(r.lo, -1.0) && near(r.hi, 11.0));
  r = fitRange(0.0, 10.0, 50.0, 0.0, 99.0);  // never more than half the range
  R1_EXPECT(near(r.lo, -5.0) && near(r.hi, 15.0));
  r = fitRange(0.0, 1e-9, 500.0, kFitMinTimeMargin, 99.0);  // the tiny absolute floor on the time axis
  R1_EXPECT(near(r.lo, -kFitMinTimeMargin) && near(r.hi, 1e-9 + kFitMinTimeMargin));
  r = fitRange(3.0, 3.0, 500.0, 0.0, 8.0);  // one key: keep the zoom scale, centre on the value
  R1_EXPECT(near(r.lo, -1.0) && near(r.hi, 7.0));
  r = fitRange(10.0, 0.0, 500.0, 0.0, 1.0);  // reversed input
  R1_EXPECT(near(r.lo, -1.0) && near(r.hi, 11.0));
  r = fitRange(std::numeric_limits<double>::quiet_NaN(), 1.0, 500.0, 0.0, 5.0);
  R1_EXPECT(std::isfinite(r.lo) && std::isfinite(r.hi) && r.hi > r.lo);
  r = fitRange(0.0, 10.0, 0.0, 0.0, 1.0);  // zero-height plot
  R1_EXPECT(std::isfinite(r.lo) && r.hi > r.lo);
  r = fitRange(3.0, 3.0, 500.0, 0.0, std::numeric_limits<double>::quiet_NaN());
  R1_EXPECT(r.hi > r.lo);
}

void testViewSanitiser() {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  R1_EXPECT(validView(View{}) && !validView(View{1, 1, 0, 1}) && !validView(View{0, 1, 5, 2}) && !validView(View{0, 1, 0, nan}));
  R1_EXPECT(sanitizedView(View{nan, 1, 0, 1}) == View{});
  const View swapped = sanitizedView(View{5, 1, 3, 0});
  R1_EXPECT(swapped.tMin == 1 && swapped.tMax == 5 && swapped.vMin == 0 && swapped.vMax == 3);
  const View thin = sanitizedView(View{1, 1, 0, 1});
  R1_EXPECT(validView(thin) && thin.timeSpan() >= kMinSpan);
  const View huge = sanitizedView(View{-1e300, 1e300, -1e300, 1e300});
  R1_EXPECT(validView(huge) && huge.tMin == -kMaxCoordinate && huge.vMax == kMaxCoordinate);
}

void testNiceStep() {
  R1_EXPECT(near(niceStep(1.3), 1.0) && near(niceStep(1.6), 2.0) && near(niceStep(3.4), 5.0) && near(niceStep(8.0), 10.0) && near(niceStep(0.07), 0.05));
  R1_EXPECT(near(niceStep(450), 500) && near(niceStep(0.2), 0.2));
  R1_EXPECT(niceStep(0) == 1.0 && niceStep(-5) == 1.0 && niceStep(std::numeric_limits<double>::quiet_NaN()) == 1.0 && niceStep(std::numeric_limits<double>::infinity()) == 1.0);
  R1_EXPECT(std::isfinite(niceStep(1e300)) && niceStep(1e-300) > 0.0);
}

void testValueGrid() {
  auto lines = valueGrid(0.0, 10.0, 500.0);
  size_t majors = 0;
  for (const GridLine& l : lines) {
    if (l.major) {
      ++majors;
      R1_EXPECT(!l.label.empty());
    } else {
      R1_EXPECT(l.label.empty());
    }
  }
  R1_EXPECT(majors == 6);  // 0, 2, ..., 10 (one fifth of the height per step)
  R1_EXPECT(lines.size() > majors && near(lines.front().position, 0.5));  // minor lines every step / 4
  std::vector<std::string> labels;
  for (const GridLine& l : valueGrid(0.0, 1.0, 400.0)) {
    if (l.major) labels.push_back(l.label);
  }
  R1_EXPECT(labels == std::vector<std::string>({"0", "0.2", "0.4", "0.6", "0.8", "1"}));
  R1_EXPECT(valueLabel(0.1 + 0.2) == "0.3" && valueLabel(-0.0) == "0" && valueLabel(1234567.1234567) == "1234567.123457" && valueLabel(1e-9) == "0");
  // Minor lines are dropped when they would be closer than 6 px.
  R1_EXPECT(valueGrid(0.0, 10.0, 60.0).size() <= 7);
  // Hostile ranges give an empty or bounded grid, never a hang.
  R1_EXPECT(valueGrid(0, 0, 100).empty() && valueGrid(5, 1, 100).empty() && valueGrid(0, 1, 0).empty());
  R1_EXPECT(valueGrid(std::numeric_limits<double>::quiet_NaN(), 1, 100).empty());
  R1_EXPECT(valueGrid(-1e300, 1e300, 500).size() < kMaxGridLines * 2);
  R1_EXPECT(valueGrid(0, 1e-12, 500).size() < kMaxGridLines * 2);
}

void testTimeGrid() {
  // Seconds: 100 s over 1200 px (12 px per second): target 10 s.
  TimeGrid g = timeGrid(0.0, 100.0, 1200.0, 30.0);
  R1_EXPECT(!g.frameMode && near(g.majorStep, 10.0));
  size_t majors = 0;
  for (const GridLine& l : g.lines) {
    if (l.major) {
      ++majors;
      R1_EXPECT(std::fmod(l.position, 10.0) == 0.0);
    }
  }
  R1_EXPECT(majors == 11);
  // Minutes use m:ss labels and the steps 1, 2, 5, 10, 30, 60 of the unit.
  g = timeGrid(0.0, 3600.0, 1200.0, 30.0);  // 1/3 px per second: target 360 s
  R1_EXPECT(!g.frameMode && g.majorStep == 600.0 && g.lines.size() > 3);
  R1_EXPECT(timeLabel(90, 30, false, 60) == "1:30" && timeLabel(3600, 30, false, 600) == "1:00:00" && timeLabel(-90, 30, false, 60) == "-1:30");
  R1_EXPECT(timeLabel(5, 30, false, 1) == "5s" && timeLabel(0.25, 30, false, 0.25) == "0.25s" && timeLabel(12.0 / 30.0, 30, true, 0.1) == "12f");

  // Acceptance 12: 30 fps, frames 40 px apart: every line on a whole frame, none closer than 30 px.
  g = timeGrid(10.0, 11.0, 1200.0, 30.0);
  R1_EXPECT(g.frameMode && near(g.majorStep, 3.0 / 30.0));
  R1_EXPECT(g.lines.size() >= 30);
  double previous = -1e9;
  bool onFrames = true;
  bool spaced = true;
  for (const GridLine& l : g.lines) {
    const double frame = l.position * 30.0;
    onFrames = onFrames && near(frame, std::round(frame), 1e-6);
    spaced = spaced && (l.position - previous) * 1200.0 >= 30.0 - 1e-6;
    previous = l.position;
    if (l.major) R1_EXPECT(near(std::fmod(std::round(frame), 3.0), 0.0) && l.label == r1ui::widgets::formatNumber(std::round(frame), 0) + "f");
  }
  R1_EXPECT(onFrames && spaced);
  // Major lines never closer than the 120 px target allows and divisors of the frame rate.
  for (const double fps : {24.0, 25.0, 30.0, 60.0, 7.0}) {
    for (const double span : {0.05, 0.2, 0.5, 0.9}) {
      const TimeGrid f = timeGrid(3.0, 3.0 + span, 1000.0, fps);
      if (!f.frameMode) continue;
      const int frames = static_cast<int>(std::lround(f.majorStep * fps));
      R1_EXPECT(frames >= 1 && static_cast<int>(fps) % frames == 0);
      for (const GridLine& l : f.lines) R1_EXPECT(near(l.position * fps, std::round(l.position * fps), 1e-6));
    }
  }
  // Without a frame rate sub-second steps use decimals.
  g = timeGrid(0.0, 1.0, 1200.0, 0.0);
  R1_EXPECT(!g.frameMode && g.majorStep <= 0.2 + 1e-12 && g.majorStep > 0.0);
  // Hostile.
  R1_EXPECT(timeGrid(0, 0, 100, 30).lines.empty() && timeGrid(5, 1, 100, 30).lines.empty() && timeGrid(0, 1, 0, 30).lines.empty());
  R1_EXPECT(timeGrid(std::numeric_limits<double>::quiet_NaN(), 1, 100, 30).lines.empty());
  R1_EXPECT(timeGrid(-1e300, 1e300, 500, 30).lines.size() <= kMaxGridLines + 1);
  R1_EXPECT(timeGrid(0, 1e-9, 500, 30).lines.size() <= kMaxGridLines + 1);
  R1_EXPECT(timeGrid(0, 10, 500, std::numeric_limits<double>::quiet_NaN()).lines.size() > 0);
  R1_EXPECT(timeGrid(0, 0.5, 500, 1e9).lines.size() <= kMaxGridLines + 1);
}

void testSnapping() {
  R1_EXPECT(near(snapToFrame(0.51, 30), 15.0 / 30.0) && near(snapToFrame(-0.02, 30), -1.0 / 30.0) && snapToFrame(0.51, 0) == 0.51);
  R1_EXPECT(near(snapToStep(1.26, 0.25), 1.25) && snapToStep(1.26, 0) == 1.26 && snapToStep(1.26, -1) == 1.26);
  R1_EXPECT(std::isnan(snapToFrame(std::numeric_limits<double>::quiet_NaN(), 30)) && snapToFrame(1e300, 30) == 1e300);
}

}  // namespace

int main() {
  testWheelFactor();
  testZoomAbout();
  testPanAndMapping();
  testFitRange();
  testViewSanitiser();
  testNiceStep();
  testValueGrid();
  testTimeGrid();
  testSnapping();
  return r1test::finish();
}

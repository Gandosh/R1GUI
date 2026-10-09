// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the view maths of the curve editor (spec 11, "Navigating the view" and "Grid and labels"):
//   the visible time and value ranges, the mapping between data and plot pixels, wheel / drag zoom
//   around an anchor with the stated limits, panning, fitting with the 50 px margin, the time grid
//   (seconds, minutes, hours or whole frames) and the value grid (1, 2, 5 times a power of ten), and
//   label text with up to six fractional digits.
// Why: view behaviour has exact numbers in the spec (90 % per notch, 120 / 30 px grid spacing, one
//   billion units, 0.00001 s fitting floor) that deserve unit tests without a window, and the widget
//   must stay finite for any input (NaN anchors, zero-size plots, 1e300 ranges).
// Callers: CurveGraph, tests. Calls: nothing.
// Invariants: a View always has tMin < tMax and vMin < vMax, all within +-kMaxCoordinate, and each
//   span at least kMinSpan and at least 1e-12 times the larger coordinate magnitude (the floating point
//   limit: zooming further in is refused, "simply stops", rule 11 of the edge cases). Functions that
//   cannot produce a valid view return the input unchanged.
#pragma once

#include <string>
#include <vector>

#include "r1ui/widgets/curveeditor/CurveMath.h"

namespace r1ui::widgets::curve {

inline constexpr double kMinSpan = 1.0e-9;
inline constexpr double kWheelFractionPerNotch = 0.10;
inline constexpr double kWheelMaxChange = 0.90;
inline constexpr double kMinWheelMultiplier = 0.05;
inline constexpr double kMaxWheelMultiplier = 32.0;
inline constexpr double kFitMarginPixels = 50.0;
inline constexpr double kFitMinTimeMargin = 0.00001;
inline constexpr double kTimeMajorSpacing = 120.0;  // px
inline constexpr double kTimeMinorSpacing = 30.0;   // px
inline constexpr size_t kMaxGridLines = 2000;

struct View {
  double tMin = -0.5;
  double tMax = 10.5;
  double vMin = -0.5;
  double vMax = 1.5;
  friend bool operator==(const View&, const View&) = default;
  double timeSpan() const { return tMax - tMin; }
  double valueSpan() const { return vMax - vMin; }
};

// Optional zoom-out limits (spec rule 6: off by default, 10000 units when switched on).
struct ZoomLimits {
  bool limitTime = false;
  bool limitValue = false;
  double maxSpan = 10000.0;
};

bool validView(const View& v);
// Repairs a view: swaps reversed ranges, enforces the minimum span around the centre and the
// coordinate limits. Non-finite input yields the default view.
View sanitizedView(const View& v);

// ---- mapping between data and a plot rectangle (physical or logical pixels, y down) ----
struct Mapping {
  double x = 0.0;  // plot rectangle
  double y = 0.0;
  double w = 1.0;
  double h = 1.0;
  View view;
  double toX(double t) const { return x + (t - view.tMin) / view.timeSpan() * w; }
  double toY(double v) const { return y + h - (v - view.vMin) / view.valueSpan() * h; }
  double toTime(double px) const { return view.tMin + (px - x) / w * view.timeSpan(); }
  double toValue(double py) const { return view.vMin + (y + h - py) / h * view.valueSpan(); }
  double timePerPixel() const { return view.timeSpan() / w; }
  double valuePerPixel() const { return view.valueSpan() / h; }
};

// ---- zoom ----
// Zoom factor of the visible range for `notches` wheel notches (positive = zoom in): (1 - p)^notches with
// p = min(0.1 * multiplier, 0.9). The multiplier is clamped to [0.05, 32] (NaN counts as 1).
double wheelZoomFactor(double notches, double multiplier);
// Scales both ranges about the anchor point (the anchor keeps its position in the plot). Factors are
// the new range over the old one. Anchors outside the view or NaN use the view's centre. Returns
// `view` unchanged when the result would violate the limits.
View zoomAbout(const View& view, double anchorT, double anchorV, double factorT, double factorV, const ZoomLimits& limits = {});
// Pans so that content follows the pointer: dxPixels > 0 moves the content right (the view left).
View panBy(const View& view, double dxPixels, double dyPixels, double plotWidth, double plotHeight);

// ---- fitting ----
// The range [lo, hi] padded with the margin of rule 10: 50 px as a fraction of the plot height, at most
// half the range, with `minMargin` as an absolute floor. A zero (or non-finite) range keeps
// `currentSpan` and centres on lo.
struct Range {
  double lo = 0.0;
  double hi = 1.0;
};
Range fitRange(double lo, double hi, double plotHeightPixels, double minMargin, double currentSpan);

// ---- grid ----
struct GridLine {
  double position = 0.0;  // time or value
  bool major = false;
  std::string label;      // major lines only
};
struct TimeGrid {
  std::vector<GridLine> lines;
  bool frameMode = false;       // major lines fall on frames
  double majorStep = 1.0;       // seconds
  double minorStep = 0.0;       // seconds, 0 = none
};
TimeGrid timeGrid(double tMin, double tMax, double plotWidthPixels, double framesPerSecond);
std::vector<GridLine> valueGrid(double vMin, double vMax, double plotHeightPixels);
// The step of 1, 2 or 5 times a power of ten nearest (in ratio) to `target`; 1 for non-positive input.
double niceStep(double target);

// Label text of a value-axis line: up to six fractional digits, trailing zeros dropped.
std::string valueLabel(double value);
// Label of a time line: "12f" (frame number) in frame mode; otherwise seconds ("5s"), minutes
// ("1:30") or hours ("1:00:00") by the step.
std::string timeLabel(double seconds, double framesPerSecond, bool frameMode, double stepSeconds);

// ---- snapping ----
double snapToFrame(double seconds, double framesPerSecond);  // nearest whole frame; unchanged for fps <= 0
double snapToStep(double value, double step);                // nearest multiple; unchanged for step <= 0

}  // namespace r1ui::widgets::curve

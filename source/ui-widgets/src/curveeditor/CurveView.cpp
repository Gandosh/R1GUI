// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CurveView.h.
// Invariants: grid generation iterates over integer line indices (never accumulates floating point
//   steps), caps the number of lines, and returns an empty grid for degenerate input; zoom and pan
//   never return an invalid View.
// Callers: CurveGraph, tests.
#include "r1ui/widgets/curveeditor/CurveView.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "r1ui/widgets/colorpicker/NumberText.h"

namespace r1ui::widgets::curve {

namespace {

bool rangeValid(double lo, double hi) {
  if (!std::isfinite(lo) || !std::isfinite(hi)) return false;
  if (lo < -kMaxCoordinate || hi > kMaxCoordinate || !(lo < hi)) return false;
  const double span = hi - lo;
  return span >= kMinSpan && span >= 1.0e-12 * std::max(std::fabs(lo), std::fabs(hi));
}

double clampFactor(double f) { return std::isfinite(f) && f > 0.0 ? std::clamp(f, 1.0e-9, 1.0e9) : 1.0; }

// Divisors of n in ascending order (n >= 1).
std::vector<int> divisorsOf(int n) {
  std::vector<int> d;
  for (int i = 1; i <= n; ++i) {
    if (n % i == 0) d.push_back(i);
  }
  return d;
}

std::string twoDigits(long long v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%02lld", v);
  return buf;
}

}  // namespace

bool validView(const View& v) { return rangeValid(v.tMin, v.tMax) && rangeValid(v.vMin, v.vMax); }

View sanitizedView(const View& in) {
  View v;
  if (!std::isfinite(in.tMin) || !std::isfinite(in.tMax) || !std::isfinite(in.vMin) || !std::isfinite(in.vMax)) return v;
  const auto fix = [](double lo, double hi, double& outLo, double& outHi) {
    if (lo > hi) std::swap(lo, hi);
    lo = std::clamp(lo, -kMaxCoordinate, kMaxCoordinate);
    hi = std::clamp(hi, -kMaxCoordinate, kMaxCoordinate);
    const double need = std::max(kMinSpan, 1.0e-11 * std::max(std::fabs(lo), std::fabs(hi)));
    if (hi - lo < need) {
      const double c = 0.5 * (lo + hi);
      lo = c - need * 0.5;
      hi = c + need * 0.5;
    }
    outLo = lo;
    outHi = hi;
  };
  fix(in.tMin, in.tMax, v.tMin, v.tMax);
  fix(in.vMin, in.vMax, v.vMin, v.vMax);
  return validView(v) ? v : View{};
}

double wheelZoomFactor(double notches, double multiplier) {
  const double m = std::isnan(multiplier) ? 1.0 : std::clamp(multiplier, kMinWheelMultiplier, kMaxWheelMultiplier);
  const double p = std::min(kWheelFractionPerNotch * m, kWheelMaxChange);
  if (!std::isfinite(notches)) return 1.0;
  return std::pow(1.0 - p, std::clamp(notches, -1000.0, 1000.0));
}

View zoomAbout(const View& view, double anchorT, double anchorV, double factorT, double factorV, const ZoomLimits& limits) {
  if (!validView(view)) return view;
  const double ft = clampFactor(factorT);
  const double fv = clampFactor(factorV);
  const double at = std::isfinite(anchorT) ? std::clamp(anchorT, view.tMin, view.tMax) : 0.5 * (view.tMin + view.tMax);
  const double av = std::isfinite(anchorV) ? std::clamp(anchorV, view.vMin, view.vMax) : 0.5 * (view.vMin + view.vMax);
  View n;
  n.tMin = at - (at - view.tMin) * ft;
  n.tMax = at + (view.tMax - at) * ft;
  n.vMin = av - (av - view.vMin) * fv;
  n.vMax = av + (view.vMax - av) * fv;
  n.tMin = std::max(n.tMin, -kMaxCoordinate);
  n.tMax = std::min(n.tMax, kMaxCoordinate);
  n.vMin = std::max(n.vMin, -kMaxCoordinate);
  n.vMax = std::min(n.vMax, kMaxCoordinate);
  if (!validView(n)) return view;
  if (limits.limitTime && ft > 1.0 && n.timeSpan() > std::max(limits.maxSpan, view.timeSpan())) return view;
  if (limits.limitValue && fv > 1.0 && n.valueSpan() > std::max(limits.maxSpan, view.valueSpan())) return view;
  return n;
}

View panBy(const View& view, double dx, double dy, double plotW, double plotH) {
  if (!validView(view) || !(plotW > 0.0) || !(plotH > 0.0) || !std::isfinite(dx) || !std::isfinite(dy)) return view;
  View n = view;
  const double dt = dx / plotW * view.timeSpan();
  const double dv = dy / plotH * view.valueSpan();
  n.tMin -= dt;
  n.tMax -= dt;
  n.vMin += dv;
  n.vMax += dv;
  // Keep inside the coordinate limits by shifting back (the span is unchanged).
  if (n.tMin < -kMaxCoordinate) { n.tMax += -kMaxCoordinate - n.tMin; n.tMin = -kMaxCoordinate; }
  if (n.tMax > kMaxCoordinate) { n.tMin -= n.tMax - kMaxCoordinate; n.tMax = kMaxCoordinate; }
  if (n.vMin < -kMaxCoordinate) { n.vMax += -kMaxCoordinate - n.vMin; n.vMin = -kMaxCoordinate; }
  if (n.vMax > kMaxCoordinate) { n.vMin -= n.vMax - kMaxCoordinate; n.vMax = kMaxCoordinate; }
  return validView(n) ? n : view;
}

Range fitRange(double lo, double hi, double plotH, double minMargin, double currentSpan) {
  if (!std::isfinite(lo) || !std::isfinite(hi)) return {0.0, std::max(currentSpan, kMinSpan)};
  if (lo > hi) std::swap(lo, hi);
  const double span = std::isfinite(currentSpan) && currentSpan > 0.0 ? currentSpan : 1.0;
  if (!(hi - lo > 0.0)) return {lo - span * 0.5, lo + span * 0.5};
  const double fraction = plotH > 0.0 ? std::min(0.5, kFitMarginPixels / plotH) : 0.5;
  const double margin = std::max(std::isfinite(minMargin) ? minMargin : 0.0, fraction * (hi - lo));
  return {lo - margin, hi + margin};
}

double niceStep(double target) {
  if (!(target > 0.0) || !std::isfinite(target)) return 1.0;
  const double p = std::pow(10.0, std::floor(std::log10(target)));
  double best = p;
  double bestError = 1e300;
  for (const double m : {1.0, 2.0, 5.0, 10.0}) {
    const double c = m * p;
    const double e = std::fabs(std::log(c / target));
    if (e < bestError) {
      bestError = e;
      best = c;
    }
  }
  return best;
}

std::string valueLabel(double value) { return formatNumber(value, 6); }

std::string timeLabel(double seconds, double fps, bool frameMode, double step) {
  if (!std::isfinite(seconds)) return "0";
  if (frameMode && fps > 0.0) return formatNumber(std::round(seconds * fps), 0) + "f";
  if (step >= 60.0) {
    const bool negative = seconds < 0.0;
    const long long total = static_cast<long long>(std::llround(std::fabs(seconds)));
    const long long h = total / 3600;
    const long long m = (total / 60) % 60;
    const long long s = total % 60;
    std::string text = h > 0 ? std::to_string(h) + ":" + twoDigits(m) + ":" + twoDigits(s) : std::to_string(m) + ":" + twoDigits(s);
    return negative ? "-" + text : text;
  }
  return formatNumber(seconds, 3) + "s";
}

std::vector<GridLine> valueGrid(double vMin, double vMax, double plotH) {
  std::vector<GridLine> lines;
  const double range = vMax - vMin;
  if (!(range > 0.0) || !std::isfinite(range) || !(plotH > 0.0)) return lines;
  const double step = niceStep(range / 5.0);
  const double minor = step / 4.0;
  if (!(step > 0.0) || !std::isfinite(step)) return lines;
  const double firstMajor = std::ceil(vMin / step);
  const double lastMajor = std::floor(vMax / step);
  if (!(lastMajor - firstMajor < static_cast<double>(kMaxGridLines))) return lines;
  const bool drawMinor = minor / range * plotH >= 6.0;
  if (drawMinor) {
    const double a = std::ceil(vMin / minor);
    const double b = std::floor(vMax / minor);
    if (b - a < static_cast<double>(kMaxGridLines)) {
      for (double k = a; k <= b; k += 1.0) {
        if (std::fmod(k, 4.0) == 0.0) continue;
        lines.push_back({k * minor, false, {}});
      }
    }
  }
  for (double k = firstMajor; k <= lastMajor; k += 1.0) {
    const double v = k * step;
    lines.push_back({v, true, valueLabel(v)});
  }
  return lines;
}

TimeGrid timeGrid(double tMin, double tMax, double plotW, double fps) {
  TimeGrid grid;
  const double span = tMax - tMin;
  if (!(span > 0.0) || !std::isfinite(span) || !(plotW > 0.0)) return grid;
  const double pxPerSecond = plotW / span;
  const double target = kTimeMajorSpacing / pxPerSecond;
  const int frameRate = std::isfinite(fps) && fps >= 1.0 && fps <= 1000.0 ? static_cast<int>(std::lround(fps)) : 0;

  if (frameRate > 0 && target < 1.0) {
    // Frame mode: lines on whole frames; major steps are divisors of the frame rate.
    grid.frameMode = true;
    const std::vector<int> divisors = divisorsOf(frameRate);
    const double targetFrames = target * frameRate;
    int major = divisors.back();
    for (const int d : divisors) {
      if (d >= targetFrames) {
        major = d;
        break;
      }
    }
    const double pxPerFrame = pxPerSecond / frameRate;
    int minor = 0;
    for (const int d : divisorsOf(major)) {
      if (d < major && d * pxPerFrame >= kTimeMinorSpacing) {
        minor = d;
        break;
      }
    }
    grid.majorStep = static_cast<double>(major) / frameRate;
    grid.minorStep = minor > 0 ? static_cast<double>(minor) / frameRate : 0.0;
    const int lineFrames = minor > 0 ? minor : major;
    const double f0 = std::ceil(tMin * frameRate / lineFrames);
    const double f1 = std::floor(tMax * frameRate / lineFrames);
    if (f1 - f0 >= static_cast<double>(kMaxGridLines)) return grid;
    for (double k = f0; k <= f1; k += 1.0) {
      const double frame = k * lineFrames;
      const bool isMajor = std::fmod(frame, static_cast<double>(major)) == 0.0;
      const double t = frame / frameRate;
      grid.lines.push_back({t, isMajor, isMajor ? timeLabel(t, fps, true, grid.majorStep) : std::string()});
    }
    return grid;
  }

  static const double kSteps[] = {1, 2, 5, 10, 30, 60, 120, 300, 600, 1800, 3600, 7200, 18000, 36000, 86400};
  double step = niceStep(target);  // sub-second steps without a frame rate, or beyond the table
  if (target >= 1.0) {
    step = 0.0;
    for (const double s : kSteps) {
      if (s >= target) {
        step = s;
        break;
      }
    }
    if (step == 0.0) step = niceStep(target);
  }
  grid.majorStep = step;
  int divisions = 1;
  for (const int n : {10, 5, 4, 2}) {
    if (step / n * pxPerSecond >= kTimeMinorSpacing) {
      divisions = n;
      break;
    }
  }
  grid.minorStep = divisions > 1 ? step / divisions : 0.0;
  const double lineStep = step / divisions;
  const double k0 = std::ceil(tMin / lineStep);
  const double k1 = std::floor(tMax / lineStep);
  if (!(k1 - k0 < static_cast<double>(kMaxGridLines))) return grid;
  for (double k = k0; k <= k1; k += 1.0) {
    const bool isMajor = std::fmod(k, static_cast<double>(divisions)) == 0.0;
    const double t = k * lineStep;
    grid.lines.push_back({t, isMajor, isMajor ? timeLabel(t, fps, false, step) : std::string()});
  }
  return grid;
}

double snapToFrame(double seconds, double fps) {
  if (!std::isfinite(seconds) || !(fps > 0.0) || !std::isfinite(fps)) return seconds;
  return std::round(seconds * fps) / fps;
}

double snapToStep(double value, double step) {
  if (!std::isfinite(value) || !(step > 0.0) || !std::isfinite(step)) return value;
  return std::round(value / step) * step;
}

}  // namespace r1ui::widgets::curve

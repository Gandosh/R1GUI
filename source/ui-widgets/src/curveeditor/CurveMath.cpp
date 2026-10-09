// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CurveMath.h.
// Invariants: every function accepts any double; results are finite (evaluation is clamped to
//   +-1e18); the Bezier time solve brackets the root in [0, 1] and falls back to bisection, so it
//   terminates for every input; flattening is bounded by depth and point count.
// Callers: CurveOps, CurveGraph, tests.
#include "r1ui/widgets/curveeditor/CurveMath.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace r1ui::widgets::curve {

namespace {

constexpr double kMaxValue = 1.0e18;

double clampSlope(double s) {
  if (std::isnan(s)) return 0.0;
  return std::clamp(s, -kMaxSlope, kMaxSlope);
}

double clampWeight(double w) {
  if (std::isnan(w)) return kDefaultWeight;
  return std::clamp(w, kMinWeight, 1.0);
}

double cubic(double a, double b, double c, double d, double u) {
  const double v = 1.0 - u;
  return v * v * v * a + 3.0 * v * v * u * b + 3.0 * v * u * u * c + u * u * u * d;
}

double cubicDerivative(double a, double b, double c, double d, double u) {
  const double v = 1.0 - u;
  return 3.0 * (v * v * (b - a) + 2.0 * v * u * (c - b) + u * u * (d - c));
}

// Parameter u in [0, 1] with x(u) == t for a Bezier whose time coordinate is monotone.
double solveParameter(const Bezier& b, double t) {
  const double span = b.x[3] - b.x[0];
  if (!(span > 0.0)) return 0.0;
  if (t <= b.x[0]) return 0.0;
  if (t >= b.x[3]) return 1.0;
  double lo = 0.0;
  double hi = 1.0;
  double u = (t - b.x[0]) / span;
  for (int i = 0; i < 64; ++i) {
    const double f = cubic(b.x[0], b.x[1], b.x[2], b.x[3], u) - t;
    if (std::fabs(f) <= 1e-13 * std::max(1.0, std::fabs(span))) break;
    if (f > 0.0) hi = u;
    else lo = u;
    const double d = cubicDerivative(b.x[0], b.x[1], b.x[2], b.x[3], u);
    double next = d != 0.0 ? u - f / d : -1.0;
    if (!(next > lo && next < hi)) next = 0.5 * (lo + hi);
    u = next;
  }
  return std::clamp(u, 0.0, 1.0);
}

// Value inside [first, last] time range.
double evaluateInside(const Curve& c, double t) {
  const size_t i = segmentIndex(c, t);
  if (i == npos) return c.keys.front().value;
  const Key& k0 = c.keys[i];
  if (i + 1 >= c.keys.size() || t <= k0.time) return k0.value;
  const Key& k1 = c.keys[i + 1];
  switch (k0.interp) {
    case Interp::Constant: return k0.value;
    case Interp::Linear: {
      const double s = (t - k0.time) / (k1.time - k0.time);
      return k0.value + (k1.value - k0.value) * s;
    }
    case Interp::Cubic: {
      Bezier b;
      if (!segmentBezier(c, i, b)) return k0.value;
      const double u = solveParameter(b, t);
      return cubic(b.y[0], b.y[1], b.y[2], b.y[3], u);
    }
  }
  return k0.value;
}

}  // namespace

double clampCoordinate(double v) {
  if (std::isnan(v)) return 0.0;
  return std::clamp(v, -kMaxCoordinate, kMaxCoordinate);
}

bool sanitize(Curve& curve) {
  const Curve before = curve;
  std::vector<Key>& keys = curve.keys;
  keys.erase(std::remove_if(keys.begin(), keys.end(), [](const Key& k) { return !std::isfinite(k.time) || !std::isfinite(k.value); }), keys.end());
  for (Key& k : keys) {
    k.time = std::clamp(k.time, -kMaxCoordinate, kMaxCoordinate);
    k.value = std::clamp(k.value, -kMaxCoordinate, kMaxCoordinate);
    k.inSlope = clampSlope(k.inSlope);
    k.outSlope = clampSlope(k.outSlope);
    k.inWeight = clampWeight(k.inWeight);
    k.outWeight = clampWeight(k.outWeight);
  }
  std::stable_sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return a.time < b.time; });
  // Equal times: the later key of the original order (the stable sort keeps it last) wins.
  std::vector<Key> unique;
  unique.reserve(keys.size());
  for (const Key& k : keys) {
    if (!unique.empty() && unique.back().time == k.time) unique.back() = k;
    else unique.push_back(k);
  }
  if (unique.size() > kMaxKeysPerCurve) unique.resize(kMaxKeysPerCurve);
  keys = std::move(unique);
  uint32_t next = 1;
  for (const Key& k : keys) next = std::max(next, k.id == UINT32_MAX ? k.id : k.id + 1);
  std::unordered_set<uint32_t> seen;
  seen.reserve(keys.size());
  for (Key& k : keys) {
    if (k.id == 0 || !seen.insert(k.id).second) {
      while (seen.count(next) != 0 || next == 0) ++next;
      k.id = next;
      seen.insert(next);
    }
  }
  return !(curve == before);
}

bool valid(const Curve& c) {
  std::unordered_set<uint32_t> seen;
  for (size_t i = 0; i < c.keys.size(); ++i) {
    const Key& k = c.keys[i];
    if (!std::isfinite(k.time) || !std::isfinite(k.value) || std::fabs(k.time) > kMaxCoordinate || std::fabs(k.value) > kMaxCoordinate) return false;
    if (i > 0 && !(k.time > c.keys[i - 1].time)) return false;
    if (k.id == 0 || !seen.insert(k.id).second) return false;
    if (!(k.inWeight >= kMinWeight && k.inWeight <= 1.0 && k.outWeight >= kMinWeight && k.outWeight <= 1.0)) return false;
    if (!std::isfinite(k.inSlope) || !std::isfinite(k.outSlope) || std::fabs(k.inSlope) > kMaxSlope || std::fabs(k.outSlope) > kMaxSlope) return false;
  }
  return c.keys.size() <= kMaxKeysPerCurve;
}

size_t segmentIndex(const Curve& c, double t) {
  if (c.keys.empty() || std::isnan(t) || t < c.keys.front().time) return npos;
  const auto it = std::upper_bound(c.keys.begin(), c.keys.end(), t, [](double v, const Key& k) { return v < k.time; });
  return static_cast<size_t>(it - c.keys.begin()) - 1;
}

size_t indexOfKey(const Curve& c, uint32_t id) {
  for (size_t i = 0; i < c.keys.size(); ++i) {
    if (c.keys[i].id == id) return i;
  }
  return npos;
}

Slopes effectiveSlopes(const Curve& c, size_t i) {
  if (i >= c.keys.size()) return {};
  const Key& k = c.keys[i];
  const bool hasPrev = i > 0;
  const bool hasNext = i + 1 < c.keys.size();
  const auto secant = [&](size_t a, size_t b) {
    const double dt = c.keys[b].time - c.keys[a].time;
    return dt > 0.0 ? (c.keys[b].value - c.keys[a].value) / dt : 0.0;
  };
  switch (k.mode) {
    case TangentMode::Linked: return {clampSlope(k.outSlope), clampSlope(k.outSlope)};
    case TangentMode::Independent: return {clampSlope(k.inSlope), clampSlope(k.outSlope)};
    case TangentMode::AutoAverage: {
      double s = 0.0;
      if (hasPrev && hasNext) s = secant(i - 1, i + 1);
      else if (hasNext) s = secant(i, i + 1);
      else if (hasPrev) s = secant(i - 1, i);
      return {clampSlope(s), clampSlope(s)};
    }
    case TangentMode::AutoSmooth: {
      double s = 0.0;
      if (hasPrev && hasNext) {
        const double dPrev = secant(i - 1, i);
        const double dNext = secant(i, i + 1);
        if (dPrev * dNext > 0.0) {
          const double h0 = k.time - c.keys[i - 1].time;
          const double h1 = c.keys[i + 1].time - k.time;
          const double w1 = 2.0 * h1 + h0;
          const double w2 = h1 + 2.0 * h0;
          s = (w1 + w2) / (w1 / dPrev + w2 / dNext);
        }
      } else if (hasNext) {
        s = secant(i, i + 1);
      } else if (hasPrev) {
        s = secant(i - 1, i);
      }
      return {clampSlope(s), clampSlope(s)};
    }
  }
  return {};
}

bool segmentBezier(const Curve& c, size_t i, Bezier& out) {
  if (i + 1 >= c.keys.size() || c.keys[i].interp != Interp::Cubic) return false;
  const Key& k0 = c.keys[i];
  const Key& k1 = c.keys[i + 1];
  const double dt = k1.time - k0.time;
  const double wOut = k0.weighted ? clampWeight(k0.outWeight) : kDefaultWeight;
  const double wIn = k1.weighted ? clampWeight(k1.inWeight) : kDefaultWeight;
  const double m0 = effectiveSlopes(c, i).out;
  const double m1 = effectiveSlopes(c, i + 1).in;
  out.x[0] = k0.time;
  out.x[1] = k0.time + wOut * dt;
  out.x[2] = k1.time - wIn * dt;
  out.x[3] = k1.time;
  out.y[0] = k0.value;
  out.y[1] = k0.value + m0 * wOut * dt;
  out.y[2] = k1.value - m1 * wIn * dt;
  out.y[3] = k1.value;
  return true;
}

double evaluate(const Curve& c, double t) {
  const size_t n = c.keys.size();
  if (n == 0) return 0.0;
  if (std::isnan(t)) return c.keys.front().value;
  const Key& first = c.keys.front();
  const Key& last = c.keys.back();
  const double span = last.time - first.time;
  double result;
  if (n == 1 || !(span > 0.0) || (t >= first.time && t <= last.time)) {
    result = (n == 1 || !(span > 0.0)) ? first.value : evaluateInside(c, t);
  } else {
    const bool before = t < first.time;
    const Extrapolation mode = before ? c.pre : c.post;
    switch (mode) {
      case Extrapolation::Constant: result = before ? first.value : last.value; break;
      case Extrapolation::Linear:
        result = before ? first.value + effectiveSlopes(c, 0).in * (t - first.time) : last.value + effectiveSlopes(c, n - 1).out * (t - last.time);
        break;
      case Extrapolation::Repeat:
      case Extrapolation::RepeatOffset:
      case Extrapolation::PingPong: {
        const double u = (t - first.time) / span;
        const double cycles = std::floor(u);
        double frac = u - cycles;
        if (mode == Extrapolation::PingPong && std::fmod(cycles, 2.0) != 0.0) frac = 1.0 - frac;
        const double inside = std::clamp(first.time + frac * span, first.time, last.time);
        result = evaluateInside(c, inside);
        if (mode == Extrapolation::RepeatOffset) result += cycles * (last.value - first.value);
        break;
      }
      default: result = first.value; break;
    }
  }
  if (std::isnan(result)) return 0.0;
  return std::clamp(result, -kMaxValue, kMaxValue);
}

double slopeAt(const Curve& c, double t) {
  const size_t n = c.keys.size();
  if (n < 2 || std::isnan(t)) return 0.0;
  if (t < c.keys.front().time) return c.pre == Extrapolation::Linear ? effectiveSlopes(c, 0).in : 0.0;
  if (t >= c.keys.back().time) return c.post == Extrapolation::Linear ? effectiveSlopes(c, n - 1).out : 0.0;
  const size_t i = segmentIndex(c, t);
  const Key& k0 = c.keys[i];
  const Key& k1 = c.keys[i + 1];
  switch (k0.interp) {
    case Interp::Constant: return 0.0;
    case Interp::Linear: return (k1.value - k0.value) / (k1.time - k0.time);
    case Interp::Cubic: {
      Bezier b;
      if (!segmentBezier(c, i, b)) return 0.0;
      const double u = solveParameter(b, t);
      const double dx = cubicDerivative(b.x[0], b.x[1], b.x[2], b.x[3], u);
      const double dy = cubicDerivative(b.y[0], b.y[1], b.y[2], b.y[3], u);
      return dx > 0.0 ? clampSlope(dy / dx) : 0.0;
    }
  }
  return 0.0;
}

namespace {

double distanceToChord(const Point& p, const Point& a, const Point& b) {
  const double dx = b.x - a.x;
  const double dy = b.y - a.y;
  const double len2 = dx * dx + dy * dy;
  if (len2 < 1e-18) return std::hypot(p.x - a.x, p.y - a.y);
  return std::fabs((p.x - a.x) * dy - (p.y - a.y) * dx) / std::sqrt(len2);
}

void flattenRecursive(const Bezier& b, double tol, int depth, std::vector<Point>& out, size_t limit) {
  if (out.size() >= limit) return;  // the point budget is spent; the caller closes the polyline
  const Point p0{b.x[0], b.y[0]};
  const Point p1{b.x[1], b.y[1]};
  const Point p2{b.x[2], b.y[2]};
  const Point p3{b.x[3], b.y[3]};
  // The curve stays within 3/4 of the larger control point distance from the chord.
  const double d = std::max(distanceToChord(p1, p0, p3), distanceToChord(p2, p0, p3));
  if (depth >= 24 || 0.75 * d <= tol || out.size() + 1 >= limit) {
    out.push_back(p3);
    return;
  }
  // de Casteljau split at u = 1/2.
  const auto mid = [](double a, double c) { return 0.5 * (a + c); };
  Bezier l, r;
  const double x01 = mid(b.x[0], b.x[1]), y01 = mid(b.y[0], b.y[1]);
  const double x12 = mid(b.x[1], b.x[2]), y12 = mid(b.y[1], b.y[2]);
  const double x23 = mid(b.x[2], b.x[3]), y23 = mid(b.y[2], b.y[3]);
  const double x012 = mid(x01, x12), y012 = mid(y01, y12);
  const double x123 = mid(x12, x23), y123 = mid(y12, y23);
  const double xm = mid(x012, x123), ym = mid(y012, y123);
  l.x[0] = b.x[0]; l.y[0] = b.y[0]; l.x[1] = x01; l.y[1] = y01; l.x[2] = x012; l.y[2] = y012; l.x[3] = xm; l.y[3] = ym;
  r.x[0] = xm; r.y[0] = ym; r.x[1] = x123; r.y[1] = y123; r.x[2] = x23; r.y[2] = y23; r.x[3] = b.x[3]; r.y[3] = b.y[3];
  flattenRecursive(l, tol, depth + 1, out, limit);
  flattenRecursive(r, tol, depth + 1, out, limit);
}

}  // namespace

void flattenCubic(const Bezier& bezier, double tolerance, std::vector<Point>& out, size_t maxPoints) {
  for (int i = 0; i < 4; ++i) {
    if (!std::isfinite(bezier.x[i]) || !std::isfinite(bezier.y[i])) {
      out.push_back({bezier.x[3], bezier.y[3]});  // hostile control points: a straight segment
      return;
    }
  }
  const double tol = std::isfinite(tolerance) && tolerance > 1e-6 ? tolerance : 1e-6;
  flattenRecursive(bezier, tol, 0, out, out.size() + std::max<size_t>(maxPoints, 2));
  if (out.empty() || out.back().x != bezier.x[3] || out.back().y != bezier.y[3]) out.push_back({bezier.x[3], bezier.y[3]});
}

}  // namespace r1ui::widgets::curve

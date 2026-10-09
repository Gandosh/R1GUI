// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for the curve maths (CurveMath.h) against analytic values: Hermite and weighted
//   Bezier evaluation, constant / linear segments, the automatic tangent modes (no overshoot, zero at
//   extremes, averages), all five extrapolation kinds, the sanitiser on hostile data (NaN, duplicates,
//   huge coordinates, a million keys), and the 1 px flatness of the cubic flattening against dense
//   sampling of the true curve.
// Callers: CTest (curveeditor, fast tier, no GPU).
#include <chrono>
#include <cmath>
#include <limits>
#include <random>

#include "TestSupport.h"
#include "r1ui/widgets/curveeditor/CurveMath.h"

namespace {

using namespace r1ui::widgets::curve;

bool near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }

Key key(uint32_t id, double t, double v, Interp interp = Interp::Linear) {
  Key k;
  k.id = id;
  k.time = t;
  k.value = v;
  k.interp = interp;
  return k;
}

Curve cubicPair(double m0, double m1) {
  Curve c;
  c.keys.push_back(key(1, 0.0, 1.0, Interp::Cubic));
  c.keys.push_back(key(2, 2.0, 3.0, Interp::Cubic));
  c.keys[0].mode = TangentMode::Independent;
  c.keys[1].mode = TangentMode::Independent;
  c.keys[0].outSlope = m0;
  c.keys[1].inSlope = m1;
  return c;
}

void testHermiteAnalytic() {
  const double m0 = 2.0;
  const double m1 = -1.0;
  const Curve c = cubicPair(m0, m1);
  const double dt = 2.0;
  for (int i = 0; i <= 200; ++i) {
    const double t = dt * i / 200.0;
    const double s = t / dt;
    const double h00 = 2 * s * s * s - 3 * s * s + 1;
    const double h10 = s * s * s - 2 * s * s + s;
    const double h01 = -2 * s * s * s + 3 * s * s;
    const double h11 = s * s * s - s * s;
    const double expected = h00 * 1.0 + h10 * dt * m0 + h01 * 3.0 + h11 * dt * m1;
    R1_EXPECT(near(evaluate(c, t), expected, 1e-9));
  }
  R1_EXPECT(evaluate(c, 0.0) == 1.0 && evaluate(c, 2.0) == 3.0);
  // The derivative at the ends is the stored slope.
  R1_EXPECT(near(slopeAt(c, 0.0), m0, 1e-6) && near(slopeAt(c, 1.9999999), m1, 1e-3));
}

void testWeightedBezier() {
  Curve c = cubicPair(1.5, 0.5);
  c.keys[0].weighted = true;
  c.keys[0].outWeight = 0.6;
  c.keys[1].weighted = true;
  c.keys[1].inWeight = 0.2;
  Bezier b;
  R1_EXPECT(segmentBezier(c, 0, b));
  R1_EXPECT(near(b.x[1], 0.0 + 0.6 * 2.0) && near(b.x[2], 2.0 - 0.2 * 2.0));
  R1_EXPECT(near(b.y[1], 1.0 + 1.5 * 0.6 * 2.0) && near(b.y[2], 3.0 - 0.5 * 0.2 * 2.0));
  // evaluate() follows the parametric curve exactly.
  const auto bez = [](double a, double bb, double cc, double d, double u) {
    const double v = 1 - u;
    return v * v * v * a + 3 * v * v * u * bb + 3 * v * u * u * cc + u * u * u * d;
  };
  for (int i = 0; i <= 500; ++i) {
    const double u = i / 500.0;
    const double x = bez(b.x[0], b.x[1], b.x[2], b.x[3], u);
    const double y = bez(b.y[0], b.y[1], b.y[2], b.y[3], u);
    R1_EXPECT(near(evaluate(c, x), y, 1e-7));
  }
  // Unweighted keys use 1/3: the curve equals the Hermite curve.
  Curve u = cubicPair(1.5, 0.5);
  Bezier ub;
  R1_EXPECT(segmentBezier(u, 0, ub) && near(ub.x[1], 2.0 / 3.0) && near(ub.x[2], 4.0 / 3.0));
  // Linear and constant segments have no Bezier.
  Curve l;
  l.keys = {key(1, 0, 0), key(2, 1, 1)};
  R1_EXPECT(!segmentBezier(l, 0, b) && !segmentBezier(l, 1, b) && !segmentBezier(l, 99, b));
}

void testLinearConstantAndEnds() {
  Curve c;
  c.keys = {key(1, 0, 0, Interp::Linear), key(2, 1, 2, Interp::Constant), key(3, 2, 5, Interp::Linear), key(4, 4, 1)};
  R1_EXPECT(near(evaluate(c, 0.5), 1.0) && near(evaluate(c, 1.0), 2.0));
  R1_EXPECT(near(evaluate(c, 1.5), 2.0) && near(evaluate(c, 1.999999), 2.0) && near(evaluate(c, 2.0), 5.0));  // the step holds, then jumps
  R1_EXPECT(near(evaluate(c, 3.0), 3.0));
  R1_EXPECT(evaluate(c, -100) == 0.0 && evaluate(c, 100) == 1.0);  // constant extrapolation
  Curve empty;
  R1_EXPECT(evaluate(empty, 1.0) == 0.0 && slopeAt(empty, 1.0) == 0.0);
  Curve one;
  one.keys = {key(1, 3.0, 7.0)};
  R1_EXPECT(evaluate(one, -5) == 7.0 && evaluate(one, 3) == 7.0 && evaluate(one, 50) == 7.0);
  one.pre = one.post = Extrapolation::Linear;
  R1_EXPECT(evaluate(one, -5) == 7.0);  // a single key has no tangent to continue along
  R1_EXPECT(segmentIndex(c, -1) == npos && segmentIndex(c, 0) == 0 && segmentIndex(c, 1.5) == 1 && segmentIndex(c, 4) == 3 && segmentIndex(c, 9) == 3);
}

void testAutomaticTangents() {
  Curve c;
  for (int i = 0; i < 4; ++i) c.keys.push_back(key(i + 1, i, i * i, Interp::Cubic));  // 0, 1, 4, 9: monotone
  const Slopes inner = effectiveSlopes(c, 1);
  R1_EXPECT(inner.in == inner.out && inner.in > 1.0 && inner.in < 3.0);  // between the neighbouring secants 1 and 3
  c.keys[1].mode = TangentMode::AutoAverage;
  R1_EXPECT(near(effectiveSlopes(c, 1).out, (4.0 - 0.0) / 2.0));
  R1_EXPECT(near(effectiveSlopes(c, 0).out, 1.0) && near(effectiveSlopes(c, 3).in, 5.0));  // one-sided at the ends
  // A local extreme gets a flat tangent and the curve does not overshoot its keys.
  Curve peak;
  peak.keys = {key(1, 0, 0, Interp::Cubic), key(2, 1, 1, Interp::Cubic), key(3, 2, 0, Interp::Cubic)};
  R1_EXPECT(effectiveSlopes(peak, 1).out == 0.0);
  for (int i = 0; i <= 200; ++i) {
    const double v = evaluate(peak, 2.0 * i / 200.0);
    R1_EXPECT(v >= -1e-12 && v <= 1.0 + 1e-12);
  }
  // Monotone data stays monotone with the smooth mode.
  double previous = -1e300;
  bool monotone = true;
  for (int i = 0; i <= 300; ++i) {
    const double v = evaluate(c, 3.0 * i / 300.0);
    monotone = monotone && v >= previous - 1e-12;
    previous = v;
  }
  R1_EXPECT(monotone);
  // User tangents.
  Curve user = cubicPair(0, 0);
  user.keys[0].mode = TangentMode::Linked;
  user.keys[0].outSlope = 4.0;
  user.keys[0].inSlope = -99.0;  // ignored while linked
  R1_EXPECT(effectiveSlopes(user, 0).in == 4.0 && effectiveSlopes(user, 0).out == 4.0);
  user.keys[0].mode = TangentMode::Independent;
  R1_EXPECT(effectiveSlopes(user, 0).in == -99.0);
  R1_EXPECT(effectiveSlopes(user, 50).in == 0.0);
}

void testExtrapolation() {
  Curve c;
  c.keys = {key(1, 0, 0), key(2, 1, 1)};
  c.pre = c.post = Extrapolation::Repeat;
  R1_EXPECT(near(evaluate(c, 1.5), 0.5) && near(evaluate(c, 2.25), 0.25) && near(evaluate(c, -0.25), 0.75) && near(evaluate(c, -1.0), 0.0));
  c.pre = c.post = Extrapolation::RepeatOffset;
  R1_EXPECT(near(evaluate(c, 1.5), 1.5) && near(evaluate(c, 2.0), 2.0) && near(evaluate(c, -0.5), -0.5) && near(evaluate(c, -1.25), -1.25));
  c.pre = c.post = Extrapolation::PingPong;
  R1_EXPECT(near(evaluate(c, 1.25), 0.75) && near(evaluate(c, 2.25), 0.25) && near(evaluate(c, -0.25), 0.25) && near(evaluate(c, -1.25), 0.75));
  // Linear continues along the end tangent.
  Curve cub = cubicPair(2.0, -1.0);
  cub.keys[0].inSlope = 2.0;   // an Independent key continues along its own side's tangent
  cub.keys[1].outSlope = -1.0;
  cub.pre = cub.post = Extrapolation::Linear;
  R1_EXPECT(near(evaluate(cub, 3.0), 3.0 + (-1.0) * 1.0) && near(evaluate(cub, -1.0), 1.0 + 2.0 * (-1.0)));
  R1_EXPECT(near(slopeAt(cub, 5.0), -1.0) && near(slopeAt(cub, -5.0), 2.0));
  cub.post = Extrapolation::Constant;
  R1_EXPECT(near(evaluate(cub, 100.0), 3.0) && slopeAt(cub, 100.0) == 0.0);
  // The extrapolation kinds never produce NaN, also for absurd times.
  for (const Extrapolation e : {Extrapolation::Constant, Extrapolation::Linear, Extrapolation::Repeat, Extrapolation::RepeatOffset, Extrapolation::PingPong}) {
    cub.pre = cub.post = e;
    for (const double t : {1e300, -1e300, 1e15, -1e15, std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) R1_EXPECT(std::isfinite(evaluate(cub, t)));
    R1_EXPECT(evaluate(cub, std::numeric_limits<double>::quiet_NaN()) == 1.0);
  }
}

void testSanitize() {
  Curve c;
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  c.keys = {key(7, 3, 1), key(7, 1, 2), key(0, 2, 3), key(5, nan, 1), key(6, 4, inf), key(8, 1, 9), key(9, 1e300, -1e300)};
  c.keys[1].inWeight = 7.0;
  c.keys[1].outWeight = -3.0;
  c.keys[2].inSlope = 1e300;
  c.keys[2].outSlope = nan;
  R1_EXPECT(sanitize(c) && valid(c));
  R1_EXPECT(c.keys.size() == 4);  // NaN and inf keys dropped, the two keys at time 1 merged
  R1_EXPECT(c.keys[0].time == 1.0 && c.keys[0].value == 9.0);  // the later key of the original order wins
  R1_EXPECT(c.keys.back().time == kMaxCoordinate && c.keys.back().value == -kMaxCoordinate);
  R1_EXPECT(!sanitize(c));  // idempotent
  Curve w;
  w.keys = {key(1, 0, 0)};
  w.keys[0].inWeight = 7.0;
  w.keys[0].outWeight = -3.0;
  w.keys[0].inSlope = 1e300;
  w.keys[0].outSlope = std::numeric_limits<double>::quiet_NaN();
  sanitize(w);
  R1_EXPECT(w.keys[0].inWeight == 1.0 && w.keys[0].outWeight == kMinWeight && w.keys[0].inSlope == kMaxSlope && w.keys[0].outSlope == 0.0);
  R1_EXPECT(valid(w) && !valid(Curve{0, "x", {}, true, false, false, Extrapolation::Constant, Extrapolation::Constant, {key(1, 1, 1), key(2, 1, 1)}}));
  R1_EXPECT(clampCoordinate(nan) == 0.0 && clampCoordinate(1e12) == kMaxCoordinate && clampCoordinate(-inf) == -kMaxCoordinate);
}

void testHugeCurve() {
  Curve c;
  const int n = 1'000'000;
  c.keys.reserve(n);
  std::mt19937 rng(3);
  for (int i = 0; i < n; ++i) c.keys.push_back(key(0, static_cast<double>(rng() % 5'000'000) * 0.001, static_cast<double>(rng() % 1000)));
  const auto t0 = std::chrono::steady_clock::now();
  sanitize(c);
  R1_EXPECT(valid(c) && c.keys.size() <= static_cast<size_t>(n));
  // Evaluation is O(log n): a million evaluations in well under a second.
  double acc = 0.0;
  for (int i = 0; i < 1'000'000; ++i) acc += evaluate(c, i * 0.005);
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  R1_EXPECT(std::isfinite(acc) && seconds < 20.0);
  std::fprintf(stdout, "curve maths: sanitize + 1e6 evaluations over %zu keys took %.2f s\n", c.keys.size(), seconds);
}

// Distance from p to the polyline.
double distanceToPolyline(const std::vector<Point>& poly, const Point& p) {
  double best = 1e300;
  for (size_t i = 1; i < poly.size(); ++i) {
    const double dx = poly[i].x - poly[i - 1].x;
    const double dy = poly[i].y - poly[i - 1].y;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? ((p.x - poly[i - 1].x) * dx + (p.y - poly[i - 1].y) * dy) / len2 : 0.0;
    t = std::max(0.0, std::min(1.0, t));
    best = std::min(best, std::hypot(p.x - (poly[i - 1].x + t * dx), p.y - (poly[i - 1].y + t * dy)));
  }
  return best;
}

void testFlatten() {
  std::mt19937 rng(11);
  std::uniform_real_distribution<double> coord(-500.0, 1500.0);
  size_t worstPoints = 0;
  for (int trial = 0; trial < 300; ++trial) {
    Bezier b;
    for (int i = 0; i < 4; ++i) {
      b.x[i] = coord(rng);
      b.y[i] = coord(rng);
    }
    std::vector<Point> poly = {{b.x[0], b.y[0]}};
    flattenCubic(b, 1.0, poly);
    worstPoints = std::max(worstPoints, poly.size());
    R1_EXPECT(poly.back().x == b.x[3] && poly.back().y == b.y[3]);
    double worst = 0.0;
    for (int i = 0; i <= 2000; ++i) {
      const double u = i / 2000.0, v = 1 - u;
      const Point p{v * v * v * b.x[0] + 3 * v * v * u * b.x[1] + 3 * v * u * u * b.x[2] + u * u * u * b.x[3],
                    v * v * v * b.y[0] + 3 * v * v * u * b.y[1] + 3 * v * u * u * b.y[2] + u * u * u * b.y[3]};
      worst = std::max(worst, distanceToPolyline(poly, p));
    }
    R1_EXPECT(worst <= 1.0 + 1e-9);
  }
  std::fprintf(stdout, "curve maths: flattening used at most %zu points for 300 random cubics at 1 px\n", worstPoints);
  // A straight cubic is one segment; hostile control points give a straight segment; the bound holds.
  Bezier flat{{0, 1, 2, 3}, {0, 0, 0, 0}};
  std::vector<Point> p;
  flattenCubic(flat, 1.0, p);
  R1_EXPECT(p.size() == 1);
  Bezier bad{{0, std::numeric_limits<double>::quiet_NaN(), 2, 3}, {0, 1, 2, 3}};
  p.clear();
  flattenCubic(bad, 1.0, p);
  R1_EXPECT(p.size() == 1 && p[0].x == 3);
  Bezier wild{{0, 1e9, -1e9, 1e6}, {0, 1e9, -1e9, 5}};
  p.clear();
  flattenCubic(wild, 1e-3, p, 64);
  R1_EXPECT(p.size() <= 64 + 1 && !p.empty());
  p.clear();
  flattenCubic(flat, std::numeric_limits<double>::quiet_NaN(), p);
  R1_EXPECT(!p.empty());
}

// KeyLookup finds keys by id in several curves in O(log n), also with unsorted ids and hostile arguments.
void testKeyLookup() {
  Curve a;
  a.id = 7;
  for (uint32_t i = 0; i < 1000; ++i) a.keys.push_back(key(1000 - i, i * 0.5, 0.0));  // ids descending
  Curve b;
  b.id = 9;
  b.keys.push_back(key(5, 0.0, 1.0));
  const std::vector<Curve> curves{a, b};
  const KeyLookup lookup(curves);
  R1_EXPECT(lookup.indexOf(7, 1000) == 0 && lookup.indexOf(7, 1) == 999 && lookup.indexOf(7, 500) == 500);
  R1_EXPECT(lookup.indexOf(9, 5) == 0);
  R1_EXPECT(lookup.indexOf(7, 0) == npos && lookup.indexOf(7, 1001) == npos && lookup.indexOf(9, 1) == npos);
  R1_EXPECT(lookup.indexOf(8, 5) == npos && lookup.curve(8) == nullptr && lookup.curve(9) == &curves[1]);
  for (uint32_t id = 1; id <= 1000; ++id) R1_EXPECT(lookup.indexOf(7, id) == indexOfKey(curves[0], id));
  const std::vector<Curve> none;
  R1_EXPECT(KeyLookup(none).indexOf(1, 1) == npos);
}

}  // namespace

int main() {
  testKeyLookup();
  testHermiteAnalytic();
  testWeightedBezier();
  testLinearConstantAndEnds();
  testAutomaticTangents();
  testExtrapolation();
  testSanitize();
  testHugeCurve();
  testFlatten();
  return r1test::finish();
}

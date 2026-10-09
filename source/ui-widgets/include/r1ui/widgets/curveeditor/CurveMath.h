// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the data model and the maths of animation curves for the curve editor: keys with time, value,
//   interpolation toward the next key and tangent data, the curve (sorted keys, extrapolation on both
//   ends, colour and flags), evaluation at any time, effective tangent slopes of the automatic
//   modes, the Bezier form of a cubic segment, and the flattening of a cubic into a polyline whose
//   deviation from the true curve is bounded.
// Why: the editor draws, hit-tests and edits the same curves; one tested pure module defines what a
//   curve IS (spec 11: constant, linear and cubic interpolation, smooth / average / user / split
//   tangents, optionally weighted, five extrapolation kinds) so the widget never disagrees with
//   itself and hostile data (NaN, 1e300, 1e6 keys, equal times) is rejected at one place.
// Callers: CurveOps, CurveGraph, CurveKeyFields, host applications that store curves, tests.
//   Calls: nothing (pure maths; the colour is a plain RGB triple).
// Invariants (enforced by sanitize()): keys are sorted by strictly increasing time, every time and
//   value is finite and within +-kMaxCoordinate, weights are within [kMinWeight, 1], slopes are finite
//   and within +-kMaxSlope; key ids are unique within a curve and non-zero. A curve may be empty.
// Evaluation: before the first and after the last key the extrapolation applies; between keys i and
//   i+1 the interpolation of key i decides: Constant holds its value until the next key, Linear is a
//   straight line, Cubic is the Hermite curve through both keys with the effective out slope of key i
//   and in slope of key i+1 (a cubic Bezier in (time, value) whose handle lengths are the weights times
//   the segment's time span; unweighted keys use 1/3, which is exactly the Hermite curve). Because
//   weights are clamped to [0, 1] the Bezier's time coordinate is monotone, so evaluation by time is
//   a function and is solved with a safeguarded Newton iteration.
// Automatic tangents: AutoAverage uses the slope between the neighbouring keys (one-sided at the
//   ends); AutoSmooth uses the monotone harmonic-mean slope, zero at local extremes, so the curve
//   never overshoots its keys. Linked keys use one user slope for both sides (the stored out slope);
//   Independent keys use both.
// Extrapolation: Constant holds the end value; Linear continues along the end tangent; Repeat tiles
//   the curve; RepeatOffset tiles it and adds the net value change per cycle; PingPong mirrors it
//   back and forth. A curve with fewer than two keys, or with zero time span, extrapolates constant.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "r1ui/widgets/colorpicker/ColorModel.h"

namespace r1ui::widgets::curve {

inline constexpr double kMaxCoordinate = 1.0e9;   // spec 11 rule 6: one billion units either way
inline constexpr double kMaxSlope = 1.0e9;
inline constexpr double kMinWeight = 0.01;
inline constexpr double kDefaultWeight = 1.0 / 3.0;
inline constexpr size_t kMaxKeysPerCurve = 1'000'000;

enum class Interp : uint8_t { Constant, Linear, Cubic };
// Spec 11 rule 54: keys 0..3 select these in order.
enum class TangentMode : uint8_t { AutoSmooth, AutoAverage, Linked, Independent };
enum class Extrapolation : uint8_t { Constant, Linear, Repeat, RepeatOffset, PingPong };

struct Key {
  uint32_t id = 0;
  double time = 0.0;
  double value = 0.0;
  Interp interp = Interp::Linear;  // toward the next key
  TangentMode mode = TangentMode::AutoSmooth;
  double inSlope = 0.0;            // user tangents (value units per time unit), used by Linked / Independent
  double outSlope = 0.0;
  bool weighted = false;
  double inWeight = kDefaultWeight;   // handle length as a fraction of the adjacent segment's time span
  double outWeight = kDefaultWeight;
  friend bool operator==(const Key&, const Key&) = default;
};

struct Curve {
  uint32_t id = 0;
  std::string name;
  color::Rgb colour{0.35, 0.65, 1.0};
  bool visible = true;
  bool locked = false;  // read-only: drawn, never edited
  bool solo = false;    // when any curve is solo only solo curves are drawn
  Extrapolation pre = Extrapolation::Constant;
  Extrapolation post = Extrapolation::Constant;
  std::vector<Key> keys;
  friend bool operator==(const Curve&, const Curve&) = default;
};

inline constexpr size_t npos = static_cast<size_t>(-1);

// ---- sanitising ----
// Repairs a curve in place: non-finite keys dropped, values clamped, keys sorted, equal times merged
// (the later key in the original order wins), ids made unique and non-zero (new ids continue after the
// largest), weights and slopes clamped, the list cut to kMaxKeysPerCurve. Returns whether it changed.
bool sanitize(Curve& curve);
// True when every invariant holds (what tests assert after every edit).
bool valid(const Curve& curve);
double clampCoordinate(double v);  // NaN -> 0

// ---- queries ----
// Index of the last key with time <= t, or npos when t is before the first key (or the curve is empty).
size_t segmentIndex(const Curve& curve, double t);
// Index of the key with `id`, or npos.
size_t indexOfKey(const Curve& curve, uint32_t id);

struct Slopes {
  double in = 0.0;
  double out = 0.0;
};
// The slopes the curve uses at key `index` (auto modes computed from the neighbours).
Slopes effectiveSlopes(const Curve& curve, size_t index);

// The Bezier control points (time, value) of the segment from key `index` to the next, for cubic
// segments; false for Constant / Linear segments or an index without a next key.
struct Bezier {
  double x[4];
  double y[4];
};
bool segmentBezier(const Curve& curve, size_t index, Bezier& out);

// Value of the curve at time t. NaN time returns the value at the first key (0 for an empty curve).
double evaluate(const Curve& curve, double t);
// Slope (derivative) of the curve at time t, for linear extrapolation and tangent estimation.
double slopeAt(const Curve& curve, double t);

// ---- flattening ----
struct Point {
  double x = 0.0;
  double y = 0.0;
};
// Appends the points of a polyline approximating the cubic Bezier given by four control points in
// any 2D space (typically screen pixels), excluding the first point and including the last, such that
// no point of the curve is farther than `tolerance` from the polyline. At most `maxPoints` are added.
void flattenCubic(const Bezier& bezier, double tolerance, std::vector<Point>& out, size_t maxPoints = 4096);

}  // namespace r1ui::widgets::curve

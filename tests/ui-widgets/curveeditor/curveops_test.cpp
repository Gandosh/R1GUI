// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for the editing operations on curve data (CurveOps.h): the selection container,
//   key insertion (interpolation inherited, flat curves, existing key, shape keeping against dense
//   samples), translation with the stacking rule and the no-crossing fast path, deletion, interpolation
//   and tangent mode changes (no jump in the curve), tangent handle dragging in every tangent mode and
//   weighted, flatten / straighten / loop match, snapping to frames staying on the curve, mirroring
//   (twice is the identity), clipboard and the three paste modes, and hostile arguments.
// Callers: CTest (curveeditor, fast tier, no GPU).
#include <chrono>
#include <cmath>
#include <limits>
#include <random>

#include "TestSupport.h"
#include "r1ui/widgets/curveeditor/CurveOps.h"

namespace {

using namespace r1ui::widgets::curve;

bool near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }

Curve ramp(int n, Interp interp = Interp::Linear) {
  Curve c;
  c.id = 1;
  c.name = "ramp";
  for (int i = 0; i < n; ++i) {
    Key k;
    k.id = static_cast<uint32_t>(i + 1);
    k.time = i;
    k.value = i * 2.0;
    k.interp = interp;
    c.keys.push_back(k);
  }
  return c;
}

void testSelection() {
  Selection s;
  const Selected a{1, 1, Part::Key};
  const Selected b{1, 2, Part::Out};
  const Selected c{2, 1, Part::Key};
  R1_EXPECT(s.empty() && s.add(c) && s.add(a) && s.add(b) && !s.add(a) && s.size() == 3);
  R1_EXPECT(s.items()[0] == a && s.items()[1] == b && s.items()[2] == c);  // sorted by curve, key, part
  R1_EXPECT(s.contains(b) && s.containsKey(1, 1) && !s.containsKey(1, 2) && s.hasHandles() && s.hasKeys());
  R1_EXPECT(s.remove(b) && !s.remove(b) && !s.hasHandles());
  s.toggle(a);
  s.toggle(b);
  R1_EXPECT(!s.contains(a) && s.contains(b));
  s.set(c);
  R1_EXPECT(s.size() == 1 && s.contains(c));
  R1_EXPECT(s.keysOf(2) == std::vector<uint32_t>({1}) && s.keysOf(1).empty());
  Selection p;
  p.add({1, 1, Part::Key});
  p.add({1, 99, Part::Key});
  p.add({7, 1, Part::Key});
  std::vector<Curve> curves = {ramp(3)};
  p.prune(curves);
  R1_EXPECT(p.size() == 1 && p.containsKey(1, 1));
  p.clear();
  R1_EXPECT(p.empty());
}

void testInsert() {
  Curve c = ramp(3, Interp::Cubic);
  c.keys[1].mode = TangentMode::Linked;  // a user mode is not inherited
  const InsertResult r = insertKey(c, 1.5, 7.0);
  R1_EXPECT(r.keyId != 0 && !r.existed && c.keys.size() == 4 && valid(c));
  R1_EXPECT(c.keys[2].time == 1.5 && c.keys[2].interp == Interp::Cubic && c.keys[2].mode == TangentMode::AutoSmooth);
  const InsertResult again = insertKey(c, 1.5, 9.0);  // rule: an occupied time selects the existing key
  R1_EXPECT(again.existed && again.keyId == r.keyId && c.keys.size() == 4 && c.keys[2].value == 7.0);
  R1_EXPECT(insertKey(c, -5.0, 0.0).keyId != 0 && c.keys.front().time == -5.0 && c.keys.front().interp == Interp::Linear);
  R1_EXPECT(insertKey(c, std::numeric_limits<double>::quiet_NaN(), 1.0).keyId == 0 && insertKey(c, 1.0, std::numeric_limits<double>::infinity()).keyId == 0);
  // A flat curve stays flat (rule 40).
  Curve flat = ramp(3);
  for (Key& k : flat.keys) k.value = 4.0;
  const InsertResult f = insertKey(flat, 0.5, 100.0);
  R1_EXPECT(flat.keys[1].id == f.keyId && flat.keys[1].value == 4.0);
  // The first key of an empty curve.
  Curve empty;
  R1_EXPECT(insertKey(empty, 2.0, 3.0).keyId == 1 && empty.keys.size() == 1 && valid(empty));
  // Ids are fresh, also after deletions.
  deleteKeys(c, {c.keys.back().id});
  std::vector<uint32_t> ids;
  for (const Key& k : c.keys) ids.push_back(k.id);
  const InsertResult g = insertKey(c, 50.0, 0.0);
  R1_EXPECT(std::find(ids.begin(), ids.end(), g.keyId) == ids.end());
  // Huge curve: insertion is bounded and stays valid.
  Curve big = ramp(100000);
  R1_EXPECT(insertKey(big, 500.5, 1.0).keyId != 0 && valid(big));
}

void testInsertKeepingShape() {
  // A smooth cubic curve: inserting on it with the shape keeping tangents changes it by very little.
  Curve c;
  c.id = 1;
  for (int i = 0; i < 5; ++i) {
    Key k;
    k.id = i + 1;
    k.time = i * 2.0;
    k.value = std::sin(i * 0.7) * 3.0;
    k.interp = Interp::Cubic;
    c.keys.push_back(k);
  }
  const Curve before = c;
  const double t = 3.1;
  const InsertResult r = insertKeyKeepingShape(c, t, evaluate(before, t));
  R1_EXPECT(r.keyId != 0 && !r.existed && valid(c));
  const Key& k = c.keys[indexOfKey(c, r.keyId)];
  R1_EXPECT(k.mode == TangentMode::Independent && k.interp == Interp::Cubic);
  // The slope over the span measured on the curve (0.1 each side).
  R1_EXPECT(near(k.inSlope, (evaluate(before, t) - evaluate(before, t - 0.1)) / 0.1, 1e-9));
  R1_EXPECT(near(k.outSlope, (evaluate(before, t + 0.1) - evaluate(before, t)) / 0.1, 1e-9));
  double worst = 0.0;
  for (int i = 0; i <= 800; ++i) worst = std::max(worst, std::fabs(evaluate(c, i * 0.01) - evaluate(before, i * 0.01)));
  R1_EXPECT(worst < 0.2);
  // On a linear curve the shape is exact.
  Curve line = ramp(3);
  const Curve lineBefore = line;
  insertKeyKeepingShape(line, 0.37, evaluate(lineBefore, 0.37));
  double lw = 0.0;
  for (int i = 0; i <= 200; ++i) lw = std::max(lw, std::fabs(evaluate(line, i * 0.01) - evaluate(lineBefore, i * 0.01)));
  R1_EXPECT(lw < 1e-12);
  R1_EXPECT(insertKeyKeepingShape(c, 1.0, 1.0, -1.0).keyId == 0 && insertKeyKeepingShape(c, 1.0, 1.0, std::numeric_limits<double>::quiet_NaN()).keyId == 0);
}

void testTranslate() {
  Curve c = ramp(5);
  R1_EXPECT(translateKeys(c, {2}, 0.25, 1.0) == 0 && near(c.keys[1].time, 1.25) && near(c.keys[1].value, 3.0) && valid(c));
  // Moving key 2 (time 1) onto key 3 (time 2) removes the stacked key: the moved key wins (rule 49).
  c = ramp(5);
  const size_t removed = translateKeys(c, {2}, 1.0, 5.0);
  R1_EXPECT(removed == 1 && c.keys.size() == 4 && valid(c));
  R1_EXPECT(indexOfKey(c, 3) == npos && indexOfKey(c, 2) != npos && c.keys[indexOfKey(c, 2)].value == 7.0);
  // Moving past neighbours re-sorts without removing anything.
  c = ramp(5);
  R1_EXPECT(translateKeys(c, {1}, 2.5, 0.0) == 0 && valid(c) && c.keys[2].id == 1);
  // A group moves together; two moved keys landing on each other keep one.
  c = ramp(6);
  R1_EXPECT(translateKeys(c, {2, 3}, 0.0, 10.0) == 0 && c.keys[1].value == 12.0 && c.keys[2].value == 14.0);
  c = ramp(6);
  Curve d = c;
  const size_t rm = translateKeys(d, {1, 2, 3}, 1.0, 0.0);  // 0,1,2 -> 1,2,3 over the keys at 1,2,3 that move too
  R1_EXPECT(valid(d) && d.keys.size() + rm == 6);
  // Unknown ids and hostile deltas change nothing.
  c = ramp(4);
  const Curve snapshot = c;
  R1_EXPECT(translateKeys(c, {99}, 1, 1) == 0 && c == snapshot);
  R1_EXPECT(translateKeys(c, {1}, std::numeric_limits<double>::quiet_NaN(), 0) == 0 && c == snapshot);
  R1_EXPECT(translateKeys(c, {1}, 0, std::numeric_limits<double>::infinity()) == 0 && c == snapshot);
  translateKeys(c, {1}, 1e300, -1e300);
  R1_EXPECT(valid(c) && c.keys.back().time == kMaxCoordinate);
  // Performance: dragging one key of 100000 does not re-sort when it does not cross a neighbour.
  Curve big = ramp(100000);
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < 200; ++i) translateKeys(big, {50000}, 0.001, 0.0);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
#ifdef NDEBUG
  constexpr double kSlack = 1.0;
#else
  constexpr double kSlack = 40.0;  // unoptimised code with checked iterators is far slower; the bound only guards the complexity
#endif
  R1_EXPECT(valid(big) && ms < 2000.0 * kSlack);
  std::fprintf(stdout, "curve ops: 200 single key drags over 100000 keys took %.1f ms\n", ms);
  // Moving 1000 keys across the curve (the sort path) stays valid.
  std::vector<uint32_t> many;
  for (uint32_t i = 1; i < 2000; i += 2) many.push_back(i);
  translateKeys(big, many, 33.3, 0.0);
  R1_EXPECT(valid(big));
}

void testDeleteAndInterp() {
  Curve c = ramp(6);
  R1_EXPECT(deleteKeys(c, {2, 4, 4, 99}) == 2 && c.keys.size() == 4 && valid(c) && indexOfKey(c, 2) == npos);
  R1_EXPECT(deleteKeys(c, {}) == 0);
  R1_EXPECT(setInterpolation(c, {1, 3}, Interp::Constant) == 2 && c.keys[0].interp == Interp::Constant && setInterpolation(c, {1}, Interp::Constant) == 0);
  // Tangent mode changes never make the curve jump.
  Curve cub = ramp(5, Interp::Cubic);
  cub.keys[2].value = 9.0;
  const Curve before = cub;
  R1_EXPECT(setTangentMode(cub, {3}, TangentMode::Linked) == 1 && cub.keys[2].mode == TangentMode::Linked);
  R1_EXPECT(near(cub.keys[2].outSlope, effectiveSlopes(before, 2).out));
  R1_EXPECT(setTangentMode(cub, {3}, TangentMode::Independent) == 1 && cub.keys[2].inSlope == cub.keys[2].outSlope);
  for (int i = 0; i <= 400; ++i) R1_EXPECT(near(evaluate(cub, i * 0.01), evaluate(before, i * 0.01), 1e-9));
  R1_EXPECT(setTangentMode(cub, {3}, TangentMode::Independent) == 0 && setTangentMode(cub, {3}, TangentMode::AutoAverage) == 1);
  R1_EXPECT(setWeighted(cub, {3, 4}, true) == 2 && setWeighted(cub, {3}, true) == 0 && cub.keys[3].weighted);
}

void testHandleDrag() {
  Curve c = ramp(3, Interp::Cubic);
  c.keys[1].value = 5.0;
  // Auto tangents become linked (rule 56) and take the slope of the handle.
  R1_EXPECT(setTangentFromHandle(c, 2, true, 0.5, 1.0));
  R1_EXPECT(c.keys[1].mode == TangentMode::Linked && near(c.keys[1].outSlope, 2.0) && near(c.keys[1].inSlope, 2.0));
  // The in handle lies at negative time: dragging it up-left (dt < 0, dv > 0) gives a negative slope.
  R1_EXPECT(setTangentFromHandle(c, 2, false, -0.5, 1.0));
  R1_EXPECT(near(c.keys[1].outSlope, -2.0) && near(c.keys[1].inSlope, -2.0));  // linked: both follow
  // Independent keys move only the dragged side.
  setTangentMode(c, {2}, TangentMode::Independent);
  setTangentFromHandle(c, 2, true, 1.0, 3.0);
  R1_EXPECT(near(c.keys[1].outSlope, 3.0) && near(c.keys[1].inSlope, -2.0));
  setTangentFromHandle(c, 2, false, -1.0, -4.0);  // in handle down-left: slope = dv / dt = 4
  R1_EXPECT(near(c.keys[1].inSlope, 4.0) && near(c.keys[1].outSlope, 3.0));
  // Weighted keys store the handle length as a fraction of the segment (clamped).
  c.keys[1].weighted = true;
  setTangentFromHandle(c, 2, true, 0.25, 0.0);
  R1_EXPECT(near(c.keys[1].outWeight, 0.25) && c.keys[1].outSlope == 0.0);
  setTangentFromHandle(c, 2, true, 9.0, 0.0);
  R1_EXPECT(c.keys[1].outWeight == 1.0);
  setTangentFromHandle(c, 2, false, -1e-12, 0.0);
  R1_EXPECT(c.keys[1].inWeight == kMinWeight);
  // A handle dragged across its key keeps a finite (steep) slope.
  Curve d = ramp(3, Interp::Cubic);
  R1_EXPECT(setTangentFromHandle(d, 2, true, -1.0, 1.0) && std::isfinite(d.keys[1].outSlope) && valid(d));
  R1_EXPECT(!setTangentFromHandle(d, 99, true, 1, 1) && !setTangentFromHandle(d, 2, true, std::numeric_limits<double>::quiet_NaN(), 1));
  R1_EXPECT(setTangentFromHandle(d, 2, true, 1e-30, 1e30) && std::fabs(d.keys[1].outSlope) <= kMaxSlope);
}

void testFlattenStraightenLoop() {
  Curve c = ramp(4, Interp::Cubic);
  c.keys[1].value = 1.0;  // monotone data: the smooth tangent of key 2 is not zero
  R1_EXPECT(flattenTangents(c, {{1, 2, Part::Out}}) == 1);
  R1_EXPECT(c.keys[1].mode == TangentMode::Independent && c.keys[1].outSlope == 0.0 && c.keys[1].inSlope != 0.0);
  R1_EXPECT(flattenTangents(c, {{1, 3, Part::Key}}) == 1 && c.keys[2].inSlope == 0.0 && c.keys[2].outSlope == 0.0);
  R1_EXPECT(flattenTangents(c, {{1, 99, Part::Key}}) == 0);
  R1_EXPECT(straightenTangents(c, {{1, 2, Part::Out}}) == 1);
  R1_EXPECT(near(c.keys[1].outSlope, (c.keys[2].value - c.keys[1].value) / (c.keys[2].time - c.keys[1].time)));
  R1_EXPECT(straightenTangents(c, {{1, 2, Part::In}}) == 1 && near(c.keys[1].inSlope, (c.keys[1].value - c.keys[0].value) / (c.keys[1].time - c.keys[0].time)));
  // Loop matching.
  Curve loop = ramp(4, Interp::Cubic);
  loop.keys[0].mode = TangentMode::Independent;
  loop.keys[0].outSlope = 3.0;
  R1_EXPECT(matchLoopTangents(loop, true) && loop.keys[3].inSlope == 3.0 && loop.keys[3].outSlope == 3.0 && loop.keys[3].mode == TangentMode::Independent);
  loop.keys[3].inSlope = -2.0;
  R1_EXPECT(matchLoopTangents(loop, false) && loop.keys[0].inSlope == -2.0 && loop.keys[0].outSlope == -2.0);
  Curve single = ramp(1);
  R1_EXPECT(!matchLoopTangents(single, true));
}

void testSnapAndFlip() {
  Curve c = ramp(3, Interp::Linear);
  c.keys[1].time = 0.52;  // between frames at 30 fps
  c.keys[1].value = 7.0;
  const Curve before = c;
  R1_EXPECT(snapKeysToFrames(c, {2}, 30.0) == 1 && near(c.keys[1].time, 16.0 / 30.0) && valid(c));
  R1_EXPECT(near(c.keys[1].value, evaluate(before, 16.0 / 30.0)));  // stays on the curve it was on
  R1_EXPECT(snapKeysToFrames(c, {2}, 30.0) == 0 && snapKeysToFrames(c, {2}, 0.0) == 0 && snapKeysToFrames(c, {2}, std::numeric_limits<double>::quiet_NaN()) == 0);
  // Mirroring twice is the identity.
  Curve m = ramp(5, Interp::Cubic);
  m.keys[1].interp = Interp::Constant;
  m.keys[2].mode = TangentMode::Independent;
  m.keys[2].inSlope = 1.0;
  m.keys[2].outSlope = 5.0;
  m.pre = Extrapolation::Repeat;
  m.post = Extrapolation::Linear;
  const Curve original = m;
  R1_EXPECT(flipHorizontal(m) && valid(m) && m.pre == Extrapolation::Linear && m.post == Extrapolation::Repeat);
  R1_EXPECT(m.keys.front().id == 5 && near(m.keys.front().time, 0.0) && m.keys[2].inSlope == -5.0 && m.keys[2].outSlope == -1.0);
  R1_EXPECT(flipHorizontal(m) && m == original);
  Curve v = original;
  R1_EXPECT(flipVertical(v) && near(v.keys[0].value, 8.0) && v.keys[2].outSlope == -5.0 && flipVertical(v) && v == original);
  Curve empty;
  R1_EXPECT(!flipHorizontal(empty) && !flipVertical(empty));
}

void testClipboard() {
  std::vector<Curve> curves = {ramp(5), ramp(5)};
  curves[1].id = 2;
  curves[1].name = "other";
  Selection s;
  s.add({1, 2, Part::Key});
  s.add({1, 4, Part::Key});
  s.add({1, 3, Part::In});  // handles are not copied
  const std::vector<ClipCurve> clip = copyKeys(curves, s);
  R1_EXPECT(clip.size() == 1 && clip[0].name == "ramp" && clip[0].keys.size() == 2 && clip[0].keys[0].time == 1.0);
  R1_EXPECT(copyKeys(curves, Selection{}).empty());

  // Replace: the destination's keys inside the pasted span are replaced (rule 69).
  Curve dest = ramp(10);
  const std::vector<uint32_t> created = pasteKeys(dest, clip[0], 4.5, PasteMode::Replace);  // lands at 4.5 and 6.5
  R1_EXPECT(created.size() == 2 && valid(dest));
  // The keys at times 5 and 6 (ids 6 and 7) lie inside 4.5 .. 6.5 and are replaced; 4 and 7 stay.
  R1_EXPECT(indexOfKey(dest, 6) == npos && indexOfKey(dest, 7) == npos && indexOfKey(dest, 5) != npos && indexOfKey(dest, 8) != npos);
  size_t at45 = 0;
  for (const Key& k : dest.keys) at45 += near(k.time, 4.5) || near(k.time, 6.5);
  R1_EXPECT(at45 == 2);
  // Merge: nothing between is removed, equal times are replaced (rule 70).
  Curve m = ramp(10);
  const size_t before = m.keys.size();
  pasteKeys(m, clip[0], 4.0, PasteMode::Merge);  // pasted keys at 4 and 6, both on existing keys
  R1_EXPECT(m.keys.size() == before && valid(m) && m.keys[4].value == 2.0);
  Curve m2 = ramp(10);
  pasteKeys(m2, clip[0], 4.5, PasteMode::Merge);
  R1_EXPECT(m2.keys.size() == before + 2 && valid(m2));
  // Relative: values continue from the curve at the paste time (rule 71).
  Curve r = ramp(10);
  pasteKeys(r, clip[0], 3.0, PasteMode::Relative);
  R1_EXPECT(valid(r) && near(r.keys[3].value, evaluate(ramp(10), 3.0)));
  // Hostile.
  R1_EXPECT(pasteKeys(r, clip[0], std::numeric_limits<double>::quiet_NaN(), PasteMode::Replace).empty());
  R1_EXPECT(pasteKeys(r, ClipCurve{}, 1.0, PasteMode::Replace).empty());
  ClipCurve hostile;
  Key bad;
  bad.time = 1.0;
  bad.value = 1e300;
  hostile.keys = {bad};
  pasteKeys(r, hostile, 1e300, PasteMode::Replace);
  R1_EXPECT(valid(r));
}

}  // namespace

int main() {
  testSelection();
  testInsert();
  testInsertKeepingShape();
  testTranslate();
  testDeleteAndInterp();
  testHandleDrag();
  testFlattenStraightenLoop();
  testSnapAndFlip();
  testClipboard();
  return r1test::finish();
}

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the editing operations of the curve editor on plain curve data, independent of any window:
//   the selection (keys and tangent handles across curves), inserting keys (also keeping the shape),
//   translating keys with stacking rules, deleting, changing interpolation, tangent mode and
//   weighting, flattening / straightening tangents, loop matching, snapping to frames, mirroring,
//   and copy / paste with the three paste modes of spec 11 (replace span, merge, relative).
// Why: spec 11 states precise editing rules (a moved key that lands on another removes the stacked
//   one, a new key at an occupied time selects the existing key, ctrl-insert keeps the curve's shape);
//   as pure functions they are unit tested against analytic results and keep the widget thin.
// Callers: CurveGraph, CurveKeyFields, tests. Calls: CurveMath.
// Invariants: every operation leaves each curve valid (CurveMath::valid) and returns what it changed;
//   operations given unknown curve or key ids do nothing; non-finite arguments are rejected.
#pragma once

#include <string>
#include <vector>

#include "r1ui/widgets/curveeditor/CurveMath.h"

namespace r1ui::widgets::curve {

// ---- selection ----
enum class Part : uint8_t { Key, In, Out };

struct Selected {
  uint32_t curve = 0;  // Curve::id
  uint32_t key = 0;    // Key::id
  Part part = Part::Key;
  friend bool operator==(const Selected&, const Selected&) = default;
  friend bool operator<(const Selected& a, const Selected& b) {
    if (a.curve != b.curve) return a.curve < b.curve;
    if (a.key != b.key) return a.key < b.key;
    return a.part < b.part;
  }
};

class Selection {
 public:
  void clear() { items_.clear(); }
  bool empty() const { return items_.empty(); }
  size_t size() const { return items_.size(); }
  const std::vector<Selected>& items() const { return items_; }
  bool contains(const Selected& s) const;
  bool containsKey(uint32_t curve, uint32_t key) const { return contains({curve, key, Part::Key}); }
  bool add(const Selected& s);      // false when already selected
  bool remove(const Selected& s);   // false when it was not selected
  void toggle(const Selected& s);
  void set(const Selected& s);      // only this item
  bool hasHandles() const;
  bool hasKeys() const;
  // Ids of the selected keys (part Key) of `curve`.
  std::vector<uint32_t> keysOf(uint32_t curve) const;
  // Like keysOf, but when the curve has no selected key the keys that own its selected tangent handles
  // (so interpolation, tangent mode and weight commands also work on a handle selection).
  std::vector<uint32_t> keysOrOwnersOf(uint32_t curve) const;
  bool anyPartSelected(uint32_t curve, uint32_t key) const;
  // Drops items whose curve or key no longer exists (after an edit that removed data).
  void prune(const std::vector<Curve>& curves);
  friend bool operator==(const Selection&, const Selection&) = default;

 private:
  std::vector<Selected> items_;  // sorted, unique
};

// ---- lookup ----
Curve* findCurve(std::vector<Curve>& curves, uint32_t id);
const Curve* findCurve(const std::vector<Curve>& curves, uint32_t id);
uint32_t nextKeyId(const Curve& curve);
uint32_t nextCurveId(const std::vector<Curve>& curves);

// ---- insertion ----
struct InsertResult {
  uint32_t keyId = 0;     // 0 when rejected (non-finite time or value, curve full)
  bool existed = false;   // a key already sits at that time: nothing was inserted, its id is returned
};
// Inserts a key at (time, value). Interpolation and tangent mode come from the key before it (or
// Linear with smooth tangents for the first key) -- spec 11 rule 41. A flat curve (all values equal,
// at least two keys) gives the new key that value (rule 40).
InsertResult insertKey(Curve& curve, double time, double value);
// Like insertKey, but the new key gets user tangents estimated over `span` time units on each side
// of the existing curve (rule 39): independent tangents measured on the curve, so a cubic curve keeps
// its shape (to the accuracy of the span) and a linear one is unchanged. The interpolation is the one
// of the curve at that time.
InsertResult insertKeyKeepingShape(Curve& curve, double time, double value, double span = 0.1);

// ---- modification ----
// Moves the keys (by id) by (dt, dv) and re-establishes the sorted order. A moved key that lands on
// the time of a key that was not moved removes that key (rule 49). Returns the number of keys removed.
size_t translateKeys(Curve& curve, const std::vector<uint32_t>& ids, double dt, double dv);
size_t deleteKeys(Curve& curve, const std::vector<uint32_t>& ids);
size_t setInterpolation(Curve& curve, const std::vector<uint32_t>& ids, Interp interp);
// Switches the tangent mode; entering Linked / Independent keeps the slopes the key had so the shape
// does not jump. Returns the number of keys changed.
size_t setTangentMode(Curve& curve, const std::vector<uint32_t>& ids, TangentMode mode);
size_t setWeighted(Curve& curve, const std::vector<uint32_t>& ids, bool weighted);
// Sets one tangent from a handle position relative to its key, in data units: the slope is dv / dt
// with dt measured toward the handle (in handles lie at negative dt). Rule 56: an auto key becomes
// Linked, a Linked key moves both sides, an Independent key only the dragged side. With weights the
// handle length (|dt| over the adjacent segment's time span) is stored as well.
bool setTangentFromHandle(Curve& curve, uint32_t keyId, bool outSide, double dt, double dv);
// Rule 58: selected tangents become horizontal. `parts` lists key / in / out selections of one curve.
size_t flattenTangents(Curve& curve, const std::vector<Selected>& parts);
// Rule 59: each selected tangent is aligned with the straight line to its neighbouring key.
size_t straightenTangents(Curve& curve, const std::vector<Selected>& parts);
// Rule 60: makes the end tangents agree: the end key takes the start tangent (fromStart) or the start
// key takes the end tangent. Both keys become Independent.
bool matchLoopTangents(Curve& curve, bool fromStart);
// Rule 62: each key (by id) moves to the nearest whole frame and keeps its place on the curve.
size_t snapKeysToFrames(Curve& curve, const std::vector<uint32_t>& ids, double framesPerSecond);
// Rule 63: mirrors the curve in time about the centre of its key range, or in value about the centre
// of its value range. Tangents are mirrored accordingly.
bool flipHorizontal(Curve& curve);
bool flipVertical(Curve& curve);

// ---- clipboard ----
struct ClipCurve {
  std::string name;
  std::vector<Key> keys;  // absolute times, ids irrelevant
};
std::vector<ClipCurve> copyKeys(const std::vector<Curve>& curves, const Selection& selection);

enum class PasteMode : uint8_t {
  Replace,   // rule 69: keys of the destination inside the pasted time span are replaced
  Merge,     // rule 70: pasted keys are added, the keys in between stay
  Relative   // rule 71: like Replace, values offset so the first pasted key continues the curve at `at`
};
// Pastes the clip's keys so that its first key lands at `at`. Returns the ids of the new keys (empty
// when the clip is empty or `at` is not finite).
std::vector<uint32_t> pasteKeys(Curve& dest, const ClipCurve& clip, double at, PasteMode mode);

}  // namespace r1ui::widgets::curve

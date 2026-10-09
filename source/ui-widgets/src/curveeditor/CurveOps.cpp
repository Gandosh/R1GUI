// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CurveOps.h.
// Invariants: every mutating function ends with the curve valid (sorted, unique times, finite); the
//   cheap path of translateKeys (no crossing) is O(n) with no allocation, the general path re-sorts.
// Callers: CurveGraph, CurveKeyFields, tests.
#include "r1ui/widgets/curveeditor/CurveOps.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "r1ui/widgets/curveeditor/CurveView.h"

namespace r1ui::widgets::curve {

namespace {

bool containsId(const std::vector<uint32_t>& sortedIds, uint32_t id) { return std::binary_search(sortedIds.begin(), sortedIds.end(), id); }

std::vector<uint32_t> sortedUnique(std::vector<uint32_t> ids) {
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  return ids;
}

bool strictlySorted(const std::vector<Key>& keys) {
  for (size_t i = 1; i < keys.size(); ++i) {
    if (!(keys[i].time > keys[i - 1].time)) return false;
  }
  return true;
}

// Re-sorts after keys moved; at equal times a moved key wins over one that did not move.
size_t resortMerged(Curve& c, const std::vector<uint32_t>& movedSorted) {
  if (strictlySorted(c.keys)) return 0;
  const size_t before = c.keys.size();
  std::stable_sort(c.keys.begin(), c.keys.end(), [&](const Key& a, const Key& b) {
    if (a.time != b.time) return a.time < b.time;
    return !containsId(movedSorted, a.id) && containsId(movedSorted, b.id);
  });
  std::vector<Key> out;
  out.reserve(c.keys.size());
  for (const Key& k : c.keys) {
    if (!out.empty() && out.back().time == k.time) out.back() = k;
    else out.push_back(k);
  }
  c.keys = std::move(out);
  return before - c.keys.size();
}

}  // namespace

// ---- Selection --------------------------------------------------------------------------------

bool Selection::contains(const Selected& s) const { return std::binary_search(items_.begin(), items_.end(), s); }

bool Selection::add(const Selected& s) {
  const auto it = std::lower_bound(items_.begin(), items_.end(), s);
  if (it != items_.end() && *it == s) return false;
  items_.insert(it, s);
  return true;
}

bool Selection::remove(const Selected& s) {
  const auto it = std::lower_bound(items_.begin(), items_.end(), s);
  if (it == items_.end() || !(*it == s)) return false;
  items_.erase(it);
  return true;
}

void Selection::toggle(const Selected& s) {
  if (!remove(s)) add(s);
}

void Selection::set(const Selected& s) {
  items_.clear();
  items_.push_back(s);
}

bool Selection::hasHandles() const {
  return std::any_of(items_.begin(), items_.end(), [](const Selected& s) { return s.part != Part::Key; });
}

bool Selection::hasKeys() const {
  return std::any_of(items_.begin(), items_.end(), [](const Selected& s) { return s.part == Part::Key; });
}

std::vector<uint32_t> Selection::keysOf(uint32_t curve) const {
  std::vector<uint32_t> ids;
  for (const Selected& s : items_) {
    if (s.curve == curve && s.part == Part::Key) ids.push_back(s.key);
  }
  return ids;
}

std::vector<uint32_t> Selection::keysOrOwnersOf(uint32_t curve) const {
  std::vector<uint32_t> ids = keysOf(curve);
  if (!ids.empty()) return ids;
  for (const Selected& s : items_) {
    if (s.curve == curve && (ids.empty() || ids.back() != s.key)) ids.push_back(s.key);  // sorted by key
  }
  return ids;
}

bool Selection::anyPartSelected(uint32_t curve, uint32_t key) const {
  return contains({curve, key, Part::Key}) || contains({curve, key, Part::In}) || contains({curve, key, Part::Out});
}

void Selection::prune(const std::vector<Curve>& curves) {
  items_.erase(std::remove_if(items_.begin(), items_.end(),
                              [&](const Selected& s) {
                                const Curve* c = findCurve(curves, s.curve);
                                return c == nullptr || indexOfKey(*c, s.key) == npos;
                              }),
               items_.end());
}

// ---- lookup -----------------------------------------------------------------------------------

Curve* findCurve(std::vector<Curve>& curves, uint32_t id) {
  for (Curve& c : curves) {
    if (c.id == id) return &c;
  }
  return nullptr;
}

const Curve* findCurve(const std::vector<Curve>& curves, uint32_t id) {
  for (const Curve& c : curves) {
    if (c.id == id) return &c;
  }
  return nullptr;
}

uint32_t nextKeyId(const Curve& curve) {
  uint32_t next = 1;
  for (const Key& k : curve.keys) next = std::max(next, k.id + 1);
  return next;
}

uint32_t nextCurveId(const std::vector<Curve>& curves) {
  uint32_t next = 1;
  for (const Curve& c : curves) next = std::max(next, c.id + 1);
  return next;
}

// ---- insertion --------------------------------------------------------------------------------

InsertResult insertKey(Curve& c, double time, double value) {
  if (!std::isfinite(time) || !std::isfinite(value)) return {};
  time = clampCoordinate(time);
  value = clampCoordinate(value);
  const size_t at = segmentIndex(c, time);
  if (at != npos && c.keys[at].time == time) return {c.keys[at].id, true};
  if (c.keys.size() >= kMaxKeysPerCurve) return {};
  Key key;
  key.id = nextKeyId(c);
  key.time = time;
  key.value = value;
  if (at != npos) {
    key.interp = c.keys[at].interp;
    key.mode = (c.keys[at].mode == TangentMode::AutoAverage) ? TangentMode::AutoAverage : TangentMode::AutoSmooth;
  }
  if (c.keys.size() >= 2 && std::all_of(c.keys.begin(), c.keys.end(), [&](const Key& k) { return k.value == c.keys.front().value; })) {
    key.value = c.keys.front().value;  // rule 40: a flat curve stays flat
  }
  c.keys.insert(c.keys.begin() + static_cast<std::ptrdiff_t>(at == npos ? 0 : at + 1), key);
  return {key.id, false};
}

InsertResult insertKeyKeepingShape(Curve& c, double time, double value, double span) {
  if (!std::isfinite(time) || !std::isfinite(value) || !(span > 0.0) || !std::isfinite(span)) return {};
  const double before = evaluate(c, time - span);
  const double after = evaluate(c, time + span);
  const InsertResult r = insertKey(c, time, value);
  if (r.keyId == 0 || r.existed) return r;
  Key& k = c.keys[indexOfKey(c, r.keyId)];
  k.mode = TangentMode::Independent;  // the interpolation stays that of the curve at this time
  k.inSlope = std::clamp((k.value - before) / span, -kMaxSlope, kMaxSlope);
  k.outSlope = std::clamp((after - k.value) / span, -kMaxSlope, kMaxSlope);
  return r;
}

// ---- modification -----------------------------------------------------------------------------

size_t translateKeys(Curve& c, const std::vector<uint32_t>& idsIn, double dt, double dv) {
  if (!std::isfinite(dt) || !std::isfinite(dv)) return 0;
  const std::vector<uint32_t> ids = sortedUnique(idsIn);
  bool any = false;
  for (Key& k : c.keys) {
    if (!containsId(ids, k.id)) continue;
    k.time = clampCoordinate(k.time + dt);
    k.value = clampCoordinate(k.value + dv);
    any = true;
  }
  if (!any) return 0;
  return resortMerged(c, ids);
}

size_t deleteKeys(Curve& c, const std::vector<uint32_t>& idsIn) {
  const std::vector<uint32_t> ids = sortedUnique(idsIn);
  const size_t before = c.keys.size();
  c.keys.erase(std::remove_if(c.keys.begin(), c.keys.end(), [&](const Key& k) { return containsId(ids, k.id); }), c.keys.end());
  return before - c.keys.size();
}

size_t setInterpolation(Curve& c, const std::vector<uint32_t>& idsIn, Interp interp) {
  const std::vector<uint32_t> ids = sortedUnique(idsIn);
  size_t changed = 0;
  for (Key& k : c.keys) {
    if (containsId(ids, k.id) && k.interp != interp) {
      k.interp = interp;
      ++changed;
    }
  }
  return changed;
}

size_t setTangentMode(Curve& c, const std::vector<uint32_t>& idsIn, TangentMode mode) {
  const std::vector<uint32_t> ids = sortedUnique(idsIn);
  size_t changed = 0;
  for (size_t i = 0; i < c.keys.size(); ++i) {
    Key& k = c.keys[i];
    if (!containsId(ids, k.id) || k.mode == mode) continue;
    const Slopes eff = effectiveSlopes(c, i);
    if (mode == TangentMode::Linked) {
      k.inSlope = k.outSlope = eff.out;
    } else if (mode == TangentMode::Independent) {
      k.inSlope = eff.in;
      k.outSlope = eff.out;
    }
    k.mode = mode;
    ++changed;
  }
  return changed;
}

size_t setWeighted(Curve& c, const std::vector<uint32_t>& idsIn, bool weighted) {
  const std::vector<uint32_t> ids = sortedUnique(idsIn);
  size_t changed = 0;
  for (Key& k : c.keys) {
    if (containsId(ids, k.id) && k.weighted != weighted) {
      k.weighted = weighted;
      ++changed;
    }
  }
  return changed;
}

bool setTangentFromHandle(Curve& c, uint32_t keyId, bool outSide, double dt, double dv) {
  const size_t i = indexOfKey(c, keyId);
  if (i == npos || !std::isfinite(dt) || !std::isfinite(dv)) return false;
  Key& k = c.keys[i];
  const double adt = std::max(std::fabs(dt), 1e-9);
  const double slope = std::clamp(outSide ? dv / adt : -dv / adt, -kMaxSlope, kMaxSlope);
  switch (k.mode) {
    case TangentMode::AutoSmooth:
    case TangentMode::AutoAverage:
    case TangentMode::Linked:
      k.mode = TangentMode::Linked;
      k.inSlope = k.outSlope = slope;
      break;
    case TangentMode::Independent:
      (outSide ? k.outSlope : k.inSlope) = slope;
      break;
  }
  if (k.weighted) {
    const bool hasSegment = outSide ? i + 1 < c.keys.size() : i > 0;
    if (hasSegment) {
      const double segment = outSide ? c.keys[i + 1].time - k.time : k.time - c.keys[i - 1].time;
      if (segment > 0.0) (outSide ? k.outWeight : k.inWeight) = std::clamp(adt / segment, kMinWeight, 1.0);
    }
  }
  return true;
}

namespace {

// Slope of a tangent after a user action on one or both sides; the key becomes Independent so the
// other side keeps what it had.
void makeIndependent(Curve& c, size_t i) {
  Key& k = c.keys[i];
  if (k.mode == TangentMode::Independent) return;
  const Slopes eff = effectiveSlopes(c, i);
  k.inSlope = eff.in;
  k.outSlope = eff.out;
  k.mode = TangentMode::Independent;
}

}  // namespace

size_t flattenTangents(Curve& c, const std::vector<Selected>& parts) {
  size_t changed = 0;
  for (const Selected& s : parts) {
    const size_t i = indexOfKey(c, s.key);
    if (i == npos) continue;
    makeIndependent(c, i);
    Key& k = c.keys[i];
    if (s.part != Part::Out) k.inSlope = 0.0;
    if (s.part != Part::In) k.outSlope = 0.0;
    ++changed;
  }
  return changed;
}

size_t straightenTangents(Curve& c, const std::vector<Selected>& parts) {
  size_t changed = 0;
  for (const Selected& s : parts) {
    const size_t i = indexOfKey(c, s.key);
    if (i == npos) continue;
    makeIndependent(c, i);
    Key& k = c.keys[i];
    if (s.part != Part::In && i + 1 < c.keys.size()) {
      const Key& n = c.keys[i + 1];
      k.outSlope = std::clamp((n.value - k.value) / (n.time - k.time), -kMaxSlope, kMaxSlope);
    }
    if (s.part != Part::Out && i > 0) {
      const Key& p = c.keys[i - 1];
      k.inSlope = std::clamp((k.value - p.value) / (k.time - p.time), -kMaxSlope, kMaxSlope);
    }
    ++changed;
  }
  return changed;
}

bool matchLoopTangents(Curve& c, bool fromStart) {
  if (c.keys.size() < 2) return false;
  const size_t last = c.keys.size() - 1;
  if (fromStart) {
    const double s = effectiveSlopes(c, 0).out;
    Key& e = c.keys[last];
    e.mode = TangentMode::Independent;
    e.inSlope = e.outSlope = s;
    e.inWeight = c.keys[0].outWeight;
    e.outWeight = c.keys[0].outWeight;
  } else {
    const double s = effectiveSlopes(c, last).in;
    Key& b = c.keys[0];
    b.mode = TangentMode::Independent;
    b.inSlope = b.outSlope = s;
    b.inWeight = c.keys[last].inWeight;
    b.outWeight = c.keys[last].inWeight;
  }
  return true;
}

size_t snapKeysToFrames(Curve& c, const std::vector<uint32_t>& idsIn, double fps) {
  if (!(fps > 0.0) || !std::isfinite(fps)) return 0;
  const std::vector<uint32_t> ids = sortedUnique(idsIn);
  const Curve original = c;
  size_t changed = 0;
  for (Key& k : c.keys) {
    if (!containsId(ids, k.id)) continue;
    const double t = snapToFrame(k.time, fps);
    if (t == k.time) continue;
    k.time = clampCoordinate(t);
    k.value = evaluate(original, k.time);
    ++changed;
  }
  resortMerged(c, ids);
  return changed;
}

bool flipHorizontal(Curve& c) {
  const size_t n = c.keys.size();
  if (n == 0) return false;
  const double pivot = 0.5 * (c.keys.front().time + c.keys.back().time);
  std::vector<Key> flipped(n);
  for (size_t j = 0; j < n; ++j) {
    Key k = c.keys[n - 1 - j];
    k.time = clampCoordinate(2.0 * pivot - k.time);
    const double in = k.inSlope;
    k.inSlope = -k.outSlope;
    k.outSlope = -in;
    std::swap(k.inWeight, k.outWeight);
    k.interp = j + 1 < n ? c.keys[n - 2 - j].interp : c.keys[n - 1].interp;
    flipped[j] = k;
  }
  c.keys = std::move(flipped);
  std::swap(c.pre, c.post);
  return true;
}

bool flipVertical(Curve& c) {
  if (c.keys.empty()) return false;
  double lo = c.keys.front().value;
  double hi = lo;
  for (const Key& k : c.keys) {
    lo = std::min(lo, k.value);
    hi = std::max(hi, k.value);
  }
  const double mid = 0.5 * (lo + hi);
  for (Key& k : c.keys) {
    k.value = clampCoordinate(2.0 * mid - k.value);
    k.inSlope = -k.inSlope;
    k.outSlope = -k.outSlope;
  }
  return true;
}

// ---- clipboard --------------------------------------------------------------------------------

std::vector<ClipCurve> copyKeys(const std::vector<Curve>& curves, const Selection& selection) {
  std::vector<ClipCurve> clip;
  for (const Curve& c : curves) {
    ClipCurve cc;
    cc.name = c.name;
    for (const Key& k : c.keys) {
      if (selection.containsKey(c.id, k.id)) cc.keys.push_back(k);
    }
    if (!cc.keys.empty()) clip.push_back(std::move(cc));
  }
  return clip;
}

std::vector<uint32_t> pasteKeys(Curve& dest, const ClipCurve& clip, double at, PasteMode mode) {
  std::vector<uint32_t> created;
  if (clip.keys.empty() || !std::isfinite(at)) return created;
  std::vector<Key> keys = clip.keys;
  std::stable_sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return a.time < b.time; });
  const double shift = at - keys.front().time;
  const double end = keys.back().time + shift;
  double offset = 0.0;
  if (mode == PasteMode::Relative) offset = evaluate(dest, at) - keys.front().value;
  for (Key& k : keys) {
    k.time = clampCoordinate(k.time + shift);
    k.value = clampCoordinate(k.value + offset);
  }
  if (mode == PasteMode::Merge) {
    dest.keys.erase(std::remove_if(dest.keys.begin(), dest.keys.end(),
                                   [&](const Key& d) {
                                     return std::binary_search(keys.begin(), keys.end(), d, [](const Key& a, const Key& b) { return a.time < b.time; });
                                   }),
                    dest.keys.end());
  } else {
    dest.keys.erase(std::remove_if(dest.keys.begin(), dest.keys.end(), [&](const Key& d) { return d.time >= at && d.time <= end; }), dest.keys.end());
  }
  uint32_t id = nextKeyId(dest);
  for (Key& k : keys) {
    if (dest.keys.size() >= kMaxKeysPerCurve) break;
    k.id = id++;
    created.push_back(k.id);
    dest.keys.push_back(k);
  }
  std::stable_sort(dest.keys.begin(), dest.keys.end(), [](const Key& a, const Key& b) { return a.time < b.time; });
  return created;
}

}  // namespace r1ui::widgets::curve

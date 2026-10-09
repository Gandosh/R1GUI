// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CurveGraph.h except painting (CurveGraphPaint.cpp): hit testing, selection,
//   the pointer gesture state machine, keyboard commands, framing and the interaction (undo) protocol.
// Invariants: curves_ always satisfy curve::valid after every edit; an interaction snapshots each
//   curve once before its first change so Escape / capture loss restore exactly the start state; a
//   gesture ends with exactly one onEndInteraction when it began an interaction; callbacks may
//   destroy the widget (every callback site re-checks the id).
// Callers: CurveEditor, tests.
#include "r1ui/widgets/curveeditor/CurveGraph.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/colorpicker/NumberText.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

namespace events = core::events;
using curve::Curve;
using curve::Key;
using curve::Part;
using curve::Selected;

constexpr double kTinyMovement = 3.0;  // px before a dominant-axis lock engages

bool hasMod(uint8_t m, uint8_t bit) { return (m & bit) != 0; }

}  // namespace

// ---- geometry ---------------------------------------------------------------------------------

void CurveGraph::onAttached() {
  style().flexGrow = 1.0;
  style().minHeight = core::layout::Length::px(kRulerHeight + 40.0);
  setFocusable(true);
}

std::string_view CurveGraph::accessibleName() const {
  return WidgetObject::accessibleName().empty() ? std::string_view("Curve graph") : WidgetObject::accessibleName();
}

double CurveGraph::plotWidth() const { return std::max(1.0, static_cast<double>(ui().absRect(id()).w)); }
double CurveGraph::plotHeight() const { return std::max(1.0, static_cast<double>(ui().absRect(id()).h) - kRulerHeight); }

curve::Mapping CurveGraph::plotMapping() const {
  curve::Mapping m;
  m.x = 0.0;
  m.y = kRulerHeight;
  m.w = plotWidth();
  m.h = plotHeight();
  m.view = view_;
  return m;
}

curve::Mapping CurveGraph::mapping() const { return plotMapping(); }

bool CurveGraph::curveDrawn(const Curve& c) const {
  if (!c.visible) return false;
  const bool anySolo = std::any_of(curves_.begin(), curves_.end(), [](const Curve& o) { return o.solo && o.visible; });
  return !anySolo || c.solo;
}

bool CurveGraph::curveEditable(const Curve& c) const { return curveDrawn(c) && !c.locked; }

double CurveGraph::valueStep() const {
  if (settings_.valueSnapStep > 0.0) return settings_.valueSnapStep;
  return curve::niceStep(view_.valueSpan() / 5.0) / 4.0;
}

// ---- data -------------------------------------------------------------------------------------

void CurveGraph::setCurves(std::vector<Curve> curves) {
  if (gesture_.kind != Gesture::Kind::None) cancelGesture();
  uint32_t nextId = 1;
  for (const Curve& c : curves) nextId = std::max(nextId, c.id + 1);
  std::vector<uint32_t> seen;
  for (Curve& c : curves) {
    if (c.id == 0 || std::find(seen.begin(), seen.end(), c.id) != seen.end()) c.id = nextId++;
    seen.push_back(c.id);
    curve::sanitize(c);
  }
  curves_ = std::move(curves);
  selection_.prune(curves_);
  if (hoverCurve_ != 0 && curve::findCurve(curves_, hoverCurve_) == nullptr) hoverCurve_ = 0;
  requestPaint();
}

void CurveGraph::setSelection(curve::Selection selection) {
  selection.prune(curves_);
  selection_ = std::move(selection);
  requestPaint();
}

void CurveGraph::setScrubTime(double t) {
  if (!std::isfinite(t)) return;
  scrubTime_ = curve::clampCoordinate(t);
  requestPaint();
}

void CurveGraph::setSettings(const CurveSettings& s) {
  settings_ = s;
  if (!std::isfinite(settings_.framesPerSecond) || settings_.framesPerSecond < 0.0) settings_.framesPerSecond = 30.0;
  settings_.wheelMultiplier = std::isnan(settings_.wheelMultiplier) ? 1.0 : std::clamp(settings_.wheelMultiplier, curve::kMinWheelMultiplier, curve::kMaxWheelMultiplier);
  requestPaint();
}

// ---- view -------------------------------------------------------------------------------------

void CurveGraph::setView(const curve::View& v) {
  view_ = curve::sanitizedView(v);
  requestPaint();
}

void CurveGraph::setViewInternal(const curve::View& v) {
  if (v == view_ || !curve::validView(v)) return;
  view_ = v;
  requestPaint();
  if (onViewChanged) onViewChanged();
}

namespace {

struct Bounds {
  bool any = false;
  double t0 = 0, t1 = 0, v0 = 0, v1 = 0;
  void add(double t, double v) {
    if (!any) {
      t0 = t1 = t;
      v0 = v1 = v;
      any = true;
      return;
    }
    t0 = std::min(t0, t);
    t1 = std::max(t1, t);
    v0 = std::min(v0, v);
    v1 = std::max(v1, v);
  }
};

}  // namespace

void CurveGraph::frameSelected() {
  Bounds b;
  const curve::KeyLookup lookup(curves_);
  for (const Selected& s : selection_.items()) {
    if (s.part != Part::Key) continue;
    const size_t i = lookup.indexOf(s.curve, s.key);
    if (i != curve::npos) b.add(lookup.curve(s.curve)->keys[i].time, lookup.curve(s.curve)->keys[i].value);
  }
  if (!b.any) {
    frameAll();
    return;
  }
  const double h = plotHeight();
  const curve::Range tr = curve::fitRange(b.t0, b.t1, h, curve::kFitMinTimeMargin, view_.timeSpan());
  const curve::Range vr = curve::fitRange(b.v0, b.v1, h, 0.0, view_.valueSpan());
  setViewInternal(curve::sanitizedView({tr.lo, tr.hi, vr.lo, vr.hi}));
}

void CurveGraph::frameAll() {
  Bounds b;
  for (const Curve& c : curves_) {
    if (!curveDrawn(c)) continue;
    for (const Key& k : c.keys) b.add(k.time, k.value);
  }
  if (!b.any) return;  // rule: fitting with no curves leaves the view unchanged
  const double h = plotHeight();
  const curve::Range tr = curve::fitRange(b.t0, b.t1, h, curve::kFitMinTimeMargin, view_.timeSpan());
  const curve::Range vr = curve::fitRange(b.v0, b.v1, h, 0.0, view_.valueSpan());
  setViewInternal(curve::sanitizedView({tr.lo, tr.hi, vr.lo, vr.hi}));
}

void CurveGraph::frameHorizontal() {
  Bounds b;
  for (const Curve& c : curves_) {
    if (!curveDrawn(c)) continue;
    for (const Key& k : c.keys) b.add(k.time, k.value);
  }
  if (!b.any) return;
  const curve::Range tr = curve::fitRange(b.t0, b.t1, plotHeight(), curve::kFitMinTimeMargin, view_.timeSpan());
  setViewInternal(curve::sanitizedView({tr.lo, tr.hi, view_.vMin, view_.vMax}));
}

void CurveGraph::frameVertical() {
  Bounds b;
  for (const Curve& c : curves_) {
    if (!curveDrawn(c)) continue;
    for (const Key& k : c.keys) b.add(k.time, k.value);
  }
  if (!b.any) return;
  const curve::Range vr = curve::fitRange(b.v0, b.v1, plotHeight(), 0.0, view_.valueSpan());
  setViewInternal(curve::sanitizedView({view_.tMin, view_.tMax, vr.lo, vr.hi}));
}

void CurveGraph::zoomWheel(double x, double y, double notches, uint8_t) {
  const curve::Mapping m = plotMapping();
  const double factor = curve::wheelZoomFactor(notches, settings_.wheelMultiplier);
  double anchorT = m.toTime(x);
  const double anchorV = m.toValue(y);
  if (settings_.zoomAtScrubTime && scrubTime_ >= view_.tMin && scrubTime_ <= view_.tMax) anchorT = scrubTime_;
  setViewInternal(curve::zoomAbout(view_, anchorT, anchorV, factor, factor, settings_.zoomLimits));
}

// ---- selection --------------------------------------------------------------------------------

void CurveGraph::notifySelection() {
  requestPaint();
  if (onSelectionChanged) onSelectionChanged();
}

void CurveGraph::setSelectionInternal(curve::Selection s) {
  if (s == selection_) return;
  selection_ = std::move(s);
  notifySelection();
}

void CurveGraph::applySelection(const Selected& item, uint8_t m) {
  curve::Selection next = selection_;
  if (hasMod(m, events::Mod::kShift)) next.add(item);
  else if (hasMod(m, events::Mod::kAlt)) next.remove(item);
  else if (hasMod(m, events::Mod::kCtrl)) next.toggle(item);
  else if (!next.contains(item)) next.set(item);
  setSelectionInternal(std::move(next));
}

void CurveGraph::selectAll() {
  curve::Selection s;
  for (const Curve& c : curves_) {
    if (!curveEditable(c)) continue;
    for (const Key& k : c.keys) s.add({c.id, k.id, Part::Key});
  }
  setSelectionInternal(std::move(s));
}

void CurveGraph::clearSelection() { setSelectionInternal({}); }

void CurveGraph::invertSelection() {
  curve::Selection s;
  for (const Curve& c : curves_) {
    if (!curveEditable(c)) continue;
    const bool hasSelected = std::any_of(c.keys.begin(), c.keys.end(), [&](const Key& k) { return selection_.containsKey(c.id, k.id); });
    if (!hasSelected) continue;  // rule 35: only curves that have a selection
    for (const Key& k : c.keys) {
      if (!selection_.containsKey(c.id, k.id)) s.add({c.id, k.id, Part::Key});
    }
  }
  setSelectionInternal(std::move(s));
}

void CurveGraph::selectAfterScrub() {
  curve::Selection s;
  for (const Curve& c : curves_) {
    if (!curveEditable(c)) continue;
    for (const Key& k : c.keys) {
      if (k.time >= scrubTime_) s.add({c.id, k.id, Part::Key});
    }
  }
  setSelectionInternal(std::move(s));
}

void CurveGraph::selectBeforeScrub() {
  curve::Selection s;
  for (const Curve& c : curves_) {
    if (!curveEditable(c)) continue;
    for (const Key& k : c.keys) {
      if (k.time <= scrubTime_) s.add({c.id, k.id, Part::Key});
    }
  }
  setSelectionInternal(std::move(s));
}

SelectionInfo CurveGraph::selectionInfo() const {
  SelectionInfo info;
  std::vector<uint32_t> curveIds;
  bool first = true;
  const curve::KeyLookup lookup(curves_);
  const bool keysSelected = selection_.hasKeys();
  uint32_t lastOwner = 0;
  uint32_t lastOwnerCurve = 0;
  for (const Selected& s : selection_.items()) {
    if (keysSelected ? s.part != Part::Key : (s.curve == lastOwnerCurve && s.key == lastOwner)) continue;
    lastOwnerCurve = s.curve;
    lastOwner = s.key;
    const size_t i = lookup.indexOf(s.curve, s.key);
    if (i == curve::npos) continue;
    const Curve* c = lookup.curve(s.curve);
    const Key& k = c->keys[i];
    if (std::find(curveIds.begin(), curveIds.end(), c->id) == curveIds.end()) curveIds.push_back(c->id);
    ++info.keyCount;
    if (first) {
      info.time = k.time;
      info.value = k.value;
      info.interp = k.interp;
      info.tangent = k.mode;
      info.weighted = k.weighted;
      first = false;
    } else {
      info.mixedTime |= k.time != info.time;
      info.mixedValue |= k.value != info.value;
      info.mixedInterp |= k.interp != info.interp;
      info.mixedTangent |= k.mode != info.tangent;
      info.mixedWeighted |= k.weighted != info.weighted;
    }
  }
  info.curveCount = curveIds.size();
  return info;
}

std::vector<uint32_t> CurveGraph::selectedCurveIds() const {
  std::vector<uint32_t> ids;
  for (const Selected& s : selection_.items()) {
    if (ids.empty() || ids.back() != s.curve) ids.push_back(s.curve);  // the selection is sorted by curve
  }
  return ids;
}

// ---- positions and hit testing ----------------------------------------------------------------

curve::Point CurveGraph::keyPosition(uint32_t curveId, uint32_t keyId) const {
  const Curve* c = curve::findCurve(curves_, curveId);
  if (c == nullptr) return {};
  const size_t i = curve::indexOfKey(*c, keyId);
  if (i == curve::npos) return {};
  const curve::Mapping m = plotMapping();
  return {m.toX(c->keys[i].time), m.toY(c->keys[i].value)};
}

bool CurveGraph::handlePosition(uint32_t curveId, uint32_t keyId, bool outSide, curve::Point& out) const {
  const Curve* c = curve::findCurve(curves_, curveId);
  if (c == nullptr) return false;
  const size_t i = curve::indexOfKey(*c, keyId);
  if (i == curve::npos) return false;
  const bool exists = outSide ? (i + 1 < c->keys.size() && c->keys[i].interp == curve::Interp::Cubic) : (i > 0 && c->keys[i - 1].interp == curve::Interp::Cubic);
  if (!exists) return false;
  const curve::Mapping m = plotMapping();
  const Key& k = c->keys[i];
  if (k.weighted) {
    curve::Bezier b;
    if (!curve::segmentBezier(*c, outSide ? i : i - 1, b)) return false;
    out = {m.toX(outSide ? b.x[1] : b.x[2]), m.toY(outSide ? b.y[1] : b.y[2])};
    return true;
  }
  const curve::Slopes s = curve::effectiveSlopes(*c, i);
  const double slope = outSide ? s.out : s.in;
  // Direction in pixels of one time unit along the tangent, normalised to the handle length.
  const double dx = 1.0 / m.timePerPixel();
  const double dy = -slope / m.valuePerPixel();
  const double len = std::hypot(dx, dy);
  if (!(len > 0.0) || !std::isfinite(len)) return false;
  const double sign = outSide ? 1.0 : -1.0;
  out = {m.toX(k.time) + sign * dx / len * kHandleLength, m.toY(k.value) + sign * dy / len * kHandleLength};
  return true;
}

Selected CurveGraph::hitItem(double x, double y) const {
  const curve::Mapping m = plotMapping();
  if (y < kRulerHeight) return {};
  Selected best;
  double bestDistance = 1e300;
  const double keyReach = kKeySize * 0.5 + kKeyHitSlop;
  for (auto it = curves_.rbegin(); it != curves_.rend(); ++it) {
    const Curve& c = *it;
    if (!curveEditable(c) || c.keys.empty()) continue;
    // Keys within reach in time.
    const double t0 = m.toTime(x - keyReach - kHandleLength);
    const double t1 = m.toTime(x + keyReach + kHandleLength);
    size_t i0 = curve::segmentIndex(c, t0);
    if (i0 == curve::npos) i0 = 0;
    for (size_t i = i0; i < c.keys.size() && c.keys[i].time <= t1; ++i) {
      const Key& k = c.keys[i];
      const bool keySelected = selection_.anyPartSelected(c.id, k.id);
      const bool handlesShown = settings_.tangents == TangentVisibility::All || (settings_.tangents == TangentVisibility::Selected && keySelected);
      if (handlesShown) {
        for (const bool out : {false, true}) {
          curve::Point p;
          if (!handlePosition(c.id, k.id, out, p)) continue;
          const double d = std::hypot(p.x - x, p.y - y);
          if (d <= kHandleHit && d < bestDistance) {
            bestDistance = d - 1e-6;  // handles win ties against a key at the same distance
            best = {c.id, k.id, out ? Part::Out : Part::In};
          }
        }
      }
      const double dx = std::fabs(m.toX(k.time) - x);
      const double dy = std::fabs(m.toY(k.value) - y);
      if (dx <= keyReach && dy <= keyReach) {
        const double d = std::hypot(dx, dy);
        if (d < bestDistance) {
          bestDistance = d;
          best = {c.id, k.id, Part::Key};
        }
      }
    }
  }
  return best;
}

uint32_t CurveGraph::hitCurve(double x, double y, double* timeOut, double* valueOut) const {
  if (y < kRulerHeight) return 0;
  const curve::Mapping m = plotMapping();
  const double t = m.toTime(x);
  if (timeOut) *timeOut = t;
  if (valueOut) *valueOut = m.toValue(y);
  uint32_t best = 0;
  double bestDistance = kHoverDistance + 1e-9;
  for (auto it = curves_.rbegin(); it != curves_.rend(); ++it) {
    const Curve& c = *it;
    if (!curveDrawn(c) || c.keys.empty()) continue;
    const double v = curve::evaluate(c, t);
    const double slope = curve::slopeAt(c, t);
    const double py = m.toY(v);
    // Perpendicular distance estimate for a slanted curve; a step edge counts through its key.
    const double slopePx = slope * m.timePerPixel() / m.valuePerPixel();
    double d = std::fabs(py - y) / std::sqrt(1.0 + slopePx * slopePx);
    const size_t i = curve::segmentIndex(c, t);
    for (size_t j = (i == curve::npos ? 0 : (i > 0 ? i - 1 : 0)); j < c.keys.size() && j <= (i == curve::npos ? 0 : i + 1); ++j) {
      const Key& k = c.keys[j];
      if (k.interp == curve::Interp::Constant && j + 1 < c.keys.size() && std::fabs(m.toX(c.keys[j + 1].time) - x) <= kHoverDistance) {
        const double lo = std::min(m.toY(k.value), m.toY(c.keys[j + 1].value));
        const double hi = std::max(m.toY(k.value), m.toY(c.keys[j + 1].value));
        if (y >= lo - kHoverDistance && y <= hi + kHoverDistance) d = std::min(d, std::fabs(m.toX(c.keys[j + 1].time) - x));
      }
    }
    if (d < bestDistance) {
      bestDistance = d;
      best = c.id;
    }
  }
  return best;
}

void CurveGraph::updateHover(double x, double y) {
  pointerX_ = x;
  pointerY_ = y;
  pointerInside_ = true;
  const uint32_t before = hoverCurve_;
  const Selected item = hitItem(x, y);
  hoverKeyCurve_ = item.curve;
  hoverKey_ = item.key;
  hoverCurve_ = item.curve != 0 ? item.curve : hitCurve(x, y);
  if (hoverCurve_ != before) requestPaint();
}

std::string_view CurveGraph::tooltipText() const {
  if (!settings_.curveTooltip || hoverCurve_ == 0 || !pointerInside_) return {};
  const Curve* c = curve::findCurve(curves_, hoverCurve_);
  if (c == nullptr) return {};
  const curve::Mapping m = plotMapping();
  const double t = m.toTime(pointerX_);
  tooltipScratch_ = c->name + ": " + formatNumber(curve::evaluate(*c, t), 4);
  return tooltipScratch_;
}

Cursor CurveGraph::cursor() const {
  switch (gesture_.kind) {
    case Gesture::Kind::Pan:
    case Gesture::Kind::MoveKeys:
    case Gesture::Kind::MoveHandle:
    case Gesture::Kind::InsertDrag:
      if (gesture_.active) return Cursor::Move;
      break;
    case Gesture::Kind::Zoom: return gesture_.active ? Cursor::ResizeNwSe : Cursor::Default;
    case Gesture::Kind::Scrub: return Cursor::ResizeHorizontal;
    default: break;
  }
  if (pointerInside_ && pointerY_ < kRulerHeight) return Cursor::ResizeHorizontal;
  return hoverKeyCurve_ != 0 ? Cursor::Pointer : Cursor::Default;
}

// ---- interactions (undo protocol) -------------------------------------------------------------

void CurveGraph::beginInteraction(const std::string& label) {
  if (interactionOpen_) return;
  interactionOpen_ = true;
  snapshots_.clear();
  selectionAtStart_ = selection_;
  if (onBeginInteraction) onBeginInteraction(label);
}

void CurveGraph::touch(uint32_t curveId) {
  if (!interactionOpen_) return;
  for (const Curve& s : snapshots_) {
    if (s.id == curveId) return;
  }
  if (const Curve* c = curve::findCurve(curves_, curveId)) snapshots_.push_back(*c);
}

void CurveGraph::changedCurves(const std::vector<uint32_t>& ids) {
  requestPaint();
  if (onChanged) onChanged(ids);
}

void CurveGraph::endInteraction(bool committed) {
  if (!interactionOpen_) return;
  interactionOpen_ = false;
  std::vector<uint32_t> restored;
  if (!committed) {
    for (const Curve& s : snapshots_) {
      if (Curve* c = curve::findCurve(curves_, s.id)) {
        *c = s;
        restored.push_back(s.id);
      }
    }
    selection_ = selectionAtStart_;
    selection_.prune(curves_);
  } else {
    selection_.prune(curves_);
  }
  snapshots_.clear();
  requestPaint();
  const core::tree::WidgetId self = id();
  if (!restored.empty() && onChanged) onChanged(restored);
  if (!ui().alive(self)) return;
  if (onEndInteraction) onEndInteraction(committed);
  if (!ui().alive(self)) return;
  if (!committed) notifySelection();
}

void CurveGraph::editSelectedKeys(const char* label, const std::function<bool(Curve&, const std::vector<uint32_t>&)>& op, bool ownersToo) {
  struct Pending {
    size_t index;
    Curve edited;
  };
  std::vector<Pending> pending;
  for (size_t i = 0; i < curves_.size(); ++i) {
    const Curve& c = curves_[i];
    if (!curveEditable(c)) continue;
    const std::vector<uint32_t> ids = ownersToo ? selection_.keysOrOwnersOf(c.id) : selection_.keysOf(c.id);
    if (ids.empty()) continue;
    Curve work = c;
    if (!op(work, ids) || work == c) continue;
    pending.push_back({i, std::move(work)});
  }
  if (pending.empty()) return;
  const core::tree::WidgetId self = id();
  beginInteraction(label);
  if (!ui().alive(self)) return;
  std::vector<uint32_t> changed;
  for (Pending& p : pending) {
    touch(curves_[p.index].id);
    curves_[p.index] = std::move(p.edited);
    changed.push_back(curves_[p.index].id);
  }
  selection_.prune(curves_);
  changedCurves(changed);
  if (!ui().alive(self)) return;
  endInteraction(true);
}

// ---- commands ---------------------------------------------------------------------------------

void CurveGraph::addKeysAtScrubTime() {
  struct Added {
    size_t index;
    Curve edited;
    uint32_t key;
  };
  std::vector<Added> added;
  for (size_t i = 0; i < curves_.size(); ++i) {
    if (!curveEditable(curves_[i])) continue;
    Curve work = curves_[i];
    const double v = curve::evaluate(work, scrubTime_);
    const curve::InsertResult r = curve::insertKey(work, scrubTime_, v);
    if (r.keyId != 0 && !r.existed) added.push_back({i, std::move(work), r.keyId});
  }
  if (added.empty()) return;
  const core::tree::WidgetId self = id();
  beginInteraction("Insert keys");
  if (!ui().alive(self)) return;
  curve::Selection sel;
  std::vector<uint32_t> changed;
  for (Added& a : added) {
    touch(curves_[a.index].id);
    curves_[a.index] = std::move(a.edited);
    sel.add({curves_[a.index].id, a.key, Part::Key});
    changed.push_back(curves_[a.index].id);
  }
  selection_ = std::move(sel);
  changedCurves(changed);
  if (!ui().alive(self)) return;
  endInteraction(true);
  if (ui().alive(self)) notifySelection();
}

void CurveGraph::deleteSelected() {
  editSelectedKeys("Delete keys", [](Curve& c, const std::vector<uint32_t>& ids) { return curve::deleteKeys(c, ids) > 0; });
}

void CurveGraph::copySelected() { clipboard_ = curve::copyKeys(curves_, selection_); }

void CurveGraph::cutSelected() {
  copySelected();
  editSelectedKeys("Cut keys", [](Curve& c, const std::vector<uint32_t>& ids) { return curve::deleteKeys(c, ids) > 0; });
}

void CurveGraph::paste(curve::PasteMode mode) {
  if (clipboard_.empty()) return;  // rule: pasting nothing records no step
  // Destination curves: the selected editable ones in order (rule 72), or the curves with the clip's
  // names, or the first editable curve.
  std::vector<size_t> targets;
  for (const uint32_t cid : selectedCurveIds()) {
    for (size_t i = 0; i < curves_.size(); ++i) {
      if (curves_[i].id == cid && curveEditable(curves_[i])) targets.push_back(i);
    }
  }
  if (targets.empty()) {
    for (const curve::ClipCurve& cc : clipboard_) {
      for (size_t i = 0; i < curves_.size(); ++i) {
        if (curves_[i].name == cc.name && curveEditable(curves_[i])) targets.push_back(i);
      }
    }
  }
  if (targets.empty()) {
    for (size_t i = 0; i < curves_.size() && targets.empty(); ++i) {
      if (curveEditable(curves_[i])) targets.push_back(i);
    }
  }
  struct Pasted {
    size_t index;
    Curve edited;
    std::vector<uint32_t> keys;
  };
  std::vector<Pasted> pasted;
  for (size_t n = 0; n < targets.size() && n < clipboard_.size(); ++n) {
    // A clip curve goes to the destination with the same name when there is one, else by order.
    size_t dest = targets[n];
    for (const size_t t : targets) {
      if (curves_[t].name == clipboard_[n].name) dest = t;
    }
    Curve work = curves_[dest];
    std::vector<uint32_t> keys = curve::pasteKeys(work, clipboard_[n], scrubTime_, mode);
    if (keys.empty() || work == curves_[dest]) continue;
    pasted.push_back({dest, std::move(work), std::move(keys)});
  }
  if (pasted.empty()) return;
  const core::tree::WidgetId self = id();
  beginInteraction(mode == curve::PasteMode::Merge ? "Merge keys" : "Paste keys");
  if (!ui().alive(self)) return;
  curve::Selection sel;
  std::vector<uint32_t> changed;
  for (Pasted& p : pasted) {
    touch(curves_[p.index].id);
    curves_[p.index] = std::move(p.edited);
    for (const uint32_t k : p.keys) sel.add({curves_[p.index].id, k, Part::Key});
    changed.push_back(curves_[p.index].id);
  }
  selection_ = std::move(sel);
  changedCurves(changed);
  if (!ui().alive(self)) return;
  endInteraction(true);
  if (ui().alive(self)) notifySelection();
}

void CurveGraph::nudgeSelected(double dt, double dv, const char* label) {
  if (selection_.hasHandles() && !selection_.hasKeys()) {
    // Nudging handles: move each selected handle by the offset in data units.
    struct Pending {
      size_t index;
      Curve edited;
    };
    std::vector<Pending> pending;
    const curve::Mapping m = plotMapping();
    for (size_t i = 0; i < curves_.size(); ++i) {
      if (!curveEditable(curves_[i])) continue;
      Curve work = curves_[i];
      bool any = false;
      for (const Selected& s : selection_.items()) {
        if (s.curve != work.id || s.part == Part::Key) continue;
        const size_t k = curve::indexOfKey(work, s.key);
        curve::Point p;
        if (k == curve::npos || !handlePosition(s.curve, s.key, s.part == Part::Out, p)) continue;
        // The handle as drawn, moved by (dt, dv) in data units, gives the new tangent.
        const double ht = m.toTime(p.x) + dt - work.keys[k].time;
        const double hv = m.toValue(p.y) + dv - work.keys[k].value;
        any |= curve::setTangentFromHandle(work, s.key, s.part == Part::Out, ht, hv);
      }
      if (any && !(work == curves_[i])) pending.push_back({i, std::move(work)});
    }
    if (pending.empty()) return;
    const core::tree::WidgetId self = id();
    beginInteraction(label);
    if (!ui().alive(self)) return;
    std::vector<uint32_t> changed;
    for (Pending& p : pending) {
      touch(curves_[p.index].id);
      curves_[p.index] = std::move(p.edited);
      changed.push_back(curves_[p.index].id);
    }
    changedCurves(changed);
    if (ui().alive(self)) endInteraction(true);
    return;
  }
  editSelectedKeys(label, [&](Curve& c, const std::vector<uint32_t>& ids) { return curve::translateKeys(c, ids, dt, dv) >= 0 && !ids.empty(); });
}

void CurveGraph::setSelectedInterpolation(curve::Interp interp) {
  editSelectedKeys(interp == curve::Interp::Constant ? "Constant interpolation" : interp == curve::Interp::Linear ? "Linear interpolation" : "Cubic interpolation",
                   [&](Curve& c, const std::vector<uint32_t>& ids) { return curve::setInterpolation(c, ids, interp) > 0; }, true);
}

void CurveGraph::setSelectedTangentMode(curve::TangentMode mode) {
  editSelectedKeys("Set tangent mode", [&](Curve& c, const std::vector<uint32_t>& ids) {
    const size_t a = curve::setInterpolation(c, ids, curve::Interp::Cubic);
    const size_t b = curve::setTangentMode(c, ids, mode);
    return a + b > 0;
  }, true);
}

void CurveGraph::toggleWeights() {
  const SelectionInfo info = selectionInfo();
  const bool target = info.mixedWeighted ? true : !info.weighted;
  editSelectedKeys("Toggle tangent weights", [&](Curve& c, const std::vector<uint32_t>& ids) { return curve::setWeighted(c, ids, target) > 0; }, true);
}

void CurveGraph::flattenSelectedTangents() {
  editSelectedKeys("Flatten tangents", [&](Curve& c, const std::vector<uint32_t>& ids) {
    std::vector<Selected> parts;
    for (const Selected& s : selection_.items()) {
      if (s.curve == c.id) parts.push_back(s);
    }
    (void)ids;
    return curve::flattenTangents(c, parts) > 0;
  }, true);
}

void CurveGraph::straightenSelectedTangents() {
  editSelectedKeys("Straighten tangents", [&](Curve& c, const std::vector<uint32_t>&) {
    std::vector<Selected> parts;
    for (const Selected& s : selection_.items()) {
      if (s.curve == c.id) parts.push_back(s);
    }
    return curve::straightenTangents(c, parts) > 0;
  }, true);
}

void CurveGraph::snapSelectedToFrames() {
  editSelectedKeys("Snap keys to frames", [&](Curve& c, const std::vector<uint32_t>& ids) { return curve::snapKeysToFrames(c, ids, settings_.framesPerSecond) > 0; });
}

void CurveGraph::flipSelectedCurvesHorizontal() {
  editSelectedKeys("Flip horizontally", [](Curve& c, const std::vector<uint32_t>&) { return curve::flipHorizontal(c); });
}

void CurveGraph::flipSelectedCurvesVertical() {
  editSelectedKeys("Flip vertically", [](Curve& c, const std::vector<uint32_t>&) { return curve::flipVertical(c); });
}

void CurveGraph::setSelectedTime(double time) {
  if (!std::isfinite(time)) return;
  editSelectedKeys("Set key time", [&](Curve& c, const std::vector<uint32_t>& ids) {
    // The first selected key takes the typed time and the others keep their distance to it.
    if (ids.empty()) return false;
    const size_t first = curve::indexOfKey(c, ids.front());
    if (first == curve::npos) return false;
    return curve::translateKeys(c, ids, curve::clampCoordinate(time) - c.keys[first].time, 0.0) >= 0;
  });
}

void CurveGraph::setSelectedValue(double value) {
  if (!std::isfinite(value)) return;
  editSelectedKeys("Set key value", [&](Curve& c, const std::vector<uint32_t>& ids) {
    bool any = false;
    for (curve::Key& k : c.keys) {
      if (!std::binary_search(ids.begin(), ids.end(), k.id)) continue;  // the ids come sorted from the selection
      k.value = curve::clampCoordinate(value);
      any = true;
    }
    return any;
  });
}

void CurveGraph::setExtrapolation(uint32_t curveId, bool post, curve::Extrapolation mode) {
  const Curve* c = curve::findCurve(curves_, curveId);
  if (c == nullptr || c->locked) return;
  if ((post ? c->post : c->pre) == mode) return;
  const core::tree::WidgetId self = id();
  beginInteraction(post ? "Set post extrapolation" : "Set pre extrapolation");
  if (!ui().alive(self)) return;
  touch(curveId);
  Curve* m = curve::findCurve(curves_, curveId);
  (post ? m->post : m->pre) = mode;
  changedCurves({curveId});
  if (ui().alive(self)) endInteraction(true);
}

void CurveGraph::matchLoop(uint32_t curveId, bool fromStart) {
  Curve* c = curve::findCurve(curves_, curveId);
  if (c == nullptr || c->locked) return;
  Curve work = *c;
  if (!curve::matchLoopTangents(work, fromStart) || work == *c) return;
  const core::tree::WidgetId self = id();
  beginInteraction("Match loop tangents");
  if (!ui().alive(self)) return;
  touch(curveId);
  *curve::findCurve(curves_, curveId) = std::move(work);
  changedCurves({curveId});
  if (ui().alive(self)) endInteraction(true);
}

// ---- insertion ---------------------------------------------------------------------------------

void CurveGraph::insertAt(double x, double y, bool keepShape, bool beginDrag) {
  const uint32_t cid = hitCurve(x, y);
  Curve* c = curve::findCurve(curves_, cid);
  if (c == nullptr || !curveEditable(*c)) return;
  const curve::Mapping m = plotMapping();
  double t = m.toTime(x);
  double v = m.toValue(y);
  if (settings_.snapTime) t = curve::snapToFrame(t, settings_.framesPerSecond);
  if (settings_.snapValue) v = curve::snapToStep(v, valueStep());
  Curve work = *c;
  const curve::InsertResult r = keepShape ? curve::insertKeyKeepingShape(work, t, v) : curve::insertKey(work, t, v);
  if (r.keyId == 0) return;
  if (r.existed) {
    setSelectionInternal([&] {
      curve::Selection s;
      s.set({cid, r.keyId, Part::Key});
      return s;
    }());
    return;
  }
  const core::tree::WidgetId self = id();
  beginInteraction(beginDrag ? "Insert and move key" : "Insert key");
  if (!ui().alive(self)) return;
  touch(cid);
  *curve::findCurve(curves_, cid) = std::move(work);
  curve::Selection s;
  s.set({cid, r.keyId, Part::Key});
  selection_ = std::move(s);
  changedCurves({cid});
  if (ui().alive(self)) notifySelection();
}

void CurveGraph::onDoubleClick(Event& e) {
  if (e.button != events::Button::Left) return;
  const Selected hit = hitItem(e.localX, e.localY);
  if (hit.curve != 0) return;
  e.markHandled();
  const core::tree::WidgetId self = id();
  insertAt(e.localX, e.localY, hasMod(e.modifiers, events::Mod::kCtrl), false);
  if (ui().alive(self) && interactionOpen_) endInteraction(true);
}

// ---- pointer ----------------------------------------------------------------------------------

bool CurveGraph::dragExceeded(double x, double y) const {
  const double threshold = ui().router().config().dragThreshold;
  return std::hypot(x - gesture_.startX, y - gesture_.startY) > threshold;
}

void CurveGraph::onPointerLeave(Event&) {
  pointerInside_ = false;
  if (hoverCurve_ != 0 || hoverKeyCurve_ != 0) requestPaint();
  hoverCurve_ = 0;
  hoverKeyCurve_ = 0;
  hoverKey_ = 0;
}

void CurveGraph::onPointerDown(Event& e) {
  if (gesture_.kind != Gesture::Kind::None) return;  // a second button while one is held
  const curve::Mapping m = plotMapping();
  Gesture g;
  g.button = e.button;
  g.startX = e.localX;
  g.startY = e.localY;
  g.startTime = m.toTime(e.localX);
  g.startValue = m.toValue(e.localY);
  g.modifiers = e.modifiers;
  g.startView = view_;
  if (!focused()) ui().router().focus(id(), events::FocusReason::Pointer);

  if (e.button == events::Button::Left) {
    if (e.localY < kRulerHeight) {
      g.kind = Gesture::Kind::Scrub;
      g.active = true;
      gesture_ = g;
      double t = g.startTime;
      if (settings_.snapTime) t = curve::snapToFrame(t, settings_.framesPerSecond);
      scrubTime_ = curve::clampCoordinate(t);
      requestPaint();
      if (onScrubChanged) onScrubChanged(scrubTime_);
    } else {
      const Selected hit = hitItem(e.localX, e.localY);
      if (hit.curve != 0) {
        g.kind = Gesture::Kind::Press;
        g.item = hit;
        g.itemWasSelected = selection_.contains(hit);
        gesture_ = g;
        applySelection(hit, e.modifiers);
      } else {
        g.kind = Gesture::Kind::Marquee;
        g.marqueeX = e.localX;
        g.marqueeY = e.localY;
        gesture_ = g;
      }
    }
  } else if (e.button == events::Button::Middle) {
    if (hasMod(e.modifiers, events::Mod::kAlt)) {
      g.kind = Gesture::Kind::Pan;
      gesture_ = g;
    } else {
      const Selected hit = hitItem(e.localX, e.localY);
      if (hit.curve != 0) {
        g.kind = Gesture::Kind::Press;
        g.item = hit;
        g.itemWasSelected = selection_.contains(hit);
        gesture_ = g;
        applySelection(hit, e.modifiers);
      } else if (hitCurve(e.localX, e.localY) != 0) {
        g.kind = Gesture::Kind::InsertDrag;  // pending until the threshold or the release
        gesture_ = g;
      } else {
        return;
      }
    }
  } else if (e.button == events::Button::Right) {
    g.kind = hasMod(e.modifiers, events::Mod::kAlt) ? Gesture::Kind::Zoom : Gesture::Kind::Pan;
    g.modifiers = e.modifiers;
    gesture_ = g;
  } else {
    return;
  }
  e.markHandled();
  ui().router().capturePointer(id());
}

void CurveGraph::startKeyDrag() {
  // Pressed key or handle: begin the interaction and snapshot every curve that has a selected part.
  Gesture& g = gesture_;
  const curve::Mapping m = plotMapping();
  if (g.item.part == Part::Key) {
    g.kind = Gesture::Kind::MoveKeys;
    const Curve* c = curve::findCurve(curves_, g.item.curve);
    const size_t i = c != nullptr ? curve::indexOfKey(*c, g.item.key) : curve::npos;
    if (i == curve::npos) {
      cancelGesture();
      return;
    }
    g.itemStart = {c->keys[i].time, c->keys[i].value};
    beginInteraction("Move keys");
    for (const uint32_t cid : selectedCurveIds()) touch(cid);
    dragBase_ = snapshots_;
    dragSelection_ = selection_;
  } else {
    g.kind = Gesture::Kind::MoveHandle;
    curve::Point p;
    if (!handlePosition(g.item.curve, g.item.key, g.item.part == Part::Out, p)) {
      cancelGesture();
      return;
    }
    const Curve* c = curve::findCurve(curves_, g.item.curve);
    const size_t i = curve::indexOfKey(*c, g.item.key);
    g.itemStart = {c->keys[i].time, c->keys[i].value};
    g.handleOffsetT = m.toTime(p.x) - m.toTime(g.startX);
    g.handleOffsetV = m.toValue(p.y) - m.toValue(g.startY);
    beginInteraction("Move tangent");
    touch(g.item.curve);
    dragBase_ = snapshots_;
    dragSelection_ = selection_;
  }
  g.active = true;
}

void CurveGraph::updateKeyDrag(double x, double y, uint8_t modifiers) {
  Gesture& g = gesture_;
  const curve::Mapping m = plotMapping();
  double dt = m.toTime(x) - g.startTime;
  double dv = m.toValue(y) - g.startValue;
  // Axis constraints: a chosen lock applies always; Shift engages the dominant axis once the
  // movement exceeds a tiny threshold and keeps it until Shift is released (rules 46, 47).
  const bool shift = hasMod(modifiers, events::Mod::kShift);
  int axis = 0;
  if (settings_.axisLock == AxisLock::Horizontal) axis = 1;
  else if (settings_.axisLock == AxisLock::Vertical) axis = 2;
  else if (shift) {
    if (!g.shiftWasDown) g.axis = 0;
    if (g.axis == 0 && std::hypot(x - g.startX, y - g.startY) > kTinyMovement) g.axis = std::fabs(x - g.startX) >= std::fabs(y - g.startY) ? 1 : 2;
    axis = g.axis;
  } else {
    g.axis = 0;
  }
  g.shiftWasDown = shift;
  if (axis == 1) dv = 0.0;
  if (axis == 2) dt = 0.0;
  // Snapping applies to the pressed key: its new position lands on the grid and the group follows.
  if (settings_.snapTime && axis != 2) dt = curve::snapToFrame(g.itemStart.x + dt, settings_.framesPerSecond) - g.itemStart.x;
  if (settings_.snapValue && axis != 1) dv = curve::snapToStep(g.itemStart.y + dv, valueStep()) - g.itemStart.y;

  std::vector<uint32_t> changed;
  for (const Curve& snap : dragBase_) {
    Curve* c = curve::findCurve(curves_, snap.id);
    if (c == nullptr) continue;
    *c = snap;
    const std::vector<uint32_t> ids = dragSelection_.keysOf(snap.id);
    if (!ids.empty()) curve::translateKeys(*c, ids, dt, dv);
    changed.push_back(snap.id);
  }
  changedCurves(changed);
}

void CurveGraph::updateHandleDrag(double x, double y) {
  Gesture& g = gesture_;
  const curve::Mapping m = plotMapping();
  const double ht = m.toTime(x) + g.handleOffsetT;
  const double hv = m.toValue(y) + g.handleOffsetV;
  for (const Curve& snap : dragBase_) {
    if (snap.id != g.item.curve) continue;
    Curve* c = curve::findCurve(curves_, snap.id);
    if (c == nullptr) return;
    *c = snap;
    curve::setTangentFromHandle(*c, g.item.key, g.item.part == Part::Out, ht - g.itemStart.x, hv - g.itemStart.y);
    changedCurves({snap.id});
    return;
  }
}

void CurveGraph::onPointerMove(Event& e) {
  const core::tree::WidgetId self = id();
  Gesture& g = gesture_;
  if (g.kind == Gesture::Kind::None) {
    if (g.kind == Gesture::Kind::None) updateHover(e.localX, e.localY);
    return;
  }
  pointerX_ = e.localX;
  pointerY_ = e.localY;
  switch (g.kind) {
    case Gesture::Kind::Press:
      if (!g.active && dragExceeded(e.localX, e.localY) && selection_.contains(g.item)) {
        startKeyDrag();
        if (!ui().alive(self)) return;
      }
      if (g.active) {
        if (g.kind == Gesture::Kind::MoveKeys) updateKeyDrag(e.localX, e.localY, e.modifiers);
        else if (g.kind == Gesture::Kind::MoveHandle) updateHandleDrag(e.localX, e.localY);
      }
      break;
    case Gesture::Kind::MoveKeys: updateKeyDrag(e.localX, e.localY, e.modifiers); break;
    case Gesture::Kind::MoveHandle: updateHandleDrag(e.localX, e.localY); break;
    case Gesture::Kind::InsertDrag:
      if (!g.active && dragExceeded(e.localX, e.localY)) {
        // The threshold is passed: insert the key where the press happened and drag it.
        insertAt(g.startX, g.startY, hasMod(g.modifiers, events::Mod::kCtrl), true);
        if (!ui().alive(self)) return;
        if (interactionOpen_ && !selection_.empty()) {
          g.item = selection_.items().front();
          g.kind = Gesture::Kind::MoveKeys;
          g.active = true;
          const Curve* c = curve::findCurve(curves_, g.item.curve);
          const size_t i = curve::indexOfKey(*c, g.item.key);
          g.itemStart = {c->keys[i].time, c->keys[i].value};
          // The inserted key is the start state of the drag (Escape still restores the curve without it,
          // from the snapshot taken before the insertion).
          dragBase_.assign(1, *c);
          dragSelection_ = selection_;
          const curve::Mapping m = plotMapping();
          g.startTime = m.toTime(g.startX);
          g.startValue = m.toValue(g.startY);
        } else {
          cancelGesture();
        }
      }
      break;
    case Gesture::Kind::Marquee:
      if (!g.active && dragExceeded(e.localX, e.localY)) g.active = true;
      if (g.active) {
        g.marqueeX = e.localX;
        g.marqueeY = e.localY;
        requestPaint();
      }
      break;
    case Gesture::Kind::Pan:
      if (!g.active && dragExceeded(e.localX, e.localY)) g.active = true;
      if (g.active) setViewInternal(curve::panBy(g.startView, e.localX - g.startX, e.localY - g.startY, plotWidth(), plotHeight()));
      break;
    case Gesture::Kind::Zoom:
      if (!g.active && dragExceeded(e.localX, e.localY)) g.active = true;
      if (g.active) {
        // Right: zoom in on time, up: zoom in on value (drag up zooms in, D17); about the press point.
        const double ft = std::pow(0.99, e.localX - g.startX);
        const double fv = std::pow(0.99, g.startY - e.localY);
        const curve::Mapping m0{0.0, kRulerHeight, plotWidth(), plotHeight(), g.startView};
        setViewInternal(curve::zoomAbout(g.startView, m0.toTime(g.startX), m0.toValue(g.startY), ft, fv, settings_.zoomLimits));
      }
      break;
    case Gesture::Kind::Scrub: {
      double t = plotMapping().toTime(e.localX);
      if (settings_.snapTime) t = curve::snapToFrame(t, settings_.framesPerSecond);
      t = curve::clampCoordinate(t);
      if (t != scrubTime_) {
        scrubTime_ = t;
        requestPaint();
        if (onScrubChanged) onScrubChanged(t);
      }
      break;
    }
    default: break;
  }
}

void CurveGraph::applyMarquee(uint8_t mods) {
  const Gesture& g = gesture_;
  const double x0 = std::min(g.startX, g.marqueeX);
  const double x1 = std::max(g.startX, g.marqueeX);
  const double y0 = std::min(g.startY, g.marqueeY);
  const double y1 = std::max(g.startY, g.marqueeY);
  const curve::Mapping m = plotMapping();
  const bool handles = selection_.hasHandles() && !selection_.hasKeys();  // rule 34: prefer the kind selected
  std::vector<Selected> hits;
  for (const Curve& c : curves_) {
    if (!curveEditable(c)) continue;
    size_t i0 = curve::segmentIndex(c, m.toTime(x0));
    if (i0 == curve::npos) i0 = 0;
    for (size_t i = i0; i < c.keys.size() && m.toX(c.keys[i].time) <= x1; ++i) {
      const Key& k = c.keys[i];
      const double px = m.toX(k.time);
      const double py = m.toY(k.value);
      if (!handles) {
        if (px >= x0 && px <= x1 && py >= y0 && py <= y1) hits.push_back({c.id, k.id, Part::Key});
      }
    }
    if (handles) {
      for (const Key& k : c.keys) {
        for (const bool out : {false, true}) {
          curve::Point p;
          if (!handlePosition(c.id, k.id, out, p)) continue;
          if (p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1) hits.push_back({c.id, k.id, out ? Part::Out : Part::In});
        }
      }
    }
  }
  curve::Selection next = hasMod(mods, events::Mod::kShift) || hasMod(mods, events::Mod::kAlt) || hasMod(mods, events::Mod::kCtrl) ? selection_ : curve::Selection{};
  for (const Selected& s : hits) {
    if (hasMod(mods, events::Mod::kShift)) next.add(s);
    else if (hasMod(mods, events::Mod::kAlt)) next.remove(s);
    else if (hasMod(mods, events::Mod::kCtrl)) next.toggle(s);
    else next.add(s);
  }
  setSelectionInternal(std::move(next));
}

void CurveGraph::finishGesture(Event* e) {
  Gesture g = gesture_;
  gesture_ = Gesture{};
  const core::tree::WidgetId self = id();
  switch (g.kind) {
    case Gesture::Kind::Press: {
      const bool plain = !hasMod(g.modifiers, events::Mod::kShift) && !hasMod(g.modifiers, events::Mod::kAlt) && !hasMod(g.modifiers, events::Mod::kCtrl);
      // A click on a selected key without dragging makes it the only selection (rule 30); the selection
      // was kept on press so that a group could be dragged.
      if (plain && selection_.contains(g.item) && selection_.size() > 1) {
        curve::Selection s;
        s.set(g.item);
        setSelectionInternal(std::move(s));
      }
      break;
    }
    case Gesture::Kind::MoveKeys:
    case Gesture::Kind::MoveHandle: endInteraction(true); break;
    case Gesture::Kind::InsertDrag:
      if (!g.active) {
        // A plain middle click: insert at the press position.
        insertAt(g.startX, g.startY, hasMod(g.modifiers, events::Mod::kCtrl), false);
        if (ui().alive(self) && interactionOpen_) endInteraction(true);
      } else {
        endInteraction(true);
      }
      break;
    case Gesture::Kind::Marquee:
      if (g.active) {
        gesture_ = g;  // applyMarquee reads the rectangle
        applyMarquee(g.modifiers);
        gesture_ = Gesture{};
      } else if (g.modifiers == 0) {
        clearSelection();  // rule 32: a click on empty space is a zero-length marquee
      }
      requestPaint();
      break;
    case Gesture::Kind::Pan:
    case Gesture::Kind::Zoom:
      if (!g.active && g.button == events::Button::Right && e != nullptr) {
        CurveContext ctx;
        const Selected hit = hitItem(g.startX, g.startY);
        ctx.x = e->x;
        ctx.y = e->y;
        ctx.time = g.startTime;
        ctx.value = g.startValue;
        if (hit.curve != 0) {
          ctx.target = CurveContext::Target::Key;
          ctx.curveId = hit.curve;
          ctx.keyId = hit.key;
          if (!selection_.contains(hit)) {
            curve::Selection s;
            s.set(hit);
            setSelectionInternal(std::move(s));
          }
        } else if (const uint32_t cid = hitCurve(g.startX, g.startY)) {
          ctx.target = CurveContext::Target::Curve;
          ctx.curveId = cid;
        }
        if (ui().alive(self) && onContextMenu) onContextMenu(ctx);
      }
      break;
    default: break;
  }
  if (ui().alive(self)) requestPaint();
}

void CurveGraph::onPointerUp(Event& e) {
  if (gesture_.kind == Gesture::Kind::None || e.button != gesture_.button) return;
  finishGesture(&e);
}

void CurveGraph::cancelGesture() {
  const bool open = interactionOpen_;
  const core::tree::WidgetId self = id();
  gesture_ = Gesture{};
  if (open) endInteraction(false);
  if (ui().alive(self)) requestPaint();
}

void CurveGraph::onCaptureLost(Event&) {
  if (gesture_.kind != Gesture::Kind::None) cancelGesture();
}

void CurveGraph::onPointerWheel(Event& e) {
  if (!std::isfinite(e.wheelY) || e.wheelY == 0.0) return;
  e.markHandled();
  zoomWheel(e.localX, e.localY, std::clamp(e.wheelY, -20.0, 20.0), e.modifiers);
}

// ---- keyboard ---------------------------------------------------------------------------------

void CurveGraph::onKeyDown(Event& e) {
  using events::Key;
  const bool ctrl = hasMod(e.modifiers, events::Mod::kCtrl) || hasMod(e.modifiers, events::Mod::kMeta);
  const bool shift = hasMod(e.modifiers, events::Mod::kShift);
  const bool alt = hasMod(e.modifiers, events::Mod::kAlt);
  const int code = static_cast<int>(e.key);
  const core::tree::WidgetId self = id();
  if (e.key == Key::Escape) {
    if (gesture_.kind != Gesture::Kind::None) {
      e.markHandled();
      cancelGesture();
      ui().router().releaseCapture();
      return;
    }
    if (!selection_.empty()) {
      e.markHandled();
      clearSelection();
    }
    return;
  }
  if (gesture_.kind != Gesture::Kind::None) {
    e.markHandled();  // other keys wait for the gesture to finish
    return;
  }
  bool used = true;
  if (ctrl && code == 'A') selectAll();
  else if (ctrl && code == 'D') clearSelection();
  else if (ctrl && code == 'I') invertSelection();
  else if (ctrl && code == 'C') copySelected();
  else if (ctrl && code == 'X') cutSelected();
  else if (ctrl && code == 'V') paste(shift ? curve::PasteMode::Merge : curve::PasteMode::Replace);
  else if (alt && code == 'V') paste(curve::PasteMode::Relative);
  else if (ctrl && code == 'W') toggleWeights();
  else if (ctrl && code == 'H') snapSelectedToFrames();
  else if (ctrl && (e.key == Key::Left || e.key == Key::Right)) {
    const double frame = settings_.framesPerSecond > 0.0 ? 1.0 / settings_.framesPerSecond : 1.0;
    nudgeSelected(e.key == Key::Left ? -frame : frame, 0.0, e.key == Key::Left ? "Translate keys left" : "Translate keys right");
  } else if (!ctrl && !alt && (e.key == Key::Left || e.key == Key::Right || e.key == Key::Up || e.key == Key::Down)) {
    const curve::Mapping m = plotMapping();
    const double px = shift ? 10.0 : 1.0;
    double dt = 0.0;
    double dv = 0.0;
    if (e.key == Key::Left) dt = -px * m.timePerPixel();
    if (e.key == Key::Right) dt = px * m.timePerPixel();
    if (e.key == Key::Up) dv = px * m.valuePerPixel();
    if (e.key == Key::Down) dv = -px * m.valuePerPixel();
    if (selection_.empty()) used = false;
    else nudgeSelected(dt, dv, "Nudge keys");
  } else if (e.key == Key::Delete || e.key == Key::Backspace) {
    if (selection_.hasKeys()) deleteSelected();
    else used = false;
  } else if (e.key == Key::Enter) addKeysAtScrubTime();
  else if (!ctrl && !alt && code == 'F') frameSelected();
  else if (!ctrl && !alt && code == 'B') {
    if (pointerInside_) {
      double t = plotMapping().toTime(pointerX_);
      if (settings_.snapTime) t = curve::snapToFrame(t, settings_.framesPerSecond);
      scrubTime_ = curve::clampCoordinate(t);
      requestPaint();
      if (onScrubChanged) onScrubChanged(scrubTime_);
    }
  } else if (!ctrl && !alt && code >= '0' && code <= '3') {
    setSelectedTangentMode(static_cast<curve::TangentMode>(code - '0'));
  } else if (!ctrl && !alt && code == '4') setSelectedInterpolation(curve::Interp::Linear);
  else if (!ctrl && !alt && code == '5') setSelectedInterpolation(curve::Interp::Constant);
  else if (!ctrl && !alt && code == '6') flattenSelectedTangents();
  else used = false;
  if (used && ui().alive(self)) e.markHandled();
}

}  // namespace r1ui::widgets

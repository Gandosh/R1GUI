// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: Engine::measure (cached dry sizing), Engine::commit (final sizing with clean-subtree
//   skipping) and the measured-leaf path that talks to the host's MeasureProvider.
// Why: see LayoutEngine.h. The cache is what keeps nested content-sized containers linear.
// Callers: FlexLayout.cpp, FlexContainer.cpp. Calls: runFlexContainer, MeasureProvider.
#include <algorithm>
#include <cmath>

#include "LayoutEngine.h"

namespace r1ui::core::layout::detail {

namespace {

// min wins over max, as in CSS.
double clampMinWins(double v, double lo, double hi) { return std::max(lo, std::min(v, hi)); }

double sanitizeExtent(double v) {
  if (std::isnan(v)) return 0.0;
  return std::clamp(v, 0.0, kMaxExtent);
}

}  // namespace

MeasureResult Engine::callMeasure(tree::WidgetId id, const MeasureInput& input) {
  ++stats_.measureCalls;
  if (provider_ == nullptr) return MeasureResult{};
  MeasureResult r = provider_->measure(id, input);
  r.width = sanitizeExtent(r.width);
  r.height = sanitizeExtent(r.height);
  return r;
}

// A leaf's content size comes from the host. Padding is added around it, AtMost caps it, and the
// widget's own min/max clamp it; if clamping changed the width, height is measured again at the
// clamped width because text height depends on the width it wraps at.
Size Engine::measureLeaf(tree::WidgetId id, const SizeConstraint& c) {
  const Style s = sanitizeStyle(tree_.get(id)->style);
  const double padW = s.padding[kLeft] + s.padding[kRight];
  const double padH = s.padding[kTop] + s.padding[kBottom];
  MeasureInput in;
  in.widthMode = c.w.mode;
  in.heightMode = c.h.mode;
  in.width = c.w.mode == MeasureMode::Undefined ? 0.0 : std::max(0.0, c.w.size - padW);
  in.height = c.h.mode == MeasureMode::Undefined ? 0.0 : std::max(0.0, c.h.size - padH);
  const MeasureResult first = callMeasure(id, in);

  auto resolve = [](const AxisConstraint& a, double pad, double content) {
    if (a.mode == MeasureMode::Exactly) return a.size;
    double v = content + pad;
    if (a.mode == MeasureMode::AtMost) v = std::min(v, a.size);
    const double lo = std::max(a.min, pad);
    return clampMinWins(v, lo, std::max(a.max, lo));
  };
  auto uncapped = [](const AxisConstraint& a, double pad, double content) {
    double v = content + pad;
    if (a.mode == MeasureMode::AtMost) v = std::min(v, a.size);
    return v;
  };

  Size out;
  out.w = resolve(c.w, padW, first.width);
  out.h = resolve(c.h, padH, first.height);
  if (c.w.mode != MeasureMode::Exactly && c.h.mode != MeasureMode::Exactly &&
      out.w != uncapped(c.w, padW, first.width)) {
    MeasureInput again = in;
    again.widthMode = MeasureMode::Exactly;
    again.width = std::max(0.0, out.w - padW);
    out.h = resolve(c.h, padH, callMeasure(id, again).height);
  }
  return out;
}

Size Engine::measure(tree::WidgetId id, const SizeConstraint& c) {
  tree::Widget* node = tree_.get(id);
  if (node == nullptr || node->style.display == Display::None) return Size{};
  if (c.w.mode == MeasureMode::Exactly && c.h.mode == MeasureMode::Exactly) {
    return Size{c.w.size, c.h.size};
  }
  NodeLayoutState& st = node->layoutState;
  // A clean node trusts any entry of the current epoch; a dirty one only entries filled in this
  // pass (its earlier entries describe the state before the change).
  const bool trusted = st.cacheEpoch == epoch_ && (!node->layoutDirty || st.cachePass == pass_);
  if (!trusted) {
    st.cacheCount = 0;
    st.cacheNext = 0;
    st.cacheEpoch = epoch_;
    st.cachePass = pass_;
  }
  for (int i = 0; i < st.cacheCount; ++i) {
    if (st.cache[i].key == c) {
      ++stats_.cacheHits;
      return Size{st.cache[i].width, st.cache[i].height};
    }
  }
  const Size result = node->style.hasMeasure ? measureLeaf(id, c) : runFlexContainer(*this, id, c, false);
  // runFlexContainer may have measured descendants but never touches this node's cache.
  st.cache[st.cacheNext] = NodeLayoutState::Entry{c, result.w, result.h};
  st.cacheNext = static_cast<uint8_t>((st.cacheNext + 1) % NodeLayoutState::kCacheSlots);
  if (st.cacheCount < NodeLayoutState::kCacheSlots) ++st.cacheCount;
  return result;
}

// Clears layout-dirty bits below (and optionally on) `id`. Subtrees that layout does not visit
// (display:none, children of a measured leaf) would otherwise stay dirty forever and make upward
// propagation stop at them.
void Engine::clearDirty(tree::WidgetId id, bool includeSelf) {
  std::vector<tree::WidgetId> stack;
  if (includeSelf) {
    stack.push_back(id);
  } else {
    for (tree::WidgetId c = tree_.firstChild(id); c.valid(); c = tree_.nextSibling(c)) stack.push_back(c);
  }
  while (!stack.empty()) {
    const tree::WidgetId cur = stack.back();
    stack.pop_back();
    if (tree::Widget* w = tree_.get(cur)) w->layoutDirty = false;
    for (tree::WidgetId c = tree_.firstChild(cur); c.valid(); c = tree_.nextSibling(c)) stack.push_back(c);
  }
}

void Engine::commit(tree::WidgetId id, double w, double h) {
  tree::Widget* node = tree_.get(id);
  if (node == nullptr) return;
  w = sanitizeExtent(w);
  h = sanitizeExtent(h);
  NodeLayoutState& st = node->layoutState;
  const bool hidden = node->style.display == Display::None;
  if (hidden) {
    w = 0.0;
    h = 0.0;
  }
  node->exact.w = w;
  node->exact.h = h;
  if (!node->layoutDirty && st.commitEpoch == epoch_ && st.commitWidth == w && st.commitHeight == h) {
    ++stats_.nodesSkipped;
    return;
  }
  if (hidden) {
    clearDirty(id, true);
  } else {
    if (node->style.hasMeasure) {
      clearDirty(id, false);
    } else {
      SizeConstraint c;
      c.w = AxisConstraint{MeasureMode::Exactly, w, 0.0, kMaxExtent};
      c.h = AxisConstraint{MeasureMode::Exactly, h, 0.0, kMaxExtent};
      runFlexContainer(*this, id, c, true);
    }
  }
  node->layoutDirty = false;
  st.commitEpoch = epoch_;
  st.commitWidth = w;
  st.commitHeight = h;
  st.committedThisPass = true;
  ++stats_.nodesCommitted;
}

}  // namespace r1ui::core::layout::detail

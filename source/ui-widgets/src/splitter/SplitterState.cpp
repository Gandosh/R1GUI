// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the explicit collapse / expand commands of Splitter and its persistence (state / restoreState).
// Invariants: collapse hands the pane's pixels to the nearest resizable neighbour, expand takes them back
//   from the pane that received them (or the nearest pane that can spare them, never below its minimum);
//   restoreState validates the whole state before changing anything.
// Callers: application shells, tests.
#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/splitter/Splitter.h"

namespace r1ui::widgets {

// ---- collapse -----------------------------------------------------------------------------------

bool Splitter::collapse(size_t index) {
  if (index >= panes_.size()) return false;
  Pane& p = panes_[index];
  if (!p.options.collapsible || !resizable(index) || p.collapsed) return false;
  const double size = sizeOf(index);
  if (size <= 0.0) return false;
  // The space goes to the nearest resizable pane that is not collapsed, after the pane first.
  size_t target = panes_.size();
  for (size_t i = index + 1; i < panes_.size() && target == panes_.size(); ++i) {
    if (resizable(i) && !panes_[i].collapsed) target = i;
  }
  for (size_t i = index; i-- > 0 && target == panes_.size();) {
    if (resizable(i) && !panes_[i].collapsed) target = i;
  }
  if (target == panes_.size()) return false;
  double total = 0.0;
  for (size_t i = 0; i < panes_.size(); ++i) {
    if (resizable(i) && !panes_[i].collapsed) total += sizeOf(i);
  }
  snapshotWeights();
  p.restoreSize = total > 0.0 ? size / total : 0.0;
  p.restoreTo = target;
  panes_[target].weight = std::max(panes_[target].weight + size, Splitter::kMinWeight);
  p.collapsed = true;
  changed();
  return true;
}

bool Splitter::expand(size_t index) {
  if (index >= panes_.size() || !panes_[index].collapsed) return false;
  double total = 0.0;
  for (size_t i = 0; i < panes_.size(); ++i) {
    if (resizable(i) && !panes_[i].collapsed) total += sizeOf(i);
  }
  const double want = std::max(panes_[index].restoreSize * total, floor_);
  // Take the space from the pane that received it, else from the nearest pane that can spare it.
  std::vector<size_t> order;
  order.push_back(panes_[index].restoreTo);
  for (size_t d = 1; d < panes_.size(); ++d) {
    if (index + d < panes_.size()) order.push_back(index + d);
    if (d <= index) order.push_back(index - d);
  }
  for (const size_t donor : order) {
    if (donor >= panes_.size() || donor == index || !resizable(donor) || panes_[donor].collapsed) continue;
    const double spare = sizeOf(donor) - effectiveMin(donor);
    if (spare < want - 1e-9 && spare < floor_) continue;
    const double take = std::min(want, spare);
    if (take <= 0.0) continue;
    snapshotWeights();
    panes_[donor].weight = std::max(panes_[donor].weight - take, Splitter::kMinWeight);
    panes_[index].weight = std::max(take, Splitter::kMinWeight);
    panes_[index].collapsed = false;
    changed();
    return true;
  }
  return false;
}

bool Splitter::toggleCollapse(size_t index) { return isCollapsed(index) ? expand(index) : collapse(index); }

// ---- persistence --------------------------------------------------------------------------------

SplitterState Splitter::state() const {
  SplitterState st;
  double total = 0.0;
  for (size_t i = 0; i < panes_.size(); ++i) {
    if (resizable(i) && !panes_[i].collapsed) total += sizeOf(i);
  }
  for (size_t i = 0; i < panes_.size(); ++i) {
    double ratio = 0.0;
    if (resizable(i)) {
      if (panes_[i].collapsed) ratio = panes_[i].restoreSize;
      else if (total > 0.0) ratio = sizeOf(i) / total;
      else ratio = panes_[i].weight;
    }
    st.ratios.push_back(ratio);
    st.collapsed.push_back(panes_[i].collapsed);
  }
  return st;
}

bool Splitter::restoreState(const SplitterState& state) {
  if (state.ratios.size() != panes_.size() || state.collapsed.size() != panes_.size()) return false;
  double sum = 0.0;
  for (size_t i = 0; i < panes_.size(); ++i) {
    const double r = state.ratios[i];
    if (!std::isfinite(r) || r < 0.0) return false;
    if (resizable(i) && !state.collapsed[i]) sum += r;
  }
  if (sum <= 0.0 && std::any_of(state.collapsed.begin(), state.collapsed.end(), [](bool c) { return !c; })) {
    bool anyResizable = false;
    for (size_t i = 0; i < panes_.size(); ++i) anyResizable = anyResizable || (resizable(i) && !state.collapsed[i]);
    if (anyResizable) return false;
  }
  for (size_t i = 0; i < panes_.size(); ++i) {
    Pane& p = panes_[i];
    if (!resizable(i)) continue;
    const bool collapse = state.collapsed[i] && p.options.collapsible;
    p.collapsed = collapse;
    if (collapse) {
      p.restoreSize = state.ratios[i];
      p.restoreTo = i;
    } else {
      p.weight = std::max(state.ratios[i], Splitter::kMinWeight);
    }
  }
  for (size_t i = 0; i < panes_.size(); ++i) {
    if (!panes_[i].collapsed) continue;
    // A restored collapse needs a receiver for expand(): the nearest resizable, uncollapsed pane.
    size_t target = i;
    for (size_t d = 1; d < panes_.size() && target == i; ++d) {
      if (i + d < panes_.size() && resizable(i + d) && !panes_[i + d].collapsed) target = i + d;
      else if (d <= i && resizable(i - d) && !panes_[i - d].collapsed) target = i - d;
    }
    panes_[i].restoreTo = target;
  }
  changed();
  return true;
}

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the public layout entry points: a full pass over a tree and an incremental pass over a
//   relayout boundary, producing rounded parent-space and absolute rectangles on every widget.
// Why: an own flexbox implementation (no third-party engine) whose results follow CSS Flexible
//   Box Layout semantics for the supported subset documented in Style.h.
// Callers: invalidation::Invalidator (incremental), hosts and tests (full). Calls: nothing
//   outside ui-core; text size arrives only through the MeasureProvider.
// Algorithm (per container): resolve each item's flex base size and clamped hypothetical size;
//   size the container (content-sized axes use max-content, or fit-content for AtMost); break
//   into lines (wrap); resolve flexible lengths with the CSS freeze loop (grow/shrink with
//   min/max clamping); size lines on the cross axis (align-content); align items (auto margins,
//   align-self, stretch); distribute main-axis free space (auto margins, justify-content);
//   mirror for reverse directions; place absolutely positioned children against the container
//   box. Percentages resolve against the container's inner size when it is definite and behave
//   as auto while a content-sized container is being sized (cyclic percent rule); they resolve
//   normally once the container size is known.
// Rounding policy: layout runs in doubles. Each node's absolute left/top/right/bottom EDGES are
//   rounded to whole pixels (half up); size = rounded right - rounded left. Because two
//   touching siblings share an exact edge they share a rounded edge, so there are never 1 px
//   gaps or overlaps and the rounding error of a row is spread over its items instead of
//   accumulating. The parent-space rect is the rounded absolute rect minus the parent's rounded
//   absolute origin.
// Robustness: styles are sanitised (Style.h); every size and coordinate is clamped to
//   +-kMaxExtent; measure results are sanitised; the tree is locked against mutation during a
//   pass; zero-size containers lay out to zero-size children without division by zero.
// Direction: left-to-right only; row = x axis, column = y axis.
// Stack: layout recurses once per tree level, about 0.5 KB per level (measured in the deep-nesting
//   test); with the tree depth cap of 512 that is roughly 0.25 MB. Per-container state lives on
//   the heap for this reason.
#pragma once

#include <cstddef>
#include <vector>

#include "r1ui/core/layout/Geometry.h"
#include "r1ui/core/layout/Measure.h"
#include "r1ui/core/tree/WidgetTree.h"

namespace r1ui::core::layout {

struct LayoutInput {
  double width = 0.0;   // viewport size given to the root; NaN/negative -> 0, clamped to kMaxExtent
  double height = 0.0;
};

struct LayoutStats {
  size_t nodesCommitted = 0;   // nodes whose children were (re)positioned
  size_t nodesSkipped = 0;     // clean nodes skipped because size and state were unchanged
  size_t measureCalls = 0;     // MeasureProvider::measure invocations
  size_t cacheHits = 0;        // dry measurements answered from the per-node cache
  size_t nodesRounded = 0;     // nodes visited by the rounding pass
};

// A widget whose absolute rectangle changed in a pass (before is empty for a first layout).
struct RectChange {
  tree::WidgetId id;
  Rect before;
  Rect after;
};

// True when a content change below `id` cannot change the widget's own size: it has fixed pixel
// width and height and has been laid out before. Invalidation stops upward propagation here.
bool isRelayoutBoundary(const tree::WidgetTree& tree, tree::WidgetId id);

// Lays out the whole subtree of `root` (a tree root) into `input`. All caches are discarded.
// `provider` may be null when no widget has Style::hasMeasure. Appends to `changes` if non-null.
LayoutStats layoutTree(tree::WidgetTree& tree, tree::WidgetId root, const LayoutInput& input,
                       MeasureProvider* provider, std::vector<RectChange>* changes = nullptr);

// Re-lays out only the subtree of a widget whose size is already known (a relayout boundary or a
// root that was laid out before), reusing the cached results of clean descendants. Returns false
// (and does nothing) for a stale id or a widget that has never been laid out.
bool layoutSubtree(tree::WidgetTree& tree, tree::WidgetId boundary, MeasureProvider* provider,
                   LayoutStats& stats, std::vector<RectChange>* changes = nullptr);

}  // namespace r1ui::core::layout

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: read-only questions about the widget tree that event routing needs: effective
//   visibility / enablement, topmost hit testing and Tab traversal order.
// Why: hit testing and focus order are pure functions of the tree and its layout results, so
//   they live outside the stateful Router and can be tested (and reused by tooltips, popups)
//   on their own.
// Callers: events::Router; later popup and tooltip managers. Calls: tree::WidgetTree only.
// Stacking: among siblings a higher `layer` is above a lower one; within a layer absolutely
//   positioned siblings are above in-flow ones; remaining ties follow sibling order (later is
//   above). Hit testing visits topmost first and returns the first hit.
// Hit rule: a widget is hit when it is shown (visible flag, display != none), enabled, not
//   hit-test-transparent, and the point lies inside its absolute rect intersected with the clip
//   rects of all its clipping ancestors. A disabled or hidden widget removes its whole subtree.
//   A transparent widget is skipped but its children are still tested.
// Cost: O(number of widgets visited); a clipping ancestor that does not contain the point prunes
//   its whole subtree.
#pragma once

#include <cstdint>

#include "r1ui/core/tree/WidgetTree.h"

namespace r1ui::core::events {

// True when the widget and all its ancestors are shown / enabled. False for a stale id.
bool isEffectivelyShown(const tree::WidgetTree& tree, tree::WidgetId id);
bool isEffectivelyEnabled(const tree::WidgetTree& tree, tree::WidgetId id);
// Alive, effectively shown and enabled, and flagged focusable.
bool isFocusable(const tree::WidgetTree& tree, tree::WidgetId id);

// Topmost hittable widget under (x, y) in the subtree of `root`; the invalid id when none.
tree::WidgetId hitTest(const tree::WidgetTree& tree, tree::WidgetId root, double x, double y);

// Next / previous widget in Tab order after `current` within the subtree of `root`, wrapping
// around. Order: tabIndex > 0 ascending (ties in document order), then tabIndex == 0 in
// document order; tabIndex < 0 widgets are skipped. A current widget that is not itself in the
// order (negative tabIndex, hidden) is positioned by its document position. Returns the invalid
// id when nothing is focusable.
tree::WidgetId nextFocusable(const tree::WidgetTree& tree, tree::WidgetId root, tree::WidgetId current,
                             bool backwards);

// Nearest widget from `id` upward (inclusive) that is focusable; invalid when none.
tree::WidgetId innermostFocusable(const tree::WidgetTree& tree, tree::WidgetId id);

// Deepest common ancestor-or-self of two widgets of the same tree; invalid when unrelated.
tree::WidgetId commonAncestor(const tree::WidgetTree& tree, tree::WidgetId a, tree::WidgetId b);

}  // namespace r1ui::core::events

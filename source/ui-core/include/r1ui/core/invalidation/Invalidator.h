// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: dirty tracking and the frame scheduler: layout-dirty / paint-dirty bits with upward
//   propagation, damage accumulation in absolute coordinates, animation tick requests, idle
//   detection and the incremental layout driver.
// Why: an idle UI must render nothing and a small change must redraw (and re-layout) only what
//   it affects. Widgets and the host call requestLayout / requestPaint instead of redrawing;
//   the host asks needsFrame(), and if true calls runFrame() and repaints the returned damage.
// Callers: widget code and the host loop (UI thread only). Calls: layout::layoutTree /
//   layoutSubtree, tree::WidgetTree.
// Contract: after changing anything that affects layout (style, content size, children) the
//   owner must call requestLayout on the widget whose own inputs changed; after changing only
//   appearance call requestPaint. Structural edits that matter for damage go through this class
//   (create, destroy, reparent, reorder, setVisible) so old and new areas are both damaged.
//   Mutating the tree directly is allowed but then the caller owns the matching requests.
// Layout propagation: requestLayout(w) marks w and its ancestors layout-dirty up to the first
//   relayout boundary (a laid-out widget with fixed pixel width and height, see
//   layout::isRelayoutBoundary) or the root. The boundary is queued; its subtree is re-laid out
//   alone, so a change inside a fixed-size panel never touches the panel's siblings or ancestors.
//   What forces more than that: a change to a widget's own size inputs re-lays out its parent
//   container (flex distribution, wrapping, justify and stretch make siblings depend on each
//   other); a content-sized (auto) chain climbs to the root; a viewport change, the first
//   layout, or requestFullLayout() runs a full pass that discards all caches. Clean subtrees
//   whose size did not change are skipped even inside a pass (no recommit, no re-measure).
// Paint propagation: requestPaint marks the widget paint-dirty and its ancestors
//   subtree-paint-dirty (stopping at an ancestor that already is) so a renderer can skip clean
//   subtrees; it adds the widget-local rectangle (default the whole widget), clipped by the
//   clipping ancestors and ignored if the widget is not shown, to the damage list using the
//   rectangles of the last layout. Layout changes add the old and new rectangle of every widget
//   whose absolute rectangle moved or resized.
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "r1ui/core/invalidation/DamageList.h"
#include "r1ui/core/layout/FlexLayout.h"
#include "r1ui/core/tree/WidgetTree.h"

namespace r1ui::core::invalidation {

struct FrameResult {
  layout::LayoutStats layoutStats;       // accumulated over the passes of this frame
  std::vector<layout::Rect> damage;      // absolute rectangles to repaint (empty = nothing)
  std::vector<tree::WidgetId> animating; // live widgets that asked for continuous frames
  bool layoutRan = false;
};

class Invalidator {
 public:
  explicit Invalidator(tree::WidgetTree& tree, size_t damageLimit = 8);

  // Registers a root with its viewport size (logical px). Calling again with a different size
  // schedules a full layout of that root.
  void setRoot(tree::WidgetId root, double width, double height);

  // ---- requests ----
  void requestLayout(tree::WidgetId widget);
  void requestFullLayout();
  void requestPaint(tree::WidgetId widget, std::optional<layout::Rect> localRect = std::nullopt);
  // Keeps frames coming until cancelAnimation(widget) (or the widget is destroyed).
  void requestAnimation(tree::WidgetId widget);
  void cancelAnimation(tree::WidgetId widget);

  // ---- structural edits with damage bookkeeping ----
  tree::CreateResult create(tree::WidgetId parent, tree::WidgetId before = tree::kNoWidget);
  tree::TreeError destroy(tree::WidgetId widget);
  tree::TreeError reparent(tree::WidgetId child, tree::WidgetId newParent, tree::WidgetId before = tree::kNoWidget);
  tree::TreeError reorder(tree::WidgetId child, tree::WidgetId before = tree::kNoWidget);
  void setVisible(tree::WidgetId widget, bool visible);

  // ---- frame ----
  // True when a frame has something to do: pending layout, accumulated damage or an animation.
  bool needsFrame() const;
  // Runs pending layout (incrementally), clears paint-dirty bits and returns the damage.
  FrameResult runFrame(layout::MeasureProvider* provider);

  const DamageList& pendingDamage() const { return damage_; }

 private:
  struct RootInfo {
    tree::WidgetId id;
    double width = 0.0;
    double height = 0.0;
    bool full = true;  // needs a full pass (first layout or viewport change)
  };

  RootInfo* findRoot(tree::WidgetId id);
  void queueLayoutRoot(tree::WidgetId id);
  void damageSubtree(tree::WidgetId id);
  layout::Rect clipOf(tree::WidgetId id) const;
  void addChanges(const std::vector<layout::RectChange>& changes);

  tree::WidgetTree& tree_;
  DamageList damage_;
  std::vector<RootInfo> roots_;
  std::vector<tree::WidgetId> pendingLayout_;  // boundaries / roots whose subtree must be re-laid out
  std::vector<tree::WidgetId> paintDirty_;
  std::vector<tree::WidgetId> animating_;
};

}  // namespace r1ui::core::invalidation

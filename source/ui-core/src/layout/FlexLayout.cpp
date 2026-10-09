// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the public layout entry points (full and incremental pass), the relayout-boundary test
//   and the edge-rounding pass that turns exact rectangles into pixel rectangles.
// Why: see FlexLayout.h (rounding policy, robustness). The rounding pass only descends into
//   subtrees that were recomputed or moved, so an incremental pass costs the dirty path.
// Callers: invalidation::Invalidator, hosts, tests. Calls: detail::Engine.
#include "r1ui/core/layout/FlexLayout.h"

#include <algorithm>
#include <cmath>

#include "LayoutEngine.h"

namespace r1ui::core::layout {

namespace {

// Rounds one coordinate half-up after clamping to the supported range.
double roundEdge(double v) { return std::floor(std::clamp(v, -kMaxExtent, kMaxExtent) + 0.5); }

double sanitizeViewport(double v) {
  if (std::isnan(v)) return 0.0;
  return std::clamp(v, 0.0, kMaxExtent);
}

int32_t toPixel(double v) { return static_cast<int32_t>(v); }  // v is integral and |v| <= kMaxExtent

struct RoundFrame {
  tree::WidgetId id;
  double parentX;
  double parentY;
  Rect parentRect;
};

}  // namespace

namespace detail {

void roundSubtree(tree::WidgetTree& tree, tree::WidgetId id, double parentExactX, double parentExactY,
                  const Rect& parentRect, LayoutStats& stats, std::vector<RectChange>* changes) {
  std::vector<RoundFrame> stack;
  stack.push_back(RoundFrame{id, parentExactX, parentExactY, parentRect});
  while (!stack.empty()) {
    const RoundFrame f = stack.back();
    stack.pop_back();
    tree::Widget* n = tree.get(f.id);
    if (n == nullptr) continue;
    const double ax = std::clamp(f.parentX + n->exact.x, -kMaxExtent, kMaxExtent);
    const double ay = std::clamp(f.parentY + n->exact.y, -kMaxExtent, kMaxExtent);
    const double l = roundEdge(ax);
    const double t = roundEdge(ay);
    const double r = roundEdge(ax + n->exact.w);
    const double b = roundEdge(ay + n->exact.h);
    const Rect abs{toPixel(l), toPixel(t), toPixel(r - l), toPixel(b - t)};
    n->rect = Rect{static_cast<int32_t>(int64_t{abs.x} - f.parentRect.x),
                   static_cast<int32_t>(int64_t{abs.y} - f.parentRect.y), abs.w, abs.h};
    if (changes != nullptr && n->absRect != abs) changes->push_back(RectChange{f.id, n->absRect, abs});
    const bool moved = n->exactAbsX != ax || n->exactAbsY != ay;
    n->absRect = abs;
    n->exactAbsX = ax;
    n->exactAbsY = ay;
    const bool descend = n->layoutState.committedThisPass || moved;
    n->layoutState.committedThisPass = false;
    ++stats.nodesRounded;
    if (!descend) continue;
    for (tree::WidgetId c = tree.firstChild(f.id); c.valid(); c = tree.nextSibling(c)) {
      stack.push_back(RoundFrame{c, ax, ay, abs});
    }
  }
}

}  // namespace detail

bool isRelayoutBoundary(const tree::WidgetTree& tree, tree::WidgetId id) {
  const tree::Widget* w = tree.get(id);
  if (w == nullptr || w->layoutState.commitEpoch == 0) return false;
  const Style s = sanitizeStyle(w->style);
  return s.width.kind == Length::Kind::Px && s.height.kind == Length::Kind::Px;
}

LayoutStats layoutTree(tree::WidgetTree& tree, tree::WidgetId root, const LayoutInput& input,
                       MeasureProvider* provider, std::vector<RectChange>* changes) {
  LayoutStats stats;
  tree::Widget* rootWidget = tree.get(root);
  if (rootWidget == nullptr || tree.parent(root).valid()) return stats;
  const tree::WidgetTree::MutationLock lock(tree);
  const uint32_t epoch = tree.bumpLayoutEpoch();
  detail::Engine engine(tree, provider, stats, epoch, tree.nextLayoutPass());
  rootWidget->exact.x = 0.0;
  rootWidget->exact.y = 0.0;
  engine.commit(root, sanitizeViewport(input.width), sanitizeViewport(input.height));
  detail::roundSubtree(tree, root, 0.0, 0.0, Rect{}, stats, changes);
  return stats;
}

bool layoutSubtree(tree::WidgetTree& tree, tree::WidgetId boundary, MeasureProvider* provider,
                   LayoutStats& stats, std::vector<RectChange>* changes) {
  tree::Widget* w = tree.get(boundary);
  if (w == nullptr || w->layoutState.commitEpoch == 0) return false;
  const tree::WidgetTree::MutationLock lock(tree);
  detail::Engine engine(tree, provider, stats, tree.layoutEpoch(), tree.nextLayoutPass());
  double parentX = 0.0;
  double parentY = 0.0;
  Rect parentRect;
  if (const tree::Widget* p = tree.get(tree.parent(boundary))) {
    parentX = p->exactAbsX;
    parentY = p->exactAbsY;
    parentRect = p->absRect;
  }
  engine.commit(boundary, w->exact.w, w->exact.h);
  detail::roundSubtree(tree, boundary, parentX, parentY, parentRect, stats, changes);
  return true;
}

}  // namespace r1ui::core::layout

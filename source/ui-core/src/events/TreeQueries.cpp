// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: effective-state queries, stacking-aware hit testing, Tab-order traversal and the common
//   ancestor query behind click synthesis.
// Why: see TreeQueries.h for the rules; this file implements them without recursion where depth
//   is unbounded (traversal) and with bounded recursion (<= kMaxSupportedDepth frames) for hit
//   testing, where early exit on the first topmost hit matters.
// Callers: events::Router, tests. Calls: tree::WidgetTree.
#include "r1ui/core/events/TreeQueries.h"

#include <algorithm>
#include <tuple>
#include <vector>

namespace r1ui::core::events {

using layout::Rect;
using tree::Widget;
using tree::WidgetId;
using tree::WidgetTree;

bool isEffectivelyShown(const WidgetTree& tree, WidgetId id) {
  const Widget* w = tree.get(id);
  if (w == nullptr) return false;
  for (WidgetId cur = id; cur.valid(); cur = tree.parent(cur)) {
    if (!tree.get(cur)->shown()) return false;
  }
  return true;
}

bool isEffectivelyEnabled(const WidgetTree& tree, WidgetId id) {
  if (!tree.alive(id)) return false;
  for (WidgetId cur = id; cur.valid(); cur = tree.parent(cur)) {
    if (!tree.get(cur)->flags.enabled) return false;
  }
  return true;
}

bool isFocusable(const WidgetTree& tree, WidgetId id) {
  const Widget* w = tree.get(id);
  return w != nullptr && w->flags.focusable && isEffectivelyShown(tree, id) &&
         isEffectivelyEnabled(tree, id);
}

WidgetId innermostFocusable(const WidgetTree& tree, WidgetId id) {
  for (WidgetId cur = id; cur.valid(); cur = tree.parent(cur)) {
    if (isFocusable(tree, cur)) return cur;
  }
  return tree::kNoWidget;
}

WidgetId commonAncestor(const WidgetTree& tree, WidgetId a, WidgetId b) {
  if (!tree.alive(a) || !tree.alive(b)) return tree::kNoWidget;
  uint32_t da = tree.depth(a);
  uint32_t db = tree.depth(b);
  while (da > db) {
    a = tree.parent(a);
    --da;
  }
  while (db > da) {
    b = tree.parent(b);
    --db;
  }
  while (a != b) {
    a = tree.parent(a);
    b = tree.parent(b);
    if (!a.valid() || !b.valid()) return tree::kNoWidget;  // different roots
  }
  return a;
}

// ---- hit testing ----

namespace {

constexpr Rect kUnbounded{-1000000000, -1000000000, 2000000000, 2000000000};

WidgetId hitNode(const WidgetTree& tree, WidgetId id, double x, double y, const Rect& clip) {
  const Widget* w = tree.get(id);
  if (w == nullptr || !w->shown() || !w->flags.enabled) return tree::kNoWidget;
  const Rect abs = w->absRect;
  const Rect visible = layout::intersect(clip, abs);
  const Rect childClip = w->clips() ? visible : clip;

  if (layout::containsPoint(childClip, x, y)) {
    bool plain = true;  // all children on layer 0 and in flow: plain reverse sibling order
    for (WidgetId c = tree.firstChild(id); c.valid(); c = tree.nextSibling(c)) {
      const Widget* cw = tree.get(c);
      if (cw->layer != 0 || cw->style.position == layout::Position::Absolute) {
        plain = false;
        break;
      }
    }
    if (plain) {
      for (WidgetId c = tree.lastChild(id); c.valid(); c = tree.prevSibling(c)) {
        const WidgetId hit = hitNode(tree, c, x, y, childClip);
        if (hit.valid()) return hit;
      }
    } else {
      // (layer, absolute, sibling order, id): sorted so the topmost child comes first.
      std::vector<std::tuple<int32_t, int, uint32_t, WidgetId>> order;
      uint32_t index = 0;
      for (WidgetId c = tree.firstChild(id); c.valid(); c = tree.nextSibling(c), ++index) {
        const Widget* cw = tree.get(c);
        order.emplace_back(cw->layer, cw->style.position == layout::Position::Absolute ? 1 : 0, index, c);
      }
      std::sort(order.begin(), order.end(), [](const auto& l, const auto& r) {
        return std::tie(std::get<0>(l), std::get<1>(l), std::get<2>(l)) >
               std::tie(std::get<0>(r), std::get<1>(r), std::get<2>(r));
      });
      for (const auto& entry : order) {
        const WidgetId hit = hitNode(tree, std::get<3>(entry), x, y, childClip);
        if (hit.valid()) return hit;
      }
    }
  }
  if (!w->flags.hitTestTransparent && layout::containsPoint(visible, x, y)) return id;
  return tree::kNoWidget;
}

}  // namespace

WidgetId hitTest(const WidgetTree& tree, WidgetId root, double x, double y) {
  return hitNode(tree, root, x, y, kUnbounded);
}

// ---- Tab order ----

namespace {

struct Candidate {
  int group;       // 0: positive tabIndex, 1: tabIndex 0
  int32_t tabIndex;
  uint32_t order;  // document (pre-order) position
  WidgetId id;
};

bool before(const Candidate& a, const Candidate& b) {
  return std::tie(a.group, a.tabIndex, a.order) < std::tie(b.group, b.tabIndex, b.order);
}

}  // namespace

WidgetId nextFocusable(const WidgetTree& tree, WidgetId root, WidgetId current, bool backwards) {
  if (!tree.alive(root)) return tree::kNoWidget;
  std::vector<Candidate> candidates;
  bool haveCurrent = false;
  Candidate cur{1, 0, 0, tree::kNoWidget};

  // Pre-order walk that skips hidden and disabled subtrees entirely.
  std::vector<WidgetId> stack{root};
  uint32_t order = 0;
  while (!stack.empty()) {
    const WidgetId id = stack.back();
    stack.pop_back();
    const Widget* w = tree.get(id);
    if (!w->shown() || !w->flags.enabled) continue;
    const uint32_t myOrder = order++;
    if (id == current) {
      haveCurrent = true;
      cur = Candidate{w->tabIndex > 0 ? 0 : 1, w->tabIndex > 0 ? w->tabIndex : 0, myOrder, id};
    }
    if (w->flags.focusable && w->tabIndex >= 0) {
      candidates.push_back(
          Candidate{w->tabIndex > 0 ? 0 : 1, w->tabIndex > 0 ? w->tabIndex : 0, myOrder, id});
    }
    for (WidgetId c = tree.lastChild(id); c.valid(); c = tree.prevSibling(c)) stack.push_back(c);
  }
  if (candidates.empty()) return tree::kNoWidget;
  std::sort(candidates.begin(), candidates.end(), before);
  if (!haveCurrent) return backwards ? candidates.back().id : candidates.front().id;

  if (!backwards) {
    for (const Candidate& c : candidates) {
      if (before(cur, c)) return c.id;
    }
    return candidates.front().id;  // wrap
  }
  for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
    if (before(*it, cur)) return it->id;
  }
  return candidates.back().id;  // wrap
}

}  // namespace r1ui::core::events

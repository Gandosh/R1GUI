// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: Invalidator request propagation, damage bookkeeping for structural edits, and the frame
//   driver that runs incremental layout and hands out the damage.
// Why: see Invalidator.h for the contract and propagation rules.
// Callers: widget code and the host loop. Calls: layout::layoutTree / layoutSubtree.
#include "r1ui/core/invalidation/Invalidator.h"

#include <algorithm>
#include <utility>

namespace r1ui::core::invalidation {

using layout::Rect;
using tree::kNoWidget;
using tree::Widget;
using tree::WidgetId;

namespace {

constexpr Rect kUnbounded{-1000000000, -1000000000, 2000000000, 2000000000};

int32_t toInt(int64_t v) { return static_cast<int32_t>(std::clamp<int64_t>(v, -1000000000, 1000000000)); }

void accumulate(layout::LayoutStats& into, const layout::LayoutStats& add) {
  into.nodesCommitted += add.nodesCommitted;
  into.nodesSkipped += add.nodesSkipped;
  into.measureCalls += add.measureCalls;
  into.cacheHits += add.cacheHits;
  into.nodesRounded += add.nodesRounded;
}

}  // namespace

Invalidator::Invalidator(tree::WidgetTree& tree, size_t damageLimit) : tree_(tree), damage_(damageLimit) {}

Invalidator::RootInfo* Invalidator::findRoot(WidgetId id) {
  for (RootInfo& r : roots_) {
    if (r.id == id) return &r;
  }
  return nullptr;
}

void Invalidator::setRoot(WidgetId root, double width, double height) {
  if (!tree_.alive(root) || tree_.parent(root).valid()) return;
  RootInfo* info = findRoot(root);
  if (info == nullptr) {
    roots_.push_back(RootInfo{root, width, height, true});
    info = &roots_.back();
  } else if (info->width != width || info->height != height) {
    info->width = width;
    info->height = height;
    info->full = true;
  } else {
    return;
  }
  requestLayout(root);
}

void Invalidator::queueLayoutRoot(WidgetId id) { pendingLayout_.push_back(id); }

// ---- layout requests ----

void Invalidator::requestLayout(WidgetId widget) {
  Widget* w = tree_.get(widget);
  if (w == nullptr) return;
  const bool wasDirty = w->layoutDirty;
  w->layoutDirty = true;
  // The widget's own bit is not proof that its ancestors are marked and a boundary is queued: it
  // may have been dirtied under a different parent (reparent) or as the stop of a descendant's
  // request. So the climb always starts at the parent; it ends cheaply at the first dirty ancestor,
  // whose own chain is queued by the same rule.
  bool marked = !wasDirty;
  WidgetId last = widget;
  // The widget's own size inputs changed, so its parent is always affected; climb from there
  // until a relayout boundary absorbs the change.
  for (WidgetId cur = tree_.parent(widget); cur.valid(); cur = tree_.parent(cur)) {
    Widget* c = tree_.get(cur);
    if (c->layoutDirty) return;
    c->layoutDirty = true;
    last = cur;
    marked = true;
    if (layout::isRelayoutBoundary(tree_, cur)) {
      queueLayoutRoot(cur);
      return;
    }
  }
  if (marked) queueLayoutRoot(last);  // reached the root (an already queued dirty root is not queued twice)
}

void Invalidator::requestFullLayout() {
  for (RootInfo& r : roots_) {
    r.full = true;
    if (Widget* w = tree_.get(r.id)) {
      if (!w->layoutDirty) {
        w->layoutDirty = true;
        queueLayoutRoot(r.id);
      }
    }
  }
}

// ---- paint requests ----

// The part of the screen where `id` can actually be seen: everything for an unclipped visible
// widget, the intersection of the clipping ancestors otherwise, nothing when it is not shown.
Rect Invalidator::clipOf(WidgetId id) const {
  Rect clip = kUnbounded;
  for (WidgetId cur = id; cur.valid(); cur = tree_.parent(cur)) {
    const Widget* w = tree_.get(cur);
    if (w == nullptr || !w->shown()) return Rect{};
    if (cur != id && w->clips()) clip = layout::intersect(clip, w->absRect);
  }
  return clip;
}

void Invalidator::requestPaint(WidgetId widget, std::optional<Rect> localRect) {
  Widget* w = tree_.get(widget);
  if (w == nullptr) return;
  if (!w->paintDirty) {
    w->paintDirty = true;
    paintDirty_.push_back(widget);
    for (WidgetId p = tree_.parent(widget); p.valid(); p = tree_.parent(p)) {
      Widget* pw = tree_.get(p);
      if (pw->subtreePaintDirty) break;
      pw->subtreePaintDirty = true;
    }
  }
  Rect area = w->absRect;
  if (localRect) {
    // Widget-local to absolute; the sums stay inside int32 because both terms are within +-1e9.
    const Rect& l = *localRect;
    area = layout::intersect(area, Rect{toInt(int64_t{w->absRect.x} + l.x), toInt(int64_t{w->absRect.y} + l.y), l.w, l.h});
  }
  damage_.add(layout::intersect(area, clipOf(widget)));
}

void Invalidator::requestAnimation(WidgetId widget) {
  Widget* w = tree_.get(widget);
  if (w == nullptr || w->animating) return;  // the flag makes a repeated request O(1)
  w->animating = true;
  animating_.push_back(widget);
}

void Invalidator::cancelAnimation(WidgetId widget) {
  if (Widget* w = tree_.get(widget)) w->animating = false;
  animating_.erase(std::remove(animating_.begin(), animating_.end(), widget), animating_.end());
}

// ---- structural edits ----

// Damages every visible rectangle of the subtree (children may overflow their parent).
void Invalidator::damageSubtree(WidgetId id) {
  const Rect clip = clipOf(id);
  if (clip.empty()) return;
  std::vector<std::pair<WidgetId, Rect>> stack{{id, clip}};
  while (!stack.empty()) {
    const auto [cur, curClip] = stack.back();
    stack.pop_back();
    const Widget* w = tree_.get(cur);
    if (w == nullptr || !w->shown()) continue;
    damage_.add(layout::intersect(w->absRect, curClip));
    const Rect childClip = w->clips() ? layout::intersect(curClip, w->absRect) : curClip;
    for (WidgetId c = tree_.firstChild(cur); c.valid(); c = tree_.nextSibling(c)) stack.emplace_back(c, childClip);
  }
}

tree::CreateResult Invalidator::create(WidgetId parent, WidgetId before) {
  const tree::CreateResult r = tree_.create(parent, before);
  if (r.ok()) requestLayout(r.id);
  return r;
}

tree::TreeError Invalidator::destroy(WidgetId widget) {
  const WidgetId parent = tree_.parent(widget);
  damageSubtree(widget);
  const tree::TreeError e = tree_.destroy(widget);
  if (e == tree::TreeError::None && parent.valid()) requestLayout(parent);
  return e;
}

tree::TreeError Invalidator::reparent(WidgetId child, WidgetId newParent, WidgetId before) {
  const WidgetId oldParent = tree_.parent(child);
  // The old place is damaged from the rectangles of the last layout while the old ancestors
  // still apply (a refused reparent only costs a harmless extra repaint); the new place is
  // damaged when the relayout reports the moved rectangles.
  damageSubtree(child);
  const tree::TreeError e = tree_.reparent(child, newParent, before);
  if (e != tree::TreeError::None) return e;
  requestLayout(child);
  if (oldParent != newParent) requestLayout(oldParent);
  return e;
}

tree::TreeError Invalidator::reorder(WidgetId child, WidgetId before) {
  const WidgetId parent = tree_.parent(child);
  damageSubtree(child);  // stacking order may change what is visible here
  const tree::TreeError e = tree_.reorder(child, before);
  if (e != tree::TreeError::None) return e;
  requestLayout(parent);
  return e;
}

void Invalidator::setVisible(WidgetId widget, bool visible) {
  Widget* w = tree_.get(widget);
  if (w == nullptr || w->flags.visible == visible) return;
  // Hiding and showing cover the same area; damage it while the widget counts as shown.
  if (visible) w->flags.visible = true;
  damageSubtree(widget);
  w->flags.visible = visible;
}

// ---- frame ----

bool Invalidator::layoutPending() const {
  for (const WidgetId id : pendingLayout_) {
    const Widget* w = tree_.get(id);
    if (w != nullptr && w->layoutDirty) return true;
  }
  return false;
}

bool Invalidator::needsFrame() const {
  if (!damage_.empty()) return true;
  if (layoutPending()) return true;
  for (const WidgetId id : animating_) {
    if (tree_.alive(id)) return true;
  }
  return false;
}

void Invalidator::addChanges(const std::vector<layout::RectChange>& changes) {
  for (const layout::RectChange& c : changes) {
    damage_.add(c.before);
    damage_.add(c.after);
  }
}

FrameResult Invalidator::runFrame(layout::MeasureProvider* provider) {
  FrameResult out;

  if (!pendingLayout_.empty()) {
    std::vector<WidgetId> work = std::move(pendingLayout_);
    pendingLayout_.clear();
    work.erase(std::remove_if(work.begin(), work.end(), [&](WidgetId id) { return !tree_.alive(id); }), work.end());
    // Shallowest first: a pass over an ancestor also cleans queued descendants.
    std::stable_sort(work.begin(), work.end(),
                     [&](WidgetId a, WidgetId b) { return tree_.depth(a) < tree_.depth(b); });
    std::vector<layout::RectChange> changes;
    for (size_t i = 0; i < work.size(); ++i) {
      const WidgetId id = work[i];
      const Widget* w = tree_.get(id);
      if (!w->layoutDirty) continue;  // already re-laid out by an ancestor's pass
      // A throwing MeasureProvider must not lose the roots that did not get their pass: they go
      // back to the queue (their dirty bits are still set) and the exception reaches the caller.
      try {
        if (!tree_.parent(id).valid()) {
          RootInfo* info = findRoot(id);
          if (info == nullptr) {
            roots_.push_back(RootInfo{id, w->exact.w, w->exact.h, w->layoutState.commitEpoch == 0});
            info = &roots_.back();
          }
          if (info->full || w->layoutState.commitEpoch == 0) {
            accumulate(out.layoutStats, layout::layoutTree(tree_, id, layout::LayoutInput{info->width, info->height},
                                                           provider, &changes));
            info->full = false;
          } else {
            layout::layoutSubtree(tree_, id, provider, out.layoutStats, &changes);
          }
        } else {
          layout::layoutSubtree(tree_, id, provider, out.layoutStats, &changes);
        }
      } catch (...) {
        pendingLayout_.insert(pendingLayout_.begin(), work.begin() + static_cast<std::ptrdiff_t>(i), work.end());
        addChanges(changes);
        throw;
      }
      out.layoutRan = true;
    }
    addChanges(changes);
  }

  for (const WidgetId id : paintDirty_) {
    Widget* w = tree_.get(id);
    if (w == nullptr) continue;
    w->paintDirty = false;
    for (WidgetId p = tree_.parent(id); p.valid(); p = tree_.parent(p)) {
      Widget* pw = tree_.get(p);
      if (!pw->subtreePaintDirty) break;
      pw->subtreePaintDirty = false;
    }
  }
  paintDirty_.clear();

  animating_.erase(std::remove_if(animating_.begin(), animating_.end(), [&](WidgetId id) { return !tree_.alive(id); }),
                   animating_.end());
  out.animating = animating_;
  out.damage = damage_.take();
  return out;
}

}  // namespace r1ui::core::invalidation

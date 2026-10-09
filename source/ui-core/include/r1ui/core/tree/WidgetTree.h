// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the retained widget tree: a generation-checked slot arena plus parent/child/sibling
//   links, with create / destroy-subtree / reparent / reorder and mutation-safe iteration.
// Why: widgets are created and destroyed from event handlers and animations while other code
//   still holds references; identifying widgets by WidgetId (slot + generation) makes
//   use-after-free impossible by construction: a destroyed widget's id never resolves again,
//   even after the slot is reused.
// Callers: layout::, events::Router, invalidation::Invalidator, host widget code (UI thread
//   only; the tree is not thread-safe). Calls: nothing outside the standard library.
// Limits (TreeLimits, validated at construction): maxDepth (root = depth 0; default 256, hard
//   cap kMaxSupportedDepth) and maxNodes (default 1,000,000; a node is roughly 0.7 KB, so the
//   default bounds the arena near 700 MB). Exceeding a limit returns a TreeError and leaves the
//   tree unchanged. Cycles cannot be built: reparenting a widget under itself or a descendant
//   is refused (WouldCreateCycle), and every other operation only adds leaves.
// Failure behavior: no mutation throws or partially applies; each returns a TreeError and checks
//   everything before touching state. A slot whose 32-bit generation is exhausted is retired
//   instead of reused. Mutations are refused with Busy while a MutationLock is held (layout
//   holds one so measure callbacks cannot invalidate the nodes it is iterating).
// Pointers: get() returns a pointer that stays valid until that widget is destroyed (nodes do
//   not move); do not store it across frames.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "r1ui/core/tree/Widget.h"
#include "r1ui/core/tree/WidgetId.h"

namespace r1ui::core::tree {

enum class TreeError : uint8_t {
  None,
  StaleId,           // an id does not name a live widget
  NodeLimit,         // maxNodes reached
  DepthLimit,        // would exceed maxDepth
  WouldCreateCycle,  // reparent under itself or a descendant
  IsRoot,            // roots cannot be reparented or reordered
  NotChildOfParent,  // the `before` sibling is not a child of the target parent
  Busy               // a MutationLock is held
};

// Short stable description of an error, for logs and test output.
const char* describe(TreeError error);

// Layout and hit testing recurse once per tree level (about 0.5 KB of stack per level in
// layout), so the hard cap keeps worst-case recursion near 0.25 MB even in a 1 MB-stack thread.
inline constexpr uint32_t kMaxSupportedDepth = 512;

struct TreeLimits {
  uint32_t maxDepth = 256;
  uint32_t maxNodes = 1'000'000;
};

struct CreateResult {
  WidgetId id;
  TreeError error = TreeError::None;
  bool ok() const { return error == TreeError::None; }
};

class WidgetTree {
 public:
  // Throws std::invalid_argument for maxNodes == 0 or maxDepth > kMaxSupportedDepth.
  explicit WidgetTree(TreeLimits limits = {});
  WidgetTree(const WidgetTree&) = delete;
  WidgetTree& operator=(const WidgetTree&) = delete;

  const TreeLimits& limits() const { return limits_; }
  size_t nodeCount() const { return nodeCount_; }
  // Bumped by every successful create, destroy, reparent or reorder.
  uint64_t structureVersion() const { return structureVersion_; }

  // ---- mutation ----
  CreateResult createRoot();
  // Appends a child, or inserts it before `before` (a child of `parent`) when valid.
  CreateResult create(WidgetId parent, WidgetId before = kNoWidget);
  // Destroys the widget and its whole subtree; every id inside becomes stale.
  TreeError destroy(WidgetId id);
  // Moves `child` (with its subtree) under `newParent`, before `before` or at the end.
  TreeError reparent(WidgetId child, WidgetId newParent, WidgetId before = kNoWidget);
  // Moves `child` within its current parent (before `before`, or to the end when invalid).
  TreeError reorder(WidgetId child, WidgetId before = kNoWidget);

  // ---- lookup ----
  bool alive(WidgetId id) const { return slotFor(id) != nullptr; }
  Widget* get(WidgetId id);
  const Widget* get(WidgetId id) const;

  // ---- structure queries; all return the invalid id / 0 for a stale id ----
  WidgetId parent(WidgetId id) const;
  WidgetId firstChild(WidgetId id) const;
  WidgetId lastChild(WidgetId id) const;
  WidgetId nextSibling(WidgetId id) const;
  WidgetId prevSibling(WidgetId id) const;
  uint32_t childCount(WidgetId id) const;
  uint32_t depth(WidgetId id) const;
  // True when `ancestor` is a proper ancestor of `node`.
  bool isAncestor(WidgetId ancestor, WidgetId node) const;
  // Appends the ids from `id` up to its root (inclusive) to `out`, leaf first.
  void ancestorsOf(WidgetId id, std::vector<WidgetId>& out) const;

  // ---- mutation-safe iteration ----
  // Both visit a snapshot of ids taken before the first callback, skipping any id that has been
  // destroyed (or, for forEachChild, moved away) by an earlier callback. Widgets created during
  // the walk are not visited. The callback may freely mutate the tree.
  template <class Fn>
  void forEachChild(WidgetId parentId, Fn&& fn) const {
    std::vector<WidgetId> snapshot;
    snapshot.reserve(childCount(parentId));
    for (WidgetId c = firstChild(parentId); c.valid(); c = nextSibling(c)) snapshot.push_back(c);
    for (WidgetId c : snapshot) {
      if (parent(c) == parentId) fn(c);
    }
  }
  // Pre-order walk over the descendants of `root` (and `root` itself when includeSelf).
  template <class Fn>
  void forEachDescendant(WidgetId root, Fn&& fn, bool includeSelf = false) const {
    std::vector<WidgetId> snapshot;
    collectPreorder(root, includeSelf, snapshot);
    for (WidgetId c : snapshot) {
      if (alive(c)) fn(c);
    }
  }

  // ---- mutation lock ----
  // While any lock exists every mutating call returns TreeError::Busy.
  class MutationLock {
   public:
    explicit MutationLock(WidgetTree& tree) : tree_(&tree) { ++tree_->lockCount_; }
    MutationLock(const MutationLock&) = delete;
    MutationLock& operator=(const MutationLock&) = delete;
    ~MutationLock() { --tree_->lockCount_; }

   private:
    WidgetTree* tree_;
  };

  // Layout bookkeeping owned by layout:: (a full pass bumps the epoch, discarding all caches).
  uint32_t layoutEpoch() const { return layoutEpoch_; }
  uint32_t bumpLayoutEpoch() { return ++layoutEpoch_; }
  // A unique id for one layout call; lets the engine trust a dirty widget's cache only within
  // the pass that refilled it.
  uint32_t nextLayoutPass() { return ++layoutPass_; }

 private:
  static constexpr uint32_t kNone = Widget::kNone;

  struct Slot {
    Widget widget;
    uint32_t generation = 1;
    bool alive = false;
    uint32_t nextFree = kNone;
  };

  const Slot* slotFor(WidgetId id) const;
  Slot* slotFor(WidgetId id);
  WidgetId idOf(uint32_t index) const;
  uint32_t allocate();
  void link(uint32_t child, uint32_t parent, uint32_t before);
  void unlink(uint32_t child);
  uint32_t subtreeHeight(uint32_t index) const;
  void setDepths(uint32_t index, uint32_t depth);
  void collectPreorder(WidgetId root, bool includeSelf, std::vector<WidgetId>& out) const;
  TreeError checkBefore(uint32_t parentIndex, WidgetId before, uint32_t& beforeIndex) const;

  TreeLimits limits_;
  std::deque<Slot> slots_;  // deque: growing never moves existing nodes
  uint32_t freeHead_ = kNone;
  size_t nodeCount_ = 0;
  uint64_t structureVersion_ = 0;
  uint32_t lockCount_ = 0;
  uint32_t layoutEpoch_ = 1;
  uint32_t layoutPass_ = 0;
};

}  // namespace r1ui::core::tree

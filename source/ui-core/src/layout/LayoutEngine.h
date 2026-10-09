// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the private layout engine: dry measurement with a per-node cache, committing final
//   sizes, and the container algorithm (FlexRun) that both of them drive.
// Why: sizing a content-sized subtree and laying it out are the same algorithm; sharing one
//   implementation (commit flag) guarantees a measured size and a committed size never disagree.
// Callers: FlexLayout.cpp / LayoutRounding.cpp (public entry points) only. Not installed.
// Invariants: measure() never writes child positions; commit() always does. Both treat the tree
//   as locked (the caller holds a MutationLock) so Widget pointers stay valid within a call.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "r1ui/core/layout/FlexLayout.h"
#include "r1ui/core/layout/Style.h"
#include "r1ui/core/tree/WidgetTree.h"

// Keeps a large-frame helper out of the recursive measure / commit path.
#if defined(_MSC_VER)
#define R1UI_NOINLINE __declspec(noinline)
#else
#define R1UI_NOINLINE __attribute__((noinline))
#endif

namespace r1ui::core::layout::detail {

struct Size {
  double w = 0.0;
  double h = 0.0;
};

// Pass-scoped second level of the measure cache: the (node, constraint) results that the tiny
// per-node cache had to drop during one layout call. The per-node cache keeps results across
// incremental passes and is all a flat tree ever needs; this table makes a node that is asked more
// than a few distinct questions (nested content-sized containers produce many) compute each answer
// once per pass instead of once per ask, which is what keeps such nests linear. Entries are valid
// for the whole pass because styles and the host's measure results do not change while the tree
// is locked. Bounded: past kMaxEntries a dropped result is simply forgotten (slower, never wrong).
class MeasureMemo {
 public:
  static constexpr size_t kMaxEntries = size_t{1} << 18;

  const Size* find(tree::WidgetId id, const SizeConstraint& c) const;
  void insert(tree::WidgetId id, const SizeConstraint& c, const Size& result);

 private:
  struct Slot {
    tree::WidgetId id;  // invalid id = empty slot
    SizeConstraint key;
    Size result;
  };

  static size_t hashOf(tree::WidgetId id, const SizeConstraint& c);
  void grow();

  std::vector<Slot> slots_;  // size is zero or a power of two, at most half full
  size_t count_ = 0;
};

class Engine {
 public:
  Engine(tree::WidgetTree& tree, MeasureProvider* provider, LayoutStats& stats, uint32_t epoch,
         uint32_t pass)
      : tree_(tree), provider_(provider), stats_(stats), epoch_(epoch), pass_(pass) {}

  tree::WidgetTree& tree() { return tree_; }
  LayoutStats& stats() { return stats_; }

  // Border-box size of `id` under constraint `c` (cached); never positions children.
  Size measure(tree::WidgetId id, const SizeConstraint& c);
  // Gives `id` its final border-box size and positions its whole subtree (skipping clean ones).
  void commit(tree::WidgetId id, double w, double h);
  // Asks the host for the content size of a measured leaf (sanitised, counted).
  MeasureResult callMeasure(tree::WidgetId id, const MeasureInput& input);

 private:
  // Not inlined: it holds a sanitised Style copy, which must not sit in every recursive frame.
  R1UI_NOINLINE Size measureLeaf(tree::WidgetId id, const SizeConstraint& c);
  void clearDirty(tree::WidgetId id, bool includeSelf);

  tree::WidgetTree& tree_;
  MeasureProvider* provider_;
  LayoutStats& stats_;
  uint32_t epoch_;
  uint32_t pass_;
  MeasureMemo memo_;
};

// Rounds exact rectangles to pixels for the subtree below `id` (see FlexLayout.h policy).
// `parentExactX/Y` and `parentRect` describe the parent (all zero for a root).
void roundSubtree(tree::WidgetTree& tree, tree::WidgetId id, double parentExactX, double parentExactY,
                  const Rect& parentRect, LayoutStats& stats, std::vector<RectChange>* changes);

// Runs the flex algorithm for one container (its style is sanitised internally);
// commit==false only computes the container size.
Size runFlexContainer(Engine& engine, tree::WidgetId id, const SizeConstraint& c, bool commit);

}  // namespace r1ui::core::layout::detail

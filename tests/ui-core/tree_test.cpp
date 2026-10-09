// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for tree::WidgetTree: id generations, subtree destroy, reparent/reorder, limits,
//   mutation-safe iteration and the hostile cases (stale ids, cycles, deep and huge trees).
// Callers: CTest (label fast).
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <vector>

#include "TestSupport.h"

using namespace core_test;
using r1ui::core::tree::kNoWidget;
using r1ui::core::tree::TreeError;
using r1ui::core::tree::TreeLimits;

namespace {

void creationAndOrder() {
  WidgetTree t;
  const WidgetId root = addRoot(t);
  const WidgetId a = addChild(t, root);
  const WidgetId b = addChild(t, root);
  const WidgetId c = addChild(t, root, Style{});
  expect(t.nodeCount() == 4, "four nodes");
  expect(t.firstChild(root) == a && t.lastChild(root) == c, "append order");
  expect(t.nextSibling(a) == b && t.prevSibling(c) == b, "sibling links");
  expect(t.parent(b) == root && !t.parent(root).valid(), "parent links");
  expect(t.childCount(root) == 3 && t.depth(a) == 1 && t.depth(root) == 0, "counts and depth");

  const auto ins = t.create(root, b);  // insert before b
  expect(ins.ok() && t.nextSibling(a) == ins.id && t.nextSibling(ins.id) == b, "insert before");
  expect(t.create(root, root).error == TreeError::NotChildOfParent, "before must be a child");

  expect(t.reorder(c, a) == TreeError::None && t.firstChild(root) == c, "reorder to front");
  expect(t.reorder(c, kNoWidget) == TreeError::None && t.lastChild(root) == c, "reorder to end");
  expect(t.reorder(root) == TreeError::IsRoot, "roots cannot be reordered");
  expect(t.reorder(c, c) == TreeError::None && t.lastChild(root) == c,
         "reorder before itself leaves order intact");
}

void staleIdsAfterDestroyAndReuse() {
  WidgetTree t;
  const WidgetId root = addRoot(t);
  const WidgetId a = addChild(t, root);
  t.get(a)->userData = 42;
  expect(t.destroy(a) == TreeError::None, "destroy");
  expect(!t.alive(a) && t.get(a) == nullptr, "stale id does not resolve");
  expect(t.destroy(a) == TreeError::StaleId, "double destroy is refused");
  const WidgetId b = addChild(t, root);  // reuses a's slot
  expect(b.index == a.index && b.generation != a.generation, "slot reused with a new generation");
  expect(t.get(a) == nullptr && t.get(b) != nullptr, "old id still stale after reuse");
  expect(t.get(b)->userData == 0, "reused slot starts clean");
  expect(!t.parent(a).valid() && t.childCount(a) == 0 && t.firstChild(a) == kNoWidget,
         "queries on a stale id are empty");
  expect(t.create(a).error == TreeError::StaleId, "create under stale parent");
  expect(t.reparent(a, root) == TreeError::StaleId, "reparent stale child");
  expect(t.reparent(b, a) == TreeError::StaleId, "reparent under stale parent");
  expect(!WidgetId{}.valid() && !t.alive(WidgetId{}), "default id is invalid");
  expect(!t.alive(WidgetId{9999, 1}), "out of range index is stale");
}

void destroySubtree() {
  WidgetTree t;
  const WidgetId root = addRoot(t);
  const WidgetId a = addChild(t, root);
  const WidgetId a1 = addChild(t, a);
  const WidgetId a2 = addChild(t, a);
  const WidgetId a11 = addChild(t, a1);
  const WidgetId keep = addChild(t, root);
  expect(t.destroy(a) == TreeError::None, "destroy subtree");
  expect(!t.alive(a) && !t.alive(a1) && !t.alive(a2) && !t.alive(a11), "all descendants stale");
  expect(t.alive(keep) && t.childCount(root) == 1 && t.firstChild(root) == keep, "siblings kept");
  expect(t.nodeCount() == 2, "node count after destroy");
}

void reparentRules() {
  WidgetTree t;
  const WidgetId root = addRoot(t);
  const WidgetId a = addChild(t, root);
  const WidgetId b = addChild(t, root);
  const WidgetId a1 = addChild(t, a);
  const WidgetId a11 = addChild(t, a1);
  expect(t.reparent(a, a) == TreeError::WouldCreateCycle, "under itself refused");
  expect(t.reparent(a, a1) == TreeError::WouldCreateCycle, "under a child refused");
  expect(t.reparent(a, a11) == TreeError::WouldCreateCycle, "under a deep descendant refused");
  expect(t.parent(a) == root && t.parent(a1) == a, "refusal leaves the tree unchanged");
  expect(t.reparent(root, a) == TreeError::IsRoot, "a root cannot be moved under its descendant");
  const WidgetId other = addRoot(t);
  expect(t.reparent(root, other) == TreeError::IsRoot, "roots are not movable");

  expect(t.reparent(a1, b) == TreeError::None, "valid reparent");
  expect(t.parent(a1) == b && t.parent(a11) == a1 && t.childCount(a) == 0 && t.childCount(b) == 1,
         "links after reparent");
  expect(t.depth(a1) == 2 && t.depth(a11) == 3, "depths updated for the moved subtree");
  expect(t.isAncestor(root, a11) && !t.isAncestor(a, a11) && !t.isAncestor(a11, a11), "isAncestor");
  std::vector<WidgetId> chain;
  t.ancestorsOf(a11, chain);
  expect(chain.size() == 4 && chain.front() == a11 && chain.back() == root, "ancestor chain leaf first");
  expect(t.reparent(a1, a, a11) == TreeError::NotChildOfParent, "before sibling must belong to target");
}

void limits() {
  bool threw = false;
  try {
    WidgetTree bad(TreeLimits{256, 0});
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expect(threw, "zero node limit rejected");
  threw = false;
  try {
    WidgetTree bad(TreeLimits{r1ui::core::tree::kMaxSupportedDepth + 1, 10});
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expect(threw, "excessive depth limit rejected");

  WidgetTree small(TreeLimits{256, 5});
  const WidgetId root = addRoot(small);
  for (int i = 0; i < 4; ++i) addChild(small, root);
  const uint64_t version = small.structureVersion();
  expect(small.create(root).error == TreeError::NodeLimit, "node limit reached");
  expect(small.createRoot().error == TreeError::NodeLimit, "node limit applies to roots");
  expect(small.nodeCount() == 5 && small.structureVersion() == version, "failed create changes nothing");
  expect(small.destroy(small.firstChild(root)) == TreeError::None && small.create(root).ok(),
         "destroying frees capacity");

  WidgetTree deep(TreeLimits{8, 1000});
  WidgetId cur = addRoot(deep);
  int made = 0;
  for (;; ++made) {
    const auto r = deep.create(cur);
    if (!r.ok()) {
      expect(r.error == TreeError::DepthLimit, "depth limit error");
      break;
    }
    cur = r.id;
  }
  expect(made == 8 && deep.depth(cur) == 8, "exactly maxDepth levels below the root");

  // A reparent that would push a subtree past the limit is refused as a whole.
  WidgetTree t(TreeLimits{4, 100});
  const WidgetId r = addRoot(t);
  const WidgetId x = addChild(t, r);
  WidgetId y = addChild(t, x);
  y = addChild(t, y);  // x -> y -> y2: height 2
  const WidgetId l1 = addChild(t, r);
  const WidgetId l2 = addChild(t, l1);
  const WidgetId l3 = addChild(t, l2);  // depth 3
  expect(t.reparent(x, l3) == TreeError::DepthLimit, "subtree would exceed depth");
  expect(t.parent(x) == r && t.depth(y) == 3, "refused reparent leaves depths intact");
  expect(t.reparent(x, l1) == TreeError::None && t.depth(y) == 4, "fits exactly at the limit");
}

void deepTreeOperations() {
  WidgetTree t(TreeLimits{512, 5000});
  const WidgetId root = addRoot(t);
  WidgetId cur = root;
  for (int i = 0; i < 512; ++i) cur = addChild(t, cur);
  expect(t.depth(cur) == 512, "512 levels deep (the supported maximum)");
  std::vector<WidgetId> up;
  t.ancestorsOf(cur, up);
  expect(up.size() == 513, "ancestor walk is iterative");
  size_t visited = 0;
  t.forEachDescendant(root, [&](WidgetId) { ++visited; });
  expect(visited == 512, "descendant walk covers a deep chain");
  expect(t.destroy(root) == TreeError::None && t.nodeCount() == 0, "destroying a deep tree is iterative");
  expect(!t.alive(cur), "deepest node stale");
}

void iterationWhileMutating() {
  WidgetTree t;
  const WidgetId root = addRoot(t);
  std::vector<WidgetId> kids;
  for (int i = 0; i < 6; ++i) kids.push_back(addChild(t, root));

  // Destroying the current child and a later sibling during the walk.
  std::vector<WidgetId> seen;
  t.forEachChild(root, [&](WidgetId c) {
    seen.push_back(c);
    if (c == kids[1]) {
      expect(t.destroy(c) == TreeError::None, "destroy current during iteration");
      expect(t.destroy(kids[3]) == TreeError::None, "destroy a later sibling during iteration");
    }
  });
  expect(seen.size() == 5 && seen[2] == kids[2] && seen[3] == kids[4], "destroyed sibling skipped");

  // Creating children during the walk does not extend it.
  int count = 0;
  t.forEachChild(root, [&](WidgetId) {
    ++count;
    (void)t.create(root);
  });
  expect(count == 4, "children created during the walk are not visited");

  // Moving a not-yet-visited child elsewhere removes it from this walk.
  const WidgetId holder = addChild(t, root);
  const WidgetId victim = addChild(t, root);
  std::vector<WidgetId> order;
  t.forEachChild(root, [&](WidgetId c) {
    order.push_back(c);
    if (order.size() == 1) expect(t.reparent(victim, holder) == TreeError::None, "move during walk");
  });
  bool visitedVictim = false;
  for (const WidgetId c : order) visitedVictim = visitedVictim || c == victim;
  expect(!order.empty() && !visitedVictim && t.parent(victim) == holder,
         "moved child is not visited under the old parent");

  // Descendant walk: destroy the subtree being walked.
  WidgetTree t2;
  const WidgetId r2 = addRoot(t2);
  const WidgetId p = addChild(t2, r2);
  const WidgetId c1 = addChild(t2, p);
  const WidgetId c2 = addChild(t2, p);
  const WidgetId q = addChild(t2, r2);
  std::vector<WidgetId> walked;
  t2.forEachDescendant(r2, [&](WidgetId id) {
    walked.push_back(id);
    if (id == p) (void)t2.destroy(p);
  });
  expect(walked.size() == 2 && walked[0] == p && walked[1] == q, "destroyed subtree not walked");
  expect(!t2.alive(c1) && !t2.alive(c2), "children died with the parent");
}

void mutationLock() {
  WidgetTree t;
  const WidgetId root = addRoot(t);
  const WidgetId a = addChild(t, root);
  {
    const WidgetTree::MutationLock lock(t);
    expect(t.create(root).error == TreeError::Busy, "create refused while locked");
    expect(t.destroy(a) == TreeError::Busy, "destroy refused while locked");
    expect(t.reparent(a, root) == TreeError::Busy, "reparent refused while locked");
    expect(t.createRoot().error == TreeError::Busy, "createRoot refused while locked");
    expect(t.alive(a), "reads still work while locked");
  }
  expect(t.destroy(a) == TreeError::None, "mutation allowed after the lock is released");
}

void hundredThousandNodes() {
  const auto t0 = std::chrono::steady_clock::now();
  WidgetTree t(TreeLimits{256, 200000});
  const WidgetId root = addRoot(t);
  std::vector<WidgetId> ids;
  ids.reserve(100000);
  ids.push_back(root);
  // A bushy tree: every node gets up to 10 children, breadth first.
  for (size_t i = 0; ids.size() < 100000; ++i) {
    for (int k = 0; k < 10 && ids.size() < 100000; ++k) ids.push_back(addChild(t, ids[i]));
  }
  expect(t.nodeCount() == 100000, "100k nodes created");
  size_t visited = 0;
  t.forEachDescendant(root, [&](WidgetId) { ++visited; });
  expect(visited == 99999, "walk visits every descendant");
  // Destroy a big subtree and check every id inside is stale while the rest survives.
  const WidgetId big = ids[1];
  std::vector<WidgetId> inside;
  t.forEachDescendant(big, [&](WidgetId id) { inside.push_back(id); }, true);
  expect(t.destroy(big) == TreeError::None, "destroy large subtree");
  bool allStale = true;
  for (const WidgetId id : inside) allStale = allStale && !t.alive(id);
  expect(allStale && t.nodeCount() == 100000 - inside.size(), "whole subtree stale, count consistent");
  // Reuse every freed slot; old ids must stay stale.
  for (size_t i = 0; i < inside.size(); ++i) (void)addChild(t, root);
  bool stillStale = true;
  for (const WidgetId id : inside) stillStale = stillStale && !t.alive(id);
  expect(stillStale && t.nodeCount() == 100000, "stale ids stay stale after slot reuse");
  const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::fprintf(stderr, "PERF tree: 100k nodes create+walk+destroy+reuse: %.1f ms\n", ms);
}

}  // namespace

int main() {
  runCase("creation_and_order", creationAndOrder);
  runCase("stale_ids_after_destroy_and_reuse", staleIdsAfterDestroyAndReuse);
  runCase("destroy_subtree", destroySubtree);
  runCase("reparent_rules", reparentRules);
  runCase("limits", limits);
  runCase("deep_tree_operations", deepTreeOperations);
  runCase("iteration_while_mutating", iterationWhileMutating);
  runCase("mutation_lock", mutationLock);
  runCase("hundred_thousand_nodes", hundredThousandNodes);
  return finish("ui-core.tree");
}

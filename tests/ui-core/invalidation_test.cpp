// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for invalidation::DamageList and Invalidator: damage accumulation and the merge
//   limit, paint damage equal to absolute bounds, idle detection, animation requests,
//   incremental layout (fixed-size panels isolate their subtree; siblings are neither re-measured
//   nor recommitted), and damage for reparent / destroy / create / visibility changes.
// Callers: CTest (label fast).
#include <cstdio>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "TestSupport.h"
#include "r1ui/core/invalidation/DamageList.h"
#include "r1ui/core/invalidation/Invalidator.h"

using namespace core_test;
using namespace r1ui::core::invalidation;
using r1ui::core::layout::Align;
using r1ui::core::layout::MeasureInput;
using r1ui::core::layout::MeasureProvider;
using r1ui::core::layout::MeasureResult;
using r1ui::core::layout::Rect;

namespace {

// Leaves report a fixed size and count how often each was measured.
class FixedMeasure : public MeasureProvider {
 public:
  struct Spec {
    double w;
    double h;
  };
  std::unordered_map<WidgetId, Spec> specs;
  std::unordered_map<WidgetId, int> calls;
  int total = 0;
  MeasureResult measure(WidgetId id, const MeasureInput&) override {
    ++total;
    ++calls[id];
    const auto it = specs.find(id);
    return it == specs.end() ? MeasureResult{} : MeasureResult{it->second.w, it->second.h};
  }
};

bool covered(const std::vector<Rect>& damage, const Rect& r) {
  for (const Rect& d : damage) {
    if (r1ui::core::layout::containsRect(d, r)) return true;
  }
  return false;
}

WidgetId addLeaf(WidgetTree& t, WidgetId parent, FixedMeasure& m, double w, double h, Style s = Style{}) {
  s.hasMeasure = true;
  s.flexShrink = 0;
  const WidgetId id = addChild(t, parent, s);
  m.specs[id] = {w, h};
  return id;
}

// root 800x600 (row): P1 and P2 are fixed 200x200 panels each holding two measured leaves,
// P3 grows to fill the rest and holds one measured leaf.
struct Panels {
  WidgetTree tree;
  FixedMeasure measure;
  WidgetId root, p1, p2, p3;
  WidgetId a1, b1, a2, b2, c3;
  Invalidator inv;

  Panels() : inv(tree) {
    root = addRoot(tree, sized(800, 600));
    tree.get(root)->style.alignItems = Align::Start;
    p1 = addChild(tree, root, sized(200, 200));
    p2 = addChild(tree, root, sized(200, 200));
    Style grow;
    grow.flexGrow = 1;
    p3 = addChild(tree, root, grow);
    for (const WidgetId p : {p1, p2}) {
      tree.get(p)->style.flexShrink = 0;
      tree.get(p)->style.alignItems = Align::Start;
    }
    a1 = addLeaf(tree, p1, measure, 50, 20);
    b1 = addLeaf(tree, p1, measure, 60, 20);
    a2 = addLeaf(tree, p2, measure, 50, 20);
    b2 = addLeaf(tree, p2, measure, 60, 20);
    c3 = addLeaf(tree, p3, measure, 70, 30);
    inv.setRoot(root, 800, 600);
    (void)inv.runFrame(&measure);
  }
  size_t nodeCount() const { return tree.nodeCount(); }
};

void damageListRules() {
  DamageList list(8);
  list.add(Rect{0, 0, 0, 10});
  list.add(Rect{5, 5, -3, 4});
  expect(list.empty(), "empty rectangles are ignored");
  list.add(Rect{0, 0, 100, 100});
  list.add(Rect{10, 10, 20, 20});
  expect(list.size() == 1, "a covered rectangle is dropped");
  list.add(Rect{200, 200, 10, 10});
  list.add(Rect{190, 190, 50, 50});
  expect(list.size() == 2, "a new rectangle swallows the ones it covers");
  expect(list.bounds().x == 0 && list.bounds().right() == 240 && list.bounds().bottom() == 240, "bounds");
  const auto taken = list.take();
  expect(taken.size() == 2 && list.empty(), "take empties the list");

  // Merge limit: 12 far-apart unit squares never exceed 8 entries and always stay covered.
  DamageList many(8);
  std::vector<Rect> added;
  for (int i = 0; i < 12; ++i) {
    const Rect r{i * 100, (i % 3) * 100, 10, 10};
    added.push_back(r);
    many.add(r);
    expect(many.size() <= 8, "never above the merge limit");
  }
  bool all = true;
  for (const Rect& r : added) all = all && covered(many.rects(), r);
  expect(all, "every added rectangle is still covered after merging");

  // The cheapest pair is merged: two neighbours rather than the far one.
  DamageList small(2);
  small.add(Rect{0, 0, 10, 10});
  small.add(Rect{1000, 1000, 10, 10});
  small.add(Rect{12, 0, 10, 10});
  expect(small.size() == 2, "limit of two");
  expect(covered(small.rects(), Rect{0, 0, 22, 10}), "the two close rectangles were merged");
  expect(covered(small.rects(), Rect{1000, 1000, 10, 10}), "the far one stayed alone");

  DamageList one(0);  // clamped to 1
  one.add(Rect{0, 0, 5, 5});
  one.add(Rect{100, 100, 5, 5});
  expect(one.size() == 1 && one.maxRects() == 1 && one.rects()[0] == (Rect{0, 0, 105, 105}), "limit 1 keeps a single box");
}

void paintDamageEqualsBounds() {
  Panels s;
  expect(!s.inv.needsFrame(), "idle after the initial frame");
  const Rect bounds = absOf(s.tree, s.b1);
  expectRect(bounds, 50, 0, 60, 20, "b1 sits after a1 inside P1");
  s.inv.requestPaint(s.b1);
  expect(s.inv.needsFrame(), "a paint request needs a frame");
  expect(s.tree.get(s.b1)->paintDirty && s.tree.get(s.p1)->subtreePaintDirty && s.tree.get(s.root)->subtreePaintDirty &&
             !s.tree.get(s.p1)->paintDirty && !s.tree.get(s.p2)->subtreePaintDirty,
         "paint-dirty marks the widget, subtree-dirty marks only its ancestors");
  const FrameResult r = s.inv.runFrame(&s.measure);
  expect(r.damage.size() == 1 && r.damage[0] == bounds, "damage equals the widget's absolute bounds");
  expect(!r.layoutRan && r.layoutStats.measureCalls == 0, "a paint-only frame does no layout");
  expect(!s.tree.get(s.b1)->paintDirty && !s.tree.get(s.root)->subtreePaintDirty, "flags cleared after the frame");
  expect(!s.inv.needsFrame(), "idle again");

  s.inv.requestPaint(s.b1, Rect{2, 3, 5, 6});
  expect(s.inv.runFrame(&s.measure).damage == std::vector<Rect>{Rect{52, 3, 5, 6}}, "local rect is offset by the widget origin");
  s.inv.requestPaint(s.b1, Rect{-10, -10, 20, 20});
  expect(s.inv.runFrame(&s.measure).damage == std::vector<Rect>{Rect{50, 0, 10, 10}}, "local rect is clipped to the widget");
  s.inv.requestPaint(s.b1, Rect{100, 100, 5, 5});
  expect(s.inv.runFrame(&s.measure).damage.empty(), "a rect outside the widget damages nothing");

  // Clipping ancestor: only the visible part is damaged.
  s.tree.get(s.p1)->flags.clipsChildren = true;
  s.tree.get(s.b1)->style.width = r1ui::core::layout::Length::px(300);
  s.tree.get(s.b1)->style.flexShrink = 0;
  s.measure.specs[s.b1] = {300, 20};
  s.inv.requestLayout(s.b1);
  (void)s.inv.runFrame(&s.measure);
  s.inv.requestPaint(s.b1);
  const FrameResult clipped = s.inv.runFrame(&s.measure);
  expect(clipped.damage.size() == 1 && clipped.damage[0] == (Rect{50, 0, 150, 20}), "damage is clipped by the panel");
  s.tree.get(s.p1)->flags.clipsChildren = false;

  // Hidden widgets draw nothing.
  s.inv.setVisible(s.b2, false);
  (void)s.inv.runFrame(&s.measure);
  s.inv.requestPaint(s.b2);
  expect(s.inv.runFrame(&s.measure).damage.empty(), "hidden widgets produce no paint damage");
  s.inv.setVisible(s.p2, false);
  s.inv.setVisible(s.p2, true);
  const FrameResult shown = s.inv.runFrame(&s.measure);
  expect(covered(shown.damage, absOf(s.tree, s.a2)), "toggling visibility damages the area");
}

void idleAndAnimation() {
  Panels s;
  const FrameResult idle = s.inv.runFrame(&s.measure);
  expect(!s.inv.needsFrame() && idle.damage.empty() && !idle.layoutRan && idle.animating.empty(),
         "an idle UI has no frame to render");
  expect(idle.layoutStats.nodesCommitted == 0 && idle.layoutStats.measureCalls == 0, "and does no work");

  s.inv.requestAnimation(s.a1);
  s.inv.requestAnimation(s.a1);
  for (int frame = 0; frame < 3; ++frame) {
    expect(s.inv.needsFrame(), "an animation keeps frames coming");
    const FrameResult f = s.inv.runFrame(&s.measure);
    expect(f.animating.size() == 1 && f.animating[0] == s.a1, "the animating widget is listed once");
    s.inv.requestPaint(s.a1);  // what a ticking widget does
  }
  s.inv.cancelAnimation(s.a1);
  (void)s.inv.runFrame(&s.measure);
  expect(!s.inv.needsFrame(), "cancelling the animation returns to idle");

  s.inv.requestAnimation(s.b1);
  (void)s.tree.destroy(s.b1);
  expect(!s.inv.needsFrame(), "a destroyed animating widget no longer drives frames");
  s.inv.requestAnimation(WidgetId{999, 1});
  expect(!s.inv.needsFrame(), "animation requests for stale ids are ignored");
}

void layoutIsolation() {
  // 1. A change inside a fixed-size panel re-lays out that panel only.
  {
    Panels s;
    const Rect p2Before = absOf(s.tree, s.p2);
    const Rect a2Before = absOf(s.tree, s.a2);
    const Rect b1Before = absOf(s.tree, s.b1);
    s.measure.specs[s.a1] = {80, 20};
    s.measure.calls.clear();
    s.measure.total = 0;
    s.inv.requestLayout(s.a1);
    expect(s.tree.get(s.a1)->layoutDirty && s.tree.get(s.p1)->layoutDirty && !s.tree.get(s.root)->layoutDirty,
           "dirty propagation stops at the fixed-size panel");
    const FrameResult f = s.inv.runFrame(&s.measure);
    expect(f.layoutRan, "layout ran");
    expect(f.layoutStats.nodesCommitted == 2, "only the panel and the changed leaf are recommitted");
    expect(s.measure.calls[s.a2] == 0 && s.measure.calls[s.b2] == 0 && s.measure.calls[s.c3] == 0,
           "siblings of the panel are not re-measured");
    expectRect(absOf(s.tree, s.a1), 0, 0, 80, 20, "the changed leaf took its new width");
    expectRect(absOf(s.tree, s.b1), 80, 0, 60, 20, "its sibling inside the panel moved");
    expect(absOf(s.tree, s.b1) != b1Before, "b1 changed");
    expect(absOf(s.tree, s.p2) == p2Before && absOf(s.tree, s.a2) == a2Before, "the other panel did not change");
    expect(covered(f.damage, Rect{50, 0, 60, 20}) && covered(f.damage, Rect{0, 0, 80, 20}),
           "damage covers the old and new rectangles of what moved");
    expect(f.damage.size() >= 1 && !covered(f.damage, Rect{400, 0, 10, 10}), "no damage outside the panel");
  }
  // 2. A change inside a growing (non fixed-size) panel climbs to the root but skips the clean panels.
  {
    Panels s;
    s.measure.specs[s.c3] = {90, 30};
    s.measure.calls.clear();
    s.measure.total = 0;
    s.inv.requestLayout(s.c3);
    const FrameResult f = s.inv.runFrame(&s.measure);
    expect(f.layoutStats.nodesCommitted == 3, "root, growing panel and leaf are recommitted");
    expect(f.layoutStats.nodesSkipped >= 2, "the clean panels are skipped, not recommitted");
    expect(s.measure.calls[s.a1] == 0 && s.measure.calls[s.b1] == 0 && s.measure.calls[s.a2] == 0 &&
               s.measure.calls[s.b2] == 0,
           "clean panels are not re-measured");
    expectRect(absOf(s.tree, s.c3), 400, 0, 90, 30, "leaf resized");
  }
  // 3. Resizing a panel itself re-lays out its parent, moves the sibling and updates its subtree
  //    without recommitting or re-measuring it.
  {
    Panels s;
    s.tree.get(s.p1)->style.width = r1ui::core::layout::Length::px(250);
    s.measure.calls.clear();
    s.measure.total = 0;
    s.inv.requestLayout(s.p1);
    const FrameResult f = s.inv.runFrame(&s.measure);
    expectRect(absOf(s.tree, s.p1), 0, 0, 250, 200, "panel resized");
    expectRect(absOf(s.tree, s.p2), 250, 0, 200, 200, "sibling panel shifted right");
    expectRect(absOf(s.tree, s.a2), 250, 0, 50, 20, "the sibling's children followed");
    expectRect(absOf(s.tree, s.b2), 300, 0, 60, 20, "the sibling's second child too");
    expectRect(absOf(s.tree, s.c3), 450, 0, 70, 30, "growing panel's leaf moved");
    expect(s.measure.calls[s.a2] == 0 && s.measure.calls[s.b2] == 0 && s.measure.calls[s.a1] == 0,
           "no clean leaf was measured again");
    expect(f.layoutStats.nodesCommitted <= 5, "only root, panels and the growing panel's chain are recommitted");
    expect(covered(f.damage, Rect{200, 0, 200, 200}) && covered(f.damage, Rect{250, 0, 200, 200}) && covered(f.damage, Rect{0, 0, 250, 200}),
           "damage covers the resized panel and the old and new place of the shifted one");
  }
  // 4. A viewport change is a full pass.
  {
    Panels s;
    s.inv.setRoot(s.root, 700, 500);
    const FrameResult f = s.inv.runFrame(&s.measure);
    expect(f.layoutStats.nodesCommitted == s.nodeCount(), "full pass commits every node");
    expectRect(absOf(s.tree, s.root), 0, 0, 700, 500, "root took the new viewport");
    expect(covered(f.damage, Rect{0, 0, 800, 600}) && covered(f.damage, Rect{0, 0, 700, 500}), "root damaged old and new");
    s.inv.requestFullLayout();
    const FrameResult g = s.inv.runFrame(&s.measure);
    expect(g.layoutStats.nodesCommitted == s.nodeCount() && g.damage.empty(),
           "a forced full pass recommits all and damages nothing when nothing moved");
  }
  // 5. Dirty requests are idempotent.
  {
    Panels s;
    s.inv.requestLayout(s.a1);
    s.inv.requestLayout(s.a1);
    s.inv.requestLayout(s.b1);
    const FrameResult f = s.inv.runFrame(&s.measure);
    expect(f.layoutStats.nodesCommitted == 3, "panel + two changed leaves, once each");
    expect(!s.tree.get(s.a1)->layoutDirty && !s.tree.get(s.p1)->layoutDirty, "dirty bits cleared");
  }
}

void structureDamage() {
  // Reparent: the leaf leaves P1 for P2; damage covers where it was and where it landed.
  {
    Panels s;
    const Rect oldPlace = absOf(s.tree, s.b1);
    expect(s.inv.reparent(s.b1, s.p2) == r1ui::core::tree::TreeError::None, "reparent");
    const FrameResult f = s.inv.runFrame(&s.measure);
    const Rect newPlace = absOf(s.tree, s.b1);
    expectRect(newPlace, 310, 0, 60, 20, "landed after P2's own children");
    expect(covered(f.damage, oldPlace), "old place damaged");
    expect(covered(f.damage, newPlace), "new place damaged");
    expect(s.tree.parent(s.b1) == s.p2, "reparented");
  }
  // Reparent refused: nothing breaks.
  {
    Panels s;
    expect(s.inv.reparent(s.p1, s.a1) != r1ui::core::tree::TreeError::None, "cycle refused through the invalidator");
    (void)s.inv.runFrame(&s.measure);
  }
  // Destroy damages the area and re-lays out the parent.
  {
    Panels s;
    const Rect gone = absOf(s.tree, s.a1);
    expect(s.inv.destroy(s.a1) == r1ui::core::tree::TreeError::None, "destroy");
    const FrameResult f = s.inv.runFrame(&s.measure);
    expect(covered(f.damage, gone), "destroyed widget's area damaged");
    expectRect(absOf(s.tree, s.b1), 0, 0, 60, 20, "sibling slid into its place");
    expect(s.inv.destroy(s.a1) == r1ui::core::tree::TreeError::StaleId, "double destroy is refused");
  }
  // Create lays out the newcomer and damages its area.
  {
    Panels s;
    const auto r = s.inv.create(s.p2);
    expect(r.ok(), "create");
    s.tree.get(r.id)->style = sized(30, 40);
    s.inv.requestLayout(r.id);
    const FrameResult f = s.inv.runFrame(&s.measure);
    expectRect(absOf(s.tree, r.id), 310, 0, 30, 40, "new widget laid out after the existing children");
    expect(covered(f.damage, absOf(s.tree, r.id)), "new widget's area damaged");
  }
  // Reorder.
  {
    Panels s;
    expect(s.inv.reorder(s.b1, s.a1) == r1ui::core::tree::TreeError::None, "reorder");
    const FrameResult f = s.inv.runFrame(&s.measure);
    expectRect(absOf(s.tree, s.b1), 0, 0, 60, 20, "order changed the layout");
    expectRect(absOf(s.tree, s.a1), 60, 0, 50, 20, "and moved the other leaf");
    expect(covered(f.damage, Rect{0, 0, 60, 20}) && covered(f.damage, Rect{60, 0, 50, 20}) && covered(f.damage, Rect{50, 0, 60, 20}),
           "damage covers the old and new places of both leaves");
  }
}

// root 400x400 (row) holding two fixed 200x400 panels A and B: both are relayout boundaries, the
// way dock panels are.
struct TwoPanels {
  WidgetTree tree;
  Invalidator inv{tree};
  FixedMeasure measure;
  WidgetId root;
  WidgetId a;
  WidgetId b;
  TwoPanels() {
    root = addRoot(tree, sized(400, 400));
    a = addChild(tree, root, sized(200, 400));
    b = addChild(tree, root, sized(200, 400));
    inv.setRoot(root, 400, 400);
    (void)inv.runFrame(&measure);
  }
};

void reparentBetweenBoundaries() {
  // A widget created under A (which dirties it and queues A) and moved under B in the same frame
  // used to stay unplaced and dirty: its own dirty bit made requestLayout return early.
  TwoPanels s;
  const auto made = s.inv.create(s.a);
  s.tree.get(made.id)->style = sized(50, 50);
  expect(s.inv.reparent(made.id, s.b) == r1ui::core::tree::TreeError::None, "reparent accepted");
  (void)s.inv.runFrame(&s.measure);
  expectRect(absOf(s.tree, made.id), 200, 0, 50, 50, "the moved widget is placed under its new panel");
  expect(!s.tree.get(made.id)->layoutDirty, "and is clean after the frame");
  expect(!s.inv.needsFrame(), "nothing stays queued");

  // An existing, laid-out widget moved between panels in one frame.
  s.inv.reparent(made.id, s.a);
  (void)s.inv.runFrame(&s.measure);
  expectRect(absOf(s.tree, made.id), 0, 0, 50, 50, "moving back places it under the first panel");

  // A dirty widget whose chain ends at panel A is moved under a clean, deeper boundary of B.
  const WidgetId deep = addChild(s.tree, s.b, sized(100, 100));
  s.inv.requestLayout(deep);
  (void)s.inv.runFrame(&s.measure);
  const auto kid = s.inv.create(s.a);
  s.tree.get(kid.id)->style = sized(20, 20);
  s.inv.requestLayout(kid.id);  // dirty chain ends at A
  expect(s.inv.reparent(kid.id, deep) == r1ui::core::tree::TreeError::None, "reparent into a nested boundary");
  (void)s.inv.runFrame(&s.measure);
  expectRect(absOf(s.tree, kid.id), 200, 0, 20, 20, "a dirty widget moved to another boundary is laid out there");
  expect(!s.tree.get(kid.id)->layoutDirty && !s.tree.get(deep)->layoutDirty, "no dirty bits remain");
}

void requestLayoutOnDirtyBoundaryReachesParent() {
  // A boundary dirtied by a descendant's request is queued alone. If its own size inputs then
  // change (it stops being a fixed-size panel), the parent must be re-laid out as well.
  TwoPanels s;
  const WidgetId inner = addChild(s.tree, s.a, sized(10, 10));
  s.inv.requestLayout(inner);  // dirties inner and stops at boundary A
  s.tree.get(s.a)->style = Style{};  // A becomes content-sized (no longer a boundary)
  s.tree.get(s.a)->style.flexShrink = 0;
  s.inv.requestLayout(s.a);
  (void)s.inv.runFrame(&s.measure);
  expectRect(absOf(s.tree, s.a), 0, 0, 10, 400, "the panel shrank to its content and the row was re-laid out");
  expectRect(absOf(s.tree, s.b), 10, 0, 200, 400, "its sibling moved up against it");
}

void throwingMeasureKeepsQueuedRoots() {
  // A provider that throws during a pass must not lose the roots that did not get theirs.
  struct Throwing : MeasureProvider {
    bool armed = true;
    MeasureResult measure(WidgetId, const MeasureInput&) override {
      if (armed) throw std::runtime_error("measure failed");
      return {10, 10};
    }
  };
  TwoPanels s;
  Throwing thrower;
  Style leaf;
  leaf.hasMeasure = true;
  leaf.alignSelf = Align::Start;
  const WidgetId inA = addChild(s.tree, s.a, leaf);
  const WidgetId inB = addChild(s.tree, s.b, leaf);
  s.inv.requestLayout(inA);
  s.inv.requestLayout(inB);
  bool threw = false;
  try {
    (void)s.inv.runFrame(&thrower);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  expect(threw, "the measure failure reaches the caller");
  expect(s.inv.needsFrame(), "the unfinished roots are still queued");
  thrower.armed = false;
  (void)s.inv.runFrame(&thrower);
  expectRect(absOf(s.tree, inA), 0, 0, 10, 10, "first panel content laid out by the retry");
  expectRect(absOf(s.tree, inB), 200, 0, 10, 10, "second panel content laid out by the retry");
  expect(!s.tree.get(inA)->layoutDirty && !s.tree.get(inB)->layoutDirty, "clean after the retry");
}

void repeatedAnimationRequestsStayCheap() {
  TwoPanels s;
  std::vector<WidgetId> many;
  for (int i = 0; i < 2000; ++i) many.push_back(addChild(s.tree, s.a, sized(1, 1)));
  for (int round = 0; round < 50; ++round) {
    for (const WidgetId id : many) s.inv.requestAnimation(id);  // 100 000 requests, 2000 distinct widgets
  }
  const FrameResult f = s.inv.runFrame(&s.measure);
  expect(f.animating.size() == 2000, "each animating widget is listed once");
  s.inv.cancelAnimation(many[5]);
  s.inv.cancelAnimation(many[5]);
  expect(s.inv.runFrame(&s.measure).animating.size() == 1999, "cancel removes exactly one");
  s.inv.requestAnimation(many[5]);
  expect(s.inv.runFrame(&s.measure).animating.size() == 2000, "and it can be requested again");
}

void manyPaintRequestsStayBounded() {
  Panels s;
  WidgetTree& t = s.tree;
  std::vector<WidgetId> leaves;
  for (int i = 0; i < 30; ++i) {
    const WidgetId p = addChild(t, s.p3);
    leaves.push_back(addLeaf(t, p, s.measure, 5, 5));
  }
  s.inv.requestLayout(s.p3);
  (void)s.inv.runFrame(&s.measure);
  for (const WidgetId id : leaves) s.inv.requestPaint(id);
  const FrameResult f = s.inv.runFrame(&s.measure);
  expect(f.damage.size() <= 8, "the damage list respects its merge limit");
  bool all = true;
  for (const WidgetId id : leaves) all = all && covered(f.damage, absOf(t, id));
  expect(all, "every damaged widget is covered");
}

void hostileUse() {
  Panels s;
  const WidgetId stale{4242, 7};
  s.inv.requestLayout(stale);
  s.inv.requestPaint(stale);
  s.inv.requestAnimation(stale);
  s.inv.cancelAnimation(stale);
  s.inv.setVisible(stale, false);
  s.inv.setRoot(stale, 10, 10);
  s.inv.setRoot(s.p1, 10, 10);  // not a root: ignored
  expect(s.inv.destroy(stale) == r1ui::core::tree::TreeError::StaleId, "stale destroy through the invalidator");
  expect(!s.inv.needsFrame(), "stale requests leave the scheduler idle");

  // A queued boundary that dies before the frame runs.
  s.inv.requestLayout(s.a1);
  (void)s.tree.destroy(s.p1);
  const FrameResult f = s.inv.runFrame(&s.measure);
  (void)f;
  expect(!s.inv.needsFrame(), "a dead queued boundary is dropped");

  // The root dies: later frames must not crash.
  s.inv.requestLayout(s.c3);
  (void)s.tree.destroy(s.root);
  (void)s.inv.runFrame(&s.measure);
  s.inv.requestPaint(s.c3);
  (void)s.inv.runFrame(&s.measure);
  expect(!s.inv.needsFrame(), "everything quiet after the root is destroyed");
}

}  // namespace

int main() {
  runCase("damage_list_rules", damageListRules);
  runCase("paint_damage_equals_bounds", paintDamageEqualsBounds);
  runCase("idle_and_animation", idleAndAnimation);
  runCase("layout_isolation", layoutIsolation);
  runCase("structure_damage", structureDamage);
  runCase("reparent_between_boundaries", reparentBetweenBoundaries);
  runCase("dirty_boundary_request_reaches_parent", requestLayoutOnDirtyBoundaryReachesParent);
  runCase("throwing_measure_keeps_queued_roots", throwingMeasureKeepsQueuedRoots);
  runCase("repeated_animation_requests", repeatedAnimationRequestsStayCheap);
  runCase("many_paint_requests_stay_bounded", manyPaintRequestsStayBounded);
  runCase("hostile_use", hostileUse);
  return finish("ui-core.invalidation");
}

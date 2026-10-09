// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for docking behavior (spec 02 acceptance scenarios, spec 08 drag rules): tab
//   activation, drag threshold, drop zones and previews, split/join/float/edge docking, closing
//   and the collapse rules, plus hostile pointer values and a randomised invariant check.
// Callers: CTest (label fast). Case names follow the spec's scenario numbers.
#include <cstdlib>

#include "TestSupport.h"

using namespace dock_test;

namespace {

constexpr Rect kRegion600{0, 0, 600, 400};

DragQuery queryAt(PanelId panel, double x, double y, Point grab = {}) { return {{x, y}, panel, grab}; }

// Scenario 1: B becomes front when activated; tab strip lists the tabs in order.
void docking_scenario_01() {
  DockLayout dock = build(3, Node::stack({1, 2, 3}));
  expect(dock.activateTab(2).ok, "activate B");
  const LayoutResult layout = dock.computeLayout(kRegion600);
  expect(frontOf(layout, 1) == 2, "B is the front tab");
  expect(layout.stacks.size() == 1 && layout.stacks[0].tabs.size() == 3, "one stack, three tabs");
  expect(!dock.activateTab(99).ok, "activating an undocked panel is an error");
}

// Scenario 2: 3 px is a click, 6 px is a drag; a reorder drop over the first slot puts C first.
void docking_scenario_02() {
  const DockConfig config;
  expect(!dragExceedsThreshold({100, 100}, {103, 100}, config), "3 px is below the threshold");
  expect(!dragExceedsThreshold({100, 100}, {105, 100}, config), "exactly 5 px is not strictly greater (spec 08 rule 1)");
  expect(dragExceedsThreshold({100, 100}, {106, 100}, config), "6 px starts a drag");
  expect(dragExceedsThreshold({0, 0}, {4, 4}, config), "diagonal distance counts (5.66 px)");
  expect(!dragExceedsThreshold({0, 0}, {std::nan(""), 0}, config), "NaN never starts a drag");

  DockLayout dock = build(3, Node::stack({1, 2, 3}));
  const LayoutResult layout = dock.computeLayout(kRegion600);
  const DropZone zone = dock.hitTestDropZone(layout, queryAt(3, 10, 10));
  expect(zone.kind == DropKind::JoinStack && zone.index == 0, "pointer over the first slot joins at 0");
  expect(dock.dock(3, zone).ok, "reorder drop succeeds");
  const Node& root = *dock.areas()[0].root;
  expect(root.tabs == std::vector<PanelId>({3, 1, 2}) && root.active == 0, "C first and front");

  // Slot counts only the other tabs: pointer far right of the strip lands after the last tab.
  const DropZone last = dock.hitTestDropZone(dock.computeLayout(kRegion600), queryAt(3, 590, 10));
  expect(last.kind == DropKind::JoinStack && last.index == 2, "far right = slot 2 (two other tabs)");
}

// Scenario 3: dragging B out leaves the neighbour front; ghost keeps the grab point.
void docking_scenario_03() {
  DockLayout dock = build(3, Node::stack({1, 2, 3}, 1));
  const LayoutResult layout = dock.computeLayout({0, 0, 2000, 1000});
  const DropZone zone = dock.hitTestDropZone(layout, queryAt(2, 5000, 5000, {30, 10}));
  expect(zone.kind == DropKind::Float, "outside every area floats");
  expect(near(zone.preview.x, 4970) && near(zone.preview.y, 4990), "ghost top-left keeps the grab offset");
  expect(near(zone.preview.w, 800) && near(zone.preview.h, 400), "ghost long side capped at 800 (2000x1000 -> 800x400)");
  expect(dock.dock(2, zone).ok, "dock as floating");
  expect(dock.areas().size() == 2, "a floating area exists");
  expect(dock.areas()[0].root->tabs == std::vector<PanelId>({1, 3}), "B left the stack");
  expect(dock.areas()[0].root->active == 1, "right neighbour C became front");

  DockLayout last = build(3, Node::stack({1, 2, 3}, 2));
  expect(last.closePanel(3).ok && last.areas()[0].root->active == 1, "closing the last front tab fronts the left neighbour");
}

// Scenario 4: 400x300 content -> inset 120x90, left zone, left-half preview, equal weights after the drop.
void docking_scenario_04() {
  DockLayout dock = build(2, Node::stack({1, 2}));
  const Rect main{0, 0, 400, 325};  // 25 px strip + 400x300 body
  const LayoutResult layout = dock.computeLayout(main);
  expect(near(layout.stacks[0].body.h, 300) && near(layout.stacks[0].body.w, 400), "body is 400x300");

  const DropZone left = dock.hitTestDropZone(layout, queryAt(2, 50, 175));
  expect(left.kind == DropKind::SplitStack && left.side == Side::Left, "50 px from the left edge picks Left");
  expect(left.preview == Rect({0, 0, 200, 325}), "preview is the left half of the region");
  expect(dock.hitTestDropZone(layout, queryAt(2, 119, 175)).kind == DropKind::SplitStack, "119 px is inside the 120 px band");
  expect(dock.hitTestDropZone(layout, queryAt(2, 121, 175)).kind == DropKind::Float, "121 px is inside the inner rectangle: no target");
  expect(dock.hitTestDropZone(layout, queryAt(2, 200, 25 + 89)).side == Side::Top, "89 px below the strip is the top band (90 px)");
  expect(dock.hitTestDropZone(layout, queryAt(2, 200, 324 - 20)).side == Side::Bottom, "bottom band");
  expect(dock.hitTestDropZone(layout, queryAt(2, 399 - 20, 175)).side == Side::Right, "right band");
  // Diagonal: near the top-left corner the shallower normalised depth wins.
  const DropZone corner = dock.hitTestDropZone(layout, queryAt(2, 40, 25 + 10));
  expect(corner.side == Side::Top, "corner point closer (relative) to the top edge picks Top");

  expect(dock.dock(2, left).ok, "drop on the left zone");
  const Node& root = *dock.areas()[0].root;
  expect(root.kind == Node::Kind::Split && root.axis == Axis::Row && root.children.size() == 2, "a row of two");
  expect(root.children[0].tabs == std::vector<PanelId>({2}) && root.children[1].tabs == std::vector<PanelId>({1}),
         "new region on the left holds the dragged tab");
  expect(root.children[0].weight == root.children[1].weight, "equal weights");
  const LayoutResult after = dock.computeLayout({0, 0, 405, 300});
  expect(near(stackOf(after, 2)->bounds.w, 200) && near(stackOf(after, 1)->bounds.w, 200), "each gets half");
}

// Scenario 5: splitting R1 on its right in a row of two equal regions gives thirds.
void docking_scenario_05() {
  DockLayout dock = build(3, Node::split(Axis::Row, {Node::stack({1}), Node::stack({2})}));
  const LayoutResult layout = dock.computeLayout({0, 0, 1010, 300});
  const DropZone zone = dock.hitTestDropZone(layout, queryAt(3, 100 + 0, 150));
  expect(zone.kind == DropKind::SplitStack, "pointer in R1 offers a split");
  DropZone right = zone;
  right.side = Side::Right;
  right.stackPanel = 1;
  expect(dock.dock(3, right).ok, "split R1 on the right");
  const Node& root = *dock.areas()[0].root;
  expect(root.children.size() == 3 && root.children[0].tabs[0] == 1 && root.children[1].tabs[0] == 3 && root.children[2].tabs[0] == 2,
         "order R1, new, R2");
  expect(root.children[0].weight == root.children[1].weight && root.children[1].weight == root.children[2].weight, "equal weights");
  const LayoutResult after = dock.computeLayout({0, 0, 1010, 300});
  for (const StackLayout& s : after.stacks) expect(near(s.bounds.w, 1000.0 / 3.0), "each region is one third");
}

// Scenario 6: bottom edge target flips a row to a column; the old row becomes one child.
void docking_scenario_06() {
  DockLayout dock = build(3, Node::split(Axis::Row, {Node::stack({1}), Node::stack({2})}));
  const Rect main{0, 0, 1010, 600};
  const LayoutResult layout = dock.computeLayout(main);
  const DropZone edge = dock.hitTestDropZone(layout, queryAt(3, 500, 597));
  expect(edge.kind == DropKind::SplitAreaEdge && edge.side == Side::Bottom, "bottom 6 px band is the edge target");
  expect(edge.preview == Rect({0, 300, 1010, 300}), "edge preview is the lower half of the area");
  expect(dock.hitTestDropZone(layout, queryAt(3, 500, 600 - 7)).kind != DropKind::SplitAreaEdge, "7 px is outside the 6 px bar");
  expect(dock.dock(3, edge).ok, "drop on the bottom edge");
  const Node& root = *dock.areas()[0].root;
  expect(root.axis == Axis::Column && root.children.size() == 2, "area flipped to a column");
  expect(root.children[0].kind == Node::Kind::Split && root.children[0].axis == Axis::Row, "old row is one child");
  expect(root.children[1].tabs == std::vector<PanelId>({3}), "new region last");
  const LayoutResult after = dock.computeLayout(main);
  expect(near(stackOf(after, 3)->bounds.w, 1010) && near(stackOf(after, 3)->bounds.h, 297.5, 1.5), "new region spans the width");

  DockLayout top = build(3, Node::split(Axis::Column, {Node::stack({1}), Node::stack({2})}));
  const DropZone topEdge = top.hitTestDropZone(top.computeLayout(main), queryAt(3, 500, 2));
  expect(topEdge.side == Side::Top && top.dock(3, topEdge).ok, "top edge on a column");
  expect(top.areas()[0].root->children.size() == 3 && top.areas()[0].root->children[0].tabs[0] == 3,
         "matching direction: new region first, siblings preserved");
  const LayoutResult t = top.computeLayout(main);
  expect(near(stackOf(t, 3)->bounds.h, (600 - 10) / 2.0, 1.5), "new full-length region takes half of the area");
}

// Scenario 7: dropping on the region's centre floats the tab, sized like the ghost.
void docking_scenario_07() {
  DockLayout dock = build(2, Node::stack({1, 2}));
  const LayoutResult layout = dock.computeLayout(kRegion600);
  const DropZone zone = dock.hitTestDropZone(layout, queryAt(2, 300, 220, {10, 5}));
  expect(zone.kind == DropKind::Float, "centre of the content has no target");
  expect(zone.preview == Rect({290, 215, 600, 400}), "ghost is the origin region size at the grab point");
  expect(dock.dock(2, zone).ok && dock.areas().size() == 2, "floating area created");
  DockLayout other = build(2, Node::stack({1, 2}));
  const LayoutResult otherLayout = other.computeLayout(kRegion600);
  const DropZone over = other.hitTestDropZone(otherLayout, queryAt(2, 50, 175));
  expect(over.kind == DropKind::SplitStack, "setup: pointer over a side zone");
  const DropZone esc = other.floatZone(otherLayout, queryAt(2, 50, 175));
  expect(esc.kind == DropKind::Float && esc.preview == Rect({50, 175, 600, 400}), "Escape lands as a float at the ghost position (spec 02 rule 21)");
  expect(other.dock(2, esc).ok && other.areas().size() == 2, "and docks as a floating area");
  expect(dock.areas()[1].rect == zone.preview && dock.areas()[1].root->tabs == std::vector<PanelId>({2}), "one tab, ghost rectangle");
  const LayoutResult after = dock.computeLayout(kRegion600);
  expect(after.areas.size() == 2 && after.areas[1].floating, "layout reports the floating area");
}

// Scenario 8 (close part only): B closes, C becomes front; history/reopen is not modelled.
void docking_scenario_08() {
  DockLayout dock = build(3, Node::stack({1, 2, 3}, 1));
  expect(dock.closePanel(2).ok, "close B");
  expect(dock.areas()[0].root->tabs == std::vector<PanelId>({1, 3}) && dock.areas()[0].root->active == 1, "C became front");
  expect(!dock.isDocked(2), "B is closed (registered, not docked)");

  DockLayout keep = build(3, Node::stack({1, 2, 3}, 2));
  expect(keep.closePanel(1).ok && keep.areas()[0].root->tabs[keep.areas()[0].root->active] == 3, "closing a back tab keeps the front tab");

  DockLayout pinned = DockLayout::create(makePanels(2, {1}), {}, Node::stack({1, 2})).layout.value();
  expect(!pinned.closePanel(1).ok && pinned.isDocked(1), "a panel that cannot close stays");
  expect(!pinned.closePanel(42).ok, "unknown panel");
}

// Scenario 9: closing a region's only tab removes it and siblings expand proportionally.
void docking_scenario_09() {
  DockLayout dock = build(3, Node::split(Axis::Row, {Node::stack({1}, 0, 1.0), Node::stack({2}, 0, 2.0), Node::stack({3}, 0, 3.0)}));
  expect(dock.closePanel(2).ok, "close the middle region");
  const Node& root = *dock.areas()[0].root;
  expect(root.children.size() == 2, "two regions remain");
  const LayoutResult layout = dock.computeLayout({0, 0, 1005, 300});
  expect(near(stackOf(layout, 1)->bounds.w, 250) && near(stackOf(layout, 3)->bounds.w, 750), "siblings keep their 1:3 proportion");
  expect(layout.handles.size() == 1, "one handle remains");
}

// Scenario 13 (size part): a never-docked panel floats at the default 1000x600.
void docking_scenario_13() {
  DockLayout dock = build(2, Node::stack({1}));
  const DropZone zone = dock.hitTestDropZone(dock.computeLayout(kRegion600), queryAt(2, 9000, 9000));
  expect(zone.kind == DropKind::Float && near(zone.preview.w, 800) && near(zone.preview.h, 480),
         "default 1000x600 scaled so the long side is 800");
  DockConfig big;
  big.ghostMaxLongSide = 2000;
  DockLayout dock2 = build(2, Node::stack({1}), big);
  const DropZone z2 = dock2.hitTestDropZone(dock2.computeLayout(kRegion600), queryAt(2, 9000, 9000));
  expect(z2.preview.w == 1000 && z2.preview.h == 600, "unscaled default size is 1000x600");
}

// Join a stack at an arbitrary index and drag a tab between stacks.
void docking_join_stack_at_index() {
  DockLayout dock = build(4, Node::split(Axis::Row, {Node::stack({1, 2, 3}), Node::stack({4})}));
  const LayoutResult layout = dock.computeLayout({0, 0, 1005, 300});
  const StackLayout& first = *stackOf(layout, 1);
  const double tabW = first.tabs[0].rect.w;
  const DropZone zone = dock.hitTestDropZone(layout, queryAt(4, first.strip.x + tabW * 1.5, 10));
  expect(zone.kind == DropKind::JoinStack && zone.index == 1 && zone.stackPanel == 1, "slot 1 of the first stack");
  expect(zone.preview == Rect({first.strip.x + tabW, first.strip.y, tabW, first.strip.h}), "insertion preview is one tab wide");
  expect(dock.dock(4, zone).ok, "join");
  expect(dock.areas()[0].root->kind == Node::Kind::Stack, "emptied stack vanished, split collapsed");
  expect(dock.areas()[0].root->tabs == std::vector<PanelId>({1, 4, 2, 3}) && dock.areas()[0].root->active == 1, "inserted at index 1, now front");
  expect(!dock.dock(4, DropZone{DropKind::JoinStack, 0, 77, Side::Left, 0, {}}).ok, "stale target stack is rejected");
}

// Closing the last tab collapses the structure (spec 02 rules 43-46).
void docking_collapse_on_close() {
  DockLayout dock = build(3, Node::split(Axis::Row, {Node::stack({1}), Node::split(Axis::Column, {Node::stack({2}), Node::stack({3})})}));
  expect(dock.closePanel(3).ok, "close C");
  const Node& root = *dock.areas()[0].root;
  expect(root.kind == Node::Kind::Split && root.children.size() == 2 && root.children[1].kind == Node::Kind::Stack, "single-child column collapsed");
  expect(dock.closePanel(2).ok && dock.areas()[0].root->kind == Node::Kind::Stack, "one region left, split gone");
  expect(dock.closePanel(1).ok && !dock.areas()[0].root.has_value() && dock.areas().size() == 1, "main area stays, empty");
  const LayoutResult layout = dock.computeLayout(kRegion600);
  expect(layout.areas.size() == 1 && layout.areas[0].empty && layout.stacks.empty(), "empty area lays out nothing");
  const DropZone zone = dock.hitTestDropZone(layout, queryAt(1, 100, 100));
  expect(zone.kind == DropKind::FillEmptyArea && zone.preview == kRegion600, "empty area offers the centre target");
  expect(dock.dock(1, zone).ok && dock.areas()[0].root->tabs == std::vector<PanelId>({1}), "first region created");
}

// A floating area disappears with its last tab, and dragging a floating tab back docks it.
void docking_floating_lifecycle() {
  DockLayout dock = build(2, Node::stack({1, 2}));
  DropZone zone;
  zone.kind = DropKind::Float;
  zone.preview = {50, 60, 300, 200};
  expect(dock.dock(2, zone).ok && dock.areas().size() == 2, "float B");
  const LayoutResult layout = dock.computeLayout(kRegion600);
  // Pointer over the floating area's own sole tab: that area is not a target, the main area is.
  const DropZone over = dock.hitTestDropZone(layout, queryAt(2, 100, 100));
  expect(over.area == kMainAreaId, "an area holding only the dragged tab is skipped");
  const DropZone back = dock.hitTestDropZone(layout, queryAt(2, 500, 10));
  expect(back.kind == DropKind::JoinStack && back.stackPanel == 1, "main strip accepts it");
  expect(dock.dock(2, back).ok && dock.areas().size() == 1, "floating area removed when its tab leaves");
  expect(dock.dock(2, zone).ok && dock.areas().size() == 2, "floating area re-created");
  expect(dock.closePanel(2).ok, "close floating B");
  expect(dock.validate().ok, "layout still valid");
}

// Own-content rule: a sole tab cannot be split off its own stack.
void docking_own_content_rule() {
  DockLayout dock = build(2, Node::split(Axis::Row, {Node::stack({1}), Node::stack({2})}));
  const LayoutResult layout = dock.computeLayout({0, 0, 1005, 300});
  const DropZone zone = dock.hitTestDropZone(layout, queryAt(1, 40, 150));
  expect(zone.kind == DropKind::Float, "own sole stack offers no split zones");
  DockLayout before = dock;
  DropZone forced{DropKind::SplitStack, 0, 1, Side::Left, 0, {}};
  expect(!dock.dock(1, forced).ok && dock == before, "a forced split onto the own sole stack is refused without changes");
  DockLayout single = build(1, Node::stack({1}));
  const DropZone edge = single.hitTestDropZone(single.computeLayout(kRegion600), queryAt(1, 300, 2));
  expect(edge.kind == DropKind::Float, "area holding only the dragged tab offers no edge target");
}

// Hostile inputs never produce NaN geometry or a crash.
void docking_hostile_inputs() {
  DockLayout dock = build(3, Node::split(Axis::Row, {Node::stack({1}), Node::stack({2, 3})}));
  const double nan = std::nan("");
  const double inf = HUGE_VAL;
  const LayoutResult nanRect = dock.computeLayout({nan, nan, nan, nan});
  for (const StackLayout& s : nanRect.stacks) expect(s.bounds.w == 0 && s.bounds.h == 0 && std::isfinite(s.bounds.x), "NaN window gives empty rects");
  const LayoutResult negative = dock.computeLayout({0, 0, -500, -10});
  for (const StackLayout& s : negative.stacks) expect(s.bounds.w >= 0 && s.bounds.h >= 0, "negative size clamps to 0");
  const LayoutResult huge = dock.computeLayout({0, 0, inf, 1e300});
  for (const StackLayout& s : huge.stacks) expect(std::isfinite(s.bounds.w) && std::isfinite(s.bounds.h), "infinite size stays finite");

  const LayoutResult layout = dock.computeLayout(kRegion600);
  for (double v : {nan, inf, -inf, 1e300}) {
    const DropZone zone = dock.hitTestDropZone(layout, {{v, v}, 3, {v, nan}});
    expect(std::isfinite(zone.preview.x) && std::isfinite(zone.preview.w), "hostile pointer yields a finite preview");
  }
  DropZone bad;
  bad.kind = DropKind::Float;
  bad.preview = {nan, 0, 100, 100};
  DockLayout before = dock;
  expect(!dock.dock(3, bad).ok && dock == before, "NaN float rectangle rejected, layout untouched");
  bad.preview = {0, 0, 100, 1e300};
  expect(!dock.dock(3, bad).ok && dock == before, "absurd float rectangle rejected");
  bad.preview = {1e9, 0, 100, 100};
  expect(!dock.dock(3, bad).ok && dock == before, "far-away float rectangle rejected");
  expect(!dock.dock(99, DropZone{}).ok, "unknown panel rejected");
}

// Many random operations: the layout stays valid, failed ops change nothing, serialisation round-trips.
void docking_random_operations_keep_invariants() {
  DockLayout dock = build(9, Node::split(Axis::Row, {Node::stack({1, 2}), Node::split(Axis::Column, {Node::stack({3, 4, 5}), Node::stack({6, 7})}), Node::stack({8, 9})}));
  uint64_t state = 0x9E3779B97F4A7C15ull;
  const auto next = [&](uint32_t bound) {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<uint32_t>((state >> 33) % bound);
  };
  const Rect main{0, 0, 1200, 800};
  int accepted = 0;
  for (int step = 0; step < 3000; ++step) {
    const DockLayout before = dock;
    const LayoutResult layout = dock.computeLayout(main);
    Status status;
    switch (next(5)) {
      case 0: {
        const PanelId panel = 1 + next(9);
        const DropZone zone = dock.hitTestDropZone(layout, queryAt(panel, next(1300), next(900), {static_cast<double>(next(40)), static_cast<double>(next(20))}));
        status = dock.dock(panel, zone);
        break;
      }
      case 1: status = dock.closePanel(1 + next(9)); break;
      case 2: status = dock.activateTab(1 + next(9)); break;
      case 3:
        if (!layout.handles.empty()) status = dock.moveSplitter(layout.handles[next(static_cast<uint32_t>(layout.handles.size()))].handle, static_cast<double>(next(600)) - 300.0, main);
        break;
      default: {
        const PanelId panel = 1 + next(9);
        DropZone zone = dock.hitTestDropZone(layout, queryAt(panel, next(1300), next(900)));
        zone.kind = DropKind::Float;
        status = dock.dock(panel, zone);
        break;
      }
    }
    accepted += status.ok ? 1 : 0;
    if (!dock.validate().ok) {
      expect(false, "layout invalid after an operation");
      break;
    }
    if (!status.ok) expect(dock == before, "a failed operation changed the layout");
  }
  expect(accepted > 1000, "most random operations were accepted (test is meaningful)");
  const LoadResult loaded = DockLayout::fromJson(dock.toJson(), makePanels(9));
  expect(loaded.ok() && *loaded.layout == dock, "random end state round-trips through JSON");
}

}  // namespace

int main() {
  runCase("docking_scenario_01", docking_scenario_01);
  runCase("docking_scenario_02", docking_scenario_02);
  runCase("docking_scenario_03", docking_scenario_03);
  runCase("docking_scenario_04", docking_scenario_04);
  runCase("docking_scenario_05", docking_scenario_05);
  runCase("docking_scenario_06", docking_scenario_06);
  runCase("docking_scenario_07", docking_scenario_07);
  runCase("docking_scenario_08", docking_scenario_08);
  runCase("docking_scenario_09", docking_scenario_09);
  runCase("docking_scenario_13", docking_scenario_13);
  runCase("docking_join_stack_at_index", docking_join_stack_at_index);
  runCase("docking_collapse_on_close", docking_collapse_on_close);
  runCase("docking_floating_lifecycle", docking_floating_lifecycle);
  runCase("docking_own_content_rule", docking_own_content_rule);
  runCase("docking_hostile_inputs", docking_hostile_inputs);
  runCase("docking_random_operations_keep_invariants", docking_random_operations_keep_invariants);
  return finish("docking_test");
}

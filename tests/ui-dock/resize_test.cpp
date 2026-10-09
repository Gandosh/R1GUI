// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for panel resizing (spec 05 acceptance scenarios): weight-based space sharing,
//   splitter handles and hit band, dragging with neighbour-only changes, floor clamping,
//   proportional window resize, tab strip width, plus degenerate window sizes.
// Callers: CTest (label fast). Case names follow the spec's scenario numbers.
#include "TestSupport.h"

using namespace dock_test;

namespace {

DockLayout rowAB(const DockConfig& config = {}) {
  return build(2, Node::split(Axis::Row, {Node::stack({1}), Node::stack({2})}), config);
}

const HandleLayout& onlyHandle(const LayoutResult& layout) { return layout.handles.at(0); }

// Scenario 1: 1005 px wide, equal weights: 500 + 5 + 500.
void resize_scenario_01() {
  DockLayout dock = rowAB();
  const LayoutResult layout = dock.computeLayout({0, 0, 1005, 300});
  expect(stackOf(layout, 1)->bounds == Rect({0, 0, 500, 300}), "A is 500 wide");
  expect(stackOf(layout, 2)->bounds == Rect({505, 0, 500, 300}), "B is 500 wide, after a 5 px handle");
  expect(layout.handles.size() == 1 && onlyHandle(layout).rect == Rect({500, 0, 5, 300}), "handle is 5 px between them");
}

// Scenario 2: dragging the handle 100 px right gives 600/400 and weights 3:2 (combined 2).
void resize_scenario_02() {
  DockLayout dock = rowAB();
  const Rect main{0, 0, 1005, 300};
  const SplitterHandle handle = onlyHandle(dock.computeLayout(main)).handle;
  expect(dock.moveSplitter(handle, 100, main).ok, "move");
  const LayoutResult layout = dock.computeLayout(main);
  expect(near(stackOf(layout, 1)->bounds.w, 600, 0.01) && near(stackOf(layout, 2)->bounds.w, 400, 0.01), "600 / 400");
  const Node& root = *dock.areas()[0].root;
  expect(near(root.children[0].weight, 1.2, 1e-9) && near(root.children[1].weight, 0.8, 1e-9), "weights keep the 3:2 ratio, combined 2");
  expect(dock.moveSplitter(handle, 0, main).ok && near(stackOf(dock.computeLayout(main), 1)->bounds.w, 600, 0.01), "zero movement is ignored");
}

// Scenario 3: with three equal regions only the two beside the dragged handle change.
void resize_scenario_03() {
  DockLayout dock = build(3, Node::split(Axis::Row, {Node::stack({1}), Node::stack({2}), Node::stack({3})}));
  const Rect main{0, 0, 1010, 300};
  const LayoutResult before = dock.computeLayout(main);
  const SplitterHandle first = before.handles[0].handle;
  expect(first.index == 0, "first handle sits between A and B");
  expect(dock.moveSplitter(first, 60, main).ok, "move");
  const LayoutResult after = dock.computeLayout(main);
  expect(near(stackOf(after, 3)->bounds.w, stackOf(before, 3)->bounds.w, 0.5), "C is unchanged");
  expect(near(stackOf(after, 1)->bounds.w, stackOf(before, 1)->bounds.w + 60, 0.5), "A grew by 60");
  expect(near(stackOf(after, 2)->bounds.w, stackOf(before, 2)->bounds.w - 60, 0.5), "B shrank by 60");
  const double sum = stackOf(after, 1)->bounds.w + stackOf(after, 2)->bounds.w + stackOf(after, 3)->bounds.w;
  expect(near(sum, 1000, 0.5), "total usable space is preserved");
}

// Scenario 4: the hit band equals the 5 px bar; 6 px away it is gone. Orientation follows the axis.
void resize_scenario_04() {
  DockLayout dock = rowAB();
  const LayoutResult layout = dock.computeLayout({0, 0, 1005, 300});
  expect(hitTestHandle(layout, {502, 100}).has_value(), "inside the bar");
  expect(hitTestHandle(layout, {500, 100}).has_value(), "left edge of the bar");
  expect(!hitTestHandle(layout, {505, 100}).has_value(), "right edge is outside (half-open)");
  expect(!hitTestHandle(layout, {499, 100}).has_value(), "just left of the bar");
  expect(!hitTestHandle(layout, {502 + 6, 100}).has_value(), "6 px off: no hover");
  expect(hitTestHandle(layout, {502, 100})->handle.axis == Axis::Row, "row split: horizontal-arrow handle");
  DockLayout column = build(2, Node::split(Axis::Column, {Node::stack({1}), Node::stack({2})}));
  const LayoutResult c = column.computeLayout({0, 0, 300, 1005});
  expect(hitTestHandle(c, {100, 502})->handle.axis == Axis::Column && onlyHandle(c).rect == Rect({0, 500, 300, 5}), "column split: vertical-arrow handle spans the width");

  // A floating area above the main area hides the handle beneath it.
  DockLayout over = build(3, Node::split(Axis::Row, {Node::stack({1}), Node::stack({2})}));
  DropZone floatZone;
  floatZone.preview = {450, 50, 300, 200};
  expect(over.dock(3, floatZone).ok, "float C over the handle");
  expect(!hitTestHandle(over.computeLayout({0, 0, 1005, 300}), {502, 100}).has_value(), "handle under a floating area is not hit");
}

// Scenario 5: the handle stops at the clamp however far the pointer travels.
void resize_scenario_05() {
  DockLayout dock = rowAB();
  const Rect main{0, 0, 1005, 300};
  const SplitterHandle handle = onlyHandle(dock.computeLayout(main)).handle;
  expect(dock.moveSplitter(handle, 1.0e9, main).ok, "huge move right");
  LayoutResult layout = dock.computeLayout(main);
  expect(near(stackOf(layout, 2)->bounds.w, 20, 0.01) && near(stackOf(layout, 1)->bounds.w, 980, 0.01), "B stops at the 20 px floor");
  expect(dock.moveSplitter(handle, 50, main).ok && near(stackOf(dock.computeLayout(main), 2)->bounds.w, 20, 0.01), "further travel is ignored");
  expect(dock.moveSplitter(handle, -1.0e9, main).ok, "huge move left");
  layout = dock.computeLayout(main);
  expect(near(stackOf(layout, 1)->bounds.w, 20, 0.01) && near(stackOf(layout, 2)->bounds.w, 980, 0.01), "A stops at the floor");
  DockLayout before = dock;
  expect(!dock.moveSplitter(handle, std::nan(""), main).ok && dock == before, "NaN delta refused");
  expect(!dock.moveSplitter(handle, -HUGE_VAL, main).ok && dock == before && dock.validate().ok, "infinite delta refused");
}

// Scenario 6: floor behaviour: default 20 (spec 05 rule 14), a larger one, and 0 (reference).
void resize_scenario_06() {
  const Rect main{0, 0, 1005, 300};
  for (const double floor : {0.0, 20.0, 40.0}) {
    DockConfig config;
    config.minPanelSize = floor;
    DockLayout dock = rowAB(config);
    const SplitterHandle handle = onlyHandle(dock.computeLayout(main)).handle;
    expect(dock.moveSplitter(handle, 1.0e6, main).ok, "push B to the floor");
    const LayoutResult layout = dock.computeLayout(main);
    expect(near(stackOf(layout, 2)->bounds.w, floor, 0.01), "B is exactly the configured floor");
    expect(dock.validate().ok, "weights stay legal even at floor 0");
    if (floor == 0.0) expect(stackOf(layout, 2)->bounds.w == 0 && layout.handles.size() == 1, "floor 0: only the handle remains visible");
  }
  expect(DockConfig{}.minPanelSize == 20.0, "documented default floor is 20 (spec 05 rule 14)");

  // Spec 05 rule 19: a child below the floor is raised, the shortfall comes from later siblings.
  DockLayout skewed = build(3, Node::split(Axis::Row, {Node::stack({1}, 0, 0.001), Node::stack({2}), Node::stack({3})}));
  const LayoutResult layout = skewed.computeLayout({0, 0, 1010, 300});
  expect(near(stackOf(layout, 1)->bounds.w, 20, 0.5), "tiny weight is raised to the floor");
  expect(near(stackOf(layout, 1)->bounds.w + stackOf(layout, 2)->bounds.w + stackOf(layout, 3)->bounds.w, 1000, 0.5), "total preserved");
  // Not enough room for every floor: plain proportional sharing.
  const LayoutResult tight = skewed.computeLayout({0, 0, 50, 300});
  expect(near(stackOf(tight, 2)->bounds.w, stackOf(tight, 3)->bounds.w, 1.0), "proportional when floors cannot be met");
}

// Scenario 7: window growth keeps the 3:2 ratio. Scenario 8: maximise/restore is lossless.
void resize_scenario_07_08() {
  DockLayout dock = rowAB();
  const Rect small{0, 0, 1005, 300};
  dock.moveSplitter(onlyHandle(dock.computeLayout(small)).handle, 100, small);
  const LayoutResult grown = dock.computeLayout({0, 0, 2005, 300});
  expect(near(stackOf(grown, 1)->bounds.w, 1200, 0.01) && near(stackOf(grown, 2)->bounds.w, 800, 0.01), "1200 / 800 at 2005 px");
  const LayoutResult before = dock.computeLayout(small);
  dock.computeLayout({0, 0, 1920, 1080});
  expect(dock.computeLayout(small).stacks.size() == before.stacks.size() && dock.computeLayout(small).stacks[0].bounds == before.stacks[0].bounds &&
         dock.computeLayout(small).stacks[1].bounds == before.stacks[1].bounds, "restore returns identical proportions");
  // Spec 05 rule 25 / edge case 5: shrinking to nothing clips instead of failing.
  const LayoutResult nothing = dock.computeLayout({0, 0, 3, 300});
  for (const StackLayout& s : nothing.stacks) expect(s.bounds.w >= 0 && std::isfinite(s.bounds.x), "no negative width when the window is too small");
  expect(nothing.handles.size() == 1, "handles keep their 5 px each");
}

// Scenario 9: closing the only tab of B leaves no handle and A fills the row.
void resize_scenario_09() {
  DockLayout dock = rowAB();
  expect(dock.closePanel(2).ok, "close B");
  const LayoutResult layout = dock.computeLayout({0, 0, 1005, 300});
  expect(layout.handles.empty() && stackOf(layout, 1)->bounds == Rect({0, 0, 1005, 300}), "A fills the area, no handle");
}

// Scenario 11: 12 tabs in a 720 px strip are 60 px each; tabs never exceed 160 px. Decision D12:
// in a narrower strip they keep the 60 px minimum and the strip reports overflow.
void resize_scenario_11() {
  std::vector<PanelId> ids;
  for (PanelId i = 1; i <= 12; ++i) ids.push_back(i);
  DockLayout dock = build(12, Node::stack(ids));
  const LayoutResult layout = dock.computeLayout({0, 0, 720, 300});
  expect(layout.stacks[0].tabs.size() == 12, "twelve tabs");
  for (const TabLayout& t : layout.stacks[0].tabs) expect(near(t.rect.w, 60, 0.001) && t.rect.h == 25, "60x25 each");
  expect(!layout.stacks[0].overflow, "exactly fitting tabs do not overflow");
  const LayoutResult narrow = dock.computeLayout({0, 0, 480, 300});
  for (const TabLayout& t : narrow.stacks[0].tabs) expect(near(t.rect.w, 60, 0.001), "tabs keep the 60 px minimum (D12)");
  expect(narrow.stacks[0].overflow && near(narrow.stacks[0].tabsWidth, 720, 0.001), "the narrow strip overflows");
  DockLayout few = build(2, Node::stack({1, 2}));
  expect(few.computeLayout({0, 0, 1000, 300}).stacks[0].tabs[0].rect.w == 160, "capped at 160 px");
}

// Neighbour rule and nesting: handles of nested splits address the right pair.
void resize_nested_handles() {
  DockLayout dock = build(4, Node::split(Axis::Row, {Node::stack({1}), Node::split(Axis::Column, {Node::stack({2}), Node::stack({3})}), Node::stack({4})}));
  const Rect main{0, 0, 1010, 505};
  const LayoutResult layout = dock.computeLayout(main);
  expect(layout.handles.size() == 3 && layout.splits.size() == 2, "two row handles and one column handle");
  const auto column = std::find_if(layout.handles.begin(), layout.handles.end(), [](const HandleLayout& h) { return h.handle.axis == Axis::Column; });
  expect(column != layout.handles.end() && column->handle.path == Path({1}), "column handle addresses the nested split");
  expect(dock.moveSplitter(column->handle, -50, main).ok, "move the nested handle");
  const LayoutResult after = dock.computeLayout(main);
  expect(near(stackOf(after, 2)->bounds.h, stackOf(layout, 2)->bounds.h - 50, 0.5) && near(stackOf(after, 1)->bounds.w, stackOf(layout, 1)->bounds.w, 0.01), "only the column pair changed");
  SplitterHandle stale = column->handle;
  stale.path = {7};
  DockLayout copy = dock;
  expect(!dock.moveSplitter(stale, 5, main).ok && dock == copy, "stale handle rejected, layout untouched");
  stale = column->handle;
  stale.index = 9;
  expect(!dock.moveSplitter(stale, 5, main).ok, "handle index out of range rejected");
  // Edges are shared: no overlap or gap other than the handle.
  const Rect odd{0, 0, 1007, 300};
  const LayoutResult o = dock.computeLayout(odd);
  expect(stackOf(o, 1)->bounds.x + stackOf(o, 1)->bounds.w == o.handles[0].rect.x, "child meets handle");
  expect(o.handles[0].rect.x + 5 == stackOf(o, 2)->bounds.x, "handle meets the next child");
}

}  // namespace

int main() {
  runCase("resize_scenario_01", resize_scenario_01);
  runCase("resize_scenario_02", resize_scenario_02);
  runCase("resize_scenario_03", resize_scenario_03);
  runCase("resize_scenario_04", resize_scenario_04);
  runCase("resize_scenario_05", resize_scenario_05);
  runCase("resize_scenario_06", resize_scenario_06);
  runCase("resize_scenario_07_08", resize_scenario_07_08);
  runCase("resize_scenario_09", resize_scenario_09);
  runCase("resize_scenario_11", resize_scenario_11);
  runCase("resize_nested_handles", resize_nested_handles);
  return finish("resize_test");
}

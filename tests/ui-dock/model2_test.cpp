// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of the Phase 5 additions to the dock model: closed-panel memory and reopening
//   (spec 02 rule 55), panel kinds, lock and float flags (D13), pinning (D10), explicit collapse
//   (D11), tab overflow and application-page strips (D12), the handle hit band (D22), window
//   rectangles, stacking and monitor fitting (D13, spec 03 rule 64).
// Why: these rules sit in DockLayout and must hold before any widget draws them; hostile and stale
//   inputs are included because every operation is a commit gate.
// Callers: CTest (label fast).
#include <limits>

#include "TestSupport.h"
#include "r1ui/dock/DockMonitors.h"

using namespace dock_test;

namespace {

PanelInfo make(PanelId id, PanelKind kind = PanelKind::Panel) {
  PanelInfo p;
  p.id = id;
  p.title = "P" + std::to_string(id);
  p.kind = kind;
  return p;
}

DockLayout buildWith(std::vector<PanelInfo> panels, Node root, const DockConfig& config = {}) {
  DockLayoutResult r = DockLayout::create(std::move(panels), config, std::move(root));
  if (!r.ok()) {
    std::fprintf(stderr, "test setup failed: %s\n", r.error.c_str());
    std::abort();
  }
  return std::move(*r.layout);
}

const Rect kMain{0, 0, 1000, 700};

// A closed tab reopens in its stack at its old index and becomes front.
void closed_slot_restores_tab_index() {
  DockLayout dock = build(4, Node::stack({1, 2, 3, 4}, 1));
  expect(dock.closePanel(2).ok, "close the front tab");
  expect(dock.closedSlot(2) != nullptr && dock.closedSlot(2)->index == 1, "the slot remembers index 1");
  expect(!dock.isDocked(2), "closed");
  const OpenOutcome out = dock.openPanel(2);
  expect(out.status.ok && !out.alreadyOpen, "reopens");
  const Node& root = *dock.areas()[0].root;
  expect(root.tabs == std::vector<PanelId>({1, 2, 3, 4}) && root.tabs[root.active] == 2, "same index, front and active");
  expect(dock.closedSlot(2) == nullptr, "docking forgets the slot");
}

// The sole tab of a region closes; the region is gone, and reopening splits beside the neighbour.
void closed_slot_restores_region_beside_anchor() {
  DockLayout dock = build(3, Node::split(Axis::Row, {Node::stack({1}), Node::stack({2}), Node::stack({3})}));
  expect(dock.closePanel(2).ok, "close the middle region's only tab");
  const ClosedSlot* slot = dock.closedSlot(2);
  expect(slot != nullptr && slot->neighbours.empty() && slot->anchor == 3 && slot->anchorSide == Side::Left, "anchor is the right neighbour");
  expect(dock.areas()[0].root->children.size() == 2, "the region vanished");
  expect(dock.openPanel(2).status.ok, "reopens");
  const Node& root = *dock.areas()[0].root;
  expect(root.children.size() == 3 && root.children[0].tabs[0] == 1 && root.children[1].tabs[0] == 2 && root.children[2].tabs[0] == 3,
         "back between 1 and 3");
}

void closed_slot_in_floating_window() {
  DockLayout dock = build(3, Node::stack({1, 2, 3}));
  DropZone zone;
  zone.preview = {40, 50, 300, 200};
  expect(dock.dock(2, zone).ok, "float panel 2");
  expect(dock.closePanel(2).ok, "close it");
  expect(dock.closedSlot(2) != nullptr && dock.closedSlot(2)->floating, "floating slot");
  expect(dock.areas().size() == 1, "the window is gone");
  expect(dock.openPanel(2).status.ok, "reopen");
  expect(dock.areas().size() == 2 && dock.areas()[1].rect == Rect({40, 50, 300, 200}), "floats again at its rectangle");
}

void documents_are_forgotten() {
  std::vector<PanelInfo> panels = {make(1), make(2, PanelKind::Document), make(3)};
  DockLayout dock = buildWith(panels, Node::stack({1, 2, 3}));
  expect(dock.closePanel(2).ok, "close a document");
  expect(dock.closedSlot(2) == nullptr, "no slot for documents (spec 02 rule 41)");
  expect(dock.closePanel(3).ok && dock.closedSlot(3) != nullptr, "ordinary panels are remembered");
}

void open_panel_rules() {
  std::vector<PanelInfo> panels = {make(1), make(2), make(3), make(4)};
  panels[2].suggested = {1, true, Side::Right};            // 3: tab beside 1
  panels[3].suggested = {2, false, Side::Bottom};          // 4: below 2
  DockLayout dock = buildWith(panels, Node::split(Axis::Row, {Node::stack({1}), Node::stack({2})}));
  expect(dock.openPanel(1).alreadyOpen, "an open panel is only brought forward (rule 8)");
  expect(dock.openPanel(3).status.ok && dock.areas()[0].root->children[0].tabs == std::vector<PanelId>({1, 3}), "suggested tab placement");
  expect(dock.openPanel(4).status.ok, "suggested split placement");
  const Node& right = dock.areas()[0].root->children[1];
  expect(right.kind == Node::Kind::Split && right.axis == Axis::Column && right.children[1].tabs[0] == 4, "4 sits below 2");
  expect(!dock.openPanel(99).status.ok, "unknown panel refused");

  // Default region and the reference editor's floating alternative.
  std::vector<PanelInfo> plain = {make(1), make(2), make(3)};
  DockLayout main = buildWith(plain, Node::stack({1}));
  expect(main.openPanel(2).status.ok && main.areas()[0].root->tabs == std::vector<PanelId>({1, 2}), "default region: the first stack");
  OpenOptions floatOpt;
  floatOpt.floatBounds = kMain;
  floatOpt.floatWhenUnplaced = true;
  expect(main.openPanel(3, floatOpt).status.ok && main.areas().size() == 2, "floatWhenUnplaced floats");
  expect(main.areas()[1].rect == Rect({0, 50, 1000, 600}), "1000 x 600 centred, capped to the bounds");
  DockLayout empty = buildWith(plain, Node::stack({1}));
  expect(empty.closePanel(1).ok && empty.areas()[0].root == std::nullopt, "main area emptied");
  expect(empty.openPanel(2).status.ok && empty.areas()[0].root.has_value(), "an empty main area is filled");
}

void flags_lock_float_close() {
  std::vector<PanelInfo> panels = {make(1), make(2), make(3)};
  panels[1].locked = true;
  panels[2].canFloat = false;
  DockLayout dock = buildWith(panels, Node::stack({1, 2, 3}));
  expect(!dock.closePanel(2).ok, "a locked tab cannot be closed");
  DropZone zone;
  zone.preview = {0, 0, 200, 200};
  expect(!dock.dock(2, zone).ok, "a locked tab cannot be dragged");
  expect(!dock.dock(3, zone).ok, "a panel that cannot float is refused a floating drop");
  expect(dock.setPanelLocked(2, false).ok && dock.closePanel(2).ok, "unlocked, it closes");
  expect(dock.setPanelLocked(3, true).ok && dock.panel(3)->locked, "lock flag set");
  expect(!dock.setPanelLocked(77, true).ok, "unknown panel refused");
  DockLayout copy = dock;
  expect(copy == dock, "copies compare equal");
  copy.setPanelLocked(3, false);
  expect(!(copy == dock), "the lock flag takes part in equality");
}

void application_page_strip() {
  std::vector<PanelInfo> panels = {make(1, PanelKind::ApplicationPage), make(2, PanelKind::ApplicationPage), make(3)};
  DockLayout dock = buildWith(panels, Node::split(Axis::Column, {Node::stack({1, 2}), Node::stack({3})}));
  const LayoutResult layout = dock.computeLayout({0, 0, 1000, 700});
  expect(near(layout.stacks[0].strip.h, 50, 0.001) && near(layout.stacks[0].tabs[0].rect.w, 210, 0.001), "application pages: 210 x 50 (rule 9)");
  expect(near(layout.stacks[1].strip.h, 25, 0.001), "ordinary stacks keep 25 px");
}

void pinned_regions() {
  DockLayout dock = build(3, Node::split(Axis::Row, {Node::stack({1}), Node::stack({2}), Node::stack({3})}));
  expect(dock.setPinned(0, {0}, true, kMain).ok, "pin the first region");
  const double pinned = dock.computeLayout(kMain).stacks[0].bounds.w;
  const LayoutResult wide = dock.computeLayout({0, 0, 1600, 700});
  expect(near(wide.stacks[0].bounds.w, pinned, 1.0), "pinned width survives a window resize");
  expect(near(wide.stacks[1].bounds.w, wide.stacks[2].bounds.w, 1.5), "the others share the rest");
  // A drag next to a pinned region updates the pin and leaves the far sibling alone.
  const LayoutResult before = dock.computeLayout(kMain);
  expect(dock.moveSplitter(before.handles[0].handle, 60, kMain).ok, "drag the handle after the pinned region");
  const LayoutResult after = dock.computeLayout(kMain);
  expect(near(after.stacks[0].bounds.w, before.stacks[0].bounds.w + 60, 1.5) && near(after.stacks[2].bounds.w, before.stacks[2].bounds.w, 1.5),
         "pinned region and its neighbour changed, the far one did not");
  expect(near(dock.computeLayout({0, 0, 1600, 700}).stacks[0].bounds.w, after.stacks[0].bounds.w, 1.0), "the new size is the pin");
  expect(dock.setPinned(0, {1}, true, kMain).ok, "pin the second region");
  expect(!dock.setPinned(0, {2}, true, kMain).ok, "pinning every region is refused");
  expect(dock.setPinned(0, {0}, false, kMain).ok && !dock.areas()[0].root->children[0].pinned, "unpin");
  expect(!dock.setPinned(0, {}, true, kMain).ok && !dock.setPinned(0, {9}, true, kMain).ok && !dock.setPinned(5, {0}, true, kMain).ok,
         "root, missing and stale targets refused");
  // Closing the only flexible region drops the pins instead of leaving an impossible split.
  DockLayout two = build(2, Node::split(Axis::Row, {Node::stack({1}), Node::stack({2})}));
  expect(two.setPinned(0, {0}, true, kMain).ok && two.closePanel(2).ok, "close the flexible region");
  expect(two.validate().ok, "layout stays valid");
}

void collapsed_regions() {
  DockLayout dock = build(3, Node::split(Axis::Row, {Node::stack({1}), Node::stack({2}), Node::stack({3})}));
  expect(dock.setCollapsed(0, {1}, true).ok, "collapse the middle region");
  LayoutResult layout = dock.computeLayout(kMain);
  expect(layout.stacks[1].collapsed && layout.stacks[1].bounds.w == 0, "zero wide");
  expect(layout.handles.size() == 2, "its handles stay so it can be brought back");
  expect(near(layout.stacks[0].bounds.w + layout.stacks[2].bounds.w + 10, 1000, 1.0), "the others take the space");
  // Dragging never collapses: pushing a handle far leaves the floor.
  DockLayout floor = build(2, Node::split(Axis::Row, {Node::stack({1}), Node::stack({2})}));
  const LayoutResult fl = floor.computeLayout(kMain);
  expect(floor.moveSplitter(fl.handles[0].handle, 5000, kMain).ok, "drag far right");
  expect(floor.computeLayout(kMain).stacks[1].bounds.w >= 20 - 0.5, "the other region keeps the 20 px floor (D11)");
  expect(floor.moveSplitter(floor.computeLayout(kMain).handles[0].handle, -5000, kMain).ok, "drag far left");
  expect(floor.computeLayout(kMain).stacks[0].bounds.w >= 20 - 0.5, "and the first one too");
  // Handle next to a collapsed region expands it.
  layout = dock.computeLayout(kMain);
  expect(dock.moveSplitter(layout.handles[0].handle, 10, kMain).ok && !dock.areas()[0].root->children[1].collapsed, "the handle brings it back");
  expect(dock.setCollapsed(0, {0}, true).ok && dock.setCollapsed(0, {1}, true).ok, "collapse two");
  expect(!dock.setCollapsed(0, {2}, true).ok, "the last visible region cannot be collapsed");
  expect(!dock.setCollapsed(0, {}, true).ok, "root refused");
  expect(dock.setCollapsed(0, {0}, false).ok && !dock.areas()[0].root->children[0].collapsed, "expand");
}

void handle_hit_band_widens_under_touch() {
  DockLayout dock = build(2, Node::split(Axis::Row, {Node::stack({1}), Node::stack({2})}));
  const LayoutResult layout = dock.computeLayout(kMain);
  const Rect bar = layout.handles[0].rect;
  const Point inside{bar.x + bar.w + 1.5, 300};
  expect(!hitTestHandle(layout, inside).has_value(), "1.5 px beside the 5 px bar misses");
  expect(hitTestHandle(layout, inside, 2.0).has_value(), "the 9 px touch band (2 px each side) hits");
  expect(!hitTestHandle(layout, {bar.x - 3.0, 300}, 2.0).has_value(), "but not 3 px away");
  expect(!hitTestHandle(layout, inside, std::numeric_limits<double>::quiet_NaN()).has_value(), "NaN band is ignored");
}

void window_rectangles_and_stacking() {
  DockLayout dock = build(3, Node::stack({1, 2, 3}));
  DropZone zone;
  zone.preview = {10, 10, 300, 200};
  expect(dock.dock(2, zone).ok, "float 2");
  zone.preview = {50, 50, 300, 200};
  expect(dock.dock(3, zone).ok, "float 3");
  const uint32_t first = dock.areas()[1].id;
  const uint32_t second = dock.areas()[2].id;
  expect(dock.raiseArea(first).ok && dock.areas()[2].id == first && dock.areas()[1].id == second, "raise puts the area last");
  expect(dock.setAreaRect(first, {5, 6, 20, 30}).ok && dock.areas()[2].rect.w == dock.config().minFloatSize, "size clamps to the floor");
  expect(!dock.setAreaRect(first, {std::numeric_limits<double>::quiet_NaN(), 0, 100, 100}).ok, "NaN refused");
  expect(!dock.setAreaRect(first, {1e9, 0, 100, 100}).ok, "out of range refused");
  expect(!dock.setAreaRect(kMainAreaId, {0, 0, 100, 100}).ok && !dock.raiseArea(kMainAreaId).ok && !dock.setAreaRect(99, {0, 0, 99, 99}).ok,
         "main and stale areas refused");
  WindowState state;
  state.maximized = true;
  state.monitor = "DISPLAY2";
  state.monitorIndex = 1;
  state.dpiScale = 1.5;
  state.hasRect = true;
  state.rect = {100, 100, 800, 600};
  expect(dock.setWindowState(kMainAreaId, state).ok && dock.areas()[0].window == state, "window state stored");
  state.dpiScale = std::numeric_limits<double>::infinity();
  expect(!dock.setWindowState(kMainAreaId, state).ok, "non-finite scale refused");
  state.dpiScale = 1.0;
  state.monitor = std::string(300, 'm');
  expect(!dock.setWindowState(kMainAreaId, state).ok, "overlong monitor name refused");
  expect(dock.setMeta({"Mine", "desc"}).ok && dock.meta().name == "Mine", "names stored");
  expect(!dock.setMeta({std::string(300, 'n'), ""}).ok, "overlong name refused");
}

void monitor_fitting() {
  MonitorSet set;
  set.monitors.push_back({"A", {0, 0, 1920, 1080}, {0, 0, 1920, 1040}, 1.0});
  set.monitors.push_back({"B", {1920, 0, 2560, 1440}, {1920, 0, 2560, 1400}, 1.5});
  set.primary = 0;
  const FitResult visible = fitWindowRect({100, 100, 800, 600}, set, 100, 64);
  expect(!visible.moved && !visible.resized && visible.rect == Rect({100, 100, 800, 600}), "a visible window stays");
  const FitResult partly = fitWindowRect({1850, 100, 800, 600}, set, 100, 64);
  expect(!partly.moved, "70 px on A plus 730 on B: visible on B, left alone");
  const FitResult gone = fitWindowRect({6000, 100, 800, 600}, set, 100, 64);
  expect(gone.moved && gone.rect == Rect({560, 220, 800, 600}), "centred on the primary work area");
  const FitResult sliver = fitWindowRect({-750, 100, 800, 600}, set, 100, 64);
  expect(sliver.moved, "only 50 px visible: brought back");
  const FitResult huge = fitWindowRect({6000, 0, 5000, 4000}, set, 100, 64);
  expect(huge.resized && huge.rect.w == 1920 && huge.rect.h == 1040 && huge.rect.x == 0 && huge.rect.y == 0, "reduced to the work area");
  const FitResult tiny = fitWindowRect({-30, 100, 40, 40}, set, 100, 64);
  expect(tiny.resized && tiny.rect.w == 64, "size clamped to the minimum");
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const FitResult bad = fitWindowRect({nan, nan, nan, -5}, set, 100, 64);
  expect(std::isfinite(bad.rect.x) && std::isfinite(bad.rect.w) && bad.rect.w >= 64 && bad.rect.h >= 64, "NaN input yields a finite rectangle");
  const FitResult once = fitWindowRect(gone.rect, set, 100, 64);
  expect(!once.moved && !once.resized, "fitting is idempotent");
  MonitorSet none;
  const FitResult unknown = fitWindowRect({6000, 100, 800, 600}, none, 100, 64);
  expect(!unknown.moved && unknown.rect.x == 6000, "no monitors: cannot judge, only clamps");
  expect(!hasUsableMonitor(none), "empty set is unusable");

  DockLayout dock = build(2, Node::stack({1, 2}));
  DropZone zone;
  zone.preview = {9000, 9000, 400, 300};
  expect(dock.dock(2, zone).ok, "float far away");
  WindowState main;
  main.hasRect = true;
  main.rect = {-5000, 0, 1280, 720};
  expect(dock.setWindowState(kMainAreaId, main).ok, "main window off screen");
  expect(dock.fitWindows(set) == 2, "both windows are brought back");
  expect(dock.areas()[1].rect.x >= 0 && dock.areas()[1].rect.x < 1920 && dock.areas()[0].window.rect.x >= 0, "visible now");
  expect(dock.fitWindows(set) == 0, "second pass changes nothing");
}

void overflowing_strip() {
  std::vector<PanelId> ids;
  for (PanelId i = 1; i <= 30; ++i) ids.push_back(i);
  DockLayout dock = build(30, Node::stack(ids));
  const LayoutResult layout = dock.computeLayout({0, 0, 500, 300});
  const StackLayout& s = layout.stacks[0];
  expect(s.overflow && near(s.tabsWidth, 1800, 0.001) && near(s.tabs.back().rect.x, 29 * 60, 0.001), "30 tabs at 60 px overflow a 500 px strip");
  expect(!dock.computeLayout({0, 0, 1800, 300}).stacks[0].overflow, "a strip as wide as the tabs does not");
  const LayoutResult zero = dock.computeLayout({0, 0, 0, 0});
  expect(!zero.stacks[0].overflow, "a zero-sized strip reports no overflow");
}

}  // namespace

int main() {
  runCase("closed_slot_restores_tab_index", closed_slot_restores_tab_index);
  runCase("closed_slot_restores_region_beside_anchor", closed_slot_restores_region_beside_anchor);
  runCase("closed_slot_in_floating_window", closed_slot_in_floating_window);
  runCase("documents_are_forgotten", documents_are_forgotten);
  runCase("open_panel_rules", open_panel_rules);
  runCase("flags_lock_float_close", flags_lock_float_close);
  runCase("application_page_strip", application_page_strip);
  runCase("pinned_regions", pinned_regions);
  runCase("collapsed_regions", collapsed_regions);
  runCase("handle_hit_band_widens_under_touch", handle_hit_band_widens_under_touch);
  runCase("window_rectangles_and_stacking", window_rectangles_and_stacking);
  runCase("monitor_fitting", monitor_fitting);
  runCase("overflowing_strip", overflowing_strip);
  return finish("model2_test");
}

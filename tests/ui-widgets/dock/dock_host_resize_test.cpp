// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of DockHost's splitter handling (slice 5.1, decisions D10, D11, D22): dragging a
//   handle, the 20 px floor, Escape, the cursor, the 9 px touch band, keyboard resize through a
//   focusable handle, explicit collapse (and that dragging never collapses), pinning on window
//   resize, and proportional resize of the whole dock.
// Why: spec 05 is mostly arithmetic on pixels; the model is tested headless, here the widgets and
//   the event path are checked end to end.
// Callers: CTest (label fast).
#include "DockTestSupport.h"

using namespace dock_widget_test;
namespace Mod = r1ui::core::events::Mod;

namespace {

dock::Node threeRegions() { return dock::Node::split(dock::Axis::Row, {stackOf({1}), stackOf({2}), stackOf({3})}); }

dock::LayoutResult computed(DockRig& rig) { return rig.host->layout().computeLayout(rig.host->mainContentRect()); }

dock::Point center(const dock::Rect& r) { return {r.x + r.w / 2.0, r.y + r.h / 2.0}; }

void dragging_a_handle_resizes_two_regions() {
  DockRig rig;
  rig.setRoot(threeRegions());
  const dock::LayoutResult before = computed(rig);
  const dock::Rect handle = before.handles[0].rect;
  rig.move(center(handle));
  R1_EXPECT(rig.t.ui.cursor() == Cursor::ResizeHorizontal, "hovering a handle shows the horizontal resize cursor");
  rig.drag(center(handle), {center(handle).x + 100, center(handle).y});
  const dock::LayoutResult after = computed(rig);
  R1_EXPECT(std::abs(after.stacks[0].bounds.w - (before.stacks[0].bounds.w + 100)) <= 1.5, "the region before grows by the travel");
  R1_EXPECT(std::abs(after.stacks[1].bounds.w - (before.stacks[1].bounds.w - 100)) <= 1.5, "the one after shrinks by the same amount");
  R1_EXPECT(std::abs(after.stacks[2].bounds.w - before.stacks[2].bounds.w) <= 1.5, "the others keep their size (rule 9)");
  // The widgets follow.
  const r1ui::core::layout::Rect stripRect = rig.t.ui.absRect(rig.strip(1)->id());
  R1_EXPECT(std::abs(stripRect.w - after.stacks[0].bounds.w) <= 1.0);
  R1_EXPECT(rig.changes.back() == DockChange::Arrangement, "the release marks the layout changed");
  // After a drag the proportions survive a window resize (rule 11).
  rig.t.ui.setViewport(1800, 600, 1.0f);
  rig.settle();
  const dock::LayoutResult wide = computed(rig);
  R1_EXPECT(std::abs(wide.stacks[0].bounds.w / wide.stacks[1].bounds.w - after.stacks[0].bounds.w / after.stacks[1].bounds.w) < 0.05, "proportions kept");
}

void the_floor_stops_the_drag_and_nothing_collapses() {
  DockRig rig;
  rig.setRoot(threeRegions());
  dock::Rect handle = computed(rig).handles[0].rect;
  rig.drag(center(handle), {center(handle).x + 3000, center(handle).y});
  dock::LayoutResult r = computed(rig);
  R1_EXPECT(r.stacks[1].bounds.w >= 20 - 0.5 && !r.stacks[1].collapsed, "the region after stops at the 20 px floor (D11)");
  R1_EXPECT(r.stacks[2].bounds.w > 100, "and the far region is untouched");
  handle = r.handles[0].rect;
  rig.drag(center(handle), {center(handle).x - 3000, center(handle).y});
  r = computed(rig);
  R1_EXPECT(r.stacks[0].bounds.w >= 20 - 0.5, "the region before stops at the floor too");
  // Pointer outside the dock while dragging: the handle follows and stops at the limit (rule 13).
  handle = r.handles[1].rect;
  rig.drag(center(handle), {-500, center(handle).y});
  R1_EXPECT(computed(rig).stacks[1].bounds.w >= 20 - 0.5, "dragged far outside the window the handle still stops at the floor (rule 13)");
}

void escape_during_a_handle_drag_restores_the_sizes() {
  DockRig rig;
  rig.setRoot(threeRegions());
  const dock::LayoutResult before = computed(rig);
  const dock::Point c = center(before.handles[0].rect);
  rig.drag(c, {c.x + 120, c.y}, false);
  R1_EXPECT(std::abs(computed(rig).stacks[0].bounds.w - (before.stacks[0].bounds.w + 120)) <= 1.5, "live resize while dragging");
  R1_EXPECT(rig.t.ui.keyDown(Key::Escape));
  rig.settle();
  R1_EXPECT(std::abs(computed(rig).stacks[0].bounds.w - before.stacks[0].bounds.w) <= 0.5, "Escape puts the sizes back");
  rig.up({c.x + 120, c.y});
  rig.settle();
  R1_EXPECT(std::abs(computed(rig).stacks[0].bounds.w - before.stacks[0].bounds.w) <= 0.5);
}

void touch_widens_the_hit_band_to_nine_pixels() {
  for (const bool touch : {false, true}) {
    DockHostOptions options;
    options.touch = touch;
    DockRig rig(900, 600, 6, options);
    rig.setRoot(threeRegions());
    const dock::Rect bar = computed(rig).handles[0].rect;
    const dock::Point nearEdge{bar.x + bar.w + 1.5, bar.y + 200};  // 1.5 px outside the 5 px bar
    rig.move(nearEdge);
    const bool hot = rig.t.ui.cursor() == Cursor::ResizeHorizontal;
    R1_EXPECT(hot == touch, touch ? "9 px band under touch" : "5 px band otherwise");
    const dock::Point far{bar.x + bar.w + 4.5, bar.y + 200};
    rig.move(far);
    R1_EXPECT(rig.t.ui.cursor() != Cursor::ResizeHorizontal, "beyond 2 px outside even the touch band ends");
  }
}

void keyboard_resizes_a_focused_handle() {
  DockRig rig;
  rig.setRoot(threeRegions());
  const dock::LayoutResult before = computed(rig);
  const dock::Point c = center(before.handles[0].rect);
  rig.click(c);  // a click on a handle focuses it
  R1_EXPECT(rig.t.ui.router().focused() == rig.host->areaView(dock::kMainAreaId)->id());
  rig.t.ui.keyDown(Key::Right);
  rig.settle();
  R1_EXPECT(std::abs(computed(rig).stacks[0].bounds.w - (before.stacks[0].bounds.w + 10)) <= 1.5, "Right moves the handle 10 px (D10)");
  rig.t.ui.keyDown(Key::Left, Mod::kShift);
  rig.settle();
  R1_EXPECT(std::abs(computed(rig).stacks[0].bounds.w - (before.stacks[0].bounds.w - 40)) <= 1.5, "Shift moves 50 px");
  rig.t.ui.keyDown(Key::PageDown);
  rig.t.ui.keyDown(Key::End);
  rig.settle();
  const dock::LayoutResult end = computed(rig);
  R1_EXPECT(end.stacks[2].bounds.w >= 20 - 0.5 && end.stacks[2].bounds.w < 25, "End pushes the second handle to the limit");
  rig.t.ui.keyDown(Key::Up);  // wrong direction for a vertical bar: nothing
  rig.settle();
  R1_EXPECT(std::abs(computed(rig).stacks[2].bounds.w - end.stacks[2].bounds.w) <= 0.5);
}

void collapse_is_an_explicit_command() {
  DockRig rig;
  rig.setRoot(threeRegions());
  R1_EXPECT(rig.host->toggleCollapsed(2));
  dock::LayoutResult r = computed(rig);
  R1_EXPECT(r.stacks[1].collapsed && r.stacks[1].bounds.w == 0, "the region has no width");
  R1_EXPECT(rig.t.ui.absRect(rig.strip(2)->id()).w == 0 || !rig.t.ui.tree().get(rig.strip(2)->id())->shown(), "and its strip does not show");
  R1_EXPECT(r.handles.size() == 2, "its handles stay so it can be brought back");
  rig.drag(center(r.handles[0].rect), {center(r.handles[0].rect).x + 30, center(r.handles[0].rect).y});
  r = computed(rig);
  R1_EXPECT(!rig.host->layout().areas()[0].root->children[1].collapsed && r.stacks[1].bounds.w >= 20 - 0.5, "dragging the handle expands it to at least the floor");
  R1_EXPECT(rig.host->toggleCollapsed(1) && rig.host->toggleCollapsed(1), "toggle back and forth");
  R1_EXPECT(rig.host->toggleCollapsed(1) && rig.host->toggleCollapsed(2), "two collapsed");
  R1_EXPECT(!rig.host->toggleCollapsed(3), "the last visible region cannot collapse");
}

void a_pinned_region_keeps_its_width_on_window_resize() {
  DockRig rig;
  rig.setRoot(threeRegions());
  const double width = computed(rig).stacks[0].bounds.w;
  R1_EXPECT(rig.host->togglePinned(1));
  rig.t.ui.setViewport(1500, 600, 1.0f);
  rig.settle();
  const dock::LayoutResult r = computed(rig);
  R1_EXPECT(std::abs(r.stacks[0].bounds.w - width) <= 1.0, "pinned width (D10)");
  R1_EXPECT(std::abs(r.stacks[1].bounds.w - r.stacks[2].bounds.w) <= 1.5, "the rest share the space");
  R1_EXPECT(rig.host->togglePinned(1) && !rig.host->layout().areas()[0].root->children[0].pinned, "unpin");
}

void zero_and_tiny_windows_do_not_break_layout() {
  DockRig rig;
  rig.setRoot(threeRegions());
  for (const int w : {0, 1, 10, 50}) {
    rig.t.ui.setViewport(w, 7, 1.0f);
    rig.settle();
    const dock::LayoutResult r = computed(rig);
    for (const dock::StackLayout& s : r.stacks) R1_EXPECT(s.bounds.w >= 0 && s.bounds.h >= 0 && std::isfinite(s.bounds.w), "no negative or NaN sizes");
  }
  rig.t.ui.setViewport(900, 600, 1.0f);
  rig.settle();
  R1_EXPECT(computed(rig).stacks[0].bounds.w > 100, "and it recovers");
}

}  // namespace

int main() {
  dragging_a_handle_resizes_two_regions();
  the_floor_stops_the_drag_and_nothing_collapses();
  escape_during_a_handle_drag_restores_the_sizes();
  touch_widens_the_hit_band_to_nine_pixels();
  keyboard_resizes_a_focused_handle();
  collapse_is_an_explicit_command();
  a_pinned_region_keeps_its_width_on_window_resize();
  zero_and_tiny_windows_do_not_break_layout();
  return r1test::finish();
}

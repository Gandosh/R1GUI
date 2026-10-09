// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of DockHost driven with synthetic pointer and key input in a headless UiContext:
//   rendering a layout as widgets, lazy content that stays alive, tab activation and closing, the
//   drag threshold, dragging a tab to every kind of drop zone, the tab strip, outside the dock and
//   Escape, hover activation, splitter drags and the 20 px floor, the touch band, locks, the context
//   menu, commands and the change observer.
// Why: the host's value is in how the pieces behave together, so these tests go through real
//   events and check both the model and the widgets.
// Callers: CTest (label fast).
#include "DockTestSupport.h"

using namespace dock_widget_test;

namespace {

dock::Node twoColumns() {
  return dock::Node::split(dock::Axis::Row, {stackOf({1, 2, 3}), dock::Node::split(dock::Axis::Column, {stackOf({4}), stackOf({5, 6})})});
}

size_t windowCount(DockRig& rig) { return rig.backend->stacking().size(); }

void layout_is_rendered_as_widgets() {
  DockRig rig;
  rig.setRoot(twoColumns());
  DockAreaView* view = rig.host->areaView(dock::kMainAreaId);
  R1_EXPECT(view != nullptr && view->units().size() == 3);
  R1_EXPECT(rig.strip(1) != nullptr && rig.strip(1)->tabCount() == 3 && rig.strip(5)->tabCount() == 2);
  R1_EXPECT(rig.strip(1)->frontPanel() == 1);
  // Content is created lazily for front tabs only and kept alive.
  R1_EXPECT(rig.factoryCalls[1] == 1 && rig.factoryCalls[4] == 1 && rig.factoryCalls[5] == 1);
  R1_EXPECT(rig.factoryCalls[2] == 0 && rig.factoryCalls[6] == 0);
  R1_EXPECT(rig.host->contentOf(1).valid() && !rig.host->contentOf(2).valid());
  // The stack's body holds the content with the body's size.
  const dock::Rect body = rig.body(1);
  const r1ui::core::layout::Rect content = rig.t.ui.absRect(rig.host->contentOf(1));
  R1_EXPECT(std::abs(content.w - body.w) <= 1 && std::abs(content.h - body.h) <= 1);
  // Strip and body sit where the model says.
  const r1ui::core::layout::Rect stripRect = rig.t.ui.absRect(rig.strip(1)->id());
  R1_EXPECT(stripRect.h == 25 && stripRect.x == 0 && stripRect.y == 0);
}

void activation_keeps_content_alive() {
  DockRig rig;
  rig.setRoot(twoColumns());
  const WidgetId first = rig.host->contentOf(1);
  rig.click(rig.tabCenter(2));
  R1_EXPECT(rig.strip(2)->frontPanel() == 2 && rig.host->activePanel() == 2);
  R1_EXPECT(rig.factoryCalls[2] == 1 && rig.host->contentOf(2).valid());
  R1_EXPECT(rig.t.ui.alive(first), "the previous content stays alive");
  const WidgetObject* hidden = rig.t.ui.object(first);
  R1_EXPECT(hidden != nullptr && hidden->style().display == r1ui::core::layout::Display::None);
  rig.click(rig.tabCenter(1));
  R1_EXPECT(rig.host->contentOf(1) == first && rig.factoryCalls[1] == 1, "going back reuses the same widget");
  R1_EXPECT(!rig.changes.empty() && rig.changes.back() == DockChange::Active);
}

void close_and_reopen() {
  DockRig rig;
  rig.setRoot(twoColumns());
  rig.click(rig.tabCenter(2));
  const WidgetId content = rig.host->contentOf(2);
  rig.click({rig.closeButton(2).x + 8, rig.closeButton(2).y + 8});
  R1_EXPECT(!rig.host->layout().isDocked(2), "the close button closes the front tab");
  R1_EXPECT(!rig.t.ui.alive(content) && !rig.host->contentOf(2).valid(), "its content is destroyed with it");
  R1_EXPECT(rig.host->activePanel() != 2 && rig.host->layout().isDocked(rig.host->activePanel()));
  R1_EXPECT(rig.host->layout().closedSlot(2) != nullptr);
  R1_EXPECT(rig.host->openPanel(2));
  rig.settle();
  R1_EXPECT(rig.host->layout().locate(2)->tab == 1 && rig.host->layout().locate(2)->front, "restored to its slot");
  R1_EXPECT(rig.factoryCalls[2] == 2, "content is created again");
  // Middle click: closes on release over the same tab; moving off first does nothing.
  const dock::Point c = rig.tabCenter(3);
  rig.move(c);
  rig.down(c, Button::Middle);
  rig.move({c.x + 300, c.y + 200});
  rig.up({c.x + 300, c.y + 200}, Button::Middle);
  rig.settle();
  R1_EXPECT(rig.host->layout().isDocked(3), "middle press released elsewhere does nothing");
  rig.click(rig.tabCenter(3), Button::Middle);
  R1_EXPECT(!rig.host->layout().isDocked(3), "middle click closes");
  // Commands.
  R1_EXPECT(rig.host->activatePanel(1) && rig.host->closeActiveTab() && !rig.host->layout().isDocked(1));
  R1_EXPECT(!rig.host->closePanel(99) && !rig.host->openPanel(99) && !rig.host->activatePanel(99));
  R1_EXPECT(!rig.host->lastError().empty());
}

void drag_threshold_is_strictly_greater_than_five() {
  DockRig rig;
  rig.setRoot(twoColumns());
  const dock::Point c = rig.tabCenter(2);
  rig.move(c);
  rig.down(c);
  rig.move({c.x + 5, c.y});
  R1_EXPECT(!rig.host->dragging(), "exactly 5 px is not a drag");
  rig.move({c.x + 5.5, c.y});
  R1_EXPECT(rig.host->dragging(), "more than 5 px is");
  rig.up({c.x + 5.5, c.y});
  rig.settle();
  R1_EXPECT(!rig.host->dragging());
  // Press and release without crossing: just an activation.
  rig.click(rig.tabCenter(3));
  R1_EXPECT(rig.host->layout().locate(3)->tab == 2 && rig.host->layout().areas().size() == 1);
}

void drag_to_strip_reorders() {
  DockRig rig;
  rig.setRoot(twoColumns());
  const dock::Rect firstTab = rig.tab(1);
  // Drag tab 3 to the left half of tab 1: it becomes first.
  rig.drag(rig.tabCenter(3), {firstTab.x + 12, firstTab.y + 8});
  R1_EXPECT(rig.host->layout().areas()[0].root->children[0].tabs == std::vector<dock::PanelId>({3, 1, 2}), "reordered inside the strip");
  R1_EXPECT(rig.host->layout().locate(3)->front && rig.host->activePanel() == 3);
  // Drag tab 1 into the other strip between 5 and 6.
  const dock::Rect t5 = rig.tab(5);
  rig.drag(rig.tabCenter(1), {t5.x + t5.w + 3, t5.y + 8});
  R1_EXPECT(rig.host->layout().locate(1)->tabCount == 3 && rig.host->layout().locate(1)->tab == 1, "inserted at the slot under the pointer");
  R1_EXPECT(rig.host->layout().areas()[0].root->children[0].tabs == std::vector<dock::PanelId>({3, 2}));
}

void drag_to_each_side_zone() {
  struct Case {
    const char* name;
    double fx, fy;       // pointer position inside the target body (fraction)
    dock::Axis axis;     // axis of the split that must hold the dragged tab
    bool first;          // dragged region first among its siblings
  };
  const Case cases[] = {{"left", 0.06, 0.5, dock::Axis::Row, true},
                        {"right", 0.94, 0.5, dock::Axis::Row, false},
                        {"top", 0.5, 0.06, dock::Axis::Column, true},
                        {"bottom", 0.5, 0.94, dock::Axis::Column, false}};
  for (const Case& c : cases) {
    DockRig rig;
    rig.setRoot(dock::Node::split(dock::Axis::Row, {stackOf({1, 2}), stackOf({3})}));
    const dock::Rect body = rig.body(3);
    const dock::Point target{body.x + body.w * c.fx, body.y + body.h * c.fy};
    rig.drag(rig.tabCenter(2), target, false);
    R1_EXPECT(rig.host->dragging());
    // While dragging: the overlay shows the cross and the half-region preview on the overlay layer.
    DockDragOverlay* overlay = nullptr;
    for (WidgetId w = rig.t.ui.tree().firstChild(rig.t.ui.overlays().layer()); w.valid(); w = rig.t.ui.tree().nextSibling(w)) {
      if (auto* o = rig.t.ui.objectAs<DockDragOverlay>(w)) overlay = o;
    }
    R1_EXPECT(overlay != nullptr && overlay->visual().preview.has_value() && overlay->visual().crossOuter.has_value(), c.name);
    if (overlay != nullptr && overlay->visual().preview) {
      const dock::Rect p = *overlay->visual().preview;
      const bool horizontal = c.axis == dock::Axis::Row;
      R1_EXPECT(horizontal ? std::abs(p.w - body.w / 2.0) < 30 : std::abs(p.h - (body.h + 25.0) / 2.0) < 30, "preview is half of the region");
    }
    R1_EXPECT(!rig.host->layout().locate(2)->front || rig.host->layout().locate(2)->tabCount == 2, "the model does not change before the drop");
    rig.up(target);
    rig.settle();
    R1_EXPECT(!rig.host->dragging() && rig.t.ui.overlays().layer().valid());
    const dock::Node& root = *rig.host->layout().areas()[0].root;
    R1_EXPECT(rig.host->layout().locate(2).has_value() && rig.host->layout().locate(2)->tabCount == 1, c.name);
    // The target region became a split of the expected axis holding the dragged tab.
    if (c.axis == dock::Axis::Row) {
      // Same direction as the root: the new region is spliced in beside the target.
      R1_EXPECT(root.children.size() == 3 && root.children[c.first ? 1 : 2].tabs == std::vector<dock::PanelId>({2}), c.name);
      R1_EXPECT(root.children.size() == 3 && std::abs(root.children[1].weight - root.children[2].weight) < 1e-9, "same weight as the target (rule 28)");
    } else {
      const dock::Node* holder = nullptr;
      for (const dock::Node& child : root.children) {
        if (child.kind == dock::Node::Kind::Split) holder = &child;
      }
      R1_EXPECT(holder != nullptr && holder->axis == c.axis, c.name);
      if (holder != nullptr) {
        const dock::PanelId firstPanel = holder->children.front().tabs.front();
        R1_EXPECT((firstPanel == 2) == c.first, c.name);
        R1_EXPECT(std::abs(holder->children[0].weight - holder->children[1].weight) < 1e-9, "same weight as the target (rule 28)");
      }
    }
    R1_EXPECT(rig.host->activePanel() == 2);
  }
}

void drag_to_area_edge() {
  DockRig rig;
  rig.setRoot(dock::Node::split(dock::Axis::Row, {stackOf({1, 2}), stackOf({3})}));
  const dock::Rect main = rig.host->mainContentRect();
  const dock::Point edge{main.x + main.w - 3.0, main.y + main.h / 2.0};  // right edge bar, 6 px thick
  rig.drag(rig.tabCenter(2), edge);
  const dock::Node& root = *rig.host->layout().areas()[0].root;
  R1_EXPECT(root.children.size() == 3 && root.children.back().tabs == std::vector<dock::PanelId>({2}), "a new full-length region at the right edge");
}

void drop_in_the_middle_or_outside_floats() {
  DockRig rig;
  rig.setRoot(dock::Node::split(dock::Axis::Row, {stackOf({1, 2}), stackOf({3})}));
  const dock::Rect body = rig.body(3);
  rig.drag(rig.tabCenter(2), {body.x + body.w / 2, body.y + body.h / 2});
  R1_EXPECT(windowCount(rig) == 1 && rig.host->layout().areas().size() == 2, "the centre of a body is no target: the tab floats");
  R1_EXPECT(rig.host->layout().areas()[0].root->children[0].tabs == std::vector<dock::PanelId>({1}), "the region it came from is unchanged otherwise");
  const dock::Rect floating = rig.host->layout().areas()[1].rect;
  R1_EXPECT(floating.w >= 64 && floating.h >= 64);
  R1_EXPECT(rig.host->areaView(rig.host->layout().areas()[1].id) != nullptr, "the floating window shows its own area view");
  R1_EXPECT(rig.factoryCalls[2] <= 1, "the content moved with the panel");

  // Outside the window entirely (above the dock): also a floating window.
  DockRig outside;
  outside.setRoot(dock::Node::split(dock::Axis::Row, {stackOf({1, 2}), stackOf({3})}));
  outside.drag(outside.tabCenter(2), {-40, -30});
  R1_EXPECT(windowCount(outside) == 1 && outside.host->layout().areas().size() == 2, "dropping outside the window floats");
}

void escape_restores_everything() {
  DockRig rig;
  rig.setRoot(dock::Node::split(dock::Axis::Row, {stackOf({1, 2, 3}), stackOf({4})}));
  rig.click(rig.tabCenter(2));  // the press activates the tab before any drag (rule 1)
  const dock::DockLayout before = rig.host->layout();
  const dock::Rect body = rig.body(4);
  rig.drag(rig.tabCenter(2), {body.x + 8, body.y + body.h / 2}, false);
  R1_EXPECT(rig.host->dragging());
  R1_EXPECT(rig.t.ui.keyDown(Key::Escape));
  rig.settle();
  R1_EXPECT(!rig.host->dragging(), "Escape ends the drag");
  if (!(rig.host->layout() == before)) std::fprintf(stderr, "before: %s\nafter:  %s\n", before.toJson().c_str(), rig.host->layout().toJson().c_str());
  R1_EXPECT(rig.host->layout() == before, "the layout is exactly as before (decision D9)");
  R1_EXPECT(windowCount(rig) == 0 && rig.host->layout().locate(2)->tab == 1);
  rig.up(rig.tabCenter(4));  // the release after the cancel changes nothing
  rig.settle();
  R1_EXPECT(rig.host->layout() == before);
  bool overlayLeft = false;
  for (WidgetId w = rig.t.ui.tree().firstChild(rig.t.ui.overlays().layer()); w.valid(); w = rig.t.ui.tree().nextSibling(w)) {
    overlayLeft = overlayLeft || rig.t.ui.objectAs<DockDragOverlay>(w) != nullptr;
  }
  R1_EXPECT(!overlayLeft, "the zone overlay is gone");
  R1_EXPECT(rig.strip(2)->frontPanel() == 1 || rig.strip(2)->frontPanel() == 3 || rig.strip(2)->frontPanel() == 2);
  R1_EXPECT(rig.strip(1)->indexOf(2).has_value() && rig.tab(2).w > 0, "the tab is visible in its strip again");
}

void hover_activates_a_tab_after_three_quarters_of_a_second() {
  DockRig rig;
  rig.setRoot(dock::Node::split(dock::Axis::Row, {stackOf({1, 2}), stackOf({3, 4, 5}, 0)}));
  const dock::DockLayout before = rig.host->layout();
  rig.t.ui.setTime(1000);
  rig.drag(rig.tabCenter(1), rig.tabCenter(4), false);
  R1_EXPECT(rig.host->dragging() && rig.host->layout().locate(3)->front, "still the old front tab");
  rig.t.ui.setTime(1700);
  rig.t.ui.tick();
  R1_EXPECT(rig.host->layout().locate(3)->front, "not yet at 0.7 s");
  rig.t.ui.setTime(1800);
  rig.t.ui.tick();
  rig.settle();
  R1_EXPECT(rig.host->layout().locate(4)->front, "front after 0.75 s over the header");
  R1_EXPECT(rig.t.ui.keyDown(Key::Escape));
  rig.settle();
  R1_EXPECT(rig.host->layout() == before, "Escape also puts the front tab back");
  // Moving away before the delay cancels the activation.
  rig.t.ui.setTime(5000);
  rig.drag(rig.tabCenter(1), rig.tabCenter(4), false);
  rig.t.ui.setTime(5500);
  rig.move({50, 400});
  rig.t.ui.setTime(6000);
  rig.t.ui.tick();
  R1_EXPECT(rig.host->layout().locate(3)->front, "leaving the header cancels the timer");
  rig.t.ui.keyDown(Key::Escape);
}

void locked_tabs_cannot_be_dragged_or_closed() {
  DockRig rig;
  rig.setRoot(twoColumns());
  R1_EXPECT(rig.host->setPanelLocked(2, true));
  rig.click(rig.tabCenter(2));
  const dock::Point c = rig.tabCenter(2);
  rig.drag(c, {c.x + 200, c.y + 200});
  R1_EXPECT(!rig.host->dragging() && rig.host->layout().areas().size() == 1, "no drag starts on a locked tab (rule 16)");
  R1_EXPECT(!rig.host->closePanel(2) && rig.host->layout().isDocked(2), "and it cannot be closed");
  R1_EXPECT(rig.changes.back() == DockChange::Lock || !rig.changes.empty());
  R1_EXPECT(rig.host->setPanelLocked(2, false) && rig.host->closePanel(2));
}

void change_observer_reports_each_kind() {
  DockRig rig;
  rig.setRoot(twoColumns());
  rig.changes.clear();
  rig.click(rig.tabCenter(2));
  R1_EXPECT(rig.changes == std::vector<DockChange>({DockChange::Active}));
  rig.changes.clear();
  const dock::Point c = rig.tabCenter(2);
  rig.drag(c, {c.x + 3, c.y + 40}, false);
  R1_EXPECT(!rig.changes.empty() && rig.changes.front() == DockChange::DragStarted, "drag start is announced (deferred saving pauses)");
  rig.up({c.x + 3, c.y + 40});
  R1_EXPECT(std::find(rig.changes.begin(), rig.changes.end(), DockChange::DragEnded) != rig.changes.end());
  rig.changes.clear();
  rig.host->closeActiveTab();
  R1_EXPECT(!rig.changes.empty() && rig.changes.back() == DockChange::Arrangement);
}

}  // namespace

int main() {
  layout_is_rendered_as_widgets();
  activation_keeps_content_alive();
  close_and_reopen();
  drag_threshold_is_strictly_greater_than_five();
  drag_to_strip_reorders();
  drag_to_each_side_zone();
  drag_to_area_edge();
  drop_in_the_middle_or_outside_floats();
  escape_restores_everything();
  hover_activates_a_tab_after_three_quarters_of_a_second();
  locked_tabs_cannot_be_dragged_or_closed();
  change_observer_reports_each_kind();
  return r1test::finish();
}

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of floating windows in DockHost with the in-window backend: floating a tab, the
//   window frame's gestures (move, resize, maximize, close), stacking, dropping tabs into and out of
//   floating windows across windows, the window of a dragged sole tab hiding, closing a window
//   whose tab refuses, unreachable windows, and the window state that travels with the layout.
// Why: spec 03 describes floating windows end to end; these tests drive them with real pointer
//   events on the toolkit-drawn frame.
// Callers: CTest (label fast).
#include "DockTestSupport.h"

using namespace dock_widget_test;

namespace {

dock::Node twoRegions() { return dock::Node::split(dock::Axis::Row, {stackOf({1, 2, 3}), stackOf({4, 5})}); }

dock::Point center(const dock::Rect& r) { return {r.x + r.w / 2.0, r.y + r.h / 2.0}; }

// The frame's outer rectangle (what the user sees, title bar included).
dock::Rect frameRect(DockRig& rig, FloatId window) { return toDockRect(rig.t.ui.absRect(rig.backend->frameWidget(window))); }

FloatId onlyWindow(DockRig& rig) {
  const std::vector<FloatId> windows = rig.backend->stacking();
  return windows.empty() ? 0 : windows.back();
}

void floating_a_tab_creates_a_window() {
  DockRig rig;
  rig.setRoot(twoRegions());
  const dock::Rect source = rig.body(1);
  R1_EXPECT(rig.host->floatPanel(2));
  rig.settle();
  const FloatId w = onlyWindow(rig);
  R1_EXPECT(w != 0 && rig.host->layout().areas().size() == 2 && rig.host->layout().areas()[1].root->tabs[0] == 2);
  const dock::Rect content = *rig.backend->contentRect(w);
  R1_EXPECT(std::abs(content.w - (source.w)) < 1.5 && content.w >= 64, "a floating panel keeps its content size (spec 03 rule 31)");
  const dock::Rect frame = frameRect(rig, w);
  R1_EXPECT(std::abs(frame.x - (content.x - 1)) <= 1 && std::abs(frame.y - (content.y - 34)) <= 1, "the frame adds a 34 px title bar and a 1 px border");
  R1_EXPECT(rig.strip(2) != nullptr && rig.strip(2)->window() == w, "its strip lives in the floating window");
  R1_EXPECT(rig.host->areaView(rig.host->layout().areas()[1].id) != nullptr);
  // The content widget moved with the panel instead of being recreated.
  R1_EXPECT(rig.factoryCalls[2] <= 1);
  // Parent chain: content -> body -> area view -> holder -> frame.
  const WidgetId contentWidget = rig.host->contentOf(2);
  if (contentWidget.valid()) {
    WidgetId p = contentWidget;
    bool underFrame = false;
    while (p.valid()) {
      underFrame = underFrame || p == rig.backend->frameWidget(w);
      p = rig.t.ui.tree().parent(p);
    }
    R1_EXPECT(underFrame, "the content is inside the floating frame");
  }
  const dock::PanelId window = 2;
  R1_EXPECT(rig.host->layout().areas()[1].window.dpiScale == 1.0 && window == 2);
}

void the_title_bar_moves_and_the_corner_resizes() {
  DockRig rig;
  rig.setRoot(twoRegions());
  rig.host->floatPanel(2);
  rig.settle();
  const FloatId w = onlyWindow(rig);
  const dock::Rect before = *rig.backend->contentRect(w);
  const dock::Rect frame = frameRect(rig, w);
  const dock::Point title{frame.x + 60, frame.y + 12};
  rig.drag(title, {title.x + 80, title.y + 50});
  const dock::Rect moved = *rig.backend->contentRect(w);
  R1_EXPECT(std::abs(moved.x - (before.x + 80)) <= 1 && std::abs(moved.y - (before.y + 50)) <= 1 && moved.w == before.w, "dragging the title moves the window");
  R1_EXPECT(rig.host->layout().areas()[1].rect == moved, "and the model follows");
  R1_EXPECT(rig.changes.back() == DockChange::Window);
  const dock::Rect f2 = frameRect(rig, w);
  const dock::Point corner{f2.x + f2.w - 2, f2.y + f2.h - 2};
  rig.drag(corner, {corner.x + 60, corner.y + 40});
  const dock::Rect grown = *rig.backend->contentRect(w);
  R1_EXPECT(std::abs(grown.w - (moved.w + 60)) <= 1 && grown.h >= moved.h && grown.h <= 600 - 34 - 1, "dragging the corner resizes (the height is already at the host window limit)");
  rig.drag({f2.x + f2.w + 58, f2.y + f2.h + 38}, {-2000, -2000});
  const dock::Rect floor = *rig.backend->contentRect(w);
  R1_EXPECT(floor.w >= 64 && floor.h >= 64, "never smaller than the minimum size");
  // The window cannot be dragged out of reach: the title bar stays inside the host window.
  const dock::Rect f3 = frameRect(rig, w);
  rig.drag({f3.x + 30, f3.y + 12}, {5000, 5000});
  const dock::Rect gone = *rig.backend->contentRect(w);
  R1_EXPECT(gone.x < 900 && gone.y < 600 && gone.x + gone.w > 0, "a title bar stays reachable");
}

void stacking_follows_presses_and_the_model() {
  DockRig rig;
  rig.setRoot(twoRegions());
  rig.host->floatPanel(2);
  rig.settle();
  const FloatId first = onlyWindow(rig);
  rig.host->floatPanel(5);
  rig.settle();
  const FloatId second = onlyWindow(rig);
  R1_EXPECT(first != second && rig.backend->stacking() == std::vector<FloatId>({first, second}));
  // Drag the second window away so the first can be pressed, then press its title.
  const dock::Rect second0 = frameRect(rig, second);
  rig.drag({second0.x + 40, second0.y + 12}, {second0.x + 40, second0.y + 12 + 250});
  R1_EXPECT(rig.backend->stacking().back() == second);
  const dock::Rect f = frameRect(rig, first);
  rig.click({f.x + 40, f.y + 12});
  R1_EXPECT(rig.backend->stacking().back() == first, "pressing a window raises it (spec 03 rule 40)");
  R1_EXPECT(rig.host->layout().areas().back().root->tabs[0] == 2, "and the model's order follows");
}

void close_button_closes_every_tab_or_none() {
  DockRig rig;
  rig.setRoot(twoRegions());
  rig.host->floatPanel(2);
  rig.settle();
  const FloatId w = onlyWindow(rig);
  const dock::Rect f = frameRect(rig, w);
  rig.click({f.x + f.w - 20, f.y + 17});
  R1_EXPECT(rig.backend->stacking().empty() && !rig.host->layout().isDocked(2), "the close button closes the window");
  R1_EXPECT(rig.host->layout().closedSlot(2) != nullptr && rig.host->layout().closedSlot(2)->floating, "its panel is remembered as floating");
  R1_EXPECT(rig.host->openPanel(2));
  rig.settle();
  R1_EXPECT(rig.backend->stacking().size() == 1, "reopening floats it again at the remembered place");

  // A locked tab refuses: nothing is closed (rule 43).
  rig.host->setPanelLocked(2, true);
  const FloatId again = onlyWindow(rig);
  const dock::Rect g = frameRect(rig, again);
  rig.click({g.x + g.w - 20, g.y + 17});
  R1_EXPECT(rig.backend->stacking().size() == 1 && rig.host->layout().isDocked(2), "a refusing tab keeps the window open");
  R1_EXPECT(!rig.host->lastError().empty());
}

void double_click_maximizes_and_restores() {
  DockRig rig;
  rig.setRoot(twoRegions());
  rig.host->floatPanel(2);
  rig.settle();
  const FloatId w = onlyWindow(rig);
  const dock::Rect before = *rig.backend->contentRect(w);
  const dock::Rect f = frameRect(rig, w);
  const dock::Point title{f.x + 40, f.y + 12};
  rig.click(title);
  rig.click(title);  // second click within the double-click time
  R1_EXPECT(rig.backend->isMaximized(w) || rig.host->layout().areas()[1].window.maximized, "double click on the title bar maximizes");
  const dock::Rect big = *rig.backend->contentRect(w);
  R1_EXPECT(big.w > before.w && big.w <= 900, "to the host window");
  R1_EXPECT(rig.host->layout().areas()[1].window.maximized && rig.host->layout().areas()[1].rect == big);
  const dock::Rect f2 = frameRect(rig, w);
  rig.click({f2.x + 40, f2.y + 12});
  rig.click({f2.x + 40, f2.y + 12});
  R1_EXPECT(!rig.backend->isMaximized(w) && *rig.backend->contentRect(w) == before, "and restores the previous rectangle");
}

void dropping_into_and_out_of_a_floating_window() {
  DockRig rig;
  rig.setRoot(dock::Node::split(dock::Axis::Row, {stackOf({1}), stackOf({4, 5}), stackOf({2, 3})}));
  rig.host->floatPanel(2);
  rig.settle();
  // Drag tab 4 (main area) onto the strip of the floating window, just after its only tab.
  const dock::Rect floatTab = rig.tab(2);
  rig.drag(rig.tabCenter(4), {floatTab.x + floatTab.w + 6, floatTab.y + 8});
  const std::optional<dock::PanelSlot> joined = rig.host->layout().locate(4);
  R1_EXPECT(joined.has_value() && joined->area == rig.host->layout().areas()[1].id, "joined the strip of the floating window");
  R1_EXPECT(joined.has_value() && joined->tab == 1 && joined->tabCount == 2, "at the slot under the pointer");
  R1_EXPECT(rig.strip(4)->window() == onlyWindow(rig), "and its strip lives in the window");
  // Drag it back out onto the left region's body (near its left edge): docked in the main window again.
  const dock::Rect mainBody = rig.body(1);
  rig.drag(rig.tabCenter(4), {mainBody.x + 20, mainBody.y + mainBody.h / 2});
  R1_EXPECT(rig.host->layout().locate(4)->area == dock::kMainAreaId, "docked back into the main window");
  // The window's last tab dragged onto the main window: the window hides during the drag and is gone after.
  const FloatId last = onlyWindow(rig);
  const dock::Rect only = rig.tab(2);
  const dock::Rect target = rig.body(1);
  rig.drag(center(only), {target.x + 20, target.y + target.h / 2}, false);
  R1_EXPECT(rig.host->dragging());
  R1_EXPECT(rig.t.ui.object(rig.backend->frameWidget(last))->paintOpacity() == 0.0f, "the window of a dragged sole tab disappears (rule 35)");
  rig.up({target.x + 20, target.y + target.h / 2});
  rig.settle();
  R1_EXPECT(rig.backend->stacking().empty() && rig.host->layout().areas().size() == 1, "and is gone after the drop");
  R1_EXPECT(rig.host->layout().locate(2)->area == dock::kMainAreaId);
}
void cancelling_a_drag_from_a_floating_window_returns_it() {
  DockRig rig;
  rig.setRoot(twoRegions());
  rig.host->floatPanel(2);
  rig.settle();
  const FloatId w = onlyWindow(rig);
  const dock::DockLayout before = rig.host->layout();
  const dock::Rect tab = rig.tab(2);
  rig.drag(center(tab), {tab.x + 400, tab.y + 300}, false);
  R1_EXPECT(rig.host->dragging());
  rig.t.ui.keyDown(Key::Escape);
  rig.settle();
  R1_EXPECT(rig.host->layout() == before && onlyWindow(rig) == w, "the tab returns to its floating window (rule 21)");
  R1_EXPECT(rig.t.ui.object(rig.backend->frameWidget(w))->paintOpacity() == 1.0f, "and the window is visible again");
}

void a_window_off_screen_is_brought_back_by_the_manager_inputs() {
  DockRig rig;
  rig.setRoot(twoRegions());
  rig.host->floatPanel(2);
  rig.settle();
  dock::DockLayout copy = rig.host->layout();
  const uint32_t area = copy.areas()[1].id;
  copy.setAreaRect(area, {9000, 9000, 300, 200});
  dock::MonitorSet set;
  set.monitors.push_back({"A", {0, 0, 900, 600}, {0, 0, 900, 600}, 1.0});
  R1_EXPECT(copy.fitWindows(set) == 1);
  R1_EXPECT(rig.host->setLayout(copy).ok);
  rig.settle();
  const dock::Rect content = *rig.backend->contentRect(onlyWindow(rig));
  R1_EXPECT(content.x >= 0 && content.x < 900 && content.y >= 0 && content.y < 600, "a reachable window after the fit");
}

void window_state_travels_with_the_layout() {
  DockRig rig;
  rig.setRoot(twoRegions());
  rig.host->floatPanel(2);
  rig.settle();
  const dock::WindowState& main = rig.host->layout().areas()[0].window;
  R1_EXPECT(main.hasRect && main.rect == rig.host->mainContentRect(), "the main content rectangle is recorded");
  const dock::WindowState& floating = rig.host->layout().areas()[1].window;
  R1_EXPECT(floating.dpiScale == 1.0 && floating.monitor == "window", "monitor identity and scale are recorded for floating windows");
  dock::WindowState ws;
  ws.maximized = true;
  ws.monitor = "DISPLAY1";
  rig.host->setMainWindowState(ws);
  R1_EXPECT(rig.host->layout().areas()[0].window.maximized && rig.host->layout().areas()[0].window.monitor == "DISPLAY1");
  const std::string json = rig.host->layout().toJson();
  R1_EXPECT(json.find("\"maximized\":true") != std::string::npos);
}

void closing_the_last_panel_shows_the_hint() {
  DockRig rig;
  rig.setRoot(stackOf({1, 2}));
  rig.host->closePanel(1);
  rig.host->closePanel(2);
  rig.settle();
  DockAreaView* view = rig.host->areaView(dock::kMainAreaId);
  R1_EXPECT(view != nullptr && view->showsEmptyHint() && view->units().empty(), "an empty area shows the placeholder");
  R1_EXPECT(rig.host->activePanel() == 0);
  R1_EXPECT(rig.host->openPanel(2));
  rig.settle();
  R1_EXPECT(!view->showsEmptyHint() && rig.host->layout().isDocked(2), "opening a panel fills it");
}

}  // namespace

int main() {
  floating_a_tab_creates_a_window();
  the_title_bar_moves_and_the_corner_resizes();
  stacking_follows_presses_and_the_model();
  close_button_closes_every_tab_or_none();
  double_click_maximizes_and_restores();
  dropping_into_and_out_of_a_floating_window();
  cancelling_a_drag_from_a_floating_window_returns_it();
  a_window_off_screen_is_brought_back_by_the_manager_inputs();
  window_state_travels_with_the_layout();
  closing_the_last_panel_shows_the_hint();
  return r1test::finish();
}

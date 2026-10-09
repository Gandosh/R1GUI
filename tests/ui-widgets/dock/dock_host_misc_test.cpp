// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of DockHost's keyboard handling, tab context menu, commands, hostile inputs and its
//   cooperation with the layout manager (slices 5.1 and 5.4 together): focus movement between strips
//   and panels, Ctrl+W, the menu, next/previous/float/reset commands, a content factory that throws,
//   layouts for other panels, destruction in the middle of a drag, a menu or a handle drag, and
//   applying a stored layout without losing the state of panels that stay open.
// Why: the failure modes of a docking host are lifetime and re-entrancy problems; each one has a case.
// Callers: CTest (label fast).
#include "DockTestSupport.h"
#include "r1ui/dock/LayoutManager.h"

using namespace dock_widget_test;
namespace Mod = r1ui::core::events::Mod;

namespace {

dock::Node twoRegions() { return dock::Node::split(dock::Axis::Row, {stackOf({1, 2, 3}), stackOf({4, 5})}); }

class FakeClock final : public dock::IClock {
 public:
  uint64_t nowMs() const override { return now; }
  uint64_t now = 1000;
};

void keyboard_moves_between_tabs_strips_and_panels() {
  DockRig rig;
  rig.setRoot(twoRegions());
  rig.click(rig.tabCenter(1));
  R1_EXPECT(rig.t.ui.router().focused() == rig.strip(1)->id(), "pressing a tab focuses its strip");
  rig.t.ui.keyDown(Key::Right);
  rig.settle();
  R1_EXPECT(rig.strip(1)->frontPanel() == 2 && rig.host->activePanel() == 2, "Right activates the next tab");
  rig.t.ui.keyDown(Key::End);
  R1_EXPECT(rig.strip(1)->frontPanel() == 3);
  rig.t.ui.keyDown(Key::Home);
  R1_EXPECT(rig.strip(1)->frontPanel() == 1);
  rig.t.ui.keyDown(Key::Left);
  R1_EXPECT(rig.strip(1)->frontPanel() == 1, "Left stops at the first tab");
  rig.t.ui.keyDown(Key::Down);
  R1_EXPECT(rig.t.ui.router().focused() == rig.host->contentOf(1), "Down moves focus into the panel");
  R1_EXPECT(rig.host->focusNextRegion(false) && rig.t.ui.router().focused() == rig.strip(4)->id(), "the next region's strip takes focus");
  R1_EXPECT(rig.host->activePanel() == 4, "and its front tab becomes the active tab");
  R1_EXPECT(rig.host->focusNextRegion(true) && rig.t.ui.router().focused() == rig.strip(1)->id());
  rig.t.ui.keyDown(static_cast<Key>('W'), Mod::kCtrl);
  rig.settle();
  R1_EXPECT(!rig.host->layout().isDocked(1) && rig.host->layout().isDocked(2), "Ctrl+W closes the front tab of the focused strip");
  R1_EXPECT(rig.host->focusActivePanel() && rig.t.ui.router().focused() == rig.host->contentOf(2), "the active panel's content can take focus");
}

void commands_for_the_command_layer() {
  DockRig rig;
  rig.setRoot(twoRegions());
  R1_EXPECT(rig.host->activatePanel(1));
  R1_EXPECT(rig.host->nextTab() && rig.host->activePanel() == 2);
  R1_EXPECT(rig.host->nextTab() && rig.host->nextTab() && rig.host->activePanel() == 1, "next wraps around");
  R1_EXPECT(rig.host->previousTab() && rig.host->activePanel() == 3, "previous wraps around");
  R1_EXPECT(rig.host->floatActiveTab() && rig.host->layout().areas().size() == 2 && rig.backend->stacking().size() == 1);
  R1_EXPECT(!rig.host->resetLayout() && !rig.host->lastError().empty(), "no default layout supplied: refused");
  const dock::Node original = *rig.host->layout().areas()[0].root;
  rig.host->setDefaultLayout([&rig] {
    dock::DockLayoutResult r = dock::DockLayout::create(rig.registry.infos(), {}, dock::Node::stack({6, 5, 4}));
    return std::move(*r.layout);
  });
  R1_EXPECT(rig.host->resetLayout());
  rig.settle();
  R1_EXPECT(rig.host->layout().areas().size() == 1 && rig.backend->stacking().empty() && rig.host->layout().areas()[0].root->tabs == std::vector<dock::PanelId>({6, 5, 4}),
            "reset applies the default and removes the floating window");
  R1_EXPECT(rig.strip(6) != nullptr && rig.strip(6)->tabCount() == 3);
  R1_EXPECT(original.kind == dock::Node::Kind::Split);
  // Close groups skip tabs that refuse and report how many closed.
  rig.host->setPanelLocked(5, true);
  R1_EXPECT(rig.host->closeOthers(6) == 1 && rig.host->layout().isDocked(5) && !rig.host->layout().isDocked(4), "locked tabs are skipped");
  rig.host->setPanelLocked(5, false);
  R1_EXPECT(rig.host->openPanel(4) && rig.host->closeToLeft(4) == 2 && rig.host->layout().areas()[0].root->tabs == std::vector<dock::PanelId>({4}), "close to the left");
}

void the_tab_context_menu() {
  DockRig rig;
  rig.setRoot(twoRegions());
  rig.click(rig.tabCenter(2), Button::Right);
  R1_EXPECT(rig.t.ui.overlays().any(), "right click opens a context menu");
  R1_EXPECT(rig.host->activePanel() == 2 && rig.strip(2)->frontPanel() == 2, "and activates the tab (rule 2)");
  rig.t.ui.keyDown(Key::Down);
  rig.t.ui.keyDown(Key::Enter);
  rig.settle();
  R1_EXPECT(!rig.host->layout().isDocked(2) && !rig.t.ui.overlays().any(), "the first entry closes the tab and the menu goes away");

  rig.click(rig.tabCenter(5), Button::Right);
  R1_EXPECT(rig.t.ui.overlays().any());
  // Close, Close Others, Close to the Left are enabled for the second tab of a two tab region; the
  // fourth enabled entry is Float.
  for (int i = 0; i < 4; ++i) rig.t.ui.keyDown(Key::Down);
  rig.t.ui.keyDown(Key::Enter);
  rig.settle();
  R1_EXPECT(rig.host->layout().areas().size() == 2 && rig.host->layout().areas()[1].root->tabs[0] == 5, "Float moves the tab to its own window");
  rig.t.ui.keyDown(Key::Escape);

  // Escape closes the menu; a right click on the empty strip uses the front tab.
  const dock::Rect strip = toDockRect(rig.t.ui.absRect(rig.strip(1)->id()));
  rig.click({strip.x + strip.w - 4, strip.y + 8}, Button::Right);
  R1_EXPECT(rig.t.ui.overlays().any());
  rig.t.ui.keyDown(Key::Escape);
  R1_EXPECT(!rig.t.ui.overlays().any());
}

void lock_via_the_menu_persists() {
  DockRig rig;
  rig.setRoot(stackOf({1, 2}));
  rig.click(rig.tabCenter(1), Button::Right);
  // Enabled entries of a first tab: Close, Close Others, Close to the Right, Float, Move to New
  // Window, Lock Tab (pin and collapse need a parent split).
  for (int i = 0; i < 6; ++i) rig.t.ui.keyDown(Key::Down);
  rig.t.ui.keyDown(Key::Enter);
  rig.settle();
  R1_EXPECT(rig.host->layout().panel(1)->locked, "Lock Tab sets the flag");
  R1_EXPECT(rig.host->layout().toJson().find("\"locked\":true") != std::string::npos, "and it is part of the saved layout (D13)");
  R1_EXPECT(rig.changes.back() == DockChange::Lock);
}

void throwing_factory_and_foreign_layouts() {
  DockRig rig;
  PanelDescriptor bad;
  bad.id = 7;
  bad.title = "Bad";
  bad.factory = [](UiContext&, WidgetId) -> WidgetId { throw std::runtime_error("boom"); };
  rig.registry.add(bad);
  R1_EXPECT(rig.host->refreshPanels().ok);
  rig.setRoot(stackOf({7, 1}));
  rig.click(rig.tabCenter(7));
  R1_EXPECT(!rig.host->contentOf(7).valid() && rig.host->lastError().find("boom") != std::string::npos, "a throwing factory leaves an empty body");
  R1_EXPECT(rig.host->layout().isDocked(7), "the panel itself stays");
  // A layout built for other panels is refused and nothing changes.
  dock::DockLayoutResult foreign = dock::DockLayout::create({{1, "A", true}, {2, "B", true}}, {}, dock::Node::stack({1}));
  const dock::DockLayout before = rig.host->layout();
  R1_EXPECT(!rig.host->setLayout(std::move(*foreign.layout)).ok && rig.host->layout() == before);
  dock::DockLayoutResult unknown = dock::DockLayout::create({{1, "A", true}, {99, "Z", true}, {2, "B", true}, {3, "C", true}, {4, "D", true}, {5, "E", true}, {6, "F", true}, {7, "G", true}}, {}, dock::Node::stack({1}));
  R1_EXPECT(!rig.host->setLayout(std::move(*unknown.layout)).ok, "an unregistered panel is refused");
}

void many_tabs_overflow_with_arrows_and_list() {
  DockRig rig(900, 600, 200, DockHostOptions{});
  std::vector<dock::PanelId> ids;
  for (dock::PanelId i = 1; i <= 200; ++i) ids.push_back(i);
  rig.setRoot(stackOf(ids));
  DockTabStrip* strip = rig.strip(1);
  R1_EXPECT(strip->overflowing() && std::abs(strip->naturalTabWidth() - 60.0) < 0.01, "tabs keep the 60 px minimum and the strip overflows (D12)");
  const dock::Rect last = strip->tabRect(199);
  R1_EXPECT(last.x > 900, "the last tab is scrolled out of view");
  rig.t.ui.wheel(300, 10, 0, -3);
  R1_EXPECT(strip->scrollOffset() > 0, "the wheel scrolls the strip");
  const double before = strip->scrollOffset();
  const dock::Rect right = strip->rightArrowRect();
  rig.click({right.x + 4, right.y + 8});
  R1_EXPECT(strip->scrollOffset() > before, "the right arrow scrolls");
  const dock::Rect list = strip->listButtonRect();
  rig.click({list.x + 4, list.y + 8});
  R1_EXPECT(rig.t.ui.overlays().any(), "the list button opens the all-tabs list");
  for (int i = 0; i < 12; ++i) rig.t.ui.keyDown(Key::Down);
  rig.t.ui.keyDown(Key::Enter);
  rig.settle();
  R1_EXPECT(rig.host->activePanel() == 12 && strip->frontPanel() == 12, "picking a tab activates it");
  const dock::Rect shown = strip->tabRect(11);
  R1_EXPECT(shown.x >= 20 && shown.x + shown.w <= 900, "and scrolls it into view");
  // Clicking a tab that is partly hidden under an arrow does not activate the arrow's neighbour.
  R1_EXPECT(rig.t.ui.alive(strip->id()));
}

void destroying_the_host_in_the_middle_of_things() {
  {  // mid tab drag
    DockRig rig;
    rig.setRoot(twoRegions());
    rig.drag(rig.tabCenter(2), {600, 300}, false);
    R1_EXPECT(rig.host->dragging());
    rig.t.ui.destroy(rig.host->id());
    rig.t.ui.pointerMove(610, 310);
    rig.t.ui.pointerUp(610, 310);
    rig.t.ui.frame();
    bool overlayLeft = false;
    for (WidgetId w = rig.t.ui.tree().firstChild(rig.t.ui.overlays().layer()); w.valid(); w = rig.t.ui.tree().nextSibling(w)) {
      overlayLeft = overlayLeft || rig.t.ui.objectAs<DockDragOverlay>(w) != nullptr;
    }
    R1_EXPECT(!overlayLeft, "the drag overlay goes with the host");
  }
  {  // with a floating window and an open menu
    DockRig rig;
    rig.setRoot(twoRegions());
    rig.host->floatPanel(2);
    rig.settle();
    rig.click(rig.tabCenter(4), Button::Right);
    R1_EXPECT(rig.t.ui.overlays().any());
    rig.t.ui.destroy(rig.host->id());
    rig.t.ui.frame();
    R1_EXPECT(rig.backend->stacking().empty(), "the host removes the windows it created");
  }
  {  // the observer destroys the host from inside a callback
    DockRig rig;
    rig.setRoot(twoRegions());
    UiContext* ui = &rig.t.ui;
    const WidgetId id = rig.host->id();
    rig.host->setOnChanged([ui, id](DockChange) { ui->destroy(id); });
    rig.host->closePanel(2);
    rig.t.ui.frame();
    R1_EXPECT(!rig.t.ui.alive(id));
  }
  {  // mid handle drag
    DockRig rig;
    rig.setRoot(twoRegions());
    const dock::Rect handle = rig.host->layout().computeLayout(rig.host->mainContentRect()).handles[0].rect;
    rig.drag({handle.x + 2, handle.y + 100}, {handle.x + 60, handle.y + 100}, false);
    rig.t.ui.destroy(rig.host->id());
    rig.t.ui.pointerUp(handle.x + 60, handle.y + 100);
    rig.t.ui.frame();
    R1_EXPECT(true);
  }
}

void applying_a_layout_during_a_drag_ends_the_drag() {
  DockRig rig;
  rig.setRoot(twoRegions());
  rig.drag(rig.tabCenter(2), {600, 300}, false);
  R1_EXPECT(rig.host->dragging());
  rig.changes.clear();
  dock::DockLayoutResult next = dock::DockLayout::create(rig.registry.infos(), {}, stackOf({5, 6}));
  R1_EXPECT(rig.host->setLayout(std::move(*next.layout)).ok);
  rig.settle();
  R1_EXPECT(!rig.host->dragging(), "a new layout ends the drag");
  R1_EXPECT(std::find(rig.changes.begin(), rig.changes.end(), DockChange::DragEnded) != rig.changes.end(), "and tells the observer, so deferred saving resumes");
  rig.up({600, 300});
  rig.settle();
  R1_EXPECT(rig.host->layout().areas()[0].root->tabs == std::vector<dock::PanelId>({5, 6}), "the release changes nothing");
}
void hostile_pointer_input_is_ignored() {
  DockRig rig;
  rig.setRoot(twoRegions());
  const double nan = std::numeric_limits<double>::quiet_NaN();
  rig.t.ui.pointerMove(nan, 5);
  rig.t.ui.pointerDown(nan, nan);
  rig.t.ui.pointerMove(1e300, -1e300);
  rig.t.ui.pointerUp(1e300, -1e300);
  rig.t.ui.pointerMove(-1e9, -1e9);
  rig.settle();
  R1_EXPECT(!rig.host->dragging() && rig.host->layout().areas().size() == 1);
  // A tab that disappears under a pending press.
  const dock::Point c = rig.tabCenter(2);
  rig.move(c);
  rig.down(c);
  rig.host->closePanel(2);
  rig.move({c.x + 50, c.y + 50});
  rig.up({c.x + 50, c.y + 50});
  rig.settle();
  R1_EXPECT(!rig.host->dragging() && !rig.host->layout().isDocked(2));
}

void applying_a_stored_layout_keeps_the_panels_that_stay_open() {
  DockRig rig;
  rig.setRoot(twoRegions());
  dock::MemoryLayoutStore store;
  FakeClock clock;
  dock::LayoutManager manager(store, clock, *rig.host);
  manager.setDefaultProvider([&rig] {
    dock::DockLayoutResult r = dock::DockLayout::create(rig.registry.infos(), {}, dock::Node::split(dock::Axis::Row, {stackOf({1, 2, 3}), stackOf({4, 5})}));
    return std::move(*r.layout);
  });
  rig.host->setOnChanged([&](DockChange c) {
    if (c == DockChange::DragStarted) manager.setSuspended(true);
    else if (c == DockChange::DragEnded) manager.setSuspended(false);
    else manager.notifyChanged();
  });
  R1_EXPECT(manager.startup().ok);
  rig.settle();
  const WidgetId content1 = rig.host->contentOf(1);
  R1_EXPECT(content1.valid() && rig.factoryCalls[1] == 1);

  std::string key;
  R1_EXPECT(manager.saveAs("Stacked", "tabs together", &key).ok);
  // Rearrange: panel 1 is dragged onto the right stack; then reload the saved arrangement.
  rig.drag(rig.tabCenter(1), {rig.tab(5).x + rig.tab(5).w + 6, rig.tab(5).y + 8});
  R1_EXPECT(rig.host->layout().locate(1)->tabCount == 3 && rig.host->layout().locate(1)->area == dock::kMainAreaId);
  R1_EXPECT(manager.dirty() && manager.msUntilSave() == 5000, "the change was reported to the manager");
  clock.now += 5000;
  manager.tick();
  R1_EXPECT(!manager.dirty() && store.read("default", "_active") == rig.host->layout().toJson(), "five seconds later the active layout is written");

  const dock::LayoutReport report = manager.load(key);
  rig.settle();
  R1_EXPECT(report.ok, "loads");
  R1_EXPECT(rig.host->layout().locate(1)->tabCount == 3 && rig.host->layout().locate(4)->tabCount == 2, "the stored arrangement is back");
  R1_EXPECT(rig.host->contentOf(1) == content1 && rig.factoryCalls[1] == 1, "panel 1 stayed open, so its content (and state) is the same widget");
  R1_EXPECT(rig.t.ui.alive(content1));

  // A layout that floats a panel: the floating window appears; loading the other one removes it.
  rig.host->floatPanel(5);
  rig.settle();
  R1_EXPECT(manager.saveAs("Floating", "", nullptr).ok);
  R1_EXPECT(manager.load(key).ok);
  rig.settle();
  R1_EXPECT(rig.backend->stacking().empty());
  R1_EXPECT(manager.load("Floating").ok);
  rig.settle();
  R1_EXPECT(rig.backend->stacking().size() == 1 && rig.host->layout().locate(5)->area != dock::kMainAreaId);
  // Rejected loads leave everything alone.
  store.write("default", "bad", "nonsense");
  const dock::DockLayout shown = rig.host->layout();
  R1_EXPECT(!manager.load("bad").ok && rig.host->layout() == shown);
  // Reset with a confirmation callback.
  R1_EXPECT(manager.resetToDefault([](std::string_view) { return false; }).cancelled && rig.host->layout() == shown);
  R1_EXPECT(manager.resetToDefault([](std::string_view) { return true; }).done);
  rig.settle();
  R1_EXPECT(rig.backend->stacking().empty() && rig.host->layout().areas().size() == 1);
}

}  // namespace

int main() {
  keyboard_moves_between_tabs_strips_and_panels();
  commands_for_the_command_layer();
  the_tab_context_menu();
  lock_via_the_menu_persists();
  throwing_factory_and_foreign_layouts();
  many_tabs_overflow_with_arrows_and_list();
  destroying_the_host_in_the_middle_of_things();
  applying_a_layout_during_a_drag_ends_the_drag();
  hostile_pointer_input_is_ignored();
  applying_a_stored_layout_keeps_the_panels_that_stay_open();
  return r1test::finish();
}

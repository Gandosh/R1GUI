// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: regression tests for TabBar state that goes stale while user code runs (phase 4 review H3,
//   M11, M17): onActivate removing or reordering tabs mid-press, animation slots of tabs whose ids
//   differ only in high bits, slots given back when a tab is removed, and the all-tabs list closing
//   with its bar.
// Why: the press handler used the tab index it computed before the callback, which read past the
//   end of the tab vectors (a Debug assertion, a heap over-read in Release) once the callback removed tabs.
// Callers: CTest (fast tier). Calls: UiContext, TabBar.
#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/tabbar/TabBar.h"

using namespace r1ui::widgets;
namespace layout = r1ui::core::layout;

namespace {

TabBar& makeBar(r1test::TestUi& t, int tabs, double width = 500) {
  t.ui.rootStyle().direction = layout::FlexDirection::Column;
  TabBar& bar = t.ui.create<TabBar>(t.ui.root());
  bar.style().width = layout::Length::px(width);
  for (int i = 1; i <= tabs; ++i) bar.addTab(static_cast<TabId>(i), "Tab " + std::to_string(i));
  t.layout();
  return bar;
}

void paintOnce(UiContext& ui) {
  r1ui::render::Painter painter;
  painter.begin(600, 200);
  ui.frame();
  ui.paint(painter);
  ui.finishPaint();
  painter.end();
}

}  // namespace

int main() {
  {  // H3: onActivate removes every tab in the middle of a press
    r1test::TestUi t(600, 200);
    TabBar& bar = makeBar(t, 3);
    bar.setOnActivate([&](TabId) {
      while (bar.tabCount() > 0) bar.removeTab(bar.tab(0).id);
    });
    const auto r = bar.tabRect(3);
    t.ui.pointerMove(r.x + 20, r.y + 10);
    t.ui.pointerDown(r.x + 20, r.y + 10);
    t.ui.pointerMove(r.x + 40, r.y + 10);
    t.ui.pointerUp(r.x + 40, r.y + 10);
    R1_EXPECT(bar.tabCount() == 0);
    R1_EXPECT(t.ui.inputFaults() == 0);
    R1_EXPECT(!bar.dragging());
  }
  {  // H3: onActivate moves the pressed tab to the front; the drag follows the tab, not the old index
    r1test::TestUi t(600, 200);
    TabBar& bar = makeBar(t, 3);
    bar.setOnActivate([&](TabId id) { bar.moveTab(id, 0); });
    const auto r = bar.tabRect(3);
    t.ui.pointerMove(r.x + 20, r.y + 10);
    t.ui.pointerDown(r.x + 20, r.y + 10);
    R1_EXPECT(bar.tab(0).id == 3);
    t.ui.pointerUp(r.x + 20, r.y + 10);
    R1_EXPECT(t.ui.inputFaults() == 0);
  }
  {  // H3: a context-menu callback after onActivate gets the id of the tab that was pressed
    r1test::TestUi t(600, 200);
    TabBar& bar = makeBar(t, 3);
    bar.setOnActivate([&](TabId) { bar.removeTab(1); });
    TabId reported = 0;
    bar.setOnContextMenu([&](TabId id, double, double) { reported = id; });
    const auto r = bar.tabRect(3);
    t.ui.pointerMove(r.x + 20, r.y + 10);
    t.ui.pointerDown(r.x + 20, r.y + 10, r1ui::core::events::Button::Right);
    R1_EXPECT(reported == 3);
    R1_EXPECT(t.ui.inputFaults() == 0);
  }
  {  // M11: ids that differ only above bit 28 must not share an animation slot (no endless frames)
    r1test::TestUi t(600, 200);
    t.ui.setAnimationsEnabled(true);
    t.ui.setFrameLoopRunning(true);
    t.ui.rootStyle().direction = layout::FlexDirection::Column;
    TabBar& bar = t.ui.create<TabBar>(t.ui.root());
    bar.style().width = layout::Length::px(500);
    bar.addTab(1, "One");
    bar.addTab(1 + (uint64_t{1} << 28), "Two");
    bar.addTab(1 + (uint64_t{1} << 40), "Three");
    t.layout();
    uint64_t now = 1000;
    int busy = 0;
    for (int i = 0; i < 40; ++i) {
      now += 16;
      t.ui.setTime(now);
      paintOnce(t.ui);
      if (i >= 20 && t.ui.needsFrame()) ++busy;
    }
    R1_EXPECT(busy == 0);
    R1_EXPECT(t.ui.animationCount() >= 6);  // two tweens per tab are alive
    // Removing a tab gives its animation slots back.
    const size_t before = t.ui.animationCount();
    bar.removeTab(1 + (uint64_t{1} << 28));
    R1_EXPECT(t.ui.animationCount() + 2 == before);
    // Adding and removing tabs forever does not grow the table.
    for (uint64_t id = 100; id < 300; ++id) {
      bar.addTab(id, "x");
      paintOnce(t.ui);
      bar.removeTab(id);
    }
    paintOnce(t.ui);
    R1_EXPECT(t.ui.animationCount() <= before + 2);
  }
  {  // M17: the all-tabs list belongs to the bar
    r1test::TestUi t(300, 200);
    TabBar& bar = makeBar(t, 30, 250);
    R1_EXPECT(bar.overflowing());
    R1_EXPECT(bar.openTabList());
    t.layout();
    R1_EXPECT(t.ui.overlays().count() >= 1);
    t.ui.destroy(bar.id());
    t.layout();
    R1_EXPECT(t.ui.overlays().count() == 0);
  }
  return r1test::finish();
}

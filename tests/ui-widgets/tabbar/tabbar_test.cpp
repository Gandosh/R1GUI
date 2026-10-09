// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for TabBar: measured tab geometry (a tab titled "Untitled" is 109 px wide),
//   activation on press, close button / middle click / Ctrl+W, neighbour activation after closing,
//   even shrinking, the D12 overflow (60 px minimum, scroll arrows, wheel, all-tabs list), drag
//   reordering with Escape cancel, tooltips with the full title, keyboard navigation and hostile
//   input (duplicate ids, thousands of tabs, invalid UTF-8, zero width, handlers that destroy the
//   bar, removal or disabling in the middle of a drag).
// Callers: CTest (tabbar fast, no GPU).
#include <cmath>
#include <string>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/tabbar/TabBar.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::events::Button;
using r1ui::core::events::Key;
namespace Mod = r1ui::core::events::Mod;
namespace layout = r1ui::core::layout;

struct Rig {
  explicit Rig(double width = 600) : t(1600, 200) {
    t.ui.rootStyle().direction = layout::FlexDirection::Column;
    bar = &t.ui.create<TabBar>(t.ui.root());
    bar->style().width = layout::Length::px(width);
    t.layout();
  }
  void setWidth(double w) {
    bar->style().width = layout::Length::px(w);
    bar->requestLayout();
    t.layout();
  }
  void click(double x, double y, Button b = Button::Left) {
    t.ui.pointerMove(x, y);
    t.ui.pointerDown(x, y, b);
    t.ui.pointerUp(x, y, b);
  }
  void clickRect(const layout::Rect& r, Button b = Button::Left) { click(r.x + r.w / 2.0, r.y + r.h / 2.0, b); }
  void paint() {
    r1ui::render::Painter painter;
    painter.begin(1600, 200);
    t.ui.paint(painter);
    t.ui.finishPaint();
    painter.end();
  }
  r1test::TestUi t;
  TabBar* bar = nullptr;
};

void testGeometry() {
  Rig rig;
  rig.bar->addTab(1, "Untitled");
  rig.bar->addTab(2, "Untitled");
  rig.t.layout();
  const layout::Rect a = rig.bar->tabRect(1);
  const layout::Rect b = rig.bar->tabRect(2);
  R1_EXPECT(std::abs(a.w - 109) <= 1);  // measured: 12 + 12 + 6 + text + 6 + 16 + 12 + 1 border
  R1_EXPECT(b.x == a.x + a.w && b.h == 35);
  const layout::Rect close = rig.bar->closeRect(1);
  R1_EXPECT(close.w == 16 && close.h == 16 && close.x + close.w == a.x + a.w - 1 - 12);
  const layout::Rect plus = rig.bar->newButtonRect();
  R1_EXPECT(plus.w == 36 && plus.h == 36 && plus.x == b.x + b.w);  // directly after the last tab
  R1_EXPECT(rig.t.ui.absRect(rig.bar->id()).h == 36);
  R1_EXPECT(!rig.bar->overflowing());
  R1_EXPECT(rig.bar->activeTab() && *rig.bar->activeTab() == 1);
  rig.bar->setShowNewButton(false);
  R1_EXPECT(rig.bar->newButtonRect().w == 0);
  rig.paint();
  // The cap: a very long title stops at 192 px.
  rig.bar->addTab(3, std::string(300, 'W'));
  rig.t.layout();
  R1_EXPECT(rig.bar->tabRect(3).w == 192);
  R1_EXPECT(rig.bar->tabRect(3).x == b.x + b.w);
  // Tabs without a close button or icon are narrower.
  rig.bar->addTab(4, "Untitled", TabOptions{.icon = "", .closable = false});
  rig.t.layout();
  R1_EXPECT(rig.bar->tabRect(4).w < a.w - 20 && rig.bar->closeRect(4).w == 0);
}

void testActivationAndClose() {
  Rig rig;
  std::vector<TabId> activated;
  std::vector<TabId> closed;
  rig.bar->setOnActivate([&](TabId id) { activated.push_back(id); });
  rig.bar->addTab(1, "One");
  rig.bar->addTab(2, "Two");
  rig.bar->addTab(3, "Three");
  rig.t.layout();
  rig.clickRect(rig.bar->tabRect(2));
  R1_EXPECT(activated.size() == 1 && activated[0] == 2 && *rig.bar->activeTab() == 2);
  rig.clickRect(rig.bar->tabRect(2));
  R1_EXPECT(activated.size() == 1);  // already active
  // The press activates at once, before the release.
  const layout::Rect t3 = rig.bar->tabRect(3);
  rig.t.ui.pointerMove(t3.x + 10, t3.y + 10);
  rig.t.ui.pointerDown(t3.x + 10, t3.y + 10);
  R1_EXPECT(*rig.bar->activeTab() == 3);
  rig.t.ui.pointerUp(t3.x + 10, t3.y + 10);
  // Close button: no handler -> removed; the right neighbour becomes active, else the left.
  rig.clickRect(rig.bar->tabRect(2));
  activated.clear();
  rig.clickRect(rig.bar->closeRect(2));
  R1_EXPECT(rig.bar->tabCount() == 2 && !rig.bar->indexOf(2));
  R1_EXPECT(*rig.bar->activeTab() == 3 && activated.size() == 1 && activated[0] == 3);
  activated.clear();
  rig.t.layout();
  rig.clickRect(rig.bar->closeRect(3));  // the last tab: the left one takes over
  R1_EXPECT(rig.bar->tabCount() == 1 && *rig.bar->activeTab() == 1 && activated.size() == 1);
  rig.bar->addTab(5, "Five");
  rig.bar->addTab(6, "Six");
  rig.t.layout();
  rig.bar->setOnCloseRequested([&](TabId id) { closed.push_back(id); });
  // Handler present: the bar does not remove by itself.
  rig.clickRect(rig.bar->closeRect(5));
  R1_EXPECT(closed.size() == 1 && closed[0] == 5 && rig.bar->indexOf(5));
  // Middle click closes only when released over the same tab.
  const layout::Rect r6 = rig.bar->tabRect(6);
  rig.click(r6.x + 20, r6.y + 10, Button::Middle);
  R1_EXPECT(closed.size() == 2 && closed[1] == 6);
  const layout::Rect r5 = rig.bar->tabRect(5);
  rig.t.ui.pointerMove(r5.x + 10, r5.y + 10);
  rig.t.ui.pointerDown(r5.x + 10, r5.y + 10, Button::Middle);
  rig.t.ui.pointerMove(r6.x + 10, r6.y + 10);
  rig.t.ui.pointerUp(r6.x + 10, r6.y + 10, Button::Middle);
  R1_EXPECT(closed.size() == 2);
  // Press on the close button but release elsewhere: nothing closes.
  const layout::Rect c5 = rig.bar->closeRect(5);
  rig.t.ui.pointerMove(c5.x + 4, c5.y + 4);
  rig.t.ui.pointerDown(c5.x + 4, c5.y + 4);
  rig.t.ui.pointerMove(r6.x + 10, r6.y + 10);
  rig.t.ui.pointerUp(r6.x + 10, r6.y + 10);
  R1_EXPECT(closed.size() == 2);
  // A non-closable tab never reports a close.
  rig.bar->addTab(7, "Pinned", TabOptions{.closable = false});
  rig.t.layout();
  rig.click(rig.bar->tabRect(7).x + 20, 10, Button::Middle);
  R1_EXPECT(closed.size() == 2);
  // New button and context menu callbacks.
  int news = 0;
  double ctxX = -1;
  TabId ctxTab = 0;
  rig.bar->setOnNewTab([&] { ++news; });
  rig.bar->setOnContextMenu([&](TabId id, double x, double) { ctxTab = id; ctxX = x; });
  rig.clickRect(rig.bar->newButtonRect());
  R1_EXPECT(news == 1);
  rig.click(rig.bar->tabRect(5).x + 5, 10, Button::Right);
  R1_EXPECT(ctxTab == 5 && ctxX == rig.bar->tabRect(5).x + 5 && *rig.bar->activeTab() == 5);
}

void testShrinkAndOverflow() {
  Rig rig(480);
  for (TabId i = 1; i <= 6; ++i) rig.bar->addTab(i, "A fairly long document title " + std::to_string(i));
  rig.t.layout();
  R1_EXPECT(!rig.bar->overflowing());
  const int w0 = rig.bar->tabRect(1).w;
  R1_EXPECT(w0 >= 60 && w0 <= 192);
  int total = 0;
  for (TabId i = 1; i <= 6; ++i) total += rig.bar->tabRect(i).w;
  R1_EXPECT(total + 36 == 480 || total + 36 == 479 || total + 36 == 481);  // fills the bar, new button at the end
  R1_EXPECT(rig.bar->newButtonRect().x + 36 == rig.t.ui.absRect(rig.bar->id()).x + 480 || std::abs(rig.bar->newButtonRect().x + 36 - 480) <= 1);

  // 12 tabs in 480 px: below 60 px each -> overflow with scrolling controls.
  for (TabId i = 7; i <= 12; ++i) rig.bar->addTab(i, "Tab " + std::to_string(i));
  rig.t.layout();
  R1_EXPECT(rig.bar->overflowing());
  R1_EXPECT(rig.bar->tabRect(1).w == 60 && rig.bar->tabRect(12).w == 60);
  R1_EXPECT(rig.bar->leftArrowRect().w == 24 && rig.bar->rightArrowRect().w == 24 && rig.bar->listButtonRect().w == 24);
  R1_EXPECT(rig.bar->newButtonRect().x + 36 == 480);
  // Scrolling: arrows, wheel, clamping.
  const layout::Rect bar = rig.t.ui.absRect(rig.bar->id());
  R1_EXPECT(rig.bar->scrollOffset() == 0.0);
  rig.clickRect(rig.bar->rightArrowRect());
  R1_EXPECT(rig.bar->scrollOffset() == 60.0);
  rig.clickRect(rig.bar->leftArrowRect());
  rig.clickRect(rig.bar->leftArrowRect());
  R1_EXPECT(rig.bar->scrollOffset() == 0.0);
  rig.t.ui.pointerMove(bar.x + 100, bar.y + 10);
  R1_EXPECT(rig.t.ui.wheel(bar.x + 100, bar.y + 10, 0, -1));
  R1_EXPECT(rig.bar->scrollOffset() == 48.0);
  rig.t.ui.wheel(bar.x + 100, bar.y + 10, 0, -100);
  const double maxScroll = rig.bar->scrollOffset();
  R1_EXPECT(maxScroll == 12 * 60 - (480 - 36 - 72));
  R1_EXPECT(!rig.t.ui.wheel(bar.x + 100, bar.y + 10, 0, -1));  // at the end: not consumed
  // Activating a tab scrolls it into view.
  rig.bar->setActiveTab(1);
  R1_EXPECT(rig.bar->scrollOffset() == 0.0);
  rig.bar->setActiveTab(12);
  R1_EXPECT(rig.bar->scrollOffset() == maxScroll);
  R1_EXPECT(!rig.bar->setActiveTab(99));
  // Tabs scrolled out of the strip are not hit.
  rig.bar->setActiveTab(1);
  int closeCalls = 0;
  rig.bar->setOnCloseRequested([&](TabId) { ++closeCalls; });
  rig.click(bar.x + 24 - 5, bar.y + 10);  // on the left arrow, not a tab
  R1_EXPECT(closeCalls == 0);
  // The all-tabs list opens an overlay with every tab; picking activates and closes it.
  R1_EXPECT(rig.bar->openTabList());
  R1_EXPECT(rig.t.ui.overlays().count() == 1);
  rig.t.layout();
  R1_EXPECT(rig.bar->openTabList() == false && rig.t.ui.overlays().count() == 0);  // second press toggles it closed
  rig.clickRect(rig.bar->listButtonRect());
  rig.t.layout();
  R1_EXPECT(rig.t.ui.overlays().count() == 1);
  rig.t.ui.keyDown(Key::Down);
  rig.t.ui.keyDown(Key::Down);
  rig.t.ui.keyDown(Key::Enter);
  R1_EXPECT(rig.t.ui.overlays().count() == 0 && *rig.bar->activeTab() != 1);
  rig.setWidth(2000);
  R1_EXPECT(!rig.bar->overflowing() && rig.bar->scrollOffset() == 0.0);
  rig.paint();
}

void testDragReorder() {
  Rig rig;
  for (TabId i = 1; i <= 4; ++i) rig.bar->addTab(i, "Doc " + std::to_string(i));
  rig.t.layout();
  std::vector<std::tuple<TabId, size_t, size_t>> moves;
  rig.bar->setOnReorder([&](TabId id, size_t from, size_t to) { moves.emplace_back(id, from, to); });
  const layout::Rect a = rig.bar->tabRect(1);
  const double y = a.y + 10;
  rig.t.ui.pointerMove(a.x + 20, y);
  rig.t.ui.pointerDown(a.x + 20, y);
  rig.t.ui.pointerMove(a.x + 24, y);  // below the threshold: no drag
  R1_EXPECT(!rig.bar->dragging());
  rig.t.ui.pointerMove(a.x + 26, y);  // strictly more than 5 px
  R1_EXPECT(rig.bar->dragging());
  const layout::Rect third = rig.bar->tabRect(3);
  rig.t.ui.pointerMove(third.x + 40, y);  // the dragged tab's centre passes the third tab's centre: slot 2
  rig.paint();
  rig.t.ui.pointerUp(third.x + 40, y);
  rig.t.layout();
  R1_EXPECT(!rig.bar->dragging() && moves.size() == 1);
  R1_EXPECT(std::get<0>(moves[0]) == 1 && std::get<1>(moves[0]) == 0 && std::get<2>(moves[0]) == 2);
  R1_EXPECT(rig.bar->tab(0).id == 2 && rig.bar->tab(1).id == 3 && rig.bar->tab(2).id == 1 && rig.bar->tab(3).id == 4);
  // Dropping where it started reports nothing.
  moves.clear();
  const layout::Rect b = rig.bar->tabRect(1);
  rig.t.ui.pointerMove(b.x + 10, y);
  rig.t.ui.pointerDown(b.x + 10, y);
  rig.t.ui.pointerMove(b.x + 30, y);
  rig.t.ui.pointerMove(b.x + 12, y);
  rig.t.ui.pointerUp(b.x + 12, y);
  R1_EXPECT(moves.empty() && rig.bar->tab(0).id == 1 + 1);
  // Escape puts the tab back.
  const layout::Rect c = rig.bar->tabRect(2);
  rig.t.ui.pointerMove(c.x + 10, y);
  rig.t.ui.pointerDown(c.x + 10, y);
  rig.t.ui.pointerMove(c.x + 80, y);
  R1_EXPECT(rig.bar->dragging());
  R1_EXPECT(rig.t.ui.keyDown(Key::Escape));
  R1_EXPECT(!rig.bar->dragging());
  rig.t.ui.pointerUp(c.x + 80, y);
  R1_EXPECT(moves.empty() && rig.bar->tab(0).id == 2);
  // A right-button press never starts a drag.
  rig.t.ui.pointerDown(c.x + 10, y, Button::Right);
  rig.t.ui.pointerMove(c.x + 90, y);
  R1_EXPECT(!rig.bar->dragging());
  rig.t.ui.pointerUp(c.x + 90, y, Button::Right);
  // Dragging far outside the strip clamps to the ends.
  const layout::Rect d = rig.bar->tabRect(2);
  rig.t.ui.pointerMove(d.x + 10, y);
  rig.t.ui.pointerDown(d.x + 10, y);
  rig.t.ui.pointerMove(1590, y + 150);
  rig.t.ui.pointerUp(1590, y + 150);
  R1_EXPECT(moves.size() == 1 && std::get<2>(moves[0]) == 3);
}

void testKeyboardAndTooltips() {
  Rig rig;
  rig.bar->addTab(1, "Alpha");
  rig.bar->addTab(2, "Beta");
  rig.bar->addTab(3, "Gamma");
  rig.t.layout();
  std::vector<TabId> closed;
  rig.bar->setOnCloseRequested([&](TabId id) { closed.push_back(id); });
  R1_EXPECT(rig.t.ui.router().focus(rig.bar->id(), r1ui::core::events::FocusReason::Keyboard));
  R1_EXPECT(rig.t.ui.keyDown(Key::Right) && *rig.bar->activeTab() == 2);
  R1_EXPECT(rig.t.ui.keyDown(Key::End) && *rig.bar->activeTab() == 3);
  R1_EXPECT(rig.t.ui.keyDown(Key::Right) && *rig.bar->activeTab() == 3);  // at the end: used, nothing moves
  R1_EXPECT(rig.t.ui.keyDown(Key::Home) && *rig.bar->activeTab() == 1);
  R1_EXPECT(rig.t.ui.keyDown(Key::Left) && *rig.bar->activeTab() == 1);
  R1_EXPECT(rig.t.ui.keyDown(static_cast<Key>(87), Mod::kCtrl) && closed.size() == 1 && closed[0] == 1);
  R1_EXPECT(!rig.t.ui.keyDown(Key::Right, Mod::kCtrl));
  rig.paint();  // focus ring
  // Tooltips: the full title for a tab, "Close <title>" for the button, the new tab text.
  const layout::Rect t2 = rig.bar->tabRect(2);
  rig.t.ui.pointerMove(t2.x + 20, t2.y + 10);
  R1_EXPECT(rig.bar->tooltipText() == "Beta");
  const layout::Rect c2 = rig.bar->closeRect(2);
  rig.t.ui.pointerMove(c2.x + 3, c2.y + 3);
  R1_EXPECT(rig.bar->tooltipText() == "Close Beta");
  rig.t.ui.pointerMove(rig.bar->newButtonRect().x + 5, 10);
  R1_EXPECT(rig.bar->tooltipText() == "New tab" && rig.t.ui.cursor() == Cursor::Pointer);
  rig.t.ui.pointerMove(rig.bar->newButtonRect().x + 100, 10);
  R1_EXPECT(rig.bar->tooltipText().empty() && rig.t.ui.cursor() == Cursor::Default);
  rig.bar->setTabTitle(2, "Renamed");
  R1_EXPECT(!rig.bar->setTabTitle(99, "x") && rig.bar->tab(1).title == "Renamed");
  rig.bar->addTab(9, "Very long title shown completely in the tooltip", TabOptions{.tooltip = "C:/docs/very-long.r1"});
  rig.t.layout();
  const layout::Rect t9 = rig.bar->tabRect(9);
  rig.t.ui.pointerMove(t9.x + 20, t9.y + 10);
  R1_EXPECT(rig.bar->tooltipText() == "C:/docs/very-long.r1");
}

void testHostile() {
  Rig rig;
  R1_EXPECT(rig.bar->addTab(1, "A"));
  R1_EXPECT(!rig.bar->addTab(1, "duplicate"));
  R1_EXPECT(!rig.bar->insertTab(5, 2, "bad index") && rig.bar->tabCount() == 1);
  R1_EXPECT(!rig.bar->removeTab(77) && !rig.bar->moveTab(1, 3) && !rig.bar->moveTab(77, 0) && !rig.bar->setTabIcon(77, "x"));
  R1_EXPECT(!rig.bar->scrollToTab(77));
  for (const std::string text : {std::string(), std::string("\xFF\xFE bad"), std::string("a\0b", 3), std::string(100000, 'x'), std::string("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9 \xD8\xB3\xD9\x84\xD8\xA7\xD9\x85")}) {
    rig.bar->addTab(100 + text.size(), text);
  }
  rig.t.layout();
  rig.paint();
  // Thousands of tabs: the cap holds, layout and painting stay sane.
  Rig many(800);
  size_t added = 0;
  for (TabId i = 0; i < 5000; ++i) added += many.bar->addTab(i, "Tab " + std::to_string(i)) ? 1 : 0;
  R1_EXPECT(added == TabBar::kMaxTabs);
  many.t.layout();
  R1_EXPECT(many.bar->overflowing());
  many.paint();
  many.bar->setActiveTab(4000);
  many.t.layout();
  R1_EXPECT(many.bar->scrollOffset() > 1000.0);
  // Rapid add / remove cycles.
  Rig rapid;
  for (int i = 0; i < 500; ++i) {
    rapid.bar->addTab(static_cast<TabId>(i), "x");
    if (i % 3 == 0) rapid.bar->removeTab(static_cast<TabId>(i / 2));
    rapid.t.layout();
  }
  R1_EXPECT(rapid.bar->tabCount() > 100);
  while (rapid.bar->tabCount() > 0) rapid.bar->removeTab(rapid.bar->tab(0).id);
  R1_EXPECT(!rapid.bar->activeTab());
  rapid.t.layout();
  rapid.paint();
  // Zero width bar.
  Rig zero(0);
  zero.bar->addTab(1, "Never visible");
  zero.t.layout();
  zero.paint();
  R1_EXPECT(zero.bar->tabRect(1).w >= 0);
  // A close handler that destroys the bar.
  Rig doomed;
  doomed.bar->addTab(1, "One");
  doomed.t.layout();
  const auto id = doomed.bar->id();
  doomed.bar->setOnCloseRequested([&](TabId) { doomed.t.ui.destroy(id); });
  doomed.clickRect(doomed.bar->closeRect(1));
  R1_EXPECT(!doomed.t.ui.alive(id));
  doomed.t.layout();
  // The tab being dragged is removed; the bar is disabled in the middle of a drag.
  Rig drag;
  for (TabId i = 1; i <= 3; ++i) drag.bar->addTab(i, "Doc " + std::to_string(i));
  drag.t.layout();
  const layout::Rect r = drag.bar->tabRect(1);
  drag.t.ui.pointerMove(r.x + 10, r.y + 10);
  drag.t.ui.pointerDown(r.x + 10, r.y + 10);
  drag.t.ui.pointerMove(r.x + 40, r.y + 10);
  R1_EXPECT(drag.bar->dragging());
  drag.bar->removeTab(1);
  R1_EXPECT(!drag.bar->dragging());
  drag.t.ui.pointerUp(r.x + 40, r.y + 10);
  const layout::Rect r2 = drag.bar->tabRect(2);
  drag.t.ui.pointerMove(r2.x + 10, r2.y + 10);
  drag.t.ui.pointerDown(r2.x + 10, r2.y + 10);
  drag.t.ui.pointerMove(r2.x + 40, r2.y + 10);
  R1_EXPECT(drag.bar->dragging());
  drag.bar->setEnabled(false);
  R1_EXPECT(!drag.bar->dragging());
  drag.t.ui.pointerUp(r2.x + 40, r2.y + 10);
  drag.bar->setEnabled(true);
  drag.t.layout();
  drag.paint();
}

}  // namespace

int main() {
  testGeometry();
  testActivationAndClose();
  testShrinkAndOverflow();
  testDragReorder();
  testKeyboardAndTooltips();
  testHostile();
  return r1test::finish();
}

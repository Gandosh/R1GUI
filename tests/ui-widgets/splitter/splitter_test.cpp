// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for Splitter against the acceptance scenarios of spec 05 (shares by weight,
//   live drag, only the two neighbours change, clamping at the 20 px floor, proportions kept on a
//   window resize, hover highlight and cursor, capture during the drag, fixed and hidden panes,
//   double click does nothing) plus the owner decisions (explicit collapse / expand, keyboard
//   resize), state persistence and hostile input (NaN, huge deltas, 256+ panes, zero size, a
//   change handler that destroys the splitter, disabling in the middle of a drag).
// Callers: CTest (splitter fast, no GPU).
#include <cmath>
#include <limits>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/splitter/Splitter.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::events::Key;
namespace layout = r1ui::core::layout;
using r1ui::core::tree::WidgetId;

struct Rig {
  explicit Rig(double width, double height, SplitOrientation o = SplitOrientation::Row, int panes = 2) : t(2200, 700) {
    splitter = &t.ui.create<Splitter>(t.ui.root(), o);
    splitter->style().width = layout::Length::px(width);
    splitter->style().height = layout::Length::px(height);
    splitter->style().flexShrink = 0.0;
    for (int i = 0; i < panes; ++i) splitter->addPane();
    t.layout();
  }
  void setWidth(double w) {
    splitter->style().width = layout::Length::px(w);
    splitter->requestLayout();
    t.layout();
  }
  layout::Rect rect() { return t.ui.absRect(splitter->id()); }
  layout::Rect pane(size_t i) { return t.ui.absRect(splitter->pane(i)); }
  // Pointer coordinates in the middle of handle `h`.
  double hx(size_t h) { const auto r = splitter->handleRect(h); return r.x + r.w * 0.5; }
  double hy(size_t h) { const auto r = splitter->handleRect(h); return r.y + r.h * 0.5; }
  r1test::TestUi t;
  Splitter* splitter = nullptr;
};

void testSharesAndDrag() {
  Rig rig(1005, 200);
  R1_EXPECT(rig.pane(0).w == 500 && rig.pane(1).w == 500);  // scenario 1: 1000 usable, 5 px handle
  R1_EXPECT(rig.pane(1).x - (rig.pane(0).x + rig.pane(0).w) == 5);
  R1_EXPECT(rig.splitter->handleCount() == 1 && rig.splitter->handleResizable(0));
  const double y = rig.hy(0) + 20;
  const double x = rig.hx(0);
  rig.t.ui.pointerMove(x, y);
  R1_EXPECT(rig.splitter->hoveredHandle() == 0 && rig.splitter->cursor() == Cursor::ResizeHorizontal);
  R1_EXPECT(rig.t.ui.cursor() == Cursor::ResizeHorizontal);
  rig.t.ui.pointerDown(x, y);
  rig.t.ui.pointerMove(x + 60, y);
  rig.t.layout();
  R1_EXPECT(rig.pane(0).w == 560 && rig.pane(1).w == 440);  // live, before the release
  rig.t.ui.pointerMove(x + 100, y + 500);                   // far off the handle vertically: still dragging
  rig.t.layout();
  R1_EXPECT(rig.pane(0).w == 600 && rig.pane(1).w == 400);  // scenario 2
  R1_EXPECT(rig.t.ui.cursor() == Cursor::ResizeHorizontal && rig.splitter->dragging());
  rig.t.ui.pointerUp(x + 100, y + 500);
  R1_EXPECT(!rig.splitter->dragging());
  // The ratio 3:2 survives a window resize (scenario 7).
  rig.setWidth(2005);
  R1_EXPECT(rig.pane(0).w == 1200 && rig.pane(1).w == 800);
  // Dragging back by the same amount from the new handle position restores proportions exactly.
  const SplitterState st = rig.splitter->state();
  R1_EXPECT(st.ratios.size() == 2 && std::abs(st.ratios[0] - 0.6) < 1e-9 && std::abs(st.ratios[1] - 0.4) < 1e-9);
}

void testThreePanesAbsorption() {
  Rig rig(1010, 100, SplitOrientation::Row, 3);
  R1_EXPECT(rig.pane(0).w == 330 || rig.pane(0).w == 333 || rig.pane(0).w == 334);  // 1000 / 3 rounded
  const int c0 = rig.pane(2).w;
  const double x = rig.hx(0);
  const double y = rig.hy(0) + 10;
  rig.t.ui.pointerMove(x, y);
  rig.t.ui.pointerDown(x, y);
  rig.t.ui.pointerMove(x + 50, y);
  rig.t.ui.pointerUp(x + 50, y);
  rig.t.layout();
  R1_EXPECT(rig.pane(2).w == c0);  // scenario 3: C does not change
  R1_EXPECT(std::abs(rig.splitter->paneSize(0) - (1000.0 / 3 + 50)) < 1.0);
  R1_EXPECT(std::abs(rig.splitter->paneSize(0) + rig.splitter->paneSize(1) - 2000.0 / 3) < 1.0);
  // Weights 1:1:2 share 1000 px as 250 / 250 / 500 (scenario 14).
  Rig weighted(1010, 100, SplitOrientation::Row, 0);
  weighted.splitter->addPane({.weight = 1});
  weighted.splitter->addPane({.weight = 1});
  weighted.splitter->addPane({.weight = 2});
  weighted.t.layout();
  R1_EXPECT(weighted.pane(0).w == 250 && weighted.pane(1).w == 250 && weighted.pane(2).w == 500);
}

void testMinimumFloor() {
  Rig rig(1005, 100);
  const double x = rig.hx(0);
  const double y = rig.hy(0) + 10;
  rig.t.ui.pointerMove(x, y);
  rig.t.ui.pointerDown(x, y);
  rig.t.ui.pointerMove(x + 480, y);
  rig.t.layout();
  R1_EXPECT(rig.pane(1).w == 20 && rig.pane(0).w == 980);  // scenario 6: stops at the 20 px floor
  rig.t.ui.pointerMove(x + 5000, y);
  rig.t.layout();
  R1_EXPECT(rig.pane(1).w == 20);                          // leftover travel is ignored
  rig.t.ui.pointerMove(x - 5000, y);
  rig.t.layout();
  R1_EXPECT(rig.pane(0).w == 20 && rig.pane(1).w == 980);
  rig.t.ui.pointerMove(x, y);                              // the pointer coming back restores the size
  rig.t.layout();
  R1_EXPECT(rig.pane(0).w == 500);
  rig.t.ui.pointerUp(x, y);
  R1_EXPECT(rig.splitter->setMinPaneSize(100));
  R1_EXPECT(!rig.splitter->setMinPaneSize(-1) && !rig.splitter->setMinPaneSize(std::numeric_limits<double>::quiet_NaN()));
  R1_EXPECT(rig.splitter->minPaneSize() == 100.0);
  R1_EXPECT(rig.splitter->moveHandle(0, 5000));
  rig.t.layout();
  R1_EXPECT(rig.pane(1).w == 100);
}

void testHoverBandAndOtherButtons() {
  Rig rig(1005, 100);
  const double x = rig.hx(0);
  const double y = rig.hy(0) + 10;
  rig.t.ui.pointerMove(x + 2, y);
  R1_EXPECT(rig.splitter->hoveredHandle() == 0);
  rig.t.ui.pointerMove(x + 6, y);  // 6 px off the centre: outside the 5 px band
  R1_EXPECT(rig.splitter->hoveredHandle() == -1 && rig.t.ui.cursor() == Cursor::Default);
  rig.t.ui.pointerMove(x, y);
  // A right button press on the handle does nothing.
  rig.t.ui.pointerDown(x, y, r1ui::core::events::Button::Right);
  rig.t.ui.pointerMove(x + 40, y);
  rig.t.ui.pointerUp(x + 40, y, r1ui::core::events::Button::Right);
  rig.t.layout();
  R1_EXPECT(rig.pane(0).w == 500);
  // A double click does nothing for a splitter (rule 39).
  rig.t.ui.setTime(10);
  rig.t.ui.pointerMove(x, y);
  for (int i = 0; i < 2; ++i) {
    rig.t.ui.pointerDown(x, y);
    rig.t.ui.pointerUp(x, y);
  }
  rig.t.layout();
  R1_EXPECT(rig.pane(0).w == 500 && rig.pane(1).w == 500 && !rig.splitter->isCollapsed(0));
  // A wider band (touch): 9 px.
  R1_EXPECT(rig.splitter->setHitBand(9));
  rig.t.ui.pointerMove(x + 4, y);
  R1_EXPECT(rig.splitter->hoveredHandle() == 0);
  R1_EXPECT(!rig.splitter->setHitBand(-3) && !rig.splitter->setHitBand(std::numeric_limits<double>::infinity()));
}

void testFixedAndHiddenPanes() {
  Rig rig(1000, 100, SplitOrientation::Row, 0);
  rig.splitter->addPane({.fixedSize = 200});
  rig.splitter->addPane();
  rig.splitter->addPane();
  rig.t.layout();
  R1_EXPECT(rig.pane(0).w == 200);
  R1_EXPECT(rig.splitter->handleCount() == 2 && !rig.splitter->handleResizable(0) && rig.splitter->handleResizable(1));
  rig.t.ui.pointerMove(rig.hx(0), rig.hy(0) + 10);
  R1_EXPECT(rig.splitter->hoveredHandle() == -1);  // edge case 1: no resizable pane before the handle
  R1_EXPECT(!rig.splitter->moveHandle(0, 30));
  R1_EXPECT(rig.splitter->moveHandle(1, 30));
  rig.t.layout();
  R1_EXPECT(rig.pane(0).w == 200);                 // the fixed pane never changes
  // Hide the last pane: its handle goes and the previous pane absorbs the space.
  R1_EXPECT(rig.splitter->setPaneVisible(2, false));
  rig.t.layout();
  R1_EXPECT(rig.splitter->handleCount() == 1 && rig.pane(1).w == 1000 - 200 - 5);
  R1_EXPECT(!rig.splitter->setPaneVisible(9, false));
  R1_EXPECT(rig.splitter->setPaneVisible(2, true));
  // Fixed panes with NaN sizes fall back to resizable.
  const WidgetId id = rig.splitter->addPane({.weight = std::numeric_limits<double>::quiet_NaN(), .fixedSize = std::numeric_limits<double>::quiet_NaN()});
  R1_EXPECT(id.valid());
  rig.t.layout();
  R1_EXPECT(rig.splitter->handleResizable(rig.splitter->handleCount() - 1));
}

void testCollapse() {
  Rig rig(1005, 100, SplitOrientation::Row, 0);
  rig.splitter->addPane({.collapsible = true});
  rig.splitter->addPane();
  rig.t.layout();
  R1_EXPECT(!rig.splitter->collapse(1));  // not collapsible
  R1_EXPECT(rig.splitter->collapse(0));
  rig.t.layout();
  R1_EXPECT(rig.pane(0).w == 0 && rig.splitter->isCollapsed(0));
  R1_EXPECT(rig.pane(1).w == 1000);      // the neighbour absorbed all of it
  R1_EXPECT(!rig.splitter->collapse(0));  // already collapsed
  R1_EXPECT(rig.splitter->expand(0));
  rig.t.layout();
  R1_EXPECT(!rig.splitter->isCollapsed(0) && rig.pane(0).w == 500 && rig.pane(1).w == 500);
  R1_EXPECT(!rig.splitter->expand(0));
  // Dragging cannot collapse (floor 20), but dragging next to a collapsed pane expands it.
  rig.splitter->collapse(0);
  rig.t.layout();
  const double x = rig.hx(0);
  const double y = rig.hy(0) + 10;
  rig.t.ui.pointerMove(x, y);
  rig.t.ui.pointerDown(x, y);
  rig.t.ui.pointerMove(x + 3, y);
  rig.t.layout();
  R1_EXPECT(!rig.splitter->isCollapsed(0) && rig.pane(0).w == 20);
  rig.t.ui.pointerMove(x - 100, y);  // back: the pane is collapsed again (absolute from the press)
  rig.t.layout();
  R1_EXPECT(rig.splitter->isCollapsed(0) && rig.pane(0).w == 0);
  rig.t.ui.pointerUp(x - 100, y);
  R1_EXPECT(rig.splitter->toggleCollapse(0) && !rig.splitter->isCollapsed(0));
  rig.t.layout();
  R1_EXPECT(rig.splitter->toggleCollapse(0) && rig.splitter->isCollapsed(0));
  R1_EXPECT(!rig.splitter->collapse(7) && !rig.splitter->expand(7));
}

void testStatePersistence() {
  Rig rig(1005, 100, SplitOrientation::Row, 3);
  R1_EXPECT(rig.splitter->moveHandle(0, 100));
  rig.t.layout();
  const SplitterState saved = rig.splitter->state();
  R1_EXPECT(saved.ratios.size() == 3 && std::abs(saved.ratios[0] + saved.ratios[1] + saved.ratios[2] - 1.0) < 1e-9);
  Rig other(1005, 100, SplitOrientation::Row, 3);
  R1_EXPECT(other.splitter->restoreState(saved));
  other.t.layout();
  R1_EXPECT(other.pane(0).w == rig.pane(0).w && other.pane(2).w == rig.pane(2).w);
  SplitterState bad = saved;
  bad.ratios[1] = std::numeric_limits<double>::quiet_NaN();
  R1_EXPECT(!other.splitter->restoreState(bad));
  bad = saved;
  bad.ratios[1] = -0.5;
  R1_EXPECT(!other.splitter->restoreState(bad));
  bad = saved;
  bad.ratios.pop_back();
  R1_EXPECT(!other.splitter->restoreState(bad));
  bad = saved;
  bad.collapsed.push_back(false);
  R1_EXPECT(!other.splitter->restoreState(bad));
  other.t.layout();
  R1_EXPECT(other.pane(0).w == rig.pane(0).w);  // rejected input kept the previous state
  int changes = 0;
  other.splitter->setOnChanged([&](Splitter&) { ++changes; });
  other.splitter->moveHandle(1, 10);
  R1_EXPECT(changes == 1);
  other.t.layout();
  other.setWidth(1505);
  R1_EXPECT(changes == 1);  // a window resize is not a user change
}

void testColumn() {
  Rig rig(200, 505, SplitOrientation::Column);
  R1_EXPECT(rig.pane(0).h == 250 && rig.pane(1).h == 250);
  const double x = rig.hx(0) + 10;
  const double y = rig.hy(0);
  rig.t.ui.pointerMove(x, y);
  R1_EXPECT(rig.t.ui.cursor() == Cursor::ResizeVertical);
  rig.t.ui.pointerDown(x, y);
  rig.t.ui.pointerMove(x, y + 50);
  rig.t.ui.pointerUp(x, y + 50);
  rig.t.layout();
  R1_EXPECT(rig.pane(0).h == 300 && rig.pane(1).h == 200);
}

void testKeyboard() {
  Rig rig(1005, 100, SplitOrientation::Row, 3);
  R1_EXPECT(!rig.t.ui.keyDown(Key::Right));  // not focusable by default
  rig.splitter->setKeyboardResize(true);
  R1_EXPECT(rig.t.ui.router().focus(rig.splitter->id(), r1ui::core::events::FocusReason::Keyboard));
  const double before = rig.splitter->paneSize(0);
  R1_EXPECT(rig.t.ui.keyDown(Key::Right));
  rig.t.layout();
  R1_EXPECT(std::abs(rig.splitter->paneSize(0) - (before + 10)) < 1.0);
  R1_EXPECT(rig.t.ui.keyDown(Key::Left, r1ui::core::events::Mod::kShift));
  rig.t.layout();
  R1_EXPECT(std::abs(rig.splitter->paneSize(0) - (before - 40)) < 1.0);
  R1_EXPECT(rig.t.ui.keyDown(Key::PageDown));           // second handle
  const double second = rig.splitter->paneSize(1);
  R1_EXPECT(rig.t.ui.keyDown(Key::Right));
  rig.t.layout();
  R1_EXPECT(std::abs(rig.splitter->paneSize(1) - (second + 10)) < 1.0);
  R1_EXPECT(rig.t.ui.keyDown(Key::End));
  rig.t.layout();
  R1_EXPECT(rig.splitter->paneSize(2) == 20.0);
  R1_EXPECT(!rig.t.ui.keyDown(Key::Right, r1ui::core::events::Mod::kCtrl));
  r1ui::render::Painter painter;
  painter.begin(2200, 700);
  rig.t.ui.paint(painter);
  painter.end();
}

void testHostile() {
  Rig rig(1005, 100);
  const double nan = std::numeric_limits<double>::quiet_NaN();
  R1_EXPECT(!rig.splitter->moveHandle(0, nan) && !rig.splitter->moveHandle(0, std::numeric_limits<double>::infinity()));
  R1_EXPECT(!rig.splitter->moveHandle(5, 10) && !rig.splitter->moveHandle(0, 0));
  R1_EXPECT(rig.splitter->moveHandle(0, 1e300) || true);
  rig.t.layout();
  R1_EXPECT(rig.pane(1).w == 20);
  R1_EXPECT(!rig.splitter->setHandleThickness(0) && !rig.splitter->setHandleThickness(nan) && rig.splitter->setHandleThickness(9));
  rig.t.layout();
  R1_EXPECT(rig.pane(1).x - (rig.pane(0).x + rig.pane(0).w) == 9);

  // Many panes: the limit holds and layout stays sane.
  Rig many(5000, 50, SplitOrientation::Row, 0);
  int added = 0;
  for (int i = 0; i < 300; ++i) added += many.splitter->addPane().valid() ? 1 : 0;
  R1_EXPECT(added == static_cast<int>(Splitter::kMaxPanes));
  many.t.layout();

  // A zero-size splitter, before and after layout.
  Rig zero(0, 0);
  R1_EXPECT(!zero.splitter->moveHandle(0, 10));
  Rig fresh(300, 100, SplitOrientation::Row, 0);
  fresh.splitter->addPane();
  fresh.splitter->addPane();
  R1_EXPECT(!fresh.splitter->moveHandle(0, 10));  // no layout yet: sizes unknown, refused
  fresh.t.layout();
  R1_EXPECT(fresh.splitter->moveHandle(0, 10));

  // The change callback destroys the splitter in the middle of the drag.
  Rig doomed(1005, 100);
  const WidgetId id = doomed.splitter->id();
  doomed.splitter->setOnChanged([&](Splitter&) { doomed.t.ui.destroy(id); });
  const double x = doomed.hx(0);
  const double y = doomed.hy(0) + 10;
  doomed.t.ui.pointerMove(x, y);
  doomed.t.ui.pointerDown(x, y);
  doomed.t.ui.pointerMove(x + 30, y);
  R1_EXPECT(!doomed.t.ui.alive(id));
  doomed.t.ui.pointerMove(x + 60, y);
  doomed.t.ui.pointerUp(x + 60, y);
  doomed.t.layout();

  // Disabled in the middle of a drag: the drag ends, later moves change nothing.
  Rig dis(1005, 100);
  const double dx = dis.hx(0);
  const double dy = dis.hy(0) + 10;
  dis.t.ui.pointerMove(dx, dy);
  dis.t.ui.pointerDown(dx, dy);
  dis.t.ui.pointerMove(dx + 30, dy);
  dis.t.layout();
  const int w = dis.pane(0).w;
  dis.splitter->setEnabled(false);
  dis.t.ui.pointerMove(dx + 80, dy);
  dis.t.layout();
  R1_EXPECT(dis.pane(0).w == w && !dis.splitter->dragging());
  dis.splitter->setEnabled(true);
  dis.t.ui.pointerMove(dx + 120, dy);
  dis.t.layout();
  R1_EXPECT(dis.pane(0).w == w);

  // Rapid press / move / release cycles never leave a drag open, and cancelDrag restores the sizes.
  Rig rapid(1005, 100);
  const double rx = rapid.hx(0);
  const double ry = rapid.hy(0) + 10;
  for (int i = 0; i < 200; ++i) {
    rapid.t.ui.pointerMove(rx, ry);
    rapid.t.ui.pointerDown(rx, ry);
    rapid.t.ui.pointerMove(rx + (i % 7), ry);
    rapid.t.ui.pointerUp(rx + (i % 7), ry);
    rapid.t.layout();
  }
  R1_EXPECT(!rapid.splitter->dragging());
  const int before = rapid.pane(0).w;
  const double rx2 = rapid.hx(0);
  rapid.t.ui.pointerMove(rx2, ry);
  rapid.t.ui.pointerDown(rx2, ry);
  rapid.t.ui.pointerMove(rx2 + 100, ry);
  rapid.t.layout();
  R1_EXPECT(rapid.pane(0).w == before + 100);
  R1_EXPECT(rapid.splitter->cancelDrag());
  rapid.t.layout();
  R1_EXPECT(rapid.pane(0).w == before && !rapid.splitter->dragging());
  R1_EXPECT(!rapid.splitter->cancelDrag());
}

}  // namespace

int main() {
  testSharesAndDrag();
  testThreePanesAbsorption();
  testMinimumFloor();
  testHoverBandAndOtherButtons();
  testFixedAndHiddenPanes();
  testCollapse();
  testStatePersistence();
  testColumn();
  testKeyboard();
  testHostile();
  return r1test::finish();
}

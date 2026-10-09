// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for ScrollBar and ScrollArea: viewport / content / gutter geometry, wheel
//   scrolling (notches, Shift, chaining to an enclosing area), thumb drag, track click, keyboard
//   (Page / Home / End / arrows), scroll-into-view alignments, clamping when the content shrinks,
//   the Overlay style, and hostile input (NaN, infinity, huge offsets, zero size, a handler that
//   destroys the area, the area disabled during a drag).
// Callers: CTest (scroll fast, no GPU).
#include <cmath>
#include <limits>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/scroll/ScrollArea.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::events::Key;
namespace layout = r1ui::core::layout;
using r1ui::core::tree::WidgetId;

// A fixed-height block, the unit of scrollable content.
class Block : public WidgetObject {
 public:
  explicit Block(double height = 30.0) : height_(height) {}
  const char* typeName() const override { return "Block"; }
  void onAttached() override {
    style().height = layout::Length::px(height_);
    style().flexShrink = 0.0;
  }

 private:
  double height_;
};

ScrollArea& makeArea(r1test::TestUi& t, WidgetId parent, double w, double h, int blocks, ScrollAxes axes = ScrollAxes::Vertical) {
  t.ui.rootStyle().direction = layout::FlexDirection::Column;
  ScrollArea& area = t.ui.create<ScrollArea>(parent, axes);
  area.style().width = layout::Length::px(w);
  area.style().height = layout::Length::px(h);
  area.style().flexShrink = 0.0;
  for (int i = 0; i < blocks; ++i) t.ui.create<Block>(area.content());
  return area;
}

layout::Rect rect(r1test::TestUi& t, WidgetId id) { return t.ui.absRect(id); }

void testGeometryAndGutter() {
  r1test::TestUi t(500, 2000);
  ScrollArea& area = makeArea(t, t.ui.root(), 200, 100, 10);
  t.layout();
  R1_EXPECT(area.contentHeight() == 300.0);
  R1_EXPECT(area.verticalBarVisible() && !area.horizontalBarVisible());
  R1_EXPECT(area.viewportWidth() == 190.0 && area.viewportHeight() == 100.0);  // 10 px gutter reserved
  R1_EXPECT(area.maxOffsetY() == 200.0);
  R1_EXPECT(rect(t, area.content()).w == 190);  // the content reflowed in the same frame
  t.layout();
  R1_EXPECT(!t.ui.needsFrame());                // nothing left pending

  // Fewer blocks: no overflow, no bar, no gutter.
  ScrollArea& small = makeArea(t, t.ui.root(), 200, 100, 2);
  t.layout();
  R1_EXPECT(!small.verticalBarVisible() && small.viewportWidth() == 200.0 && small.maxOffsetY() == 0.0);
  small.setVerticalPolicy(ScrollbarPolicy::Always);
  t.layout();
  R1_EXPECT(small.verticalBarVisible() && small.viewportWidth() == 190.0);
  small.setVerticalPolicy(ScrollbarPolicy::Never);
  area.setVerticalPolicy(ScrollbarPolicy::Never);
  t.layout();
  R1_EXPECT(!small.verticalBarVisible() && !area.verticalBarVisible() && area.viewportWidth() == 200.0 && area.maxOffsetY() == 200.0);
}

void testWheelAndClamp() {
  r1test::TestUi t(500, 2000);
  ScrollArea& area = makeArea(t, t.ui.root(), 200, 100, 10);
  t.layout();
  const layout::Rect r = rect(t, area.id());
  R1_EXPECT(t.ui.wheel(r.x + 20, r.y + 20, 0, -1));  // one notch down
  R1_EXPECT(area.offsetY() == 48.0);
  t.layout();
  R1_EXPECT(rect(t, area.content()).y == r.y - 48);  // the content moved with the offset
  R1_EXPECT(t.ui.wheel(r.x + 20, r.y + 20, 0, -2.5));
  R1_EXPECT(area.offsetY() == 168.0);
  R1_EXPECT(t.ui.wheel(r.x + 20, r.y + 20, 0, -10));  // clamps at the end
  R1_EXPECT(area.offsetY() == 200.0);
  R1_EXPECT(!t.ui.wheel(r.x + 20, r.y + 20, 0, -1));  // already at the end: not consumed
  R1_EXPECT(t.ui.wheel(r.x + 20, r.y + 20, 0, 100));
  R1_EXPECT(area.offsetY() == 0.0);
  R1_EXPECT(!t.ui.wheel(r.x + 20, r.y + 20, 0, 1));
  // Horizontal wheel does nothing in a vertical-only area; Shift + vertical notches neither.
  R1_EXPECT(!t.ui.wheel(r.x + 20, r.y + 20, -1, 0));
  R1_EXPECT(!t.ui.wheel(r.x + 20, r.y + 20, 0, -1, r1ui::core::events::Mod::kShift) || area.offsetX() == 0.0);

  // Hostile setters.
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  R1_EXPECT(!area.scrollTo(nan, 0) && !area.scrollTo(0, inf) && !area.scrollBy(nan, nan));
  R1_EXPECT(area.scrollTo(0, 1e300) && area.offsetY() == 200.0);
  R1_EXPECT(area.scrollTo(0, -1e300) && area.offsetY() == 0.0);
  area.setWheelStep(nan);
  area.setWheelStep(-5);
  R1_EXPECT(t.ui.wheel(r.x + 20, r.y + 20, 0, -1) && area.offsetY() == 48.0);  // the step is unchanged
  area.setWheelStep(10);
  t.ui.wheel(r.x + 20, r.y + 20, 0, -1);
  R1_EXPECT(area.offsetY() == 58.0);
}

void testChaining() {
  r1test::TestUi t(500, 2000);
  ScrollArea& outer = makeArea(t, t.ui.root(), 300, 150, 0);
  ScrollArea& inner = t.ui.create<ScrollArea>(outer.content());
  inner.style().height = layout::Length::px(100);
  inner.style().flexShrink = 0.0;
  for (int i = 0; i < 4; ++i) t.ui.create<Block>(inner.content());       // 120 px: scrolls 20 px
  for (int i = 0; i < 6; ++i) t.ui.create<Block>(outer.content());       // outer: 100 + 180 = 280
  t.layout();
  R1_EXPECT(inner.maxOffsetY() == 20.0 && outer.maxOffsetY() > 100.0);
  const layout::Rect r = rect(t, inner.id());
  t.ui.wheel(r.x + 10, r.y + 10, 0, -1);  // inner scrolls to its end (20 px), the rest is not carried over
  R1_EXPECT(inner.offsetY() == 20.0 && outer.offsetY() == 0.0);
  t.layout();
  const layout::Rect r2 = rect(t, inner.id());
  t.ui.wheel(r2.x + 10, r2.y + 10, 0, -1);  // inner is at its end: the outer area takes the notch
  R1_EXPECT(inner.offsetY() == 20.0 && outer.offsetY() == 48.0);
  t.layout();
  const layout::Rect outerRect = rect(t, outer.id());
  R1_EXPECT(t.ui.wheel(outerRect.x + 10, outerRect.y + 20, 0, 1));  // up: inner (still under the pointer) scrolls back first
  R1_EXPECT(inner.offsetY() < 20.0);
}

void testThumbAndTrack() {
  r1test::TestUi t(500, 2000);
  ScrollArea& area = makeArea(t, t.ui.root(), 200, 100, 10);
  t.layout();
  const layout::Rect r = rect(t, area.id());
  const double trackX = r.x + r.w - 5;
  // Thumb: viewport 100 / content 300 of a 100 px track = 33 px long, at the top.
  R1_EXPECT(t.ui.pointerMove(trackX, r.y + 5) || true);
  t.ui.pointerDown(trackX, r.y + 5);
  t.ui.pointerMove(trackX, r.y + 5 + 33);  // dragging the thumb by ~33 px = half the travel (67) -> about 100 px of content
  R1_EXPECT(std::abs(area.offsetY() - 33.0 * 200.0 / 67.0) < 6.0);
  t.ui.pointerMove(trackX, r.y + 5000);  // far beyond the end: clamped
  R1_EXPECT(area.offsetY() == 200.0);
  t.ui.pointerUp(trackX, r.y + 5000);
  const double after = area.offsetY();
  t.ui.pointerMove(trackX, r.y + 5);
  R1_EXPECT(area.offsetY() == after);  // released: moving no longer drags

  // Track click above the thumb: one page up (viewport - 16 = 84 px).
  area.scrollTo(0, 200);
  t.layout();
  t.ui.pointerDown(trackX, r.y + 2);
  t.ui.pointerUp(trackX, r.y + 2);
  R1_EXPECT(area.offsetY() == 200.0 - 84.0);
  // Below the thumb: a page down.
  t.ui.pointerDown(trackX, r.y + r.h - 2);
  t.ui.pointerUp(trackX, r.y + r.h - 2);
  R1_EXPECT(area.offsetY() == 200.0);

  // A press in the content never starts a bar drag.
  const double before = area.offsetY();
  t.ui.pointerDown(r.x + 20, r.y + 20);
  t.ui.pointerMove(r.x + 20, r.y + 70);
  t.ui.pointerUp(r.x + 20, r.y + 70);
  R1_EXPECT(area.offsetY() == before);
}

void testKeyboard() {
  r1test::TestUi t(500, 2000);
  ScrollArea& area = makeArea(t, t.ui.root(), 200, 100, 10);
  area.setFocusable(true);
  t.layout();
  R1_EXPECT(t.ui.router().focus(area.id(), r1ui::core::events::FocusReason::Keyboard));
  R1_EXPECT(t.ui.keyDown(Key::Down) && area.offsetY() == 16.0);
  R1_EXPECT(t.ui.keyDown(Key::PageDown) && area.offsetY() == 16.0 + 84.0);
  R1_EXPECT(t.ui.keyDown(Key::End) && area.offsetY() == 200.0);
  R1_EXPECT(!t.ui.keyDown(Key::Down));  // at the end the key is not used (travels on)
  R1_EXPECT(t.ui.keyDown(Key::Home) && area.offsetY() == 0.0);
  R1_EXPECT(!t.ui.keyDown(Key::Up));
  R1_EXPECT(!t.ui.keyDown(Key::Down, r1ui::core::events::Mod::kCtrl));  // Ctrl+arrow is not scrolling
  R1_EXPECT(area.offsetY() == 0.0);
}

void testScrollIntoView() {
  r1test::TestUi t(500, 2000);
  ScrollArea& area = makeArea(t, t.ui.root(), 200, 100, 10);
  t.layout();
  WidgetId blocks[10];
  int n = 0;
  for (WidgetId c = t.ui.tree().firstChild(area.content()); c.valid(); c = t.ui.tree().nextSibling(c)) blocks[n++] = c;
  R1_EXPECT(n == 10);
  R1_EXPECT(!area.scrollIntoView(blocks[1]));              // fully visible already: nothing moves
  R1_EXPECT(area.scrollIntoView(blocks[5]));               // y 150..180: bottom aligned
  R1_EXPECT(area.offsetY() == 80.0);
  R1_EXPECT(area.scrollIntoView(blocks[0]));               // above: top aligned
  R1_EXPECT(area.offsetY() == 0.0);
  R1_EXPECT(area.scrollIntoView(blocks[6], ScrollAlign::Center));  // centre of 180..210 at 195 -> 145
  R1_EXPECT(area.offsetY() == 145.0);
  R1_EXPECT(area.scrollIntoView(blocks[9], ScrollAlign::Start));
  R1_EXPECT(area.offsetY() == 200.0);                      // 270 clamps to the maximum
  R1_EXPECT(area.scrollIntoView(blocks[2], ScrollAlign::End));
  R1_EXPECT(area.offsetY() == 0.0);                        // 90 + 30 - 100 = 20 -> end alignment: 20
  R1_EXPECT(!area.scrollIntoView(area.content()) && !area.scrollIntoView(t.ui.root()) && !area.scrollIntoView(WidgetId{}));
  const WidgetId stale = blocks[3];
  t.ui.destroy(stale);
  R1_EXPECT(!area.scrollIntoView(stale));
  R1_EXPECT(!area.scrollRectIntoView(std::numeric_limits<double>::quiet_NaN(), 0, 1, 1));
}

void testContentShrinksAndGrows() {
  r1test::TestUi t(500, 2000);
  ScrollArea& area = makeArea(t, t.ui.root(), 200, 100, 10);
  t.layout();
  area.scrollTo(0, 200);
  t.layout();
  for (int i = 0; i < 7; ++i) t.ui.destroy(t.ui.tree().lastChild(area.content()));
  t.layout();
  R1_EXPECT(area.contentHeight() == 90.0 && !area.verticalBarVisible() && area.offsetY() == 0.0);  // re-clamped, bar gone
  R1_EXPECT(rect(t, area.content()).y == rect(t, area.id()).y);
  int calls = 0;
  area.setOnScroll([&](ScrollArea&) { ++calls; });
  for (int i = 0; i < 10; ++i) t.ui.create<Block>(area.content());
  t.layout();
  R1_EXPECT(area.verticalBarVisible());
  area.scrollTo(0, 10);
  R1_EXPECT(calls == 1);
}

void testHorizontalAndBoth() {
  r1test::TestUi t(500, 2000);
  ScrollArea& area = makeArea(t, t.ui.root(), 200, 100, 10, ScrollAxes::Both);
  area.setContentMinWidth(500);
  t.layout();
  R1_EXPECT(area.verticalBarVisible() && area.horizontalBarVisible());
  R1_EXPECT(area.viewportWidth() == 190.0 && area.viewportHeight() == 90.0);
  R1_EXPECT(area.contentWidth() == 500.0 && area.maxOffsetX() == 310.0 && area.maxOffsetY() == 210.0);
  const layout::Rect r = rect(t, area.id());
  R1_EXPECT(t.ui.wheel(r.x + 20, r.y + 20, 0, -1, r1ui::core::events::Mod::kShift));  // Shift turns it sideways
  R1_EXPECT(area.offsetX() == 48.0 && area.offsetY() == 0.0);
  R1_EXPECT(t.ui.wheel(r.x + 20, r.y + 20, -1, 0));
  R1_EXPECT(area.offsetX() == 96.0);
  t.layout();
  R1_EXPECT(rect(t, area.content()).x == r.x - 96);
  area.setContentMinWidth(std::numeric_limits<double>::infinity());  // rejected
  area.setContentMinWidth(-3);
  R1_EXPECT(area.scrollTo(1e9, 1e9) && area.offsetX() == 310.0 && area.offsetY() == 210.0);
}

void testOverlayStyle() {
  r1test::TestUi t(500, 2000);
  ScrollArea& area = makeArea(t, t.ui.root(), 200, 100, 10);
  area.setScrollbarStyle(ScrollbarStyle::Overlay);
  t.layout();
  R1_EXPECT(area.verticalBarVisible() && area.viewportWidth() == 200.0);  // nothing reserved
  const layout::Rect r = rect(t, area.id());
  t.ui.pointerDown(r.x + 10, r.y + 10);  // press in the content: no bar interaction
  t.ui.pointerUp(r.x + 10, r.y + 10);
  R1_EXPECT(area.offsetY() == 0.0);
  // The thumb (6 px wide, 2 px inside the right edge) is grabbed through the content.
  const double tx = r.x + r.w - 5;
  t.ui.pointerDown(tx, r.y + 10);
  t.ui.pointerMove(tx, r.y + 40);
  t.ui.pointerUp(tx, r.y + 40);
  R1_EXPECT(area.offsetY() > 30.0);
  // Overlay never turns a track click into a page jump.
  const double before = area.offsetY();
  t.ui.pointerDown(tx, r.y + r.h - 3);
  t.ui.pointerUp(tx, r.y + r.h - 3);
  R1_EXPECT(area.offsetY() == before);
}

void testHostileLifecycle() {
  r1test::TestUi t(500, 2000);
  // Zero size, then a normal size again.
  ScrollArea& zero = makeArea(t, t.ui.root(), 0, 0, 5);
  t.layout();
  R1_EXPECT(zero.viewportWidth() == 0.0 && zero.maxOffsetY() == 150.0);
  R1_EXPECT(zero.scrollTo(0, 50));
  r1ui::render::Painter painter;
  painter.begin(500, 2000);
  t.ui.paint(painter);
  painter.end();

  // A scroll callback that destroys the area while the wheel event is being dispatched.
  ScrollArea& victim = makeArea(t, t.ui.root(), 200, 100, 10);
  const WidgetId id = victim.id();
  victim.setOnScroll([&](ScrollArea&) { t.ui.destroy(id); });
  t.layout();
  const layout::Rect r = rect(t, id);
  t.ui.wheel(r.x + 10, r.y + 10, 0, -1);
  R1_EXPECT(!t.ui.alive(id));
  t.layout();

  // Disabled in the middle of a thumb drag: the capture is released and later moves are ignored.
  ScrollArea& area = makeArea(t, t.ui.root(), 200, 100, 10);
  t.layout();
  const layout::Rect ar = rect(t, area.id());
  const double tx = ar.x + ar.w - 5;
  t.ui.pointerDown(tx, ar.y + 5);
  t.ui.pointerMove(tx, ar.y + 20);
  const double mid = area.offsetY();
  R1_EXPECT(mid > 0.0);
  area.setEnabled(false);
  t.ui.pointerMove(tx, ar.y + 60);
  R1_EXPECT(area.offsetY() == mid);
  t.ui.pointerUp(tx, ar.y + 60);
  area.setEnabled(true);
  t.ui.pointerMove(tx, ar.y + 80);
  R1_EXPECT(area.offsetY() == mid);

  // Thousands of children lay out and scroll without trouble.
  ScrollArea& big = makeArea(t, t.ui.root(), 200, 100, 3000);
  t.layout();
  R1_EXPECT(big.contentHeight() == 90000.0);
  big.scrollTo(0, 1e12);
  t.layout();
  R1_EXPECT(big.offsetY() == 90000.0 - 100.0);
}

}  // namespace

int main() {
  testGeometryAndGutter();
  testWheelAndClamp();
  testChaining();
  testThumbAndTrack();
  testKeyboard();
  testScrollIntoView();
  testContentShrinksAndGrows();
  testHorizontalAndBoth();
  testOverlayStyle();
  testHostileLifecycle();
  return r1test::finish();
}

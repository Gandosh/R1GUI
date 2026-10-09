// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: behaviour tests of the popover without a GPU: placement next to an anchor widget (gap 8,
//   flipping at the window edge, pushed inside), content padding and fixed width, dismissal (outside
//   press delivered, a press on the anchor only closes so the trigger toggles, Escape, window
//   deactivation when asked), focus moved in on open and restored on close, following an anchor that
//   moves, closing when the anchor or an owner widget is destroyed or hidden, and hostile input
//   (stale anchor id, non-finite padding and width, zero-size window, closing inside the content's
//   own handlers, double close).
// Callers: CTest (label fast).
#include "TestSupport.h"
#include "r1ui/widgets/popover/Popover.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::events::Key;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;

struct Box : WidgetObject {
  const char* typeName() const override { return "Box"; }
  void onAttached() override {
    style().width = layout::Length::px(60);
    style().height = layout::Length::px(24);
    style().position = layout::Position::Absolute;
    place(100, 100);
  }
  void place(double x, double y) {
    style().inset[layout::kLeft] = layout::Length::px(x);
    style().inset[layout::kTop] = layout::Length::px(y);
    requestLayout();
  }
  void onPointerDown(Event&) override { ++downs; }
  int downs = 0;
};

struct Field : WidgetObject {
  const char* typeName() const override { return "Field"; }
  void onAttached() override {
    style().width = layout::Length::px(80);
    style().height = layout::Length::px(26);
    setFocusable(true);
  }
};

void testPlacement() {
  r1test::TestUi t(400, 300);
  Box& anchor = t.ui.create<Box>(t.ui.root());
  t.layout();
  PopoverOptions o;
  o.anchorWidget = anchor.id();
  o.placement = Placement::BelowStart;
  o.gap = 8.0;
  o.padding = 12.0;
  o.width = 200.0;
  const PopoverHandle h = openPopover(t.ui, o);
  R1_EXPECT(h.valid());
  Field& content = t.ui.create<Field>(h.host);
  t.layout();
  const auto host = t.ui.absRect(h.host);
  const auto a = t.ui.absRect(anchor.id());
  R1_EXPECT(host.x == a.x && host.y == a.bottom() + 8);
  R1_EXPECT(host.w == 200);
  // 12 px padding plus the 1 px border that has no layout effect.
  R1_EXPECT(t.ui.absRect(content.id()).x == host.x + 13 && t.ui.absRect(content.id()).y == host.y + 13);
  R1_EXPECT(host.h == 26 + 26);
  // Near the bottom edge it flips above its anchor.
  anchor.place(100, 270);
  closePopover(t.ui, h);
  t.layout();
  PopoverOptions flipped = o;
  flipped.placement = Placement::BelowStart;
  const PopoverHandle h2 = openPopover(t.ui, flipped);
  t.ui.create<Field>(h2.host);
  t.layout();
  R1_EXPECT(t.ui.absRect(h2.host).bottom() <= t.ui.absRect(anchor.id()).y);
  R1_EXPECT(closePopover(t.ui, h2));
  R1_EXPECT(!closePopover(t.ui, h2));  // a second close reports that it was already gone
  R1_EXPECT(!isPopoverOpen(t.ui, h2));
}

void testDismissAndToggle() {
  r1test::TestUi t(400, 300);
  Box& anchor = t.ui.create<Box>(t.ui.root());
  Box& other = t.ui.create<Box>(t.ui.root());
  other.place(250, 200);
  Field& outside = t.ui.create<Field>(t.ui.root());
  outside.style().position = layout::Position::Absolute;
  outside.style().inset[layout::kLeft] = layout::Length::px(10);
  outside.style().inset[layout::kTop] = layout::Length::px(10);
  t.layout();
  t.ui.router().focus(outside.id(), r1ui::core::events::FocusReason::Keyboard);
  DismissReason reason = DismissReason::Programmatic;
  bool closed = false;
  PopoverOptions o;
  o.anchorWidget = anchor.id();
  o.onClosed = [&](DismissReason r) {
    reason = r;
    closed = true;
  };
  PopoverHandle h = openPopover(t.ui, o);
  Field& inside = t.ui.create<Field>(h.host);
  t.layout();
  // Focus moved to the first focusable widget inside once placed.
  R1_EXPECT(t.ui.router().focused() == inside.id());
  // A press on the anchor only closes (the trigger toggles) and is not delivered.
  t.ui.pointerDown(110, 110);
  t.ui.pointerUp(110, 110);
  R1_EXPECT(closed && reason == DismissReason::OutsidePress && anchor.downs == 0);
  R1_EXPECT(t.ui.router().focused() == outside.id());  // focus restored
  // An outside press elsewhere closes it and is delivered.
  closed = false;
  h = openPopover(t.ui, o);
  t.ui.create<Field>(h.host);
  t.layout();
  t.ui.pointerDown(260, 210);
  t.ui.pointerUp(260, 210);
  R1_EXPECT(closed && other.downs == 1);
  // Escape closes it.
  closed = false;
  h = openPopover(t.ui, o);
  t.ui.create<Field>(h.host);
  t.layout();
  t.ui.keyDown(Key::Escape);
  R1_EXPECT(closed && reason == DismissReason::Escape);
  // Window deactivation closes only when asked.
  closed = false;
  h = openPopover(t.ui, o);
  t.layout();
  t.ui.setWindowActive(false);
  R1_EXPECT(!closed);
  t.ui.setWindowActive(true);
  closePopover(t.ui, h);
  PopoverOptions strict = o;
  strict.dismissOnWindowDeactivate = true;
  closed = false;
  h = openPopover(t.ui, strict);
  t.layout();
  t.ui.setWindowActive(false);
  R1_EXPECT(closed && reason == DismissReason::WindowDeactivated);
  t.ui.setWindowActive(true);
}

void testFollowAndOwner() {
  r1test::TestUi t(400, 300);
  Box& anchor = t.ui.create<Box>(t.ui.root());
  Box& owner = t.ui.create<Box>(t.ui.root());
  owner.place(300, 10);
  t.layout();
  PopoverOptions o;
  o.anchorWidget = anchor.id();
  o.owner = owner.id();
  o.gap = 4.0;
  PopoverHandle h = openPopover(t.ui, o);
  t.ui.create<Field>(h.host);
  t.layout();
  // The popover follows the anchor when it moves.
  anchor.place(20, 40);
  t.layout();
  t.layout();
  R1_EXPECT(t.ui.absRect(h.host).y == 40 + 24 + 4);
  // Hiding the anchor closes the popover.
  t.ui.invalidator().setVisible(anchor.id(), false);
  t.ui.setTime(t.ui.now() + 200);  // hiding needs no layout: the watch polls on the context clock
  t.ui.tick();
  R1_EXPECT(!isPopoverOpen(t.ui, h));
  t.ui.invalidator().setVisible(anchor.id(), true);
  // Destroying the owner closes it.
  h = openPopover(t.ui, o);
  t.layout();
  R1_EXPECT(isPopoverOpen(t.ui, h));
  t.ui.destroy(owner.id());
  t.layout();
  R1_EXPECT(!isPopoverOpen(t.ui, h));
  // Destroying the anchor closes it.
  o.owner = {};
  h = openPopover(t.ui, o);
  t.layout();
  t.ui.destroy(anchor.id());
  t.layout();
  R1_EXPECT(!isPopoverOpen(t.ui, h));
  R1_EXPECT(t.ui.overlays().count() == 0);
}

void testHostile() {
  r1test::TestUi t(400, 300);
  // A stale anchor id falls back to the rectangle; non-finite padding and width are ignored.
  PopoverOptions o;
  o.anchorWidget = WidgetId{12345, 7};
  o.anchorRect = {50, 50, 40, 20};
  o.padding = std::nan("");
  o.width = std::numeric_limits<double>::infinity();
  o.gap = -5.0;
  PopoverHandle h = openPopover(t.ui, o);
  R1_EXPECT(h.valid());
  Field& f = t.ui.create<Field>(h.host);
  t.layout();
  R1_EXPECT(t.ui.absRect(h.host).w == 82);  // the field plus the 1 px border on each side... padding 0 + 1
  (void)f;
  // Closing from inside the content's own handler is safe.
  struct Closer : WidgetObject {
    const char* typeName() const override { return "Closer"; }
    void onAttached() override { style().width = layout::Length::px(40); style().height = layout::Length::px(20); }
    void onClick(Event&) override { closePopover(ui(), handle); }
    PopoverHandle handle;
  };
  closePopover(t.ui, h);
  h = openPopover(t.ui, o);
  Closer& closer = t.ui.create<Closer>(h.host);
  closer.handle = h;
  t.layout();
  const auto r = t.ui.absRect(closer.id());
  t.ui.pointerMove(r.x + 5, r.y + 5);
  t.ui.pointerDown(r.x + 5, r.y + 5);
  t.ui.pointerUp(r.x + 5, r.y + 5);
  R1_EXPECT(!isPopoverOpen(t.ui, h));
  // A zero-size window must not crash or loop.
  r1test::TestUi tiny(0, 0);
  PopoverOptions z;
  z.anchorRect = {0, 0, 10, 10};
  const PopoverHandle zh = openPopover(tiny.ui, z);
  tiny.ui.create<Field>(zh.host);
  tiny.layout();
  tiny.layout();
  closePopover(tiny.ui, zh);
  // Destroying the host widget directly (instead of closing) is tolerated: closing still works.
  PopoverHandle again = openPopover(t.ui, o);
  t.ui.destroy(again.host);
  t.layout();
  t.ui.overlays().closeAll();
  R1_EXPECT(t.ui.overlays().count() == 0);
}

}  // namespace

int main() {
  testPlacement();
  testDismissAndToggle();
  testFollowAndOwner();
  testHostile();
  return r1test::finish();
}

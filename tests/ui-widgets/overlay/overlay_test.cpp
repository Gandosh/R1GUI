// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for the overlay layer and the tooltip manager: placement below the anchor and
//   flipping at the window edge, outside-press dismissal with pass-through, anchor toggling,
//   Escape ordering (top overlay first, closeAllOnEscape stacks, escapeFirst vs the focused widget),
//   modal blocking, the Tab focus trap, focus restore (and not restoring when focus moved on),
//   stacking, window deactivation, closing from inside a handler, the 80% height cap, and the
//   tooltip timing and hide rules of spec 10.
// Why: menus, selects, popovers, dialogs and tooltips are all built on this mechanism; its rules
//   come from specs 01 and 10 and must hold before any of those widgets exist.
// Callers: CTest (label fast, no GPU).
#include <cmath>

#include "TestSupport.h"
#include "r1ui/widgets/overlay/OverlayHost.h"

namespace {

using namespace r1ui::widgets;
namespace events = r1ui::core::events;
namespace layout = r1ui::core::layout;
using r1ui::core::tree::WidgetId;

// A focusable box that counts what it receives.
class Box : public WidgetObject {
 public:
  explicit Box(double w = 100, double h = 30) : w_(w), h_(h) {}
  const char* typeName() const override { return "Box"; }
  void onAttached() override {
    style().width = layout::Length::px(w_);
    style().height = layout::Length::px(h_);
    setFocusable(true);
  }
  void onPointerDown(Event&) override { downs++; }
  void onClick(Event&) override {
    clicks++;
    if (onClickFn) onClickFn();
  }
  void onKeyDown(Event& e) override {
    keys++;
    if (e.key == events::Key::Escape && swallowEscape) e.markHandled();
  }
  int downs = 0, clicks = 0, keys = 0;
  bool swallowEscape = false;
  std::function<void()> onClickFn;

 private:
  double w_, h_;
};

struct Rig {
  Rig() : t(400, 300) {
    background = &t.ui.create<Box>(t.ui.root(), 400.0, 300.0);
    t.layout();
  }
  r1test::TestUi t;
  Box* background = nullptr;
  r1ui::widgets::UiContext& ui() { return t.ui; }
};

OverlayOptions menuOptions(layout::Rect anchor) {
  OverlayOptions o;
  o.anchor = anchor;
  o.surface = OverlaySurface::Menu;
  return o;
}

void testPlacement() {
  Rig r;
  auto& ui = r.ui();
  OverlayHandle h = ui.overlays().open(menuOptions({50, 40, 80, 20}));
  Box& item = ui.create<Box>(h.host, 120.0, 30.0);
  R1_EXPECT(h.valid() && ui.overlays().isOpen(h.id) && ui.overlays().count() == 1);
  R1_EXPECT(!ui.tree().get(h.host)->flags.visible);  // hidden until placed
  r.t.layout();
  const layout::Rect host = ui.absRect(h.host);
  R1_EXPECT(ui.tree().get(h.host)->flags.visible);
  R1_EXPECT(host.x == 50 && host.y == 60);                       // below the anchor, left edges aligned
  R1_EXPECT(host.w == 120 + 8 && host.h == 30 + 8);              // content plus the menu padding of 4 on each side
  R1_EXPECT(ui.absRect(item.id()).x == 54 && ui.absRect(item.id()).y == 64);
  R1_EXPECT(!ui.needsFrame());

  // Not clipped by ancestors: an overlay placed outside the (small) parent still paints and hits.
  R1_EXPECT(ui.router().hitTest(60, 70) == item.id());

  // Near the bottom the menu opens above its anchor (spec 10 rule 21).
  OverlayOptions low = menuOptions({50, 280, 80, 20});
  OverlayHandle h2 = ui.overlays().open(low);
  ui.create<Box>(h2.host, 120.0, 60.0);
  r.t.layout();
  R1_EXPECT(ui.absRect(h2.host).y + ui.absRect(h2.host).h == 280);

  // matchAnchorWidth and the 80% height limit.
  ui.overlays().closeAll();
  OverlayOptions sel = menuOptions({20, 20, 150, 26});
  sel.matchAnchorWidth = true;
  OverlayHandle h3 = ui.overlays().open(sel);
  ui.create<Box>(h3.host, 40.0, 500.0);
  r.t.layout();
  R1_EXPECT(ui.absRect(h3.host).w >= 150);
  R1_EXPECT(ui.absRect(h3.host).h <= 300 * 0.8 - 8 + 0.5);
  R1_EXPECT(ui.absRect(h3.host).y + ui.absRect(h3.host).h <= 296);
}

void testOutsidePress() {
  Rig r;
  auto& ui = r.ui();
  int closed = 0;
  DismissReason why = DismissReason::Programmatic;
  OverlayOptions o = menuOptions({50, 40, 80, 20});
  o.onClosed = [&](DismissReason reason) {
    closed++;
    why = reason;
  };
  OverlayHandle h = ui.overlays().open(o);
  Box& item = ui.create<Box>(h.host, 120.0, 30.0);
  r.t.layout();
  const layout::Rect ir = ui.absRect(item.id());

  // A press inside keeps it open and reaches the item.
  ui.pointerMove(ir.x + 5, ir.y + 5);
  ui.pointerDown(ir.x + 5, ir.y + 5);
  ui.pointerUp(ir.x + 5, ir.y + 5);
  R1_EXPECT(ui.overlays().isOpen(h.id) && item.clicks == 1 && closed == 0);

  // A press outside closes it and is also delivered to what was clicked (spec 10 rule 38).
  ui.pointerMove(300, 250);
  ui.pointerDown(300, 250);
  R1_EXPECT(!ui.overlays().isOpen(h.id) && closed == 1 && why == DismissReason::OutsidePress);
  R1_EXPECT(r.background->downs == 1);
  ui.pointerUp(300, 250);
  R1_EXPECT(!ui.alive(h.host) && !ui.alive(item.id()));

  // A press in the anchor widget only closes (so the trigger can toggle).
  Box& trigger = ui.create<Box>(ui.root(), 80.0, 20.0);
  trigger.style().position = layout::Position::Absolute;
  trigger.style().inset[layout::kLeft] = layout::Length::px(200);
  trigger.style().inset[layout::kTop] = layout::Length::px(10);
  trigger.requestLayout();
  r.t.layout();
  OverlayOptions toggle = menuOptions(ui.absRect(trigger.id()));
  toggle.anchorWidget = trigger.id();
  OverlayHandle h2 = ui.overlays().open(toggle);
  ui.create<Box>(h2.host, 50.0, 20.0);
  r.t.layout();
  ui.pointerMove(210, 20);
  ui.pointerDown(210, 20);
  R1_EXPECT(!ui.overlays().isOpen(h2.id) && trigger.downs == 0);
  ui.pointerUp(210, 20);

  // dismissOnOutsidePress = false keeps it.
  OverlayOptions sticky = menuOptions({10, 10, 10, 10});
  sticky.dismissOnOutsidePress = false;
  OverlayHandle h3 = ui.overlays().open(sticky);
  ui.create<Box>(h3.host, 50.0, 20.0);
  r.t.layout();
  ui.pointerDown(300, 250);
  ui.pointerUp(300, 250);
  R1_EXPECT(ui.overlays().isOpen(h3.id));
  ui.overlays().closeAll();
  R1_EXPECT(ui.overlays().count() == 0 && !ui.alive(h3.host));
}

void testEscapeOrder() {
  Rig r;
  auto& ui = r.ui();
  // Two stacked overlays: Escape closes the top one first.
  OverlayHandle lower = ui.overlays().open(menuOptions({10, 10, 40, 20}));
  ui.create<Box>(lower.host, 60.0, 20.0);
  OverlayHandle upper = ui.overlays().open(menuOptions({100, 10, 40, 20}));
  ui.create<Box>(upper.host, 60.0, 20.0);
  r.t.layout();
  R1_EXPECT(ui.overlays().topmost() == upper.id && ui.overlays().stack().size() == 2);
  ui.keyDown(events::Key::Escape);
  R1_EXPECT(!ui.overlays().isOpen(upper.id) && ui.overlays().isOpen(lower.id));
  ui.keyDown(events::Key::Escape);
  R1_EXPECT(ui.overlays().count() == 0);
  R1_EXPECT(!ui.keyDown(events::Key::Escape));  // nothing left: Escape falls through

  // A menu stack (closeAllOnEscape) closes entirely and Escape is consumed before the focused widget.
  Box& focused = ui.create<Box>(ui.root(), 30.0, 30.0);
  ui.router().focus(focused.id());
  OverlayOptions m1 = menuOptions({10, 10, 40, 20});
  m1.closeAllOnEscape = true;
  m1.escapeFirst = true;
  OverlayOptions m2 = m1;
  m2.anchor = {60, 10, 40, 20};
  OverlayHandle a = ui.overlays().open(m1);
  ui.create<Box>(a.host, 60.0, 20.0);
  OverlayHandle b = ui.overlays().open(m2);
  ui.create<Box>(b.host, 60.0, 20.0);
  r.t.layout();
  ui.keyDown(events::Key::Escape);
  R1_EXPECT(ui.overlays().count() == 0 && focused.keys == 0);
  R1_EXPECT(ui.router().focused() == focused.id());  // focus came back (spec 01 rule 14)

  // A popover (not escapeFirst) lets the focused widget in it use Escape first (text edit, rule 16.3).
  OverlayHandle pop = ui.overlays().open(menuOptions({10, 10, 40, 20}));
  Box& field = ui.create<Box>(pop.host, 60.0, 20.0);
  field.swallowEscape = true;
  r.t.layout();
  ui.router().focus(field.id());
  ui.keyDown(events::Key::Escape);
  R1_EXPECT(ui.overlays().isOpen(pop.id) && field.keys == 1);  // the field used it
  field.swallowEscape = false;
  ui.keyDown(events::Key::Escape);
  R1_EXPECT(!ui.overlays().isOpen(pop.id));                      // unused: the overlay takes it
  R1_EXPECT(ui.router().focused() == focused.id());

  // Escape disabled on the overlay.
  OverlayOptions noEsc = menuOptions({10, 10, 40, 20});
  noEsc.dismissOnEscape = false;
  OverlayHandle keep = ui.overlays().open(noEsc);
  ui.create<Box>(keep.host, 60.0, 20.0);
  r.t.layout();
  ui.keyDown(events::Key::Escape);
  R1_EXPECT(ui.overlays().isOpen(keep.id));
  ui.overlays().close(keep.id);
}

void testModalAndFocusTrap() {
  Rig r;
  auto& ui = r.ui();
  Box& outsideA = ui.create<Box>(ui.root(), 30.0, 30.0);
  Box& outsideB = ui.create<Box>(ui.root(), 30.0, 30.0);
  (void)outsideB;
  r.t.layout();
  ui.router().focus(outsideA.id(), events::FocusReason::Keyboard);

  OverlayOptions o;
  o.surface = OverlaySurface::Dialog;
  o.placement = Placement::Center;
  o.modal = true;
  o.scrim = true;
  o.dismissOnOutsidePress = false;
  o.focusOnOpen = true;
  OverlayHandle d = ui.overlays().open(o);
  Box& first = ui.create<Box>(d.host, 80.0, 24.0);
  Box& second = ui.create<Box>(d.host, 80.0, 24.0);
  r.t.layout();
  const layout::Rect hr = ui.absRect(d.host);
  R1_EXPECT(std::abs(hr.x + hr.w / 2 - 200) <= 1 && std::abs(hr.y + hr.h / 2 - 150) <= 1);  // centred
  R1_EXPECT(ui.overlays().anyModal());
  R1_EXPECT(ui.router().focused() == first.id());  // focus moved into the dialog when it opened

  // Pointer input behind it is swallowed by the blocker.
  const int downs = r.background->downs;
  ui.pointerMove(5, 5);
  ui.pointerDown(5, 5);
  ui.pointerUp(5, 5);
  R1_EXPECT(r.background->downs == downs && outsideA.downs == 0 && ui.overlays().isOpen(d.id));

  // Tab cycles inside the dialog only, forward and backward.
  ui.keyDown(events::Key::Tab);
  R1_EXPECT(ui.router().focused() == second.id());
  ui.keyDown(events::Key::Tab);
  R1_EXPECT(ui.router().focused() == first.id());  // wrapped without visiting the widgets behind it
  ui.keyDown(events::Key::Tab, events::Mod::kShift);
  R1_EXPECT(ui.router().focused() == second.id());
  // Focus forced outside is pulled back at the next frame.
  ui.router().focus(outsideA.id());
  ui.frame();
  R1_EXPECT(ui.router().focused() == first.id());

  // Escape closes the dialog; focus returns to where it was (spec 01 rule 15).
  ui.keyDown(events::Key::Escape);
  R1_EXPECT(!ui.overlays().any() && ui.router().focused() == outsideA.id());
  R1_EXPECT(ui.router().focusVisible());
}

void testFocusNotStolenBack() {
  Rig r;
  auto& ui = r.ui();
  Box& a = ui.create<Box>(ui.root(), 30.0, 30.0);
  Box& b = ui.create<Box>(ui.root(), 30.0, 30.0);
  r.t.layout();
  ui.router().focus(a.id());
  OverlayHandle h = ui.overlays().open(menuOptions({10, 10, 40, 20}));
  Box& item = ui.create<Box>(h.host, 60.0, 20.0);
  r.t.layout();
  // The chosen entry's command moves focus to b, then the menu closes: b keeps it (scenario 13).
  item.onClickFn = [&] {
    ui.router().focus(b.id());
    ui.overlays().close(h.id);
  };
  const layout::Rect ir = ui.absRect(item.id());
  ui.pointerMove(ir.x + 3, ir.y + 3);
  ui.pointerDown(ir.x + 3, ir.y + 3);
  ui.pointerUp(ir.x + 3, ir.y + 3);  // closes the overlay from inside the item's own click handler
  R1_EXPECT(!ui.overlays().any() && ui.router().focused() == b.id());
  // Without a command, closing restores the earlier focus.
  ui.router().focus(a.id());
  OverlayHandle h2 = ui.overlays().open(menuOptions({10, 10, 40, 20}));
  Box& item2 = ui.create<Box>(h2.host, 60.0, 20.0);
  r.t.layout();
  ui.router().focus(item2.id());
  ui.overlays().close(h2.id);
  R1_EXPECT(ui.router().focused() == a.id());
}

void testWindowDeactivateAndStale() {
  Rig r;
  auto& ui = r.ui();
  OverlayOptions o = menuOptions({10, 10, 40, 20});
  o.dismissOnWindowDeactivate = true;
  OverlayHandle h = ui.overlays().open(o);
  ui.create<Box>(h.host, 60.0, 20.0);
  OverlayHandle keep = ui.overlays().open(menuOptions({100, 10, 40, 20}));
  ui.create<Box>(keep.host, 60.0, 20.0);
  r.t.layout();
  ui.setWindowActive(false);
  R1_EXPECT(!ui.overlays().isOpen(h.id) && ui.overlays().isOpen(keep.id));

  // Stale and invalid ids are harmless; closing twice is false.
  R1_EXPECT(!ui.overlays().close(h.id));
  R1_EXPECT(!ui.overlays().close(OverlayId{}));
  R1_EXPECT(ui.overlays().hostOf(h.id) == WidgetId{});
  ui.overlays().setPosition(h.id, 5, 5);
  ui.overlays().setAnchor(h.id, {1, 1, 1, 1});
  // Destroying the host directly (a widget removing its own popup) leaves the manager consistent.
  ui.destroy(keep.host);
  ui.keyDown(events::Key::Escape);
  ui.frame();
  ui.overlays().closeAll();
  R1_EXPECT(ui.overlays().count() == 0);
}

void testTooltips() {
  Rig r;
  auto& ui = r.ui();
  ui.setFrameLoopRunning(true);
  Box& a = ui.create<Box>(ui.root(), 100.0, 30.0);
  a.setTooltip("Align left");
  Box& b = ui.create<Box>(ui.root(), 100.0, 30.0);
  b.setTooltip("Align right");
  Box& plain = ui.create<Box>(ui.root(), 100.0, 30.0);
  Box& child = ui.create<Box>(plain.id(), 20.0, 10.0);
  (void)child;
  r.t.layout();
  const layout::Rect ra = ui.absRect(a.id());
  const layout::Rect rb = ui.absRect(b.id());
  auto& tips = ui.tooltips();

  // Timing: rest 50 ms then 150 ms (scenario 1): nothing at 150 ms, shown at 200 ms.
  ui.setTime(1000);
  ui.pointerMove(ra.x + 10, ra.y + 10);
  R1_EXPECT(!tips.visible() && tips.source() == a.id());
  ui.setTime(1000 + 149);
  R1_EXPECT(!ui.tick() && !tips.visible());
  R1_EXPECT(ui.msUntilTick().has_value() && *ui.msUntilTick() == 51);
  ui.setTime(1000 + 200);
  R1_EXPECT(ui.tick() && tips.visible() && tips.text() == "Align left");
  r.t.layout();
  const layout::Rect tr = ui.absRect(tips.overlay().host);
  R1_EXPECT(tr.x == ra.x + 10 + 12 && tr.y == ra.y + 10 + 8);  // 12 right and 8 below the pointer
  R1_EXPECT(ui.router().hitTest(tr.x + 2, tr.y + 2) != tips.overlay().host);  // never takes the pointer

  // Moving inside the same widget keeps it and it follows the pointer (scenario 2).
  ui.setTime(1300);
  ui.pointerMove(ra.x + 50, ra.y + 10);
  R1_EXPECT(tips.visible());
  r.t.layout();
  R1_EXPECT(ui.absRect(tips.overlay().host).x == ra.x + 50 + 12);

  // A different widget with a different tooltip: the old one closes at once, the new one waits.
  ui.setTime(1400);
  ui.pointerMove(rb.x + 10, rb.y + 10);
  R1_EXPECT(!tips.visible() && tips.source() == b.id());
  ui.setTime(1400 + 200);
  ui.tick();
  R1_EXPECT(tips.visible() && tips.text() == "Align right");

  // A press closes it and suppresses it until the pointer moves (rule 7).
  ui.pointerDown(rb.x + 10, rb.y + 10);
  R1_EXPECT(!tips.visible());
  ui.setTime(1400 + 1000);
  ui.tick();
  R1_EXPECT(!tips.visible());
  ui.pointerUp(rb.x + 10, rb.y + 10);

  // No tooltip while a button is held: press elsewhere, then hover a tooltip widget.
  ui.setTime(3000);
  ui.pointerMove(5, 250);
  ui.pointerDown(5, 250);
  ui.pointerMove(ra.x + 10, ra.y + 10);
  ui.setTime(3300);
  ui.tick();
  R1_EXPECT(!tips.visible());
  ui.pointerUp(ra.x + 10, ra.y + 10);

  // Plain widgets inherit the nearest ancestor's tooltip, and the tooltip hides when the pointer leaves.
  plain.setTooltip("Plain group");
  ui.setTime(4000);
  const layout::Rect rp = ui.absRect(plain.id());
  ui.pointerMove(rp.x + rp.w - 3, rp.y + rp.h - 3);  // inside the group, outside its small child
  R1_EXPECT(tips.source() == plain.id());
  ui.setTime(4300);
  ui.tick();
  R1_EXPECT(tips.visible());
  ui.pointerLeftWindow();
  R1_EXPECT(!tips.visible());

  // A modal overlay opening closes the tooltip; deactivating the window prevents new ones.
  ui.setTime(5000);
  ui.pointerMove(ra.x + 10, ra.y + 10);
  ui.setTime(5300);
  ui.tick();
  R1_EXPECT(tips.visible());
  OverlayOptions modal;
  modal.modal = true;
  modal.surface = OverlaySurface::Dialog;
  modal.placement = Placement::Center;
  OverlayHandle d = ui.overlays().open(modal);
  ui.create<Box>(d.host, 50.0, 20.0);
  R1_EXPECT(!tips.visible());
  ui.overlays().close(d.id);
  ui.setWindowActive(false);
  ui.setTime(6000);
  ui.pointerMove(ra.x + 12, ra.y + 12);
  ui.setTime(6400);
  ui.tick();
  R1_EXPECT(!tips.visible());
  ui.setWindowActive(true);

  // The source disappearing closes it; the global switch turns tooltips off.
  ui.setTime(7000);
  ui.pointerMove(ra.x + 14, ra.y + 14);
  ui.setTime(7300);
  ui.tick();
  R1_EXPECT(tips.visible());
  ui.destroy(a.id());
  ui.tick();
  R1_EXPECT(!tips.visible());
  tips.setEnabled(false);
  ui.setTime(8000);
  ui.pointerMove(rb.x + 10, rb.y + 10);
  ui.setTime(8400);
  ui.tick();
  R1_EXPECT(!tips.visible());
}

void testTooltipAtScreenEdge() {
  Rig r;
  auto& ui = r.ui();
  Box& edge = ui.create<Box>(ui.root(), 400.0, 300.0);
  edge.setTooltip("A fairly long tooltip text that needs room");
  r.t.layout();
  ui.setTime(1000);
  ui.pointerMove(395, 295);
  ui.setTime(1300);
  ui.tick();
  r.t.layout();
  const layout::Rect tr = ui.absRect(ui.tooltips().overlay().host);
  R1_EXPECT(ui.tooltips().visible() && tr.x >= 0 && tr.y >= 0 && tr.x + tr.w <= 400 && tr.y + tr.h <= 300);  // always inside
  R1_EXPECT(!(395 >= tr.x && 395 <= tr.x + tr.w && 295 >= tr.y && 295 <= tr.y + tr.h));                    // never covers the pointer tip
}

}  // namespace

int main() {
  testPlacement();
  testOutsidePress();
  testEscapeOrder();
  testModalAndFocusTrap();
  testFocusNotStolenBack();
  testWindowDeactivateAndStale();
  testTooltips();
  testTooltipAtScreenEdge();
  return r1test::finish();
}

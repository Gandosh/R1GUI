// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the tests of PieTrigger with synthetic pointer input on an injected clock: hold and release over a
//   slot (the pie is drawn after the delay, highlights follow the pointer, release runs the command),
//   flick faster than the draw delay in all eight directions (no overlay ever), the dead zone, a quick
//   click falling back to the context menu, Escape (drawn and not drawn), pointer leaving the window,
//   capture lost, window deactivation, a command that throws, disabled / empty / missing slots, providers
//   that return nothing or fail, other buttons, destroying the trigger mid-gesture, the swallowed Click,
//   leftovers (timers, overlays, capture) after every path, and hostile pointer positions.
// Callers: CTest (label fast).
#include <cmath>
#include <limits>

#include "PieFixture.h"
#include "r1ui/widgets/pie/PieMenu.h"

namespace {

using namespace r1test;
using r1ui::widgets::PieMenu;

constexpr double kCx = 400.0;
constexpr double kCy = 300.0;

// Nothing may be left behind once a gesture is over.
void expectClean(PieFixture& f, const char* where) {
  if (f.trigger->gestureActive() || f.t.ui.overlays().any() || f.t.ui.msUntilTick().has_value() || f.t.ui.router().capturer().valid()) {
    std::fprintf(stderr, "leftovers after %s: active=%d overlays=%zu timer=%d capture=%d\n", where, f.trigger->gestureActive(), f.t.ui.overlays().count(),
                 f.t.ui.msUntilTick().has_value(), f.t.ui.router().capturer().valid());
    R1_EXPECT(false);
  }
}

void testHoldAndRelease() {
  PieFixture f;
  f.at(1000);
  f.pressRight(kCx, kCy);
  R1_EXPECT(f.trigger->gestureActive() && !f.trigger->pieDrawn() && f.providerCalls == 1);
  R1_EXPECT(f.probe->rightDowns == 0);  // the press was taken in the capture phase
  R1_EXPECT(f.t.ui.router().capturer() == f.trigger->id());
  R1_EXPECT(!f.t.ui.overlays().any());
  f.advanceTo(1149);
  R1_EXPECT(!f.trigger->pieDrawn() && !f.t.ui.overlays().any());
  f.advanceTo(1150);
  R1_EXPECT(f.trigger->pieDrawn() && f.t.ui.overlays().count() == 1 && f.trigger->pieWidget().valid());
  PieMenu* pie = f.t.ui.objectAs<PieMenu>(f.trigger->pieWidget());
  R1_EXPECT(pie != nullptr && pie->slotCount() == 8 && pie->highlight() == -1);
  // The pie is centred on the press point.
  const auto rect = f.t.ui.absRect(f.trigger->pieWidget());
  R1_EXPECT_NEAR(rect.x + rect.w / 2.0, kCx, 1.0);
  R1_EXPECT_NEAR(rect.y + rect.h / 2.0, kCy, 1.0);

  // Move over slot 2 (right), then slot 5, highlight follows; release over slot 5 runs its command.
  auto p2 = PieFixture::slotPoint(kCx, kCy, 2, 124);
  f.at(1300);
  f.moveTo(p2.first, p2.second);
  R1_EXPECT(f.trigger->highlighted() == 2 && pie->highlight() == 2);
  auto p5 = PieFixture::slotPoint(kCx, kCy, 5, 124);
  f.moveTo(p5.first, p5.second);
  R1_EXPECT(f.trigger->highlighted() == 5 && pie->highlight() == 5);
  f.at(1700);
  f.releaseRight(p5.first, p5.second);
  R1_EXPECT(f.runs["pie.cmd5"] == 1 && f.totalRuns() == 1);
  R1_EXPECT(f.executed.size() == 1 && f.executed[0] == "pie.cmd5" && f.lastResult.isHandled());
  R1_EXPECT(f.fallbacks.empty() && f.cancelled == 0);
  R1_EXPECT(f.probe->rightClicks == 0);  // the Click that follows the release is swallowed
  f.t.layout();
  expectClean(f, "hold and release");
}

void testFlickWithoutDrawing() {
  for (int slot = 0; slot < 8; ++slot) {
    PieFixture f;
    f.at(2000);
    f.pressRight(kCx, kCy);
    f.at(2040);
    const auto p = PieFixture::slotPoint(kCx, kCy, slot, 45);  // just beyond the dead zone, 40 ms later
    f.moveTo(p.first, p.second);
    R1_EXPECT(!f.trigger->pieDrawn() && !f.t.ui.overlays().any());
    f.at(2080);
    f.releaseRight(p.first, p.second);
    const std::string expected = "pie.cmd" + std::to_string(slot);
    R1_EXPECT(f.totalRuns() == 1 && f.runs[expected] == 1);
    R1_EXPECT(f.probe->rightClicks == 0 && f.fallbacks.empty());
    f.t.layout();
    expectClean(f, "flick");
  }
}

void testDeadZoneAndClick() {
  {  // a quick click without movement: fall back to the normal context menu
    PieFixture f;
    f.pressRight(kCx, kCy);
    f.at(1100);
    f.releaseRight(kCx, kCy);
    R1_EXPECT(f.fallbacks.size() == 1 && f.fallbacks[0].first == kCx && f.fallbacks[0].second == kCy);
    R1_EXPECT(f.totalRuns() == 0 && f.cancelled == 0 && !f.t.ui.overlays().any());
    R1_EXPECT(f.probe->rightClicks == 0);  // the host reacts to onFallback, not to a second Click
    expectClean(f, "click");
  }
  {  // 179 ms is a click, 180 ms is not (the pie was already drawn at 150 ms)
    PieFixture f;
    f.pressRight(kCx, kCy);
    f.advanceTo(1179);
    R1_EXPECT(f.trigger->pieDrawn());
    f.releaseRight(kCx, kCy);
    R1_EXPECT(f.fallbacks.size() == 1 && f.cancelled == 0);
    expectClean(f, "click while drawn");
  }
  {
    PieFixture f;
    f.pressRight(kCx, kCy);
    f.advanceTo(1180);
    f.releaseRight(kCx, kCy);
    R1_EXPECT(f.fallbacks.empty() && f.cancelled == 1 && f.totalRuns() == 0);
    expectClean(f, "held without choosing");
  }
  {  // moving only inside the dead zone and releasing late cancels
    PieFixture f;
    f.pressRight(kCx, kCy);
    f.advanceTo(1400);
    f.moveTo(kCx + 12, kCy - 8);
    R1_EXPECT(f.trigger->highlighted() == -1);
    f.releaseRight(kCx + 12, kCy - 8);
    R1_EXPECT(f.cancelled == 1 && f.totalRuns() == 0 && f.fallbacks.empty());
    expectClean(f, "dead zone");
  }
  {  // out to a slot and back to the centre cancels
    PieFixture f;
    f.pressRight(kCx, kCy);
    f.advanceTo(1300);
    const auto p = PieFixture::slotPoint(kCx, kCy, 1, 100);
    f.moveTo(p.first, p.second);
    R1_EXPECT(f.trigger->highlighted() == 1);
    f.moveTo(kCx + 3, kCy + 2);
    R1_EXPECT(f.trigger->highlighted() == -1);
    f.releaseRight(kCx + 3, kCy + 2);
    R1_EXPECT(f.cancelled == 1 && f.totalRuns() == 0);
    expectClean(f, "back to the centre");
  }
}

void testEscape() {
  {  // Escape while the pie is drawn: the overlay takes it
    PieFixture f;
    f.pressRight(kCx, kCy);
    f.advanceTo(1200);
    R1_EXPECT(f.trigger->pieDrawn());
    f.t.ui.keyDown(Key::Escape);
    R1_EXPECT(!f.trigger->gestureActive() && !f.t.ui.overlays().any() && f.cancelled == 1);
    const auto p = PieFixture::slotPoint(kCx, kCy, 2, 100);
    f.moveTo(p.first, p.second);
    f.releaseRight(p.first, p.second);  // the held button's release does nothing
    R1_EXPECT(f.totalRuns() == 0 && f.fallbacks.empty() && f.cancelled == 1 && f.probe->rightClicks == 0);
    f.t.layout();
    expectClean(f, "escape drawn");
    // A new gesture works afterwards.
    f.at(5000);
    f.pressRight(kCx, kCy);
    R1_EXPECT(f.trigger->gestureActive());
    f.releaseRight(kCx, kCy);
  }
  {  // Escape before the pie is drawn, with focus inside the trigger
    PieFixture f;
    f.probe->setFocusable(true);
    f.t.ui.focusWidget(f.probe->id());
    f.pressRight(kCx, kCy);
    R1_EXPECT(f.trigger->gestureActive() && !f.trigger->pieDrawn());
    f.t.ui.keyDown(Key::Escape);
    R1_EXPECT(!f.trigger->gestureActive() && f.cancelled == 1 && !f.t.ui.msUntilTick().has_value());
    f.releaseRight(kCx, kCy);
    R1_EXPECT(f.fallbacks.empty() && f.totalRuns() == 0);
    expectClean(f, "escape early");
  }
  {  // The host forwards Escape through cancelGesture()
    PieFixture f;
    f.pressRight(kCx, kCy);
    f.trigger->cancelGesture();
    f.trigger->cancelGesture();  // harmless twice
    R1_EXPECT(f.cancelled == 1);
    f.releaseRight(kCx, kCy);
    R1_EXPECT(f.fallbacks.empty());
    expectClean(f, "cancelGesture");
  }
}

void testWindowAndCapture() {
  {  // The pointer leaves the window: the platform keeps delivering positions, direction still selects
    PieFixture f;
    f.pressRight(40, 300);
    f.advanceTo(1200);
    f.t.ui.pointerLeftWindow();
    R1_EXPECT(f.trigger->gestureActive());
    f.moveTo(-60, 300);  // outside the window, to the left: slot 6
    R1_EXPECT(f.trigger->highlighted() == 6);
    f.releaseRight(-60, 300);
    R1_EXPECT(f.runs["pie.cmd6"] == 1);
    f.t.layout();
    expectClean(f, "left the window");
  }
  {  // The capture is taken away (focus lost to another window, a system menu, ...)
    PieFixture f;
    f.pressRight(kCx, kCy);
    f.advanceTo(1200);
    f.t.ui.router().cancelPointerInteraction();
    R1_EXPECT(!f.trigger->gestureActive() && f.cancelled == 1 && !f.t.ui.overlays().any());
    f.releaseRight(kCx, kCy);
    R1_EXPECT(f.totalRuns() == 0 && f.fallbacks.empty());
    f.t.layout();
    expectClean(f, "capture lost");
  }
  {  // The window loses activation while the pie is up
    PieFixture f;
    f.pressRight(kCx, kCy);
    f.advanceTo(1200);
    f.t.ui.setWindowActive(false);
    R1_EXPECT(!f.trigger->gestureActive() && f.cancelled == 1 && !f.t.ui.overlays().any());
    f.t.ui.setWindowActive(true);
    f.releaseRight(kCx, kCy);
    R1_EXPECT(f.totalRuns() == 0);
    f.t.layout();
    expectClean(f, "deactivated");
  }
}

void testCommandProblems() {
  {  // a command that throws: contained, reported, the trigger stays usable
    PieFixture f;
    f.throwing["pie.cmd3"] = true;
    const auto p = PieFixture::slotPoint(kCx, kCy, 3, 60);
    f.pressRight(kCx, kCy);
    f.at(1050);
    f.moveTo(p.first, p.second);
    f.releaseRight(p.first, p.second);
    R1_EXPECT(f.runs["pie.cmd3"] == 1 && f.executed.size() == 1 && !f.lastResult.isHandled());
    R1_EXPECT(f.t.ui.inputFaults() == 0);
    expectClean(f, "throwing command");
    f.at(3000);
    f.pressRight(kCx, kCy);
    f.at(3040);
    const auto q = PieFixture::slotPoint(kCx, kCy, 4, 60);
    f.moveTo(q.first, q.second);
    f.releaseRight(q.first, q.second);
    R1_EXPECT(f.runs["pie.cmd4"] == 1);
  }
  {  // a callback that throws is contained by the context's fault boundary
    PieFixture f;
    f.trigger->setOnExecuted([](const std::string&, const cmd::ExecuteResult&) { throw std::runtime_error("callback"); });
    f.pressRight(kCx, kCy);
    f.at(1050);
    const auto p = PieFixture::slotPoint(kCx, kCy, 0, 60);
    f.moveTo(p.first, p.second);
    f.releaseRight(p.first, p.second);
    R1_EXPECT(f.t.ui.inputFaults() == 1);
    f.t.layout();
    expectClean(f, "throwing callback");
  }
  {  // disabled, empty and missing slots are never chosen
    PieFixture f;
    f.enabled["pie.cmd1"] = false;
    f.pie.entries[2] = {};                              // empty
    f.pie.entries[3] = {"plugin.not.registered", "", ""};  // missing
    f.provider = f.pie;
    for (const int slot : {1, 2, 3}) {
      f.at(1000);
      f.pressRight(kCx, kCy);
      f.at(1060);
      const auto p = PieFixture::slotPoint(kCx, kCy, slot, 60);
      f.moveTo(p.first, p.second);
      R1_EXPECT(f.trigger->highlighted() == -1);
      f.releaseRight(p.first, p.second);
    }
    R1_EXPECT(f.totalRuns() == 0 && f.cancelled == 3 && f.fallbacks.empty());
    expectClean(f, "unselectable");
    // The command is enabled again by the time of the next gesture.
    f.enabled["pie.cmd1"] = true;
    f.at(4000);
    f.pressRight(kCx, kCy);
    f.at(4040);
    const auto p = PieFixture::slotPoint(kCx, kCy, 1, 60);
    f.moveTo(p.first, p.second);
    f.releaseRight(p.first, p.second);
    R1_EXPECT(f.runs["pie.cmd1"] == 1);
  }
  {  // a command disabled between press and release does not run
    PieFixture f;
    f.pressRight(kCx, kCy);
    f.at(1050);
    const auto p = PieFixture::slotPoint(kCx, kCy, 7, 60);
    f.moveTo(p.first, p.second);
    f.enabled["pie.cmd7"] = false;
    f.releaseRight(p.first, p.second);
    R1_EXPECT(f.runs["pie.cmd7"] == 0 && f.executed.size() == 1 && !f.lastResult.isHandled());
  }
}

void testProviderAndButtons() {
  {  // no pie here: the press goes on to the children and nothing is captured
    PieFixture f;
    f.provider.reset();
    f.pressRight(kCx, kCy);
    R1_EXPECT(!f.trigger->gestureActive() && f.probe->rightDowns == 1);
    R1_EXPECT(!f.t.ui.router().capturer().valid());
    f.releaseRight(kCx, kCy);
    R1_EXPECT(f.probe->rightClicks == 1 && f.fallbacks.empty());
  }
  {  // a provider that throws means no pie; a menu that is not a pie is ignored
    PieFixture f;
    f.providerThrows = true;
    f.pressRight(kCx, kCy);
    R1_EXPECT(!f.trigger->gestureActive() && f.probe->rightDowns == 1);
    f.releaseRight(kCx, kCy);
    f.providerThrows = false;
    f.provider = cm::makeEmptyMenu(cm::MenuKind::Panel, "Panel");
    f.pressRight(kCx, kCy);
    R1_EXPECT(!f.trigger->gestureActive());
    f.releaseRight(kCx, kCy);
    cm::CustomMenu broken = f.pie;
    broken.entries.resize(3);
    f.provider = broken;
    f.pressRight(kCx, kCy);
    R1_EXPECT(!f.trigger->gestureActive());
    f.releaseRight(kCx, kCy);
  }
  {  // the provider sees the press position; a stale swallow flag never eats an ordinary right click
    PieFixture f;
    f.pressRight(123, 234);
    R1_EXPECT(f.lastProviderPoint.first == 123 && f.lastProviderPoint.second == 234);
    f.at(1050);
    const auto p = PieFixture::slotPoint(123, 234, 2, 60);
    f.moveTo(p.first, p.second);  // a flick that moved more than the drag threshold: no Click is sent at all
    f.releaseRight(p.first, p.second);
    f.provider.reset();
    f.pressRight(300, 300);
    f.releaseRight(300, 300);
    R1_EXPECT(f.probe->rightClicks == 1);
  }
  {  // other buttons are left alone
    PieFixture f;
    f.t.ui.pointerMove(kCx, kCy);
    f.t.ui.pointerDown(kCx, kCy, Button::Left);
    f.t.ui.pointerUp(kCx, kCy, Button::Left);
    f.t.ui.pointerDown(kCx, kCy, Button::Middle);
    f.t.ui.pointerUp(kCx, kCy, Button::Middle);
    R1_EXPECT(!f.trigger->gestureActive() && f.providerCalls == 0);
    // Right pressed while left is held: ignored
    f.t.ui.pointerDown(kCx, kCy, Button::Left);
    f.t.ui.pointerDown(kCx, kCy, Button::Right);
    R1_EXPECT(!f.trigger->gestureActive());
    f.t.ui.pointerUp(kCx, kCy, Button::Right);
    f.t.ui.pointerUp(kCx, kCy, Button::Left);
    expectClean(f, "other buttons");
  }
  {  // a press on the trigger's own area (no child under the pointer) works the same
    PieFixture f;
    f.probe->style().width = r1ui::core::layout::Length::px(50);
    f.probe->style().height = r1ui::core::layout::Length::px(50);
    f.t.layout();
    f.pressRight(600, 400);
    R1_EXPECT(f.trigger->gestureActive() && f.probe->rightDowns == 0);
    f.at(1050);
    const auto p = PieFixture::slotPoint(600, 400, 6, 60);
    f.moveTo(p.first, p.second);
    f.releaseRight(p.first, p.second);
    R1_EXPECT(f.runs["pie.cmd6"] == 1);
    expectClean(f, "press on the trigger itself");
  }
  {  // a disabled trigger does nothing
    PieFixture f;
    f.trigger->setEnabled(false);
    f.pressRight(kCx, kCy);
    R1_EXPECT(!f.trigger->gestureActive());
    f.releaseRight(kCx, kCy);
  }
}

void testDestroyAndHostile() {
  {  // destroying the trigger while the pie is up leaves nothing behind
    PieFixture f;
    f.pressRight(kCx, kCy);
    f.advanceTo(1200);
    R1_EXPECT(f.t.ui.overlays().any());
    f.t.ui.destroy(f.trigger->id());
    f.trigger = nullptr;
    f.t.layout();
    R1_EXPECT(!f.t.ui.overlays().any() && !f.t.ui.msUntilTick().has_value());
    f.releaseRight(kCx, kCy);
    f.t.ui.pointerMove(5, 5);
    R1_EXPECT(f.t.ui.inputFaults() == 0);
  }
  {  // destroying the trigger while the draw timer is pending
    PieFixture f;
    f.pressRight(kCx, kCy);
    R1_EXPECT(f.t.ui.msUntilTick().has_value());
    f.t.ui.destroy(f.trigger->id());
    f.trigger = nullptr;
    f.advanceTo(5000);
    R1_EXPECT(!f.t.ui.overlays().any() && !f.t.ui.msUntilTick().has_value());
    f.releaseRight(kCx, kCy);
  }
  {  // a command that destroys the trigger from inside the gesture's release
    PieFixture f;
    f.registry.remove("pie.cmd2");
    cmd::CommandDef def;
    def.id = "pie.cmd2";
    def.label = "Destroyer";
    PieFixture* self = &f;
    def.execute = [self](const cmd::ExecuteArgs&) {
      self->t.ui.destroy(self->trigger->id());
      return cmd::ExecuteResult::handled();
    };
    f.registry.add(def);
    f.pressRight(kCx, kCy);
    f.at(1050);
    const auto p = PieFixture::slotPoint(kCx, kCy, 2, 60);
    f.moveTo(p.first, p.second);
    f.releaseRight(p.first, p.second);
    R1_EXPECT(!f.t.ui.alive(f.trigger->id()) && f.t.ui.inputFaults() == 0);
    f.trigger = nullptr;
    f.t.layout();
    R1_EXPECT(!f.t.ui.overlays().any() && !f.t.ui.msUntilTick().has_value());
  }
  {  // hostile pointer positions never break the gesture
    PieFixture f;
    f.pressRight(kCx, kCy);
    f.at(1050);
    f.moveTo(std::nan(""), 10);
    f.moveTo(5, std::numeric_limits<double>::infinity());
    R1_EXPECT(f.trigger->gestureActive() && f.trigger->highlighted() == -1);
    f.moveTo(1e12, kCy);
    R1_EXPECT(f.trigger->highlighted() == 2);
    f.releaseRight(std::nan(""), std::nan(""));  // the router drops a non-finite release; the button is still held
    R1_EXPECT(f.trigger->gestureActive());
    f.releaseRight(1e12, kCy);
    R1_EXPECT(f.runs["pie.cmd2"] == 1);
    f.t.layout();
    expectClean(f, "hostile positions");
  }
  {  // many gestures in a row do not leak widgets
    PieFixture f;
    const size_t before = f.t.ui.widgetCount();
    for (int i = 0; i < 200; ++i) {
      f.at(10000 + static_cast<uint64_t>(i) * 1000);
      f.pressRight(kCx, kCy);
      f.advanceTo(10000 + static_cast<uint64_t>(i) * 1000 + 200);
      const auto p = PieFixture::slotPoint(kCx, kCy, i % 8, 100);
      f.moveTo(p.first, p.second);
      f.releaseRight(p.first, p.second);
      f.t.layout();
    }
    R1_EXPECT(f.totalRuns() == 200);
    R1_EXPECT(f.t.ui.widgetCount() == before);
    R1_EXPECT(f.t.ui.animationCount() == 0 && f.t.ui.layoutCallbackCount() == 0);
    expectClean(f, "200 gestures");
  }
}

}  // namespace

int main() {
  testHoldAndRelease();
  testFlickWithoutDrawing();
  testDeadZoneAndClick();
  testEscape();
  testWindowAndCapture();
  testCommandProblems();
  testProviderAndButtons();
  testDestroyAndHostile();
  return r1test::finish();
}

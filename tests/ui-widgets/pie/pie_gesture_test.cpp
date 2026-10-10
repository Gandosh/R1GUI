// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the tests of the pure pie logic: slot geometry (angles and offsets for 4, 6 and 8 slots), the
//   direction to slot mapping (every slot centre, both sides of every sector boundary, the dead zone,
//   hostile numbers) and PieGesture with an injected clock: hold and release over a slot, flick faster
//   than the draw delay, flick in all directions, dead zone, cancel, click fallback, the exact 150 ms and
//   180 ms thresholds, unselectable slots, back to the centre, clocks running backwards.
// Callers: CTest (label fast).
#include <cmath>
#include <limits>

#include "TestSupport.h"
#include "r1ui/widgets/pie/PieGesture.h"

namespace {

using namespace r1ui::widgets;

constexpr double kInf = std::numeric_limits<double>::infinity();
const double kNaN = std::nan("");

std::vector<bool> allSelectable(size_t n) { return std::vector<bool>(n, true); }

// Moves from the origin (100, 100) by `distance` in the direction of slot `slot` of `count`.
void toward(PieGesture& g, int count, int slot, double distance) {
  const PiePoint p = pieSlotOffset(count, slot, distance);
  g.move(100.0 + p.x, 100.0 + p.y);
}

void testGeometry() {
  for (const int count : {4, 6, 8}) {
    for (int i = 0; i < count; ++i) {
      R1_EXPECT_NEAR(pieSlotAngle(count, i), 360.0 * i / count, 1e-9);
      const PiePoint p = pieSlotOffset(count, i, 100.0);
      R1_EXPECT_NEAR(std::hypot(p.x, p.y), 100.0, 1e-9);
      // The centre of every slot selects that slot, at any distance beyond the dead zone.
      for (const double distance : {30.0, 124.0, 5000.0}) {
        const PiePoint q = pieSlotOffset(count, i, distance);
        const auto slot = pieSlotForDirection(q.x, q.y, count, 24.0);
        R1_EXPECT(slot && *slot == i);
      }
    }
  }
  // Slot 0 is straight up, 2 of 8 straight right, 4 of 8 straight down, 6 of 8 straight left.
  R1_EXPECT(*pieSlotForDirection(0, -50, 8, 24) == 0 && *pieSlotForDirection(50, 0, 8, 24) == 2);
  R1_EXPECT(*pieSlotForDirection(0, 50, 8, 24) == 4 && *pieSlotForDirection(-50, 0, 8, 24) == 6);
  R1_EXPECT(*pieSlotForDirection(50, -50, 8, 24) == 1 && *pieSlotForDirection(-50, -50, 8, 24) == 7);
  // Sector boundaries (22.5 degrees for 8 slots): just inside each side.
  for (int i = 0; i < 8; ++i) {
    for (const double offset : {-22.0, 22.0}) {
      const double a = (45.0 * i + offset) * 3.14159265358979323846 / 180.0;
      const auto slot = pieSlotForDirection(std::sin(a) * 80.0, -std::cos(a) * 80.0, 8, 24.0);
      R1_EXPECT(slot && *slot == i);
    }
  }
  // Six slots: slot 1 sits at 60 degrees, so 45 degrees (the old "up-right" of eight) still belongs to it.
  R1_EXPECT(*pieSlotForDirection(60, -60, 6, 24) == 1);
  // The dead zone is inclusive of its edge; just outside selects.
  R1_EXPECT(!pieSlotForDirection(24.0, 0.0, 8, 24.0) && pieSlotForDirection(24.5, 0.0, 8, 24.0));
  R1_EXPECT(!pieSlotForDirection(0.0, 0.0, 8, 24.0));
  // Hostile input.
  R1_EXPECT(!pieSlotForDirection(kNaN, 50, 8, 24) && !pieSlotForDirection(50, kInf, 8, 24) && !pieSlotForDirection(50, 50, 8, kNaN));
  R1_EXPECT(!pieSlotForDirection(50, 50, 5, 24) && !pieSlotForDirection(50, 50, 0, 24) && !pieSlotForDirection(50, 50, -8, 24));
  R1_EXPECT(pieSlotForDirection(1e300, 1e300, 8, 24) == std::optional<int>(3));  // down-right (y grows downwards)
  R1_EXPECT(pieSlotAngle(8, -1) == 0.0 && pieSlotAngle(8, 8) == 0.0 && pieSlotAngle(7, 1) == 0.0);
}

void testHoldAndRelease() {
  PieGesture g;
  R1_EXPECT(g.begin(100, 100, 1000, allSelectable(8)));
  R1_EXPECT(g.active() && !g.drawn() && g.highlighted() == -1);
  R1_EXPECT(g.msUntilDraw(1000) == 150 && g.msUntilDraw(1100) == 50 && g.msUntilDraw(1150) == 0);
  R1_EXPECT(!g.tick(1149) && !g.drawn());
  R1_EXPECT(g.tick(1150) && g.drawn());
  R1_EXPECT(!g.tick(1200));  // reported once
  toward(g, 8, 3, 124);
  R1_EXPECT(g.highlighted() == 3);
  toward(g, 8, 5, 124);
  R1_EXPECT(g.highlighted() == 5);
  const PiePoint p = pieSlotOffset(8, 5, 124);
  const PieOutcome out = g.release(100 + p.x, 100 + p.y, 1900);
  R1_EXPECT(out.kind == PieOutcomeKind::Execute && out.slot == 5);
  R1_EXPECT(!g.active() && !g.drawn() && g.highlighted() == -1);
  // A second release is a no-op.
  R1_EXPECT(g.release(0, 0, 2000).kind == PieOutcomeKind::None);
}

void testFlickFasterThanDrawDelay() {
  for (const int count : {4, 6, 8}) {
    for (int slot = 0; slot < count; ++slot) {
      PieGesture g;
      g.begin(100, 100, 5000, allSelectable(static_cast<size_t>(count)));
      toward(g, count, slot, 40);  // barely beyond the dead zone, no need to reach the slot
      R1_EXPECT(!g.drawn());
      const PiePoint p = pieSlotOffset(count, slot, 45);
      const PieOutcome out = g.release(100 + p.x, 100 + p.y, 5060);  // 60 ms: never drawn
      R1_EXPECT(out.kind == PieOutcomeKind::Execute && out.slot == slot);
    }
  }
  // Even a 1 ms flick executes.
  PieGesture g;
  g.begin(100, 100, 100, allSelectable(8));
  const PieOutcome out = g.release(100, 40, 101);
  R1_EXPECT(out.kind == PieOutcomeKind::Execute && out.slot == 0);
}

void testClickCancelAndThresholds() {
  PieGesture g;
  // A quick press and release without movement is a click: fall back to the context menu.
  g.begin(100, 100, 1000, allSelectable(8));
  R1_EXPECT(g.release(100, 100, 1179).kind == PieOutcomeKind::Fallback);   // 179 ms
  g.begin(100, 100, 1000, allSelectable(8));
  R1_EXPECT(g.release(100, 100, 1180).kind == PieOutcomeKind::Cancel);     // 180 ms: not a click any more
  // Jitter inside the dead zone still counts as "no movement".
  g.begin(100, 100, 1000, allSelectable(8));
  g.move(110, 105);
  g.move(95, 98);
  R1_EXPECT(g.release(101, 101, 1100).kind == PieOutcomeKind::Fallback);
  // Holding with the pie drawn and releasing at the centre cancels.
  g.begin(100, 100, 1000, allSelectable(8));
  g.tick(1200);
  R1_EXPECT(g.release(100, 100, 2000).kind == PieOutcomeKind::Cancel);
  // Out to a slot and back to the centre before releasing cancels, even when it was fast.
  g.begin(100, 100, 1000, allSelectable(8));
  toward(g, 8, 2, 100);
  R1_EXPECT(g.highlighted() == 2);
  g.move(100, 100);
  R1_EXPECT(g.highlighted() == -1);
  R1_EXPECT(g.release(100, 100, 1050).kind == PieOutcomeKind::Cancel);
  // cancel() ends without an outcome and frees the gesture for the next one.
  g.begin(100, 100, 1000, allSelectable(8));
  g.cancel();
  R1_EXPECT(!g.active() && g.release(100, 100, 1100).kind == PieOutcomeKind::None);
  R1_EXPECT(g.begin(100, 100, 3000, allSelectable(8)));
}

void testUnselectableSlots() {
  std::vector<bool> selectable = allSelectable(8);
  selectable[2] = false;  // empty, disabled or missing
  PieGesture g;
  g.begin(100, 100, 0, selectable);
  toward(g, 8, 2, 100);
  R1_EXPECT(g.highlighted() == -1);  // never highlighted
  const PiePoint p = pieSlotOffset(8, 2, 100);
  R1_EXPECT(g.release(100 + p.x, 100 + p.y, 1000).kind == PieOutcomeKind::Cancel);
  // A quick flick onto an unselectable slot is a cancel, not a click.
  g.begin(100, 100, 0, selectable);
  R1_EXPECT(g.release(100 + p.x, 100 + p.y, 30).kind == PieOutcomeKind::Cancel);
  // Neighbours still work.
  g.begin(100, 100, 0, selectable);
  toward(g, 8, 3, 100);
  R1_EXPECT(g.highlighted() == 3);
  g.cancel();
}

void testHostileInput() {
  PieGesture g;
  R1_EXPECT(!g.begin(kNaN, 0, 0, allSelectable(8)) && !g.begin(0, kInf, 0, allSelectable(8)));
  R1_EXPECT(!g.begin(0, 0, 0, {}) && !g.begin(0, 0, 0, allSelectable(5)) && !g.begin(0, 0, 0, allSelectable(9)) && !g.active());
  R1_EXPECT(g.begin(100, 100, 1000, allSelectable(8)));
  R1_EXPECT(!g.begin(0, 0, 0, allSelectable(8)));  // already active
  R1_EXPECT(!g.move(kNaN, 5) && !g.move(5, -kInf));  // dropped
  toward(g, 8, 6, 80);
  R1_EXPECT(g.highlighted() == 6);
  R1_EXPECT(!g.move(kNaN, kNaN) && g.highlighted() == 6);
  // A non-finite release position keeps the last good sample's highlight.
  R1_EXPECT(g.release(kNaN, kNaN, 1100).kind == PieOutcomeKind::Execute);
  // The clock running backwards counts as zero elapsed time.
  g.begin(100, 100, 5000, allSelectable(8));
  R1_EXPECT(!g.tick(10) && g.msUntilDraw(10) == 150);
  R1_EXPECT(g.release(100, 100, 10).kind == PieOutcomeKind::Fallback);
  // Huge coordinates are fine.
  g.begin(0, 0, 0, allSelectable(8));
  R1_EXPECT(g.move(1e300, 0) && g.highlighted() == 2);
  g.cancel();
  g.begin(0, 0, 0, allSelectable(8));
  g.move(1e308, 1e308);  // squares overflow to infinity: must not select garbage
  R1_EXPECT(g.highlighted() == -1 || g.highlighted() == 3);
  g.cancel();
  // Config values are clamped.
  PieGestureConfig c;
  c.deadZone = kNaN;
  c.drawDelayMs = ~uint64_t{0};
  c.clickMaxMs = ~uint64_t{0};
  g.setConfig(c);
  R1_EXPECT(g.config().deadZone == 24.0 && g.config().drawDelayMs == 10000 && g.config().clickMaxMs == 10000);
  c.deadZone = 0.0;
  g.setConfig(c);
  R1_EXPECT(g.config().deadZone == 4.0);
  c.deadZone = 1e9;
  g.setConfig(c);
  R1_EXPECT(g.config().deadZone == 200.0);
}

void testCustomConfig() {
  PieGestureConfig c;
  c.drawDelayMs = 400;
  c.clickMaxMs = 300;
  c.deadZone = 40.0;
  PieGesture g(c);
  g.begin(100, 100, 0, allSelectable(8));
  R1_EXPECT(!g.tick(399) && g.tick(400));
  g.move(100 + 35, 100);
  R1_EXPECT(g.highlighted() == -1);  // inside the larger dead zone
  g.move(100 + 45, 100);
  R1_EXPECT(g.highlighted() == 2);
  g.cancel();
  g.begin(100, 100, 0, allSelectable(8));
  R1_EXPECT(g.release(100, 100, 299).kind == PieOutcomeKind::Fallback);
}

}  // namespace

int main() {
  testGeometry();
  testHoldAndRelease();
  testFlickFasterThanDrawDelay();
  testClickCancelAndThresholds();
  testUnselectableSlots();
  testHostileInput();
  testCustomConfig();
  return r1test::finish();
}

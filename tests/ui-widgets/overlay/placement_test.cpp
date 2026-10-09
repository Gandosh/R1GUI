// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for the popup and tooltip placement geometry (spec 10 rules 13-23): preferred side,
//   flipping, pushing inside the window, popups larger than the window, hostile numbers.
// Callers: CTest (label fast).
#include <cmath>
#include <limits>

#include "TestSupport.h"
#include "r1ui/widgets/overlay/Placement.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::layout::Rect;

PlacementResult place(Placement p, Rect anchor, double w, double h, double gap = 0.0, bool flip = true) {
  PlacementInput in;
  in.width = w;
  in.height = h;
  in.anchor = anchor;
  in.bounds = {0, 0, 800, 600};
  in.placement = p;
  in.gap = gap;
  in.flip = flip;
  return placePopup(in);
}

void testPopups() {
  auto r = place(Placement::BelowStart, {100, 100, 80, 20}, 100, 50);
  R1_EXPECT(r.x == 100 && r.y == 120 && !r.flipped);
  r = place(Placement::BelowStart, {100, 100, 80, 20}, 100, 50, 4);
  R1_EXPECT(r.y == 124);

  r = place(Placement::BelowStart, {100, 560, 80, 20}, 100, 50);  // no room below, room above
  R1_EXPECT(r.y == 510 && r.flipped && r.actual == Placement::AboveStart);
  r = place(Placement::BelowStart, {100, 560, 80, 20}, 100, 50, 0, false);  // flip disabled: pushed inside instead
  R1_EXPECT(!r.flipped && r.y == 550);
  r = place(Placement::AboveStart, {100, 10, 80, 20}, 100, 50);  // opposite of the above case
  R1_EXPECT(r.y == 30 && r.flipped && r.actual == Placement::BelowStart);
  r = place(Placement::BelowStart, {100, 10, 80, 20}, 100, 580);  // fits neither side: stay below, pushed inside
  R1_EXPECT(!r.flipped && r.y == 20);

  r = place(Placement::BelowStart, {750, 100, 40, 20}, 100, 50);  // pushed back in horizontally
  R1_EXPECT(r.x == 700);
  r = place(Placement::BelowEnd, {710, 100, 80, 20}, 100, 50);
  R1_EXPECT(r.x == 690);
  r = place(Placement::BelowCenter, {100, 100, 80, 20}, 40, 50);
  R1_EXPECT(r.x == 120);

  r = place(Placement::RightStart, {300, 50, 80, 20}, 100, 40);
  R1_EXPECT(r.x == 380 && r.y == 50 && !r.flipped);
  r = place(Placement::RightStart, {700, 50, 80, 20}, 100, 40);  // submenu flips to the left
  R1_EXPECT(r.x == 600 && r.flipped && r.actual == Placement::LeftStart);
  r = place(Placement::RightStart, {300, 580, 80, 20}, 100, 60);  // pushed up inside
  R1_EXPECT(r.y == 540);
  r = place(Placement::LeftStart, {10, 50, 80, 20}, 100, 40);
  R1_EXPECT(r.x == 90 && r.flipped);

  r = place(Placement::Center, {0, 0, 0, 0}, 200, 100);
  R1_EXPECT(r.x == 300 && r.y == 250);
  r = place(Placement::Manual, {790, 590, 0, 0}, 50, 30);
  R1_EXPECT(r.x == 750 && r.y == 570);
}

void testHostile() {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  auto r = place(Placement::BelowStart, {100, 100, 80, 20}, nan, -5);
  R1_EXPECT(std::isfinite(r.x) && std::isfinite(r.y) && r.x == 100 && r.y == 120);
  r = place(Placement::BelowStart, {100, 100, 80, 20}, inf, inf);
  R1_EXPECT(std::isfinite(r.x) && std::isfinite(r.y));
  r = place(Placement::BelowStart, {100, 100, 80, 20}, 2000, 2000);  // bigger than the window: top-left
  R1_EXPECT(r.x == 0 && r.y == 0);
  PlacementInput in;
  in.width = 10;
  in.height = 10;
  in.anchor = {50, 50, 10, 10};
  const PlacementResult unbounded = placePopup(in);  // empty bounds: no clamping, still defined
  R1_EXPECT(unbounded.x == 50 && unbounded.y == 60);
  r = place(Placement::BelowStart, {2000000000, 2000000000, 10, 10}, 10, 10);
  R1_EXPECT(r.x >= 0 && r.x <= 790 && r.y >= 0 && r.y <= 590);
}

void testTooltips() {
  TooltipPlacementInput in;
  in.width = 80;
  in.height = 20;
  in.bounds = {0, 0, 800, 600};
  in.pointerX = 100;
  in.pointerY = 100;
  Point p = placeTooltip(in);
  R1_EXPECT(p.x == 112 && p.y == 108);  // 12 right, 8 below

  in.pointerX = 790;  // shifted back by exactly the overflow
  p = placeTooltip(in);
  R1_EXPECT(p.x == 720 && p.y == 108);

  in.pointerY = 590;  // the shifted spot would cover the pointer: upper left with a 16 x 12 gap
  p = placeTooltip(in);
  R1_EXPECT(p.x == 694 && p.y == 558);

  in.pointerX = 100;
  in.pointerY = 100;
  in.exclusion = {100, 100, 150, 30};  // must not cover the open submenu entry
  p = placeTooltip(in);
  R1_EXPECT(p.x == 112 && p.y == 133);  // just below it (3 px), on the side nearer the preferred spot

  in.exclusion = {};
  in.pointerX = std::numeric_limits<double>::quiet_NaN();
  p = placeTooltip(in);
  R1_EXPECT(std::isfinite(p.x) && std::isfinite(p.y));
}

}  // namespace

int main() {
  testPopups();
  testHostile();
  testTooltips();
  return r1test::finish();
}

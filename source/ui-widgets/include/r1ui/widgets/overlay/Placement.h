// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the pure geometry of popup placement: where a popup of a given size goes relative to an
//   anchor rectangle inside a bounds rectangle, with flipping at the edges, and the tooltip
//   placement rules.
// Why: spec 10 (rules 13-23) defines placement exactly; keeping it free of widgets and the tree
//   makes every rule a one-line unit test, including hostile sizes (zero, negative, larger than the
//   bounds, non-finite).
// Callers: OverlayManager (menus, selects, popovers, dialogs), TooltipManager. Calls: nothing.
// Units: logical pixels, window space. All inputs are sanitised (non-finite -> 0, negative sizes ->
//   0); a popup larger than the bounds is placed at the bounds' top-left (it is then scrolled or
//   clipped by its owner).
// Rules: Below*/Above* put the popup under/over the anchor with the Start/End/Center edge aligned;
//   when the preferred side does not fit but the opposite one does the popup flips, when neither
//   fits it stays on the preferred side; the result is then pushed inside the bounds on both axes.
//   Right/Left (submenus) align tops, flip horizontally the same way and push inside vertically.
//   Center puts the popup in the middle of the bounds (dialogs). Manual keeps the given point.
#pragma once

#include "r1ui/core/layout/Geometry.h"

namespace r1ui::widgets {

enum class Placement : uint8_t {
  BelowStart,
  BelowEnd,
  BelowCenter,
  AboveStart,
  AboveEnd,
  AboveCenter,
  RightStart,   // submenu: left edge at the anchor's right edge, tops aligned
  LeftStart,
  Center,       // centred in the bounds (anchor ignored)
  Manual        // top-left at the anchor rectangle's top-left, only pushed inside the bounds
};

struct PlacementInput {
  double width = 0.0;   // popup size
  double height = 0.0;
  core::layout::Rect anchor;
  core::layout::Rect bounds;  // usable area (window minus margins)
  Placement placement = Placement::BelowStart;
  double gap = 0.0;           // space between anchor and popup on the main axis
  bool flip = true;
};

struct PlacementResult {
  double x = 0.0;
  double y = 0.0;
  Placement actual = Placement::BelowStart;  // after flipping
  bool flipped = false;
};

PlacementResult placePopup(const PlacementInput& input);

// Tooltip placement (spec 10 rules 13-17): preferred spot 12 px right and 8 px below the pointer,
// shifted inside the bounds by the overflow; when that would cover the pointer the tooltip goes to
// the upper left of the pointer with a 16 x 12 gap (or to the other side when that leaves the
// bounds). `exclusion` (empty = none) is an area the tooltip must not cover: it is placed just
// beyond it (4 px right, 3 px below the exclusion) on the side nearer the preferred spot.
struct TooltipPlacementInput {
  double width = 0.0;
  double height = 0.0;
  double pointerX = 0.0;
  double pointerY = 0.0;
  core::layout::Rect bounds;
  core::layout::Rect exclusion;
};
struct Point {
  double x = 0.0;
  double y = 0.0;
};
Point placeTooltip(const TooltipPlacementInput& input);

}  // namespace r1ui::widgets

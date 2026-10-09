// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Placement.h.
// Callers: OverlayManager, TooltipManager, tests.
#include "r1ui/widgets/overlay/Placement.h"

#include <algorithm>
#include <cmath>

namespace r1ui::widgets {

namespace {

using core::layout::Rect;

double clean(double v) { return std::isfinite(v) ? v : 0.0; }
double size(double v) { return std::max(0.0, clean(v)); }

struct Box {
  double l = 0, t = 0, r = 0, b = 0;
  double w() const { return r - l; }
  double h() const { return b - t; }
};

Box toBox(const Rect& r) { return {double(r.x), double(r.y), double(r.x) + r.w, double(r.y) + r.h}; }

// Pushes a span [pos, pos+len) inside [lo, hi]; a span longer than the room starts at lo.
double pushInside(double pos, double len, double lo, double hi) {
  if (len >= hi - lo) return lo;
  return std::clamp(pos, lo, hi - len);
}

bool within(double x, double y, double w, double h, const Box& bounds) {
  return x >= bounds.l && y >= bounds.t && x + w <= bounds.r && y + h <= bounds.b;
}

}  // namespace

PlacementResult placePopup(const PlacementInput& in) {
  const double w = size(in.width);
  const double h = size(in.height);
  const double gap = size(in.gap);
  const Box a = toBox(in.anchor);
  const bool bounded = !in.bounds.empty();
  const Box bd = bounded ? toBox(in.bounds) : Box{-1e9, -1e9, 1e9, 1e9};
  PlacementResult out;
  out.actual = in.placement;
  double x = a.l;
  double y = a.t;

  const auto alignX = [&](Placement p) {
    switch (p) {
      case Placement::BelowEnd:
      case Placement::AboveEnd: return a.r - w;
      case Placement::BelowCenter:
      case Placement::AboveCenter: return a.l + (a.w() - w) / 2.0;
      default: return a.l;
    }
  };

  switch (in.placement) {
    case Placement::BelowStart:
    case Placement::BelowEnd:
    case Placement::BelowCenter:
    case Placement::AboveStart:
    case Placement::AboveEnd:
    case Placement::AboveCenter: {
      const bool below = in.placement == Placement::BelowStart || in.placement == Placement::BelowEnd || in.placement == Placement::BelowCenter;
      const double yBelow = a.b + gap;
      const double yAbove = a.t - gap - h;
      const bool fitsBelow = yBelow + h <= bd.b;
      const bool fitsAbove = yAbove >= bd.t;
      bool useBelow = below;
      if (in.flip && below && !fitsBelow && fitsAbove) useBelow = false;
      if (in.flip && !below && !fitsAbove && fitsBelow) useBelow = true;
      out.flipped = useBelow != below;
      y = useBelow ? yBelow : yAbove;
      // Keep the horizontal alignment kind of the requested placement.
      const Placement kind = in.placement;
      x = alignX(kind);
      if (out.flipped) {
        const bool s = kind == Placement::BelowStart || kind == Placement::AboveStart;
        const bool e = kind == Placement::BelowEnd || kind == Placement::AboveEnd;
        out.actual = useBelow ? (s ? Placement::BelowStart : e ? Placement::BelowEnd : Placement::BelowCenter)
                              : (s ? Placement::AboveStart : e ? Placement::AboveEnd : Placement::AboveCenter);
      }
      break;
    }
    case Placement::RightStart:
    case Placement::LeftStart: {
      const bool right = in.placement == Placement::RightStart;
      const double xRight = a.r + gap;
      const double xLeft = a.l - gap - w;
      const bool fitsRight = xRight + w <= bd.r;
      const bool fitsLeft = xLeft >= bd.l;
      bool useRight = right;
      if (in.flip && right && !fitsRight && fitsLeft) useRight = false;
      if (in.flip && !right && !fitsLeft && fitsRight) useRight = true;
      out.flipped = useRight != right;
      out.actual = useRight ? Placement::RightStart : Placement::LeftStart;
      x = useRight ? xRight : xLeft;
      y = a.t;
      break;
    }
    case Placement::Center:
      x = bd.l + (bd.w() - w) / 2.0;
      y = bd.t + (bd.h() - h) / 2.0;
      break;
    case Placement::Manual: break;
  }
  out.x = pushInside(x, w, bd.l, bd.r);
  out.y = pushInside(y, h, bd.t, bd.b);
  return out;
}

Point placeTooltip(const TooltipPlacementInput& in) {
  constexpr double kOffsetX = 12.0;
  constexpr double kOffsetY = 8.0;
  constexpr double kGapX = 16.0;
  constexpr double kGapY = 12.0;
  constexpr double kExclusionX = 4.0;
  constexpr double kExclusionY = 3.0;
  const double w = size(in.width);
  const double h = size(in.height);
  const double px = clean(in.pointerX);
  const double py = clean(in.pointerY);
  const bool bounded = !in.bounds.empty();
  const Box bd = bounded ? toBox(in.bounds) : Box{-1e9, -1e9, 1e9, 1e9};
  const auto inside = [&](double x, double y) { return Point{pushInside(x, w, bd.l, bd.r), pushInside(y, h, bd.t, bd.b)}; };

  // Preferred spot, shifted back inside by the overflow.
  Point p = inside(px + kOffsetX, py + kOffsetY);
  const auto coversPointer = [&](const Point& q) { return px >= q.x && px <= q.x + w && py >= q.y && py <= q.y + h; };

  if (coversPointer(p)) {
    // Upper left of the pointer, or the other side when that leaves the bounds.
    const Point candidates[] = {{px - kGapX - w, py - kGapY - h}, {px + kGapX, py - kGapY - h}, {px - kGapX - w, py + kGapY}};
    p = inside(candidates[0].x, candidates[0].y);
    for (const Point& c : candidates) {
      if (within(c.x, c.y, w, h, bd)) {
        p = c;
        break;
      }
    }
  }

  if (!in.exclusion.empty()) {
    const Box ex = toBox(in.exclusion);
    const bool overlaps = p.x < ex.r && p.x + w > ex.l && p.y < ex.b && p.y + h > ex.t;
    if (overlaps) {
      const Point preferred = {px + kOffsetX, py + kOffsetY};
      const Point beside = inside(ex.r + kExclusionX, std::max(p.y, ex.t));
      const Point under = inside(std::max(p.x, ex.l), ex.b + kExclusionY);
      const auto dist2 = [&](const Point& q) { return (q.x - preferred.x) * (q.x - preferred.x) + (q.y - preferred.y) * (q.y - preferred.y); };
      p = dist2(beside) <= dist2(under) ? beside : under;
    }
  }
  return p;
}

}  // namespace r1ui::widgets

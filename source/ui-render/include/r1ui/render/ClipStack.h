// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the axis-aligned clip rectangle stack with nested intersection, and the float-to-pixel
//   snapping rule for clip rectangles.
// Why: the painter must flush a batch whenever the clip changes and cull draws outside it; the
//   intersection and snapping rules are pure integer math tested without a GPU.
// Callers: paint/Painter.cpp; tests/ui-render fast tier.
// Invariants: the current clip is always inside the bounds given to reset(); an empty clip is
//   legal (everything is culled) and stays empty for every nested push.
#pragma once

#include <cstddef>
#include <vector>

#include "r1ui/render/PaintList.h"

namespace r1ui::render {

// Rectangle with float coordinates (physical pixels, y down).
struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

// Intersection of two rectangles; the result is empty (w = h = 0) when they do not overlap.
IRect intersect(const IRect& a, const IRect& b);

// Rounds both edges to the nearest pixel (halves round up) so a clip given in fractional layout
// coordinates snaps like a 1 px edge would. Non-finite input yields an empty rectangle (nothing
// is drawn; fail closed); values beyond +-2^29 are clamped so the arithmetic cannot overflow.
IRect snapToPixels(const Rect& rect);

class ClipStack {
 public:
  // Clears the stack; `bounds` (normally the whole target) becomes the base clip.
  void reset(const IRect& bounds);
  // Pushes the intersection of the current clip and `rect` (snapped to pixels).
  void push(const Rect& rect);
  // Throws std::logic_error when only the base clip is left.
  void pop();

  const IRect& current() const { return stack_.back(); }
  // Number of pushes not yet popped.
  size_t depth() const { return stack_.size() - 1; }

 private:
  std::vector<IRect> stack_{IRect{}};
};

}  // namespace r1ui::render

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the accumulated damage region of a frame as a small bounded list of rectangles in
//   absolute (window) coordinates.
// Why: redrawing only what changed needs the changed area, but an unbounded list of rectangles
//   costs more to process than the overdraw it saves; a merge limit keeps the list small.
// Callers: invalidation::Invalidator fills it; the renderer consumes the rectangles (scissor
//   regions or a single bounding box). Calls: layout::Rect helpers.
// Policy for add(): empty rectangles are ignored; a rectangle already covered by an existing one
//   is dropped; existing rectangles covered by the new one are removed; when the list then
//   exceeds `maxRects`, the two rectangles whose union adds the least extra area are merged
//   (repeatedly) until the limit holds. The union of the list therefore always covers every
//   added rectangle, and the list never holds more than maxRects entries.
#pragma once

#include <cstddef>
#include <vector>

#include "r1ui/core/layout/Geometry.h"

namespace r1ui::core::invalidation {

class DamageList {
 public:
  // maxRects is clamped to at least 1.
  explicit DamageList(size_t maxRects = 8) : maxRects_(maxRects == 0 ? 1 : maxRects) {}

  void add(const layout::Rect& rect);
  bool empty() const { return rects_.empty(); }
  size_t size() const { return rects_.size(); }
  size_t maxRects() const { return maxRects_; }
  const std::vector<layout::Rect>& rects() const { return rects_; }
  // Smallest rectangle covering the whole list (empty when the list is empty).
  layout::Rect bounds() const;
  // Moves the rectangles out and leaves the list empty.
  std::vector<layout::Rect> take();
  void clear() { rects_.clear(); }

 private:
  void mergeCheapestPair();

  size_t maxRects_;
  std::vector<layout::Rect> rects_;
};

}  // namespace r1ui::core::invalidation

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the mapping between grapheme boundaries (UTF-8 byte offsets) and x positions of a
//   shaped run: caret placement, hit-testing and selection extents.
// Why: the shaper reports clusters per glyph, but the caret must sit on grapheme boundaries,
//   including inside ligatures ("fi" is one glyph with two boundaries) and between combining
//   marks; this derives one x per boundary from the cluster data.
// Callers: TextEditor (caret x, hit-test, scroll), truncateWithEllipsis, widgets.
// Rule: a glyph cluster covering n graphemes is split into n equal parts (the usual ligature
//   caret convention); in right-to-left clusters the parts run right to left.
// Invariants: boundaries are sorted by byte offset, include offset 0 and text.size(), and every
//   entry is a grapheme boundary of `text`.
#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "r1ui/text/Shaper.h"

namespace r1ui::text {

class CaretMap {
 public:
  struct Stop {
    std::size_t offset = 0;  // byte offset of the grapheme boundary
    float x = 0;             // pixels from the start of the run
  };

  // `text` must be the string whose offsets the run's clusters refer to (the original text, or
  // run.sanitizedText when the run was repaired).
  static CaretMap build(const ShapedRun& run, std::string_view text);

  // Empty map = one stop at offset 0, x 0.
  CaretMap() : stops_{Stop{}} {}

  // x of a boundary; for an offset that is not a boundary, the x of the boundary before it.
  float xForOffset(std::size_t offset) const;

  // Offset of the boundary whose x is closest to `x`; ties go to the smaller offset.
  std::size_t offsetForX(float x) const;

  float width() const { return width_; }
  const std::vector<Stop>& stops() const { return stops_; }

 private:
  std::vector<Stop> stops_;
  float width_ = 0;
};

}  // namespace r1ui::text

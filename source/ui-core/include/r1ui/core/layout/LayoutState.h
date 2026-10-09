// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the per-widget scratch state of the layout engine (measure cache, commit stamp).
// Why: re-measuring a content-sized subtree for every ancestor variant would be exponential in
//   nesting depth; a tiny per-node cache keyed by the constraint tuple keeps layout linear, and
//   the commit stamp lets an incremental pass skip clean subtrees entirely.
// Callers: tree::Widget embeds one; only layout::FlexLayout reads or writes it.
// Invariants: a cache entry is valid only while its epoch equals the tree's layout epoch AND the
//   widget is not layout-dirty; a full layout pass bumps the epoch, which discards all entries.
#pragma once

#include <cstdint>

#include "r1ui/core/layout/Measure.h"

namespace r1ui::core::layout {

// One axis of a sizing request: how to read `size`, plus the node's own resolved min/max clamp.
struct AxisConstraint {
  MeasureMode mode = MeasureMode::Undefined;
  double size = 0.0;
  double min = 0.0;
  double max = 0.0;
  friend bool operator==(const AxisConstraint&, const AxisConstraint&) = default;
};

struct SizeConstraint {
  AxisConstraint w;
  AxisConstraint h;
  friend bool operator==(const SizeConstraint&, const SizeConstraint&) = default;
};

struct NodeLayoutState {
  static constexpr int kCacheSlots = 3;

  struct Entry {
    SizeConstraint key;
    double width = 0.0;
    double height = 0.0;
  };

  Entry cache[kCacheSlots];
  uint8_t cacheCount = 0;
  uint8_t cacheNext = 0;
  uint32_t cacheEpoch = 0;
  uint32_t cachePass = 0;  // pass that last (re)filled the cache; only trusted for dirty widgets

  // Last committed (final) size and the epoch it was committed in; epoch 0 = never laid out.
  uint32_t commitEpoch = 0;
  double commitWidth = 0.0;
  double commitHeight = 0.0;
  // Set by a commit, consumed (and cleared) by the rounding pass of the same layout call.
  bool committedThisPass = false;
};

}  // namespace r1ui::core::layout

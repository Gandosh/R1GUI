// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: DamageList insertion, containment pruning and merge-to-limit.
// Why: see DamageList.h for the policy.
// Callers: Invalidator, tests.
#include "r1ui/core/invalidation/DamageList.h"

#include <algorithm>
#include <utility>

namespace r1ui::core::invalidation {

using layout::Rect;

void DamageList::add(const Rect& rect) {
  if (rect.empty()) return;
  for (const Rect& existing : rects_) {
    if (layout::containsRect(existing, rect)) return;
  }
  rects_.erase(std::remove_if(rects_.begin(), rects_.end(),
                              [&](const Rect& existing) { return layout::containsRect(rect, existing); }),
               rects_.end());
  rects_.push_back(rect);
  while (rects_.size() > maxRects_) mergeCheapestPair();
}

// Merges the pair whose bounding box wastes the least area (union area minus both areas).
void DamageList::mergeCheapestPair() {
  size_t bestI = 0;
  size_t bestJ = 1;
  int64_t bestCost = -1;
  for (size_t i = 0; i < rects_.size(); ++i) {
    for (size_t j = i + 1; j < rects_.size(); ++j) {
      const int64_t cost = layout::unite(rects_[i], rects_[j]).area() - rects_[i].area() - rects_[j].area();
      if (bestCost < 0 || cost < bestCost) {
        bestCost = cost;
        bestI = i;
        bestJ = j;
      }
    }
  }
  rects_[bestI] = layout::unite(rects_[bestI], rects_[bestJ]);
  rects_.erase(rects_.begin() + static_cast<std::ptrdiff_t>(bestJ));
}

Rect DamageList::bounds() const {
  Rect all;
  for (const Rect& r : rects_) all = layout::unite(all, r);
  return all;
}

std::vector<Rect> DamageList::take() {
  std::vector<Rect> out = std::move(rects_);
  rects_.clear();
  return out;
}

}  // namespace r1ui::core::invalidation

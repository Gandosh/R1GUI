// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: ShelfPacker implementation (see r1ui/text/ShelfPacker.h).
// Policy: reuse the shortest existing shelf that is tall enough but not more than twice as tall
//   (bounded waste), taking the tightest free span; otherwise open a new shelf; as a last resort
//   reuse any completely empty shelf that is tall enough. Empty shelves at the top of the stack
//   are given back to the open area.
#include "r1ui/text/ShelfPacker.h"

#include <algorithm>

namespace r1ui::text {

namespace {

constexpr int kShelfQuantum = 4;

int roundUp(int value, int quantum) { return (value + quantum - 1) / quantum * quantum; }

}  // namespace

ShelfPacker::ShelfPacker(int width, int height)
    : width_(width > 0 ? width : 0), height_(height > 0 ? height : 0) {
  if (width_ == 0 || height_ == 0) {
    width_ = 0;
    height_ = 0;
  }
}

std::optional<PackedRect> ShelfPacker::takeFromShelf(Shelf& shelf, int w, int h) {
  // Tightest free span that is wide enough.
  std::size_t best = shelf.freeSpans.size();
  for (std::size_t i = 0; i < shelf.freeSpans.size(); ++i) {
    if (shelf.freeSpans[i].w >= w && (best == shelf.freeSpans.size() || shelf.freeSpans[i].w < shelf.freeSpans[best].w)) {
      best = i;
    }
  }
  if (best == shelf.freeSpans.size()) return std::nullopt;
  Span& span = shelf.freeSpans[best];
  const PackedRect rect{span.x, shelf.y, w, h};
  span.x += w;
  span.w -= w;
  if (span.w == 0) shelf.freeSpans.erase(shelf.freeSpans.begin() + static_cast<std::ptrdiff_t>(best));
  ++shelf.live;
  ++liveCount_;
  liveArea_ += static_cast<long long>(w) * h;
  return rect;
}

std::optional<PackedRect> ShelfPacker::allocate(int w, int h) {
  if (w <= 0 || h <= 0 || w > width_ || h > height_) return std::nullopt;

  // 1. Best-fitting existing shelf: tall enough, and not wastefully taller than needed.
  Shelf* best = nullptr;
  for (Shelf& s : shelves_) {
    if (s.h < h || s.h > std::max(h * 2, h + kShelfQuantum)) continue;
    const bool fits = std::any_of(s.freeSpans.begin(), s.freeSpans.end(), [&](const Span& sp) { return sp.w >= w; });
    if (fits && (best == nullptr || s.h < best->h)) best = &s;
  }
  if (best != nullptr) return takeFromShelf(*best, w, h);

  // 2. A new shelf on top of the stack.
  const int shelfHeight = std::min(roundUp(h, kShelfQuantum), height_ - openTop_);
  if (shelfHeight >= h) {
    shelves_.push_back(Shelf{openTop_, shelfHeight, 0, {Span{0, width_}}});
    openTop_ += shelfHeight;
    return takeFromShelf(shelves_.back(), w, h);
  }

  // 3. Recycle a completely empty shelf that is tall enough, whatever its height.
  for (Shelf& s : shelves_) {
    if (s.live == 0 && s.h >= h) return takeFromShelf(s, w, h);
  }
  return std::nullopt;
}

bool ShelfPacker::release(const PackedRect& rect) {
  const auto it = std::find_if(shelves_.begin(), shelves_.end(), [&](const Shelf& s) { return s.y == rect.y; });
  if (it == shelves_.end() || it->live == 0) return false;
  if (rect.w <= 0 || rect.h <= 0 || rect.h > it->h || rect.x < 0 || rect.x + rect.w > width_) return false;

  Shelf& shelf = *it;
  // A live rectangle never intersects a free span; reject anything that does (double release).
  for (const Span& sp : shelf.freeSpans) {
    if (rect.x < sp.x + sp.w && sp.x < rect.x + rect.w) return false;
  }

  // Insert the span in order and merge with touching neighbours.
  auto pos = std::lower_bound(shelf.freeSpans.begin(), shelf.freeSpans.end(), rect.x,
                              [](const Span& sp, int x) { return sp.x < x; });
  pos = shelf.freeSpans.insert(pos, Span{rect.x, rect.w});
  if (pos + 1 != shelf.freeSpans.end() && pos->x + pos->w == (pos + 1)->x) {
    pos->w += (pos + 1)->w;
    pos = shelf.freeSpans.erase(pos + 1) - 1;
  }
  if (pos != shelf.freeSpans.begin() && (pos - 1)->x + (pos - 1)->w == pos->x) {
    (pos - 1)->w += pos->w;
    shelf.freeSpans.erase(pos);
  }

  --shelf.live;
  --liveCount_;
  liveArea_ -= static_cast<long long>(rect.w) * rect.h;
  if (shelf.live == 0) {
    shelf.freeSpans.assign(1, Span{0, width_});
    trimTopShelves();
  }
  return true;
}

// Returns empty shelves at the top of the stack to the open area so later, taller rectangles
// can start a fresh shelf of the right height.
void ShelfPacker::trimTopShelves() {
  while (!shelves_.empty() && shelves_.back().live == 0) {
    openTop_ = shelves_.back().y;
    shelves_.pop_back();
  }
}

}  // namespace r1ui::text

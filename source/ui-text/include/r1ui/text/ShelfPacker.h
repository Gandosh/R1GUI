// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: rectangle packing for the glyph atlas: horizontal shelves with per-shelf free spans,
//   supporting release so evicted glyphs make room again.
// Why: glyph bitmaps are small and similar in height, so shelves waste little and allocate in
//   O(shelves + spans); release + coalescing is what lets LRU eviction free space without
//   repacking (which would move glyphs the renderer already references).
// Callers: GlyphAtlas, tests.
// Invariants: live rectangles never overlap and always lie inside [0,width) x [0,height).
//   A shelf's height is its tallest allocation rounded up to a multiple of 4. Shelves that become
//   empty are recycled (the topmost ones are returned to the open area).
// Failure behavior: allocate returns nullopt when nothing fits (the caller decides whether to
//   evict); release returns false for a rectangle that is not a live allocation.
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace r1ui::text {

struct PackedRect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  friend bool operator==(const PackedRect&, const PackedRect&) = default;
};

class ShelfPacker {
 public:
  // Both dimensions must be positive (otherwise the packer is empty and always full).
  ShelfPacker(int width, int height);

  std::optional<PackedRect> allocate(int w, int h);
  bool release(const PackedRect& rect);

  int width() const { return width_; }
  int height() const { return height_; }
  std::size_t liveCount() const { return liveCount_; }
  long long liveArea() const { return liveArea_; }

 private:
  struct Span {
    int x;
    int w;
  };
  struct Shelf {
    int y;
    int h;
    std::size_t live;
    std::vector<Span> freeSpans;  // sorted by x, non-adjacent
  };

  std::optional<PackedRect> takeFromShelf(Shelf& shelf, int w, int h);
  void trimTopShelves();

  int width_;
  int height_;
  int openTop_ = 0;  // first row not covered by any shelf
  std::vector<Shelf> shelves_;  // sorted by y
  std::size_t liveCount_ = 0;
  long long liveArea_ = 0;
};

}  // namespace r1ui::text

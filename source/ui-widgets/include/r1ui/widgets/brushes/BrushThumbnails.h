// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: BrushThumbnailSource, the pictures of the brush library (slice 5.22): a bounded texture cache
//   (LRU by entry count and bytes) fed by a ThumbnailProvider the host implements, with the scheduler that
//   asks for the visible pictures first within a time budget. It reuses the machinery of the asset browser
//   (thumbnailgrid/ThumbnailCache.h); the provider contract is the same: produce() returns Ready, Pending
//   or Failed and must return quickly; a provider that works on other threads returns Pending and calls
//   notifyReady() from the UI thread when the picture can be fetched.
// Why: a library of 2000 brushes must not create 2000 pictures when it opens, must not stall a frame
//   waiting for one, and must not hold unbounded memory; the tiles show their icon until the picture exists.
// Callers: BrushLibraryController (owns one, shares it with each popup it opens), the popup (pump and
//   find while painting), the host (provider and texture sink). Calls: ThumbnailCache, ThumbnailScheduler.
// Keys: a picture is identified by BrushLibraryModel::thumbnailKey(index); the provider maps a key back to
//   a brush with BrushLibraryModel::indexOfThumbnailKey. The cache outlives a popup, so opening the library
//   again shows the pictures at once.
// Memory: defaults hold at most 1024 pictures and 64 MiB (a 64 px picture is 16 KiB, a 128 px one 64 KiB);
//   pictures the current window shows (plus a margin of two rows) are never evicted while drawn.
// Failure behavior: a provider failure keeps the icon until invalidate(key); a texture creation exception
//   is caught by the cache. Nothing here throws into paint.
// Threading: UI thread only.
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <unordered_set>
#include <vector>

#include "r1ui/widgets/thumbnailgrid/ThumbnailCache.h"

namespace r1ui::widgets {

class BrushThumbnailSource {
 public:
  BrushThumbnailSource(thumbs::ThumbnailProvider& provider, thumbs::ThumbnailTextureSink& sink, thumbs::CacheLimits limits = {1024, size_t{64} * 1024 * 1024});
  BrushThumbnailSource(const BrushThumbnailSource&) = delete;
  BrushThumbnailSource& operator=(const BrushThumbnailSource&) = delete;

  // One scheduling pass for the keys the popup shows (visible ones first, the rest are the margin). Call it
  // before drawing in the same paint: it may evict textures no frame reads any more. True while keys remain
  // that may still produce a picture (the caller keeps frames coming).
  bool pump(std::span<const uint64_t> wanted, std::span<const uint64_t> visible, uint32_t bucket);
  // The picture of `key` at any size, or nullptr (draw the icon).
  const thumbs::ThumbnailTexture* find(uint64_t key, uint32_t bucket);

  void notifyReady(uint64_t key) { scheduler_.notifyReady(key); }
  void invalidate(uint64_t key) { scheduler_.invalidate(key); }
  void invalidateAll() { scheduler_.invalidateAll(); }

  size_t cached() const { return cache_.size(); }
  size_t bytes() const { return cache_.bytes(); }
  size_t requests() const { return scheduler_.requested(); }
  bool failed(uint64_t key) const { return scheduler_.failed(key); }
  // Test hook: replaces the clock the scheduler's time budget reads (milliseconds).
  void setClock(std::function<double()> clock) { clock_ = std::move(clock); }

  // The picture edge to request for a display scale: 64, 128 or 256.
  static uint32_t bucketFor(double scale);

 private:
  thumbs::ThumbnailCache cache_;
  thumbs::ThumbnailScheduler scheduler_;
  std::function<double()> clock_;
  std::unordered_set<uint64_t> protected_;
};

}  // namespace r1ui::widgets

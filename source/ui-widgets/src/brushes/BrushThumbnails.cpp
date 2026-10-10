// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of BrushThumbnails.h.
// Invariants: the protected set is exactly the keys of the last pump (visible plus margin), so the cache
//   never evicts a picture the popup is about to draw; the scheduler budget defaults to 5 ms per pass.
// Callers: BrushLibraryPopup, tests.
#include "r1ui/widgets/brushes/BrushThumbnails.h"

#include <chrono>

namespace r1ui::widgets {

namespace {

double steadyMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

}  // namespace

BrushThumbnailSource::BrushThumbnailSource(thumbs::ThumbnailProvider& provider, thumbs::ThumbnailTextureSink& sink, thumbs::CacheLimits limits)
    : cache_(sink, limits), scheduler_(cache_) {
  scheduler_.setProvider(&provider);
}

bool BrushThumbnailSource::pump(std::span<const uint64_t> wanted, std::span<const uint64_t> visible, uint32_t bucket) {
  protected_.clear();
  protected_.insert(visible.begin(), visible.end());
  protected_.insert(wanted.begin(), wanted.end());
  const std::function<double()> now = clock_ ? clock_ : std::function<double()>(steadyMs);
  return scheduler_.pump(wanted, bucket, [this](uint64_t key) { return protected_.count(key) != 0; }, now);
}

const thumbs::ThumbnailTexture* BrushThumbnailSource::find(uint64_t key, uint32_t bucket) {
  const thumbs::ThumbnailCache::Entry* entry = cache_.find(key, bucket, true);
  if (entry == nullptr || !entry->texture || entry->texture->width() == 0 || entry->texture->height() == 0) return nullptr;
  return entry->texture.get();
}

uint32_t BrushThumbnailSource::bucketFor(double scale) {
  const double edge = 64.0 * (scale > 0.0 && scale < 16.0 ? scale : 1.0);
  if (edge <= 64.0) return 64;
  return edge <= 128.0 ? 128 : 256;
}

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: GlyphAtlas implementation (see r1ui/text/GlyphAtlas.h for bounds and the frame rule).
// Invariants: every entry with hasRect owns exactly one live ShelfPacker rectangle; entries_ and
//   lru_ always have the same size; pixels_ is width*height bytes. On any failure path no
//   rectangle is leaked and no entry is left half-built.
#include "r1ui/text/GlyphAtlas.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace r1ui::text {

namespace {

constexpr int kPadding = 1;

std::uint32_t quantize64(float value) { return static_cast<std::uint32_t>(std::lround(value * 64.0f)); }

}  // namespace

std::size_t GlyphAtlas::KeyHash::operator()(const Key& k) const {
  std::uint64_t h = 1469598103934665603ull;
  for (const std::uint32_t v : {k.font, k.sizeQ, k.glyph, k.emboldenQ, k.bin}) {
    h = (h ^ v) * 1099511628211ull;
    h ^= h >> 29;
  }
  return static_cast<std::size_t>(h);
}

GlyphAtlas::GlyphAtlas(const AtlasConfig& config)
    : config_(config),
      packer_(config.width, config.height),
      pixels_(static_cast<std::size_t>(config.width) * static_cast<std::size_t>(config.height), 0) {}

Result<GlyphAtlas> GlyphAtlas::create(const AtlasConfig& config) {
  const auto inRange = [](int v) { return v >= kMinAtlasDimension && v <= kMaxAtlasDimension; };
  if (!inRange(config.width) || !inRange(config.height) || config.maxEvictionPasses < 1 ||
      config.maxEvictionPasses > 64 || config.maxEntries < 16 || config.maxEntries > (1u << 20)) {
    return makeError(ErrorCode::InvalidArgument, "atlas configuration outside the documented ranges");
  }
  try {
    return GlyphAtlas(config);
  } catch (const std::bad_alloc&) {
    return makeError(ErrorCode::Internal, "out of memory allocating the atlas image");
  }
}

void GlyphAtlas::markDirty(const PackedRect& r) {
  if (!dirty_) {
    dirty_ = DirtyRect{r.x, r.y, r.w, r.h};
    return;
  }
  const int x0 = std::min(dirty_->x, r.x);
  const int y0 = std::min(dirty_->y, r.y);
  const int x1 = std::max(dirty_->x + dirty_->w, r.x + r.w);
  const int y1 = std::max(dirty_->y + dirty_->h, r.y + r.h);
  dirty_ = DirtyRect{x0, y0, x1 - x0, y1 - y0};
}

std::optional<DirtyRect> GlyphAtlas::takeDirtyRect() {
  std::optional<DirtyRect> out = dirty_;
  dirty_.reset();
  return out;
}

AtlasStats GlyphAtlas::stats() const {
  return AtlasStats{entries_.size(), evictedTotal_, packer_.liveArea(), frame_};
}

void GlyphAtlas::clear() {
  entries_.clear();
  lru_.clear();
  packer_ = ShelfPacker(config_.width, config_.height);
  std::fill(pixels_.begin(), pixels_.end(), std::uint8_t{0});
  markDirty(PackedRect{0, 0, config_.width, config_.height});
}

// Evicts up to `count` of the least recently used entries, stopping at the first entry that was
// used in the current frame (everything newer in the list is pinned as well).
std::size_t GlyphAtlas::evictOldest(std::size_t count) {
  std::size_t evicted = 0;
  while (evicted < count && !lru_.empty()) {
    const auto it = entries_.find(lru_.back());
    if (it->second.lastFrame == frame_) break;
    if (it->second.hasRect) packer_.release(it->second.padded);
    entries_.erase(it);
    lru_.pop_back();
    ++evicted;
  }
  evictedTotal_ += evicted;
  return evicted;
}

std::optional<PackedRect> GlyphAtlas::allocateWithEviction(int w, int h) {
  for (int pass = 0; pass <= config_.maxEvictionPasses; ++pass) {
    if (std::optional<PackedRect> rect = packer_.allocate(w, h)) return rect;
    if (pass == config_.maxEvictionPasses) break;
    if (evictOldest(std::max<std::size_t>(1, entries_.size() / 8)) == 0) break;
  }
  return std::nullopt;
}

AtlasLookup GlyphAtlas::get(const Font& font, std::uint32_t glyphId, const RasterParams& params) {
  AtlasLookup out;
  if (!isValidPixelSize(params.pixelSize) || params.subpixelBin < 0 || params.subpixelBin >= kSubpixelBins ||
      !std::isfinite(params.emboldenPx) || params.emboldenPx < 0.0f || params.emboldenPx > params.pixelSize) {
    out.status = LookupStatus::InvalidArgument;
    return out;
  }

  const Key key{font.id(), quantize64(params.pixelSize), glyphId, quantize64(params.emboldenPx),
                static_cast<std::uint32_t>(params.subpixelBin)};
  if (const auto hit = entries_.find(key); hit != entries_.end()) {
    lru_.splice(lru_.begin(), lru_, hit->second.lru);
    hit->second.lastFrame = frame_;
    out.status = LookupStatus::Ok;
    out.glyph = hit->second.glyph;
    return out;
  }

  if (entries_.size() >= config_.maxEntries &&
      evictOldest(std::max<std::size_t>(1, entries_.size() / 8)) == 0) {
    out.status = LookupStatus::AtlasFull;
    return out;
  }

  Result<GlyphBitmap> raster = rasterizeGlyph(font, glyphId, params);
  if (!raster.ok()) {
    switch (raster.error().code) {
      case ErrorCode::InvalidArgument: out.status = LookupStatus::InvalidArgument; break;
      case ErrorCode::GlyphTooLarge: out.status = LookupStatus::GlyphTooLarge; break;
      default: out.status = LookupStatus::Internal; break;
    }
    return out;
  }
  const GlyphBitmap& bitmap = raster.value();

  Entry entry;
  entry.lastFrame = frame_;
  bool listed = false;
  try {
    if (bitmap.width > 0 && bitmap.height > 0) {
      const int paddedW = bitmap.width + 2 * kPadding;
      const int paddedH = bitmap.height + 2 * kPadding;
      if (paddedW > config_.width || paddedH > config_.height) {
        out.status = LookupStatus::GlyphTooLarge;
        return out;
      }
      const std::optional<PackedRect> rect = allocateWithEviction(paddedW, paddedH);
      if (!rect) {
        out.status = LookupStatus::AtlasFull;
        return out;
      }
      // Clear the padded area first: the rectangle may hold an evicted glyph's pixels.
      for (int row = 0; row < rect->h; ++row) {
        std::uint8_t* line = pixels_.data() + static_cast<std::size_t>(rect->y + row) * static_cast<std::size_t>(config_.width) +
                             static_cast<std::size_t>(rect->x);
        std::memset(line, 0, static_cast<std::size_t>(rect->w));
      }
      for (int row = 0; row < bitmap.height; ++row) {
        std::uint8_t* line = pixels_.data() +
                             static_cast<std::size_t>(rect->y + kPadding + row) * static_cast<std::size_t>(config_.width) +
                             static_cast<std::size_t>(rect->x + kPadding);
        std::memcpy(line, bitmap.coverage.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(bitmap.width),
                    static_cast<std::size_t>(bitmap.width));
      }
      markDirty(*rect);

      entry.padded = *rect;
      entry.hasRect = true;
      AtlasGlyph& g = entry.glyph;
      g.visible = true;
      g.x = rect->x + kPadding;
      g.y = rect->y + kPadding;
      g.w = bitmap.width;
      g.h = bitmap.height;
      g.left = bitmap.left;
      g.top = bitmap.top;
      g.u0 = static_cast<float>(g.x) / static_cast<float>(config_.width);
      g.v0 = static_cast<float>(g.y) / static_cast<float>(config_.height);
      g.u1 = static_cast<float>(g.x + g.w) / static_cast<float>(config_.width);
      g.v1 = static_cast<float>(g.y + g.h) / static_cast<float>(config_.height);
    }

    lru_.push_front(key);
    listed = true;
    entry.lru = lru_.begin();
    out.glyph = entry.glyph;
    entries_.emplace(key, entry);
    out.status = LookupStatus::Ok;
  } catch (const std::bad_alloc&) {
    // Roll back so the rectangle and list node are not leaked.
    if (listed) lru_.pop_front();
    if (entry.hasRect) packer_.release(entry.padded);
    out.status = LookupStatus::Internal;
    out.glyph = AtlasGlyph{};
  }
  return out;
}

}  // namespace r1ui::text

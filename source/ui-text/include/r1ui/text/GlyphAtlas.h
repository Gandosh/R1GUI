// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the CPU-side glyph atlas: an R8 coverage image, a bounded glyph cache with LRU eviction,
//   and dirty-region tracking for incremental GPU upload.
// Why: text drawing samples glyph coverage from one texture; the renderer (written separately)
//   uploads only what changed and draws quads that reference atlas rectangles by UV.
// Callers: GlyphQuads (and through it widgets); ui-render reads pixels()/takeDirtyRect().
// Cache key: (font id, pixel size in 1/64 px, subpixel bin 0..3, glyph id, embolden in 1/64 px).
// Bounds: dimensions are 64..kMaxAtlasDimension (4096) per side, default 2048x2048 = 4 MiB of
//   R8; at most maxEntries cache entries (default 65536) including ink-less glyphs. When the
//   atlas or the entry table is full, up to maxEvictionPasses passes each evict the oldest
//   ~1/8 of the entries; if that frees nothing (everything was used in the current frame) the
//   lookup reports AtlasFull and the caller flushes the frame and retries after beginFrame().
// Frame rule: entries looked up since the last beginFrame() are pinned and never evicted, so a
//   quad list built during one frame always points at valid atlas pixels.
// Padding: every glyph is stored with a 1 px zeroed border so bilinear sampling never bleeds in
//   neighbours; the reported rectangle and UVs cover the ink only.
// Threading: UI thread only; not synchronized.
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <unordered_map>
#include <vector>

#include "r1ui/text/Font.h"
#include "r1ui/text/Rasterizer.h"
#include "r1ui/text/Result.h"
#include "r1ui/text/ShelfPacker.h"

namespace r1ui::text {

inline constexpr int kMaxAtlasDimension = 4096;
inline constexpr int kMinAtlasDimension = 64;

struct AtlasConfig {
  int width = 2048;
  int height = 2048;
  int maxEvictionPasses = 8;            // 1..64
  std::size_t maxEntries = 65536;       // 16..1048576
};

struct DirtyRect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

struct AtlasGlyph {
  bool visible = false;  // false: the glyph has no ink (space); all other fields are 0
  int x = 0;             // ink rectangle inside the atlas, pixels
  int y = 0;
  int w = 0;
  int h = 0;
  int left = 0;          // bearing: ink left edge relative to the integer pen x
  int top = 0;           // bearing: ink top edge above the baseline (positive = up)
  float u0 = 0;
  float v0 = 0;
  float u1 = 0;
  float v1 = 0;
};

enum class LookupStatus { Ok, AtlasFull, GlyphTooLarge, InvalidArgument, Internal };

struct AtlasLookup {
  LookupStatus status = LookupStatus::Internal;
  AtlasGlyph glyph;
};

struct AtlasStats {
  std::size_t entries = 0;
  std::size_t evictedTotal = 0;
  long long liveArea = 0;   // padded pixels currently allocated
  std::uint64_t frame = 0;
};

class GlyphAtlas {
 public:
  // Errors: InvalidArgument when the configuration is outside the documented ranges.
  static Result<GlyphAtlas> create(const AtlasConfig& config = {});

  // Starts a new frame: everything looked up before this call becomes evictable.
  void beginFrame() { ++frame_; }

  // Finds or rasterizes and stores a glyph. Never throws; failures are reported in the status
  // and leave the cache consistent.
  AtlasLookup get(const Font& font, std::uint32_t glyphId, const RasterParams& params);

  // Drops every entry and zeroes the image (the whole image becomes dirty).
  void clear();

  int width() const { return config_.width; }
  int height() const { return config_.height; }
  const std::uint8_t* pixels() const { return pixels_.data(); }  // width*height bytes, row-major

  // The bounding box of everything written since the last call, or nullopt if nothing changed.
  std::optional<DirtyRect> takeDirtyRect();

  AtlasStats stats() const;

 private:
  struct Key {
    std::uint32_t font;
    std::uint32_t sizeQ;
    std::uint32_t glyph;
    std::uint32_t emboldenQ;
    std::uint32_t bin;
    friend bool operator==(const Key&, const Key&) = default;
  };
  struct KeyHash {
    std::size_t operator()(const Key& k) const;
  };
  struct Entry {
    AtlasGlyph glyph;
    PackedRect padded;
    bool hasRect = false;
    std::uint64_t lastFrame = 0;
    std::list<Key>::iterator lru;
  };

  explicit GlyphAtlas(const AtlasConfig& config);

  std::size_t evictOldest(std::size_t count);
  std::optional<PackedRect> allocateWithEviction(int w, int h);
  void markDirty(const PackedRect& r);

  AtlasConfig config_;
  ShelfPacker packer_;
  std::vector<std::uint8_t> pixels_;
  std::unordered_map<Key, Entry, KeyHash> entries_;
  std::list<Key> lru_;  // front = most recently used
  std::optional<DirtyRect> dirty_;
  std::uint64_t frame_ = 1;
  std::size_t evictedTotal_ = 0;
};

}  // namespace r1ui::text

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the picture side of the asset browser: the provider interface a host implements to produce
//   thumbnail pixels without blocking the UI, the texture sink interface that turns pixels into
//   Painter textures (a GPU implementation and an in-memory one for tests), the bounded cache of
//   those textures with least-recently-used eviction (spec 12 rules 18, 22, 23, 26), and the
//   scheduler that asks for visible pictures first within a time budget (rules 18, 19).
// Why: thumbnails are the expensive part of a browser. The widget must never wait for a picture, must
//   never hold more memory than its limits, must produce what the user can see before anything else,
//   and must retire textures only when no frame still reads them. Each of those is a rule here, tested
//   without a GPU through MemoryThumbnailTextures.
// Callers: ThumbnailGrid, hosts (provider and sink implementations), tests. Calls: render::TextureRef
//   (a plain handle, no GPU type).
// Threading: UI thread only. A provider that works on other threads returns Pending and calls
//   ThumbnailScheduler::notifyReady / invalidate (or ThumbnailGrid's forwarding methods) from the UI
//   thread when the picture can be fetched.
// Memory: the cache is bounded by an entry count (default 4096, spec 12 rule 22) AND a byte budget
//   (default 256 MiB, because 4096 pictures of 256 x 256 would be 1 GiB); the least recently used
//   unprotected entry goes first; entries the caller marks protected (the visible window plus 64
//   items each side, rule 23) are never evicted, so the cache can exceed its limits only by the size
//   of that window.
// Failure behaviour: a provider failure keeps the type icon (the key is remembered as failed) until
//   invalidate(key); a texture creation exception is caught, the key counts as failed, the cache is
//   unchanged.
#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "r1ui/render/Painter.h"

namespace r1ui::widgets::thumbs {

// ---- pixels ----
struct ThumbnailImage {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba;  // width * height * 4, straight alpha, sRGB, row 0 at the top
  bool valid() const { return width > 0 && height > 0 && rgba.size() == static_cast<size_t>(width) * height * 4; }
};

enum class ThumbnailStatus : uint8_t {
  Ready,    // `out` holds the picture
  Pending,  // not available yet: ask again later (the host may call notifyReady)
  Failed    // cannot be produced: keep the type icon until invalidate()
};

class ThumbnailProvider {
 public:
  virtual ~ThumbnailProvider() = default;
  // Called on the UI thread with a time budget; must return quickly. `sizePx` is the edge of the
  // square the grid would like (64, 128 or 256); the provider may return any size.
  virtual ThumbnailStatus produce(uint64_t itemKey, uint32_t sizePx, ThumbnailImage& out) = 0;
};

// ---- textures ----
class ThumbnailTexture {
 public:
  virtual ~ThumbnailTexture() = default;
  virtual render::TextureRef ref() const = 0;
  virtual uint32_t width() const = 0;
  virtual uint32_t height() const = 0;
};

class ThumbnailTextureSink {
 public:
  virtual ~ThumbnailTextureSink() = default;
  // Creates an RGBA texture; throws on failure (the caller handles it).
  virtual std::unique_ptr<ThumbnailTexture> create(uint32_t width, uint32_t height, std::span<const uint8_t> rgba) = 0;
};

// In-memory sink: ids from 0x2000, counts what is alive. For tests and headless painting.
class MemoryThumbnailTextures final : public ThumbnailTextureSink {
 public:
  std::unique_ptr<ThumbnailTexture> create(uint32_t width, uint32_t height, std::span<const uint8_t> rgba) override;
  size_t alive() const { return *alive_; }
  size_t created() const { return created_; }
  void failNext(bool fail) { failNext_ = fail; }

 private:
  std::shared_ptr<size_t> alive_ = std::make_shared<size_t>(0);
  size_t created_ = 0;
  uint64_t nextId_ = 0x2000;
  bool failNext_ = false;
};

// ---- cache ----
struct CacheLimits {
  size_t maxEntries = 4096;
  size_t maxBytes = size_t{256} * 1024 * 1024;
};

class ThumbnailCache {
 public:
  struct Entry {
    uint64_t key = 0;
    uint32_t bucket = 0;  // the requested edge the picture was made for
    std::unique_ptr<ThumbnailTexture> texture;
    size_t bytes = 0;
  };

  ThumbnailCache(ThumbnailTextureSink& sink, CacheLimits limits = {}) : sink_(&sink), limits_(limits) {}
  ThumbnailCache(const ThumbnailCache&) = delete;
  ThumbnailCache& operator=(const ThumbnailCache&) = delete;

  // The entry for (key, bucket), or the one for the same key at another bucket when `anyBucket`;
  // marks it most recently used. nullptr when absent.
  const Entry* find(uint64_t key, uint32_t bucket, bool anyBucket);
  // Uploads `image` as the picture of (key, bucket), replacing an older picture of that pair, and
  // evicts down to the limits, skipping protected keys. False when the image is invalid, larger than
  // the whole byte budget, or the texture could not be created.
  bool insert(uint64_t key, uint32_t bucket, const ThumbnailImage& image, const std::function<bool(uint64_t)>& isProtected);
  void erase(uint64_t key);  // every bucket of the key
  void clear();
  void setLimits(const CacheLimits& limits) { limits_ = limits; }
  const CacheLimits& limits() const { return limits_; }
  size_t size() const { return entries_.size(); }
  size_t bytes() const { return bytes_; }
  bool contains(uint64_t key) const;

 private:
  struct Key {
    uint64_t key;
    uint32_t bucket;
    friend bool operator==(const Key&, const Key&) = default;
  };
  struct KeyHash {
    size_t operator()(const Key& k) const { return std::hash<uint64_t>{}(k.key * 1315423911u + k.bucket); }
  };
  using List = std::list<Entry>;
  void evict(const std::function<bool(uint64_t)>& isProtected);

  ThumbnailTextureSink* sink_;
  CacheLimits limits_;
  List entries_;  // front = most recently used
  std::unordered_map<Key, List::iterator, KeyHash> index_;
  size_t bytes_ = 0;
};

// ---- scheduling ----
struct SchedulerConfig {
  double budgetMs = 5.0;       // spec 12 rule 19: per refresh
  size_t maxPerPump = 32;      // a cap even when every request is instant
};

class ThumbnailScheduler {
 public:
  ThumbnailScheduler(ThumbnailCache& cache, SchedulerConfig config = {}) : cache_(&cache), config_(config) {}

  void setProvider(ThumbnailProvider* provider) { provider_ = provider; }
  // One scheduling pass: for the keys in `wanted` (highest priority first) that have no picture at
  // `bucket`, asks the provider until the time budget (measured with `nowMs`, a monotonic clock in
  // milliseconds) or maxPerPump is spent. Returns true when keys remain that may still produce a
  // picture (the caller keeps frames coming).
  bool pump(std::span<const uint64_t> wanted, uint32_t bucket, const std::function<bool(uint64_t)>& isProtected, const std::function<double()>& nowMs);
  // A picture of the key can be fetched now (a Pending provider became ready).
  void notifyReady(uint64_t key) { failed_.erase(key); }
  // The look of the item changed (asset reloaded or edited, rule 24): drop its picture and failure state.
  void invalidate(uint64_t key);
  void invalidateAll();
  bool failed(uint64_t key) const { return failed_.count(key) != 0; }
  size_t requested() const { return requested_; }

 private:
  ThumbnailCache* cache_;
  ThumbnailProvider* provider_ = nullptr;
  SchedulerConfig config_;
  std::unordered_set<uint64_t> failed_;
  size_t requested_ = 0;
};

}  // namespace r1ui::widgets::thumbs

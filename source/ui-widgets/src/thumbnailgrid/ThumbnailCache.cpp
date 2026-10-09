// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of ThumbnailCache.h.
// Invariants: bytes_ is the sum of the entries' bytes; index_ maps exactly the entries of entries_;
//   a texture is destroyed only by erase / eviction / clear (never while still referenced by a map
//   slot); eviction never removes a protected key or the entry just inserted.
// Callers: ThumbnailGrid, tests.
#include "r1ui/widgets/thumbnailgrid/ThumbnailCache.h"

#include <algorithm>
#include <stdexcept>

namespace r1ui::widgets::thumbs {

namespace {

constexpr uint32_t kBuckets[] = {64, 128, 256};
constexpr uint32_t kMaxEdge = 16384;

class MemoryTexture final : public ThumbnailTexture {
 public:
  MemoryTexture(uint64_t id, uint32_t w, uint32_t h, std::shared_ptr<size_t> alive) : id_(id), w_(w), h_(h), alive_(std::move(alive)) { ++*alive_; }
  ~MemoryTexture() override { --*alive_; }
  render::TextureRef ref() const override { return {id_, render::TextureKind::Color}; }
  uint32_t width() const override { return w_; }
  uint32_t height() const override { return h_; }

 private:
  uint64_t id_;
  uint32_t w_;
  uint32_t h_;
  std::shared_ptr<size_t> alive_;
};

}  // namespace

std::unique_ptr<ThumbnailTexture> MemoryThumbnailTextures::create(uint32_t width, uint32_t height, std::span<const uint8_t> rgba) {
  if (failNext_) {
    failNext_ = false;
    throw std::runtime_error("texture creation failed");
  }
  if (width == 0 || height == 0 || rgba.size() != static_cast<size_t>(width) * height * 4) throw std::invalid_argument("bad thumbnail pixels");
  ++created_;
  return std::make_unique<MemoryTexture>(nextId_++, width, height, alive_);
}

// ---- ThumbnailCache ---------------------------------------------------------------------------

const ThumbnailCache::Entry* ThumbnailCache::find(uint64_t key, uint32_t bucket, bool anyBucket) {
  const auto touch = [&](uint32_t b) -> const Entry* {
    const auto it = index_.find({key, b});
    if (it == index_.end()) return nullptr;
    entries_.splice(entries_.begin(), entries_, it->second);
    return &*it->second;
  };
  if (const Entry* e = touch(bucket)) return e;
  if (!anyBucket) return nullptr;
  for (const uint32_t b : kBuckets) {
    if (b != bucket) {
      if (const Entry* e = touch(b)) return e;
    }
  }
  return nullptr;
}

bool ThumbnailCache::contains(uint64_t key) const {
  for (const uint32_t b : kBuckets) {
    if (index_.count({key, b}) != 0) return true;
  }
  return false;
}

bool ThumbnailCache::insert(uint64_t key, uint32_t bucket, const ThumbnailImage& image, const std::function<bool(uint64_t)>& isProtected) {
  if (!image.valid() || image.width > kMaxEdge || image.height > kMaxEdge) return false;
  const size_t bytes = static_cast<size_t>(image.width) * image.height * 4;
  if (bytes > limits_.maxBytes || limits_.maxEntries == 0) return false;
  std::unique_ptr<ThumbnailTexture> texture;
  try {
    texture = sink_->create(image.width, image.height, image.rgba);
  } catch (const std::exception&) {
    return false;
  }
  if (!texture) return false;
  const auto old = index_.find({key, bucket});
  if (old != index_.end()) {
    bytes_ -= old->second->bytes;
    entries_.erase(old->second);
    index_.erase(old);
  }
  Entry e;
  e.key = key;
  e.bucket = bucket;
  e.texture = std::move(texture);
  e.bytes = bytes;
  entries_.push_front(std::move(e));
  index_[{key, bucket}] = entries_.begin();
  bytes_ += bytes;
  evict(isProtected);
  return true;
}

void ThumbnailCache::evict(const std::function<bool(uint64_t)>& isProtected) {
  while (entries_.size() > limits_.maxEntries || bytes_ > limits_.maxBytes) {
    // From the least recently used end, skipping protected keys; the newest entry (front) stays.
    auto victim = entries_.end();
    for (auto it = std::prev(entries_.end());; --it) {
      if (it == entries_.begin()) break;
      if (!isProtected || !isProtected(it->key)) {
        victim = it;
        break;
      }
    }
    if (victim == entries_.end()) return;  // everything left is protected
    bytes_ -= victim->bytes;
    index_.erase({victim->key, victim->bucket});
    entries_.erase(victim);
  }
}

void ThumbnailCache::erase(uint64_t key) {
  for (const uint32_t b : kBuckets) {
    const auto it = index_.find({key, b});
    if (it == index_.end()) continue;
    bytes_ -= it->second->bytes;
    entries_.erase(it->second);
    index_.erase(it);
  }
}

void ThumbnailCache::clear() {
  index_.clear();
  entries_.clear();
  bytes_ = 0;
}

// ---- ThumbnailScheduler -----------------------------------------------------------------------

bool ThumbnailScheduler::pump(std::span<const uint64_t> wanted, uint32_t bucket, const std::function<bool(uint64_t)>& isProtected,
                              const std::function<double()>& nowMs) {
  if (provider_ == nullptr) return false;
  const double start = nowMs ? nowMs() : 0.0;
  size_t processed = 0;
  bool remaining = false;
  for (const uint64_t key : wanted) {
    if (failed_.count(key) != 0) continue;
    if (cache_->find(key, bucket, false) != nullptr) continue;
    // At least one request per pass guarantees progress; after that the budget decides.
    if (processed > 0 && (processed >= config_.maxPerPump || (nowMs && nowMs() - start >= config_.budgetMs))) {
      remaining = true;
      break;
    }
    ThumbnailImage image;
    const ThumbnailStatus status = provider_->produce(key, bucket, image);
    ++processed;
    ++requested_;
    switch (status) {
      case ThumbnailStatus::Ready:
        if (!cache_->insert(key, bucket, image, isProtected)) failed_.insert(key);
        break;
      case ThumbnailStatus::Pending: remaining = true; break;
      case ThumbnailStatus::Failed: failed_.insert(key); break;
    }
  }
  return remaining;
}

void ThumbnailScheduler::invalidate(uint64_t key) {
  failed_.erase(key);
  cache_->erase(key);
}

void ThumbnailScheduler::invalidateAll() {
  failed_.clear();
  cache_->clear();
}

}  // namespace r1ui::widgets::thumbs

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for the thumbnail pipeline (ThumbnailCache.h) without a GPU: LRU eviction against
//   the 4096 entry default and the byte budget, protected keys, replacement of a picture, texture
//   failures, and the scheduler: visible keys first, the 5 ms budget and per-pass cap, Pending and
//   Failed handling, invalidate / notifyReady, and that the texture count equals the cache size (no
//   leak) after every operation.
// Callers: CTest (thumbnailgrid, fast tier, no GPU).
#include <chrono>
#include <random>
#include <unordered_set>

#include "TestSupport.h"
#include "r1ui/widgets/thumbnailgrid/ThumbnailCache.h"

namespace {

using namespace r1ui::widgets::thumbs;

ThumbnailImage image(uint32_t edge = 4) {
  ThumbnailImage img;
  img.width = img.height = edge;
  img.rgba.assign(static_cast<size_t>(edge) * edge * 4, 200);
  return img;
}

const auto kNothingProtected = [](uint64_t) { return false; };

void testEvictionOrder() {
  MemoryThumbnailTextures sink;
  ThumbnailCache cache(sink, CacheLimits{3, size_t{1} << 30});
  R1_EXPECT(cache.insert(1, 64, image(), kNothingProtected) && cache.insert(2, 64, image(), kNothingProtected) && cache.insert(3, 64, image(), kNothingProtected));
  R1_EXPECT(cache.size() == 3 && sink.alive() == 3);
  R1_EXPECT(cache.find(1, 64, false) != nullptr);  // touching 1 makes 2 the least recently used
  R1_EXPECT(cache.insert(4, 64, image(), kNothingProtected));
  R1_EXPECT(cache.size() == 3 && !cache.contains(2) && cache.contains(1) && cache.contains(3) && cache.contains(4) && sink.alive() == 3);
  // Replacing a picture does not grow the cache and releases the old texture.
  R1_EXPECT(cache.insert(3, 64, image(8), kNothingProtected) && cache.size() == 3 && sink.alive() == 3 && cache.find(3, 64, false)->texture->width() == 8);
  // A second size bucket of the same key is a second entry; anyBucket finds the other one.
  R1_EXPECT(cache.insert(4, 128, image(), kNothingProtected));
  R1_EXPECT(cache.size() == 3 && sink.alive() == 3);
  R1_EXPECT(cache.find(4, 256, true) != nullptr && cache.find(4, 256, false) == nullptr && cache.find(99, 64, true) == nullptr);
  cache.erase(4);
  R1_EXPECT(!cache.contains(4) && cache.size() == sink.alive());
  cache.clear();
  R1_EXPECT(cache.size() == 0 && sink.alive() == 0 && cache.bytes() == 0);
}

void testProtectedKeys() {
  MemoryThumbnailTextures sink;
  ThumbnailCache cache(sink, CacheLimits{4, size_t{1} << 30});
  std::unordered_set<uint64_t> keep = {1, 2};
  const auto isProtected = [&](uint64_t k) { return keep.count(k) != 0; };
  for (uint64_t k = 1; k <= 8; ++k) cache.insert(k, 64, image(), isProtected);
  // 1 and 2 are the oldest but protected: they stay, the others make way.
  R1_EXPECT(cache.contains(1) && cache.contains(2) && cache.contains(8) && cache.size() == 4 && sink.alive() == 4);
  // When everything is protected the cache exceeds its limit rather than dropping a visible picture.
  keep = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (uint64_t k = 9; k <= 10; ++k) cache.insert(k, 64, image(), isProtected);
  R1_EXPECT(cache.size() == 6 && sink.alive() == 6);
  keep.clear();
  cache.insert(11, 64, image(), isProtected);
  R1_EXPECT(cache.size() == 4 && sink.alive() == 4 && cache.contains(11));
}

void testDefaultLimit() {
  MemoryThumbnailTextures sink;
  ThumbnailCache cache(sink);  // 4096 entries (rule 22)
  for (uint64_t k = 0; k < 10000; ++k) cache.insert(k, 64, image(2), kNothingProtected);
  R1_EXPECT(cache.size() == 4096 && sink.alive() == 4096 && cache.contains(9999) && !cache.contains(5903) && cache.contains(5904));
  // Items touched recently survive a long scroll: touch the first still-cached key, then insert more.
  cache.find(5904, 64, false);
  for (uint64_t k = 10000; k < 10100; ++k) cache.insert(k, 64, image(2), kNothingProtected);
  R1_EXPECT(cache.contains(5904) && !cache.contains(5905));
}

void testByteBudget() {
  MemoryThumbnailTextures sink;
  ThumbnailCache cache(sink, CacheLimits{1000, 64 * 64 * 4 * 3});  // room for three 64 x 64 pictures
  for (uint64_t k = 1; k <= 6; ++k) cache.insert(k, 64, image(64), kNothingProtected);
  R1_EXPECT(cache.size() == 3 && cache.bytes() == 3 * 64 * 64 * 4 && sink.alive() == 3 && cache.contains(6) && !cache.contains(3));
  // A picture bigger than the whole budget is refused and changes nothing.
  R1_EXPECT(!cache.insert(100, 256, image(256), kNothingProtected) && cache.size() == 3);
  // One big picture evicts as many small ones as needed.
  ThumbnailCache big(sink, CacheLimits{1000, 128 * 128 * 4 + 64 * 64 * 4});
  for (uint64_t k = 1; k <= 4; ++k) big.insert(k, 64, image(64), kNothingProtected);
  R1_EXPECT(big.insert(50, 128, image(128), kNothingProtected) && big.contains(50) && big.bytes() <= 128 * 128 * 4 + 64 * 64 * 4);
}

void testInvalidAndFailing() {
  MemoryThumbnailTextures sink;
  ThumbnailCache cache(sink, CacheLimits{10, size_t{1} << 30});
  ThumbnailImage bad;
  R1_EXPECT(!cache.insert(1, 64, bad, kNothingProtected));
  bad.width = 4;
  bad.height = 4;
  bad.rgba.assign(10, 0);  // wrong byte count
  R1_EXPECT(!cache.insert(1, 64, bad, kNothingProtected));
  ThumbnailImage huge;
  huge.width = 100000;
  huge.height = 100000;
  R1_EXPECT(!cache.insert(1, 64, huge, kNothingProtected));
  sink.failNext(true);
  R1_EXPECT(!cache.insert(2, 64, image(), kNothingProtected) && cache.size() == 0 && sink.alive() == 0);
  R1_EXPECT(cache.insert(2, 64, image(), kNothingProtected) && cache.size() == 1);
  ThumbnailCache none(sink, CacheLimits{0, 100});
  R1_EXPECT(!none.insert(1, 64, image(), kNothingProtected));
}

class ScriptedProvider final : public ThumbnailProvider {
 public:
  ThumbnailStatus produce(uint64_t key, uint32_t sizePx, ThumbnailImage& out) override {
    calls.push_back(key);
    lastSize = sizePx;
    const auto it = statuses.find(key);
    const ThumbnailStatus s = it == statuses.end() ? ThumbnailStatus::Ready : it->second;
    if (s == ThumbnailStatus::Ready) out = image();
    clockMs += costMs;
    return s;
  }
  std::vector<uint64_t> calls;
  std::unordered_map<uint64_t, ThumbnailStatus> statuses;
  double clockMs = 0.0;
  double costMs = 0.0;
  uint32_t lastSize = 0;
};

void testScheduler() {
  MemoryThumbnailTextures sink;
  ThumbnailCache cache(sink);
  ScriptedProvider provider;
  ThumbnailScheduler sched(cache, SchedulerConfig{5.0, 32});
  sched.setProvider(&provider);
  const auto now = [&] { return provider.clockMs; };
  const auto none = [](uint64_t) { return false; };
  // Keys are requested in the order given (visible first) and only once.
  std::vector<uint64_t> wanted = {10, 11, 12, 3, 4};
  R1_EXPECT(!sched.pump(wanted, 128, none, now));
  R1_EXPECT(provider.calls == wanted && provider.lastSize == 128 && cache.size() == 5);
  provider.calls.clear();
  R1_EXPECT(!sched.pump(wanted, 128, none, now) && provider.calls.empty());  // all present
  // A different size bucket is asked for again.
  R1_EXPECT(!sched.pump({wanted.data(), 2}, 256, none, now) && provider.calls.size() == 2 && provider.lastSize == 256);

  // The time budget: with 2 ms per request about three requests fit in 5 ms (the budget is checked
  // before each request, the first always runs), the rest wait for the next pass.
  provider.calls.clear();
  provider.costMs = 2.0;
  std::vector<uint64_t> many;
  for (uint64_t k = 100; k < 120; ++k) many.push_back(k);
  R1_EXPECT(sched.pump(many, 64, none, now));
  R1_EXPECT(provider.calls.size() == 3 && provider.calls[0] == 100);
  R1_EXPECT(sched.pump(many, 64, none, now) && provider.calls.size() == 6 && provider.calls[3] == 103);
  // The per-pass cap holds even when requests are free.
  provider.costMs = 0.0;
  provider.calls.clear();
  std::vector<uint64_t> lots;
  for (uint64_t k = 1000; k < 1100; ++k) lots.push_back(k);
  R1_EXPECT(sched.pump(lots, 64, none, now) && provider.calls.size() == 32);
  // Even a request that takes longer than the whole budget completes (progress is guaranteed).
  provider.costMs = 50.0;
  provider.calls.clear();
  R1_EXPECT(sched.pump(std::vector<uint64_t>{2000, 2001}, 64, none, now) && provider.calls.size() == 1);

  // Pending keeps being asked, Failed is not asked again until invalidated, notifyReady clears Failed.
  provider.costMs = 0.0;
  provider.statuses[7] = ThumbnailStatus::Pending;
  provider.statuses[8] = ThumbnailStatus::Failed;
  provider.calls.clear();
  R1_EXPECT(sched.pump(std::vector<uint64_t>{7, 8}, 64, none, now));
  R1_EXPECT(sched.failed(8) && !sched.failed(7));
  provider.calls.clear();
  R1_EXPECT(sched.pump(std::vector<uint64_t>{7, 8}, 64, none, now) && provider.calls == std::vector<uint64_t>({7}));
  provider.statuses[7] = ThumbnailStatus::Ready;
  R1_EXPECT(!sched.pump(std::vector<uint64_t>{7, 8}, 64, none, now) && cache.contains(7));
  provider.statuses[8] = ThumbnailStatus::Ready;
  provider.calls.clear();
  sched.pump(std::vector<uint64_t>{8}, 64, none, now);
  R1_EXPECT(provider.calls.empty());  // still failed
  sched.invalidate(8);
  R1_EXPECT(!sched.failed(8));
  sched.pump(std::vector<uint64_t>{8}, 64, none, now);
  R1_EXPECT(cache.contains(8));
  // invalidate drops the picture: it is produced again (rule 24).
  provider.calls.clear();
  sched.invalidate(7);
  R1_EXPECT(!cache.contains(7));
  sched.pump(std::vector<uint64_t>{7}, 64, none, now);
  R1_EXPECT(provider.calls.size() == 1 && cache.contains(7));
  sched.notifyReady(12345);
  sched.invalidateAll();
  R1_EXPECT(cache.size() == 0 && sink.alive() == 0);
  // No provider: nothing happens and nothing is reported pending.
  ThumbnailScheduler idle(cache);
  R1_EXPECT(!idle.pump(std::vector<uint64_t>{1, 2, 3}, 64, none, now));
  // A texture failure marks the key failed instead of retrying forever.
  sink.failNext(true);
  provider.calls.clear();
  sched.pump(std::vector<uint64_t>{900}, 64, none, now);
  R1_EXPECT(sched.failed(900) && !cache.contains(900));
}

void testNoLeakUnderChurn() {
  MemoryThumbnailTextures sink;
  ThumbnailCache cache(sink, CacheLimits{100, size_t{1} << 30});
  std::mt19937 rng(1);
  for (int i = 0; i < 20000; ++i) {
    const uint64_t k = rng() % 500;
    switch (rng() % 5) {
      case 0: cache.erase(k); break;
      case 1: cache.find(k, 64, rng() % 2 == 0); break;
      case 2: cache.insert(k, (rng() % 2) ? 64u : 128u, image(2), [&](uint64_t p) { return p % 10 == 0 && rng() % 2 == 0; }); break;
      default: cache.insert(k, 64, image(2), kNothingProtected); break;
    }
    if (sink.alive() != cache.size()) {
      R1_EXPECT(false);
      break;
    }
  }
  R1_EXPECT(cache.size() <= 100 + 50 && sink.alive() == cache.size());
  cache.clear();
  R1_EXPECT(sink.alive() == 0);
}

}  // namespace

int main() {
  testEvictionOrder();
  testProtectedKeys();
  testDefaultLimit();
  testByteBudget();
  testInvalidAndFailing();
  testScheduler();
  testNoLeakUnderChurn();
  return r1test::finish();
}

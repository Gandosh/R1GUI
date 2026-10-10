// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of the brush library's pictures (slice 5.22): only the visible tiles (plus a margin) ask the
//   provider, a Pending picture shows the icon until notifyReady, a failed one keeps the icon, the cache is
//   bounded while scrolling through 2000 brushes, reopening the library reuses the pictures, and an idle
//   popup asks for no more frames.
// Callers: CTest (fast; the textures are in memory).
#include <set>
#include <unordered_set>

#include "BrushFixture.h"
#include "r1ui/widgets/brushes/BrushThumbnails.h"

using namespace r1test;
namespace th = r1ui::widgets::thumbs;

namespace {

class FakeProvider final : public th::ThumbnailProvider {
 public:
  th::ThumbnailStatus produce(uint64_t key, uint32_t, th::ThumbnailImage& out) override {
    ++calls;
    keys.insert(key);
    if (failing.count(key) != 0) return th::ThumbnailStatus::Failed;
    if (pending.count(key) != 0) return th::ThumbnailStatus::Pending;
    out.width = 4;
    out.height = 4;
    out.rgba.assign(4 * 4 * 4, 200);
    return th::ThumbnailStatus::Ready;
  }
  size_t calls = 0;
  std::set<uint64_t> keys;
  std::unordered_set<uint64_t> pending;
  std::unordered_set<uint64_t> failing;
};

void pumpFrames(BrushFixture& f, int count) {
  for (int i = 0; i < count; ++i) f.paint();
}

}  // namespace

int main() {
  // Only what is shown (plus a margin) is requested; pictures arrive within a few frames.
  {
    BrushFixture f(manyBrushes(2000));
    FakeProvider provider;
    th::MemoryThumbnailTextures sink;
    f.controller->setThumbnails(&provider, &sink);
    R1_EXPECT(f.controller->thumbnails() != nullptr);
    f.movePointer(450, 350);
    f.pressOpen();
    pumpFrames(f, 6);
    R1_EXPECT(provider.calls > 0 && provider.calls < 100);
    R1_EXPECT(f.controller->thumbnails()->cached() == provider.calls && sink.alive() == provider.calls);
    // Idle once everything shown has its picture: no more frames are requested.
    R1_EXPECT(!f.ui().needsFrame());
    const size_t callsWhenIdle = provider.calls;
    pumpFrames(f, 3);
    R1_EXPECT(provider.calls == callsWhenIdle);
    for (const auto& tile : f.popup()->result().tiles) {
      if (f.popup()->tileRect(static_cast<size_t>(&tile - f.popup()->result().tiles.data())).w > 0.0) {
        R1_EXPECT(f.controller->thumbnails()->find(f.model.thumbnailKey(tile.brush), 64) != nullptr);
      }
    }

    // Scrolling through the whole library keeps the cache inside its limit and frees evicted textures.
    for (int i = 0; i < 100; ++i) {
      f.press(Key::PageDown);
      f.paint();
    }
    f.press(Key::End);
    pumpFrames(f, 6);
    R1_EXPECT(f.popup()->highlight() == 1999);
    R1_EXPECT(f.controller->thumbnails()->cached() <= 1024 && sink.alive() == f.controller->thumbnails()->cached());
    R1_EXPECT(provider.calls > 400 && provider.calls < 2400);

    // Reopening reuses the pictures: the provider is not asked again for what is cached.
    f.press(Key::Home);
    pumpFrames(f, 4);
    f.press(Key::Escape);
    const size_t before = provider.calls;
    f.movePointer(450, 350);
    f.pressOpen();
    pumpFrames(f, 4);
    R1_EXPECT(provider.calls <= before + 100);
    f.press(Key::Escape);
    // Without a source the tiles show icons only.
    f.controller->setThumbnails(nullptr, nullptr);
    f.pressOpen();
    pumpFrames(f, 2);
    R1_EXPECT(f.controller->thumbnails() == nullptr && f.open());
    f.press(Key::Escape);
  }

  // Pending shows the icon until notifyReady; Failed keeps the icon.
  {
    BrushFixture f(sampleBrushes());
    FakeProvider provider;
    th::MemoryThumbnailTextures sink;
    const uint64_t pendingKey = f.model.thumbnailKey(*f.model.indexOfId("blob"));
    const uint64_t failedKey = f.model.thumbnailKey(*f.model.indexOfId("clay"));
    provider.pending.insert(pendingKey);
    provider.failing.insert(failedKey);
    f.controller->setThumbnails(&provider, &sink);
    f.pressOpen();
    pumpFrames(f, 4);
    R1_EXPECT(f.controller->thumbnails()->find(pendingKey, 64) == nullptr);
    R1_EXPECT(f.controller->thumbnails()->find(failedKey, 64) == nullptr && f.controller->thumbnails()->failed(failedKey));
    R1_EXPECT(f.controller->thumbnails()->find(f.model.thumbnailKey(*f.model.indexOfId("trim")), 64) != nullptr);
    provider.pending.clear();
    f.controller->thumbnails()->notifyReady(pendingKey);
    pumpFrames(f, 3);
    R1_EXPECT(f.controller->thumbnails()->find(pendingKey, 64) != nullptr);
    // A failed picture is asked for again only after invalidate().
    provider.failing.clear();
    const size_t calls = provider.calls;
    pumpFrames(f, 3);
    R1_EXPECT(provider.calls == calls);
    f.controller->thumbnails()->invalidate(failedKey);
    pumpFrames(f, 3);
    R1_EXPECT(f.controller->thumbnails()->find(failedKey, 64) != nullptr);
  }

  // The buckets follow the display scale.
  R1_EXPECT(r1ui::widgets::BrushThumbnailSource::bucketFor(1.0) == 64);
  R1_EXPECT(r1ui::widgets::BrushThumbnailSource::bucketFor(1.5) == 128);
  R1_EXPECT(r1ui::widgets::BrushThumbnailSource::bucketFor(2.0) == 128);
  R1_EXPECT(r1ui::widgets::BrushThumbnailSource::bucketFor(3.0) == 256);
  R1_EXPECT(r1ui::widgets::BrushThumbnailSource::bucketFor(0.0) == 64);
  R1_EXPECT(r1ui::widgets::BrushThumbnailSource::bucketFor(-4.0) == 64);
  return r1test::finish();
}

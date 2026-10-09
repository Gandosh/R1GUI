// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for the bounded event queue (drop oldest, drop count) and the DPI conversion
//   helpers, including NaN / infinite / saturating inputs.
// Callers: CTest (label fast). Exit code 0 = pass.
#include <cmath>
#include <limits>

#include "TestSupport.h"
#include "r1ui/platform/BoundedQueue.h"
#include "r1ui/platform/Dpi.h"
#include "r1ui/platform/Events.h"
#include "r1ui/platform/Window.h"

using namespace r1ui::platform;
using platform_test::expect;
using platform_test::runCase;

namespace {

void queueDropsOldest() {
  BoundedQueue<int> q(3);
  for (int i = 1; i <= 5; ++i) q.push(i);
  expect(q.size() == 3 && q.dropped() == 2, "two oldest dropped");
  const auto items = q.drain();
  expect(items.size() == 3 && items[0] == 3 && items[1] == 4 && items[2] == 5, "newest three kept in order");
  expect(q.size() == 0 && q.dropped() == 2, "drain empties but keeps the drop counter");
  expect(q.back() == nullptr, "empty queue has no back");
  q.push(9);
  expect(q.back() != nullptr && *q.back() == 9, "back exposes the newest entry");
}

void queueBoundHoldsUnderFlood() {
  BoundedQueue<Event> q(kMaxQueuedWindowEvents);
  for (int i = 0; i < 100000; ++i) {
    Event e;
    e.virtualKey = static_cast<uint32_t>(i);
    q.push(e);
    if (q.size() > q.capacity()) {
      expect(false, "size exceeded capacity");
      return;
    }
  }
  expect(q.dropped() == 100000 - q.capacity(), "every overflow counted");
  const auto items = q.drain();
  expect(items.front().virtualKey == 100000 - q.capacity(), "oldest survivor is the right one");
  expect(items.back().virtualKey == 99999, "newest survivor is the last pushed");
}

void queueZeroCapacityIsOne() {
  BoundedQueue<int> q(0);
  q.push(1);
  q.push(2);
  expect(q.capacity() == 1 && q.size() == 1 && q.dropped() == 1 && *q.back() == 2, "capacity 0 behaves as 1");
}

// Event queue policy: a flood of moves and wheel events never evicts the events that close an
// interaction, and the queue stays bounded.
void rankedQueueKeepsClosingEvents() {
  const auto ev = [](EventType t, uint32_t tag) {
    Event e;
    e.type = t;
    e.virtualKey = tag;
    return e;
  };
  BoundedQueue<Event> q(8, &eventRank);
  q.push(ev(EventType::MouseDown, 1));
  q.push(ev(EventType::KeyDown, 2));
  for (uint32_t i = 0; i < 1000; ++i) q.push(ev(i % 2 == 0 ? EventType::Wheel : EventType::MouseMove, 100 + i));
  q.push(ev(EventType::MouseUp, 3));
  q.push(ev(EventType::KeyUp, 4));
  q.push(ev(EventType::FocusLost, 5));
  q.push(ev(EventType::CaptureLost, 6));
  q.push(ev(EventType::CloseRequested, 7));
  for (uint32_t i = 0; i < 1000; ++i) q.push(ev(EventType::Wheel, 2000 + i));
  expect(q.size() <= 8, "bounded by capacity while coalescible entries exist");
  const auto items = q.drain();
  const auto has = [&](uint32_t tag) {
    for (const Event& e : items) {
      if (e.virtualKey == tag) return true;
    }
    return false;
  };
  expect(has(1) && has(2), "ordinary presses outrank wheel and moves");
  expect(has(3) && has(4) && has(5) && has(6) && has(7), "release, focus, capture and close events survive");
  expect(q.dropped() > 1900 && q.droppedPinned() == 0, "only coalescible entries were dropped");

  // Only pinned entries: the queue may grow, but only up to a bound.
  BoundedQueue<Event> pinned(4, &eventRank);
  for (uint32_t i = 0; i < 4 * kPinnedGrowth + 50; ++i) pinned.push(ev(EventType::KeyUp, i));
  expect(pinned.size() == 4 * kPinnedGrowth, "pinned growth is bounded");
  expect(pinned.droppedPinned() == 50, "beyond the bound the oldest pinned entries go and are counted");
  const auto kept = pinned.drain();
  expect(kept.front().virtualKey == 50 && kept.back().virtualKey == 4 * kPinnedGrowth + 49, "newest pinned entries kept in order");

  // A move arriving when only presses are queued is the one dropped, not an older press.
  BoundedQueue<Event> presses(2, &eventRank);
  presses.push(ev(EventType::KeyDown, 1));
  presses.push(ev(EventType::KeyDown, 2));
  presses.push(ev(EventType::MouseMove, 3));
  const auto p = presses.drain();
  expect(p.size() == 2 && p[0].virtualKey == 1 && p[1].virtualKey == 2 && presses.dropped() == 1, "incoming move loses against queued presses");
  // Auto-repeat ranks as coalescible.
  Event repeat = ev(EventType::KeyDown, 9);
  repeat.repeat = true;
  expect(eventRank(repeat) == kRankCoalescible && eventRank(ev(EventType::KeyDown, 9)) == kRankOrdinary, "repeat is coalescible");
}

void scaleHelpers() {
  expect(dpiScaleFromDpi(96) == 1.0f && dpiScaleFromDpi(144) == 1.5f && dpiScaleFromDpi(192) == 2.0f, "96/144/192 dpi");
  expect(dpiScaleFromDpi(0) == 1.0f, "unknown dpi is 1.0");
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  expect(sanitizeScale(nan) == 1.0f && sanitizeScale(inf) == 1.0f && sanitizeScale(-2.0f) == 1.0f &&
             sanitizeScale(0.0f) == 1.0f,
         "non-finite and non-positive scales become 1.0");
  expect(sanitizeScale(1.25f) == 1.25f, "good scale unchanged");
}

void logicalToPhysicalRounding() {
  expect(logicalToPhysical(6.0f, 1.0f) == 6, "6 at 100%");
  expect(logicalToPhysical(6.0f, 1.5f) == 9, "6 at 150%");
  expect(logicalToPhysical(6.0f, 1.25f) == 8, "7.5 rounds half away from zero");
  expect(logicalToPhysical(-6.0f, 1.25f) == -8, "-7.5 rounds away from zero");
  expect(logicalToPhysical(100.0f, 2.0f) == 200, "100 at 200%");
  expect(logicalToPhysical(10.0f, std::numeric_limits<float>::quiet_NaN()) == 10, "NaN scale falls back to 1.0");
  expect(logicalToPhysical(std::numeric_limits<float>::quiet_NaN(), 1.0f) == 0, "NaN logical is 0");
  expect(logicalToPhysical(1e30f, 2.0f) == std::numeric_limits<int>::max(), "saturates high");
  expect(logicalToPhysical(-1e30f, 2.0f) == std::numeric_limits<int>::min(), "saturates low");
  expect(logicalToPhysical(std::numeric_limits<float>::infinity(), 1.0f) == std::numeric_limits<int>::max(),
         "infinity saturates");
  expect(physicalToLogical(150, 1.5f) == 100.0f, "150 px at 150% is 100 logical");
  expect(physicalToLogical(150, 0.0f) == 150.0f, "zero scale falls back to 1.0");
}

}  // namespace

int main() {
  runCase("queue_drops_oldest", queueDropsOldest);
  runCase("queue_bound_holds_under_flood", queueBoundHoldsUnderFlood);
  runCase("queue_zero_capacity", queueZeroCapacityIsOne);
  runCase("ranked_queue_keeps_closing_events", rankedQueueKeepsClosingEvents);
  runCase("scale_helpers", scaleHelpers);
  runCase("logical_to_physical", logicalToPhysicalRounding);
  return platform_test::finish("ui-platform queue/dpi test");
}

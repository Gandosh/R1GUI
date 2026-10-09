// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of the one-shot timer service of UiContext (setTimer, cancelTimer, tick, msUntilTick):
//   due ordering, cancellation (also from another timer's callback), timers set from a callback,
//   the msUntilTick contribution, delays that would overflow the clock, the table limit and an
//   empty callback.
// Callers: CTest (label fast).
#include <limits>

#include "TestSupport.h"

namespace {

using namespace r1ui::widgets;

void testOrderAndCancel() {
  r1test::TestUi t(100, 100);
  std::vector<int> order;
  t.ui.setTime(1000);
  const auto a = t.ui.setTimer(300, [&] { order.push_back(3); });
  t.ui.setTimer(100, [&] { order.push_back(1); });
  t.ui.setTimer(200, [&] { order.push_back(2); });
  R1_EXPECT(a != 0);
  R1_EXPECT(t.ui.msUntilTick() == 100);
  t.ui.setTime(1099);
  R1_EXPECT(!t.ui.tick() && order.empty());
  t.ui.setTime(1500);
  R1_EXPECT(t.ui.tick());
  R1_EXPECT((order == std::vector<int>{1, 2, 3}));  // due order, not creation order
  R1_EXPECT(!t.ui.msUntilTick().has_value());
  // Cancel before it is due; cancelling twice or an unknown id reports false.
  const auto b = t.ui.setTimer(50, [&] { order.push_back(9); });
  R1_EXPECT(t.ui.cancelTimer(b) && !t.ui.cancelTimer(b) && !t.ui.cancelTimer(0) && !t.ui.cancelTimer(123456));
  t.ui.setTime(2000);
  t.ui.tick();
  R1_EXPECT(order.size() == 3);
}

void testCallbacks() {
  r1test::TestUi t(100, 100);
  t.ui.setTime(0);
  // A callback cancels another timer that is due in the same tick: the other one does not run.
  int ran = 0;
  UiContext::TimerId second = 0;
  t.ui.setTimer(10, [&] { t.ui.cancelTimer(second); });
  second = t.ui.setTimer(20, [&] { ++ran; });
  t.ui.setTime(100);
  t.ui.tick();
  R1_EXPECT(ran == 0);
  // A timer set by a callback with zero delay waits for the next tick (no endless loop in one tick).
  int chain = 0;
  std::function<void()> again = [&] {
    ++chain;
    if (chain < 5) t.ui.setTimer(0, again);
  };
  t.ui.setTimer(0, again);
  t.ui.tick();
  R1_EXPECT(chain == 1);
  R1_EXPECT(t.ui.msUntilTick() == 0);
  for (int i = 0; i < 10; ++i) t.ui.tick();
  R1_EXPECT(chain == 5);
}

void testLimits() {
  r1test::TestUi t(100, 100);
  t.ui.setTime(std::numeric_limits<uint64_t>::max() - 5);
  // A delay that would overflow the clock is clamped to "never", not wrapped into the past.
  int ran = 0;
  t.ui.setTimer(std::numeric_limits<uint64_t>::max(), [&] { ++ran; });
  t.ui.tick();
  R1_EXPECT(ran == 0);
  // An empty callback is refused; the table is bounded.
  R1_EXPECT(t.ui.setTimer(1, nullptr) == 0);
  r1test::TestUi many(100, 100);
  size_t made = 0;
  for (size_t i = 0; i < UiContext::kMaxTimers + 10; ++i) {
    if (many.ui.setTimer(1000000, [] {}) != 0) ++made;
  }
  R1_EXPECT(made == UiContext::kMaxTimers);
}

}  // namespace

int main() {
  testOrderAndCancel();
  testCallbacks();
  testLimits();
  return r1test::finish();
}

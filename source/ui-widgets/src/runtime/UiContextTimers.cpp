// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the one-shot timer service of UiContext (setTimer / cancelTimer, run from tick()).
// Why: menus (submenu open and close delays), toasts (auto dismiss) and other widgets need "do this
//   later" on the context clock without a thread or a polling widget; the shell already waits for
//   msUntilTick(), so a timer costs nothing while idle.
// Invariants: timers run ordered by due time then creation; a callback runs with its timer already
//   removed, so it may cancel or set timers; timers set during a tick run no earlier than the next
//   tick; the table never exceeds kMaxTimers.
// Callers: UiContext::tick / msUntilTick, widgets through UiContext::setTimer.
#include <algorithm>
#include <cstdint>
#include <exception>
#include <utility>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

UiContext::TimerId UiContext::setTimer(uint64_t delayMs, std::function<void()> callback) {
  if (!callback || timers_.size() >= kMaxTimers) return 0;
  const TimerId id = nextTimerId_++;
  if (nextTimerId_ == 0) nextTimerId_ = 1;  // the id space wrapped: 0 stays "no timer"
  // A delay that would overflow the clock is clamped to "very far away".
  const uint64_t due = delayMs > UINT64_MAX - nowMs_ ? UINT64_MAX : nowMs_ + delayMs;
  timers_.push_back(Timer{id, due, std::move(callback)});
  return id;
}

bool UiContext::cancelTimer(TimerId id) {
  if (id == 0) return false;
  const auto it = std::find_if(timers_.begin(), timers_.end(), [id](const Timer& t) { return t.id == id; });
  if (it == timers_.end()) return false;
  timers_.erase(it);
  return true;
}

bool UiContext::runDueTimers() {
  // The due set is fixed first so timers created by the callbacks wait for the next tick.
  std::vector<std::pair<uint64_t, TimerId>> due;
  for (const Timer& t : timers_) {
    if (t.dueMs <= nowMs_) due.emplace_back(t.dueMs, t.id);
  }
  if (due.empty()) return false;
  std::sort(due.begin(), due.end());
  bool ran = false;
  for (const auto& entry : due) {
    const TimerId id = entry.second;
    const auto it = std::find_if(timers_.begin(), timers_.end(), [id](const Timer& t) { return t.id == id; });
    if (it == timers_.end()) continue;  // cancelled by an earlier callback
    std::function<void()> callback = std::move(it->callback);
    timers_.erase(it);
    try {
      callback();
    } catch (const std::exception& e) {
      noteFault(e.what());
    } catch (...) {
      noteFault(nullptr);
    }
    ran = true;
  }
  return ran;
}

std::optional<uint64_t> UiContext::msUntilTimer() const {
  if (timers_.empty()) return std::nullopt;
  uint64_t earliest = UINT64_MAX;
  for (const Timer& t : timers_) earliest = std::min(earliest, t.dueMs);
  return earliest <= nowMs_ ? uint64_t{0} : earliest - nowMs_;
}

}  // namespace r1ui::widgets

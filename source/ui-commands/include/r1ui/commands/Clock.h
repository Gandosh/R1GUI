// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the time source of the command module (Clock) with a monotonic implementation (SteadyClock)
//   and a hand-driven one for tests and hosts that run on their own clock (ManualClock).
// Why: the sequence timeout of decision D14 (1.5 s) must be testable without sleeping and must run on
//   the same clock as the rest of a host's UI (the UiContext clock), so time is an interface.
// Callers: CommandRouter. Calls: <chrono> (SteadyClock only).
#pragma once

#include <chrono>
#include <cstdint>

namespace r1ui::commands {

class Clock {
 public:
  virtual ~Clock() = default;
  // Monotonic milliseconds; only differences are meaningful.
  virtual uint64_t nowMs() const = 0;
};

class SteadyClock final : public Clock {
 public:
  uint64_t nowMs() const override {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
  }
};

class ManualClock final : public Clock {
 public:
  uint64_t nowMs() const override { return now_; }
  void set(uint64_t ms) { now_ = ms; }
  void advance(uint64_t ms) { now_ += ms; }

 private:
  uint64_t now_ = 0;
};

}  // namespace r1ui::commands

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: AppLoop's step, live step and shutdown (see AppLoop.h for the protocol).
// Invariants: step() and liveStep() never run inside each other (flags), windows are freed only at
//   the point of step() that is outside every handler, a thrown error from a live step is stored and
//   surfaces from the next step() instead of unwinding through the OS window procedure, and the wait
//   uses the smallest delay any context reports so an idle application uses no CPU.
// Callers: applications, tests.
#include "r1ui/widgets/dock/native/AppLoop.h"

#include <algorithm>

#include "r1ui/core/CheckedCast.h"

namespace r1ui::widgets {

AppLoop::AppLoop(platform::Window& main, NativeFloatingBackend& backend, AppLoopHooks hooks)
    : main_(main), backend_(backend), hooks_(std::move(hooks)), started_(std::chrono::steady_clock::now()) {
  main_.setLiveCallback([this] { liveStep(); });
  backend_.setLiveCallback([this] { liveStep(); });
}

AppLoop::~AppLoop() {
  main_.setLiveCallback({});  // nothing may call back into a half-destroyed loop
  backend_.setLiveCallback({});
}

uint64_t AppLoop::nowMs() const {
  return core::checkedCast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_).count());
}

void AppLoop::drawAll(uint64_t now, bool& drew) {
  if (hooks_.mainNeedsFrame && hooks_.renderMain && main_.clientWidth() > 0 && main_.clientHeight() > 0 && hooks_.mainNeedsFrame()) {
    hooks_.renderMain(now);
    drew = true;
  }
  if (backend_.needsFrame()) {
    backend_.renderFrames(now);
    drew = true;
  }
}

bool AppLoop::step(bool block, unsigned maxWaitMs) {
  if (pending_ != nullptr) {
    const std::exception_ptr error = pending_;
    pending_ = nullptr;
    std::rethrow_exception(error);
  }
  if (inStep_) return true;  // never nested (a handler pumping messages itself)
  struct Guard {
    bool& flag;
    explicit Guard(bool& f) : flag(f) { flag = true; }
    ~Guard() { flag = false; }
  } guard(inStep_);

  // The OS move/size loop (and so liveStep) can only run inside pumpEvents(); while the step handles
  // events or draws, `busy_` keeps a live step out.
  if (!main_.pumpEvents()) return false;
  busy_ = true;
  struct Busy {
    bool& flag;
    ~Busy() { flag = false; }
  } busy{busy_};
  uint64_t now = nowMs();
  if (hooks_.processMain) hooks_.processMain(now);
  backend_.processEvents(now);
  backend_.collectGarbage();  // outside every handler: windows destroyed during the dispatch above go now
  if (hooks_.quitRequested && hooks_.quitRequested()) return false;

  bool drew = false;
  now = nowMs();
  drawAll(now, drew);
  if (drew || !block) return true;
  busy_ = false;

  std::optional<uint64_t> wake = backend_.msUntilTick(now);
  if (hooks_.mainMsUntilTick) {
    if (const std::optional<uint64_t> ms = hooks_.mainMsUntilTick(now)) wake = wake ? std::min(*wake, *ms) : *ms;
  }
  const unsigned scheduled = wake ? static_cast<unsigned>(std::min<uint64_t>(*wake, 0x7FFFFFFFu)) : platform::kWaitForever;
  ++idleWaits_;
  main_.waitForEvents(std::min(scheduled, maxWaitMs));
  return true;
}

void AppLoop::run() {
  while (step(true)) {
  }
}

// Runs inside the OS move/size loop of one window: the same work as a step, minus pumping (the OS is
// pumping) and minus freeing windows.
void AppLoop::liveStep() {
  if (inLive_ || busy_ || pending_ != nullptr) return;
  inLive_ = true;
  try {
    ++liveSteps_;
    uint64_t now = nowMs();
    if (hooks_.processMain) hooks_.processMain(now);
    backend_.processEvents(now);
    bool drew = false;
    now = nowMs();
    drawAll(now, drew);
  } catch (...) {
    pending_ = std::current_exception();
  }
  inLive_ = false;
}

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: AppLoop, the multi-window run loop: it pumps the OS messages of every window of the thread,
//   lets the application handle the main window's events, lets the native backend handle the
//   floating windows' events, draws every window that needs a frame, sleeps without spinning until
//   the next message or the nearest timer of any context, keeps all windows drawing during the OS
//   move/size modal loop, and collects the windows that were destroyed during dispatch.
// Why: PreviewApp has a single-window loop; with native floating windows the same protocol must run
//   over many windows and many UiContexts (docs/dev/widgets.md, multi-window rules) and nothing may
//   be destroyed while its own handler is on the stack.
// Callers: the application's main (run), tests and the manual harness (step). The application keeps
//   its own per-window code and gives AppLoop four callbacks.
// Protocol per step: main.pumpEvents() (dispatches the whole thread queue: every window records its
//   own events) -> hooks.processMain (the application drains the main window's events and applies
//   them) -> backend.processEvents (floating windows' events, listener calls, lost windows, pointer
//   tracking) -> backend.collectGarbage (windows destroyed during the dispatch above are freed now,
//   outside every handler) -> draw the main window if hooks.mainNeedsFrame, then the floating windows
//   -> when nothing was drawn and `block`: wait for a message for at most min(timers of all contexts,
//   pointer-tracking interval, maxWaitMs).
// Live callback: while Windows runs its own move/size loop (a title-bar drag, an edge resize) the
//   loop above is not running; every window calls liveStep() on size changes and a ~60 Hz timer, which
//   handles events and draws all windows but never frees windows (the OS loop still uses the one that
//   called) and never re-enters. Errors thrown there are stored and rethrown from the next step().
// Lifetime: destroy the AppLoop before the backend and the main window (it removes the live
//   callbacks in its destructor).
// Threading: UI thread only.
#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <optional>

#include "r1ui/platform/Window.h"
#include "r1ui/widgets/dock/native/NativeFloatingBackend.h"

namespace r1ui::widgets {

struct AppLoopHooks {
  // Drains the main window's events and applies them (the application's own input code). Called once
  // per step, always before the floating windows' events.
  std::function<void(uint64_t nowMs)> processMain;
  std::function<bool()> mainNeedsFrame;
  std::function<void(uint64_t nowMs)> renderMain;
  // Milliseconds until the main window's contexts have timer work; nullopt = none.
  std::function<std::optional<uint64_t>(uint64_t nowMs)> mainMsUntilTick;
  // True once the application wants to end (a quit command, Escape policy); optional.
  std::function<bool()> quitRequested;
};

class AppLoop {
 public:
  AppLoop(platform::Window& main, NativeFloatingBackend& backend, AppLoopHooks hooks);
  ~AppLoop();
  AppLoop(const AppLoop&) = delete;
  AppLoop& operator=(const AppLoop&) = delete;

  // One iteration (see the protocol above). Returns false when the main window closed or the
  // application asked to quit. Rethrows an error stored by the live step.
  bool step(bool block, unsigned maxWaitMs = platform::kWaitForever);
  void run();

  uint64_t nowMs() const;
  uint64_t liveSteps() const { return liveSteps_; }
  uint64_t idleWaits() const { return idleWaits_; }

 private:
  void liveStep();
  void drawAll(uint64_t now, bool& drew);

  platform::Window& main_;
  NativeFloatingBackend& backend_;
  AppLoopHooks hooks_;
  std::chrono::steady_clock::time_point started_;
  std::exception_ptr pending_;
  bool inLive_ = false;
  bool inStep_ = false;
  bool busy_ = false;  // a step is handling events or drawing: no live step may start
  uint64_t liveSteps_ = 0;
  uint64_t idleWaits_ = 0;
};

}  // namespace r1ui::widgets

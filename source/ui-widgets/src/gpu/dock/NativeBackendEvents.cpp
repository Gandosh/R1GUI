// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the loop side of NativeFloatingBackend: draining each floating window's platform events,
//   turning what the user or the OS did into listener calls (moved, resized, maximized, scale,
//   activated, close requested), noticing windows the OS destroyed, bringing unreachable windows
//   back after a display change, sampling the pointer while a tab drag tracks it, rendering and
//   scheduling.
// Invariants: a listener call is made only when the OS state differs from the state the backend
//   stored (the echo rule); the table is never iterated while the listener runs (ids are snapshotted
//   and every window is looked up again after each callback, since the listener may destroy any
//   window); a lost window is retired before the listener hears of it, so the id is already invalid
//   and the notification happens exactly once.
// Callers: AppLoop, tests (through the public loop-side methods of NativeFloatingBackend).
#include <algorithm>
#include <cmath>

#include "NativeWin32.h"
#include "NativeWindow.h"
#include "r1ui/platform/Monitors.h"
#include "r1ui/widgets/dock/native/NativeFloatingBackend.h"

namespace r1ui::widgets {

using native::NativeWindow;
using native::WindowChanges;

namespace {

// Rectangles closer than this (logical px) are the same: the OS works in whole physical pixels.
constexpr double kSameTolerance = 0.51;

bool sameRect(const dock::Rect& a, const dock::Rect& b) {
  return std::abs(a.x - b.x) < kSameTolerance && std::abs(a.y - b.y) < kSameTolerance && std::abs(a.w - b.w) < kSameTolerance &&
         std::abs(a.h - b.h) < kSameTolerance;
}

}  // namespace

// ---- the event pass ---------------------------------------------------------------------------------

void NativeFloatingBackend::processEvents(uint64_t nowMs) {
  // A moved main window changes where every screen coordinate lands; the host re-lays out on a layout
  // pass only, so ask for one.
  if (mainUi_ != nullptr && mainWidget_.valid() && mainUi_->alive(mainWidget_)) {
    const dock::Rect before = lastMain_;
    if (!sameRect(mainContentRect(), before)) {
      if (WidgetObject* widget = mainUi_->object(mainWidget_)) widget->requestLayout();
    }
  }
  for (const FloatId id : table_.order()) {
    NativeWindow* w = find(id);
    if (w == nullptr) continue;
    if (!w->window().isAlive()) {
      reportLost(id);
      continue;
    }
    const WindowChanges changes = w->processEvents(nowMs);
    handleChanges(id, changes);
  }
  pollTracking();
}

void NativeFloatingBackend::reportLost(FloatId window) {
  NativeWindow* w = find(window);
  if (w == nullptr) return;
  table_.retire(window);
  if (listener_ != nullptr) listener_->onFloatLost(window);
}

// What the user or the OS did to the window, in the order the host needs it: a scale change before the
// rectangle it implies, maximize before the rectangle of the maximized window, then close and raise.
void NativeFloatingBackend::handleChanges(FloatId window, const WindowChanges& changes) {
  NativeWindow* w = find(window);
  if (w == nullptr) return;
  if (changes.displayChanged) {
    onDisplayChanged();
    w = find(window);
    if (w == nullptr) return;
  }
  if (changes.dpi) {
    const double scale = w->scale();
    const bool hostDriven = w->expectedScale > 0.0 && std::abs(scale - w->expectedScale) < 1.0e-3;
    w->expectedScale = 0.0;
    if (!hostDriven && listener_ != nullptr) {
      listener_->onFloatScaleChanged(window, scale);
      w = find(window);
      if (w == nullptr) return;
    }
  }
  if (changes.geometry && !w->window().isMinimized()) {
    const bool maximized = w->window().isMaximized();
    const dock::Rect content = readContent(*w);
    const bool maximizeChanged = maximized != w->maximized;
    const bool rectChanged = !sameRect(content, w->content);
    w->maximized = maximized;
    w->content = content;
    if (maximizeChanged && listener_ != nullptr) {
      listener_->onFloatMaximizedChanged(window, maximized);
      w = find(window);
      if (w == nullptr) return;
    }
    if ((maximizeChanged || rectChanged) && listener_ != nullptr) {
      listener_->onFloatMoved(window, content);
      w = find(window);
      if (w == nullptr) return;
    }
  }
  if (changes.closeRequested && listener_ != nullptr) {
    listener_->onFloatCloseRequested(window);
    w = find(window);
    if (w == nullptr) return;
  }
  // A press raises the window. A focus event may be stale (the window got the focus at creation and
  // others were created since), so it counts only while the window still is the foreground window.
  if ((changes.pressed || (changes.focused && native::os::isForeground(w->window()))) && w->shown) {
    bool changed = false;
    table_.raise(window, &changed);
    if (changed && listener_ != nullptr) listener_->onFloatActivated(window);
  }
}

// Monitors came, went or moved: re-read them and bring every window that no longer shows at least the
// visible margin on a monitor back (owner decision, spec 03 rule 64). A moved window is reported like
// any move the OS made.
void NativeFloatingBackend::onDisplayChanged() {
  refreshMonitors();
  for (const FloatId id : table_.order()) {
    NativeWindow* w = find(id);
    if (w == nullptr || w->maximized || w->window().isMinimized() || !w->window().isAlive()) continue;
    const platform::Rect outer = w->window().windowRect();
    const platform::Rect fitted = platform::clampToVisible(outer, space_.monitors(), options_.visibleMarginLogical);
    if (fitted == outer) continue;
    w->window().setWindowRect(fitted);
    w->content = readContent(*w);
    if (listener_ != nullptr) listener_->onFloatMoved(id, w->content);
  }
}

// ---- pointer tracking ------------------------------------------------------------------------------------

// Delivers the pointer while the left button is held and exactly one release when it goes up, in screen
// logical pixels. The sample comes from the OS (or the test's source), not from any window's events,
// so it also works outside every window, over a hidden source window and after the capture moved. A
// button that was never seen down (input posted to a window instead of real input) delivers nothing:
// the strip's own events drive that drag.
void NativeFloatingBackend::pollTracking() {
  if (track_.sink == nullptr) return;
  IPointerSink* const sink = track_.sink;
  const PointerSample s = samplePointer();
  const bool moved = !track_.haveLast || !(s.physical == track_.last);
  const bool wasDown = track_.wasDown;
  track_.wasDown = s.leftDown;
  track_.haveLast = true;
  track_.last = s.physical;
  const dock::Point screen = space_.toLogical(s.physical.x, s.physical.y);
  if (s.leftDown) {
    if (moved || !wasDown) sink->onTrackedPointer(screen, true);
  } else if (wasDown) {
    sink->onTrackedPointer(screen, false);
  }
}

// ---- frames and scheduling ----------------------------------------------------------------------------------

bool NativeFloatingBackend::needsFrame() const {
  for (const FloatId id : table_.order()) {
    const NativeWindow* w = find(id);
    if (w != nullptr && w->needsFrame()) return true;
  }
  return false;
}

void NativeFloatingBackend::renderFrames(uint64_t nowMs) {
  for (const FloatId id : table_.order()) {
    if (NativeWindow* w = find(id)) w->render(nowMs);
  }
}

std::optional<uint64_t> NativeFloatingBackend::msUntilTick(uint64_t nowMs) {
  std::optional<uint64_t> soonest;
  const auto consider = [&](uint64_t ms) { soonest = soonest ? std::min(*soonest, ms) : ms; };
  if (track_.sink != nullptr) consider(options_.trackingIntervalMs);
  for (const FloatId id : table_.order()) {
    NativeWindow* w = find(id);
    if (w == nullptr || !w->shown) continue;
    if (const std::optional<uint64_t> ms = w->msUntilTick(nowMs)) consider(*ms);
  }
  return soonest;
}

void NativeFloatingBackend::setLiveCallback(std::function<void()> callback) {
  live_ = std::move(callback);
  for (const FloatId id : table_.order()) {
    if (NativeWindow* w = find(id)) w->window().setLiveCallback(live_);
  }
}

}  // namespace r1ui::widgets

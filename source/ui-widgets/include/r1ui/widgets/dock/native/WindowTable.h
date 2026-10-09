// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: WindowTable<W>, the bookkeeping of the native backend's windows independent of what a window
//   is: ids that are never reused, the stacking order (bottom to top), lookups, and deferred
//   destruction (retire now, destroy at a safe point).
// Why: a window can be destroyed while one of its own event handlers is on the stack (the dock drops
//   the last tab of a floating window onto the main window from that window's pointer-up handler and
//   then destroys the window). The table therefore removes a window from every lookup at once and
//   keeps the object alive in a parking list until sweep(), which the run loop calls after event
//   dispatch. The order of destruction is fixed and testable: parked windows first, in the order they
//   were retired; the live ones afterwards from the topmost down (see ~WindowTable).
// Callers: NativeFloatingBackend; tests use a fake W that records its destruction.
// Threading: UI thread only. W is held by unique_ptr, so pointers returned by find() stay valid until
//   the window is destroyed by sweep() or by the table's destructor.
#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "r1ui/widgets/dock/FloatingBackend.h"

namespace r1ui::widgets::native {

template <class W>
class WindowTable {
 public:
  WindowTable() = default;
  WindowTable(const WindowTable&) = delete;
  WindowTable& operator=(const WindowTable&) = delete;
  ~WindowTable() {
    for (std::unique_ptr<W>& w : parked_) w.reset();  // retirement order (clear() does not promise one)
    while (!live_.empty()) live_.pop_back();          // topmost first
  }

  // The id the next add() will give (so a window can be built knowing its id).
  FloatId nextId() const { return next_; }

  // Takes ownership; the window is on top. The id is never reused and never 0 (the main window).
  FloatId add(std::unique_ptr<W> window) {
    const FloatId id = next_++;
    live_.push_back({id, std::move(window)});
    return id;
  }

  W* find(FloatId id) {
    for (Entry& e : live_) {
      if (e.id == id) return e.window.get();
    }
    return nullptr;
  }
  const W* find(FloatId id) const {
    for (const Entry& e : live_) {
      if (e.id == id) return e.window.get();
    }
    return nullptr;
  }

  size_t size() const { return live_.size(); }
  size_t parkedCount() const { return parked_.size(); }

  // Ids bottom to top.
  std::vector<FloatId> order() const {
    std::vector<FloatId> ids;
    ids.reserve(live_.size());
    for (const Entry& e : live_) ids.push_back(e.id);
    return ids;
  }

  // Moves the window to the top. False for an unknown id; `changed` (when given) tells whether the
  // order actually changed (raising the top window is a no-op).
  bool raise(FloatId id, bool* changed = nullptr) {
    for (auto it = live_.begin(); it != live_.end(); ++it) {
      if (it->id != id) continue;
      const bool already = std::next(it) == live_.end();
      if (changed != nullptr) *changed = !already;
      if (!already) std::rotate(it, it + 1, live_.end());
      return true;
    }
    return false;
  }

  // Removes the window from the table; the object lives on in the parking list until sweep().
  // False for an unknown id (idempotent: retiring twice is harmless).
  bool retire(FloatId id) {
    for (auto it = live_.begin(); it != live_.end(); ++it) {
      if (it->id != id) continue;
      parked_.push_back(std::move(it->window));
      live_.erase(it);
      return true;
    }
    return false;
  }

  // Destroys the parked windows in retirement order. A destructor may retire further windows (the
  // list is taken first, so they wait for the next sweep).
  void sweep() {
    std::vector<std::unique_ptr<W>> doomed;
    doomed.swap(parked_);
    for (std::unique_ptr<W>& w : doomed) w.reset();
  }

 private:
  struct Entry {
    FloatId id = 0;
    std::unique_ptr<W> window;
  };
  std::vector<Entry> live_;
  std::vector<std::unique_ptr<W>> parked_;
  FloatId next_ = 1;
};

}  // namespace r1ui::widgets::native

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: a bounded FIFO with a drop policy and drop counters.
// Why: window event queues must not grow without bound if the application stops draining them
//   (guard register: leaks and memory growth), but losing the wrong entry corrupts input state:
//   a lost button/key release leaves a button or key stuck, a lost focus/capture loss leaves a
//   drag running. Entries therefore carry a rank, and overflow discards the lowest-ranked entry
//   first (the oldest of equal rank).
// Callers: the Win32 window backend (key, click, pointer and unified event queues), tests.
// Ranks: 0 = coalescible (moves, wheel, auto-repeat), 1 = ordinary, 2 = pinned (releases, focus,
//   capture, size, close: state-closing events). Pinned entries are never discarded to make room
//   for anything; when only pinned entries remain the queue grows past its capacity up to
//   kPinnedGrowth times (memory stays bounded), and only beyond that the oldest pinned entry is
//   dropped and counted in droppedPinned(). A queue without a rank function ranks everything 1,
//   which is plain "drop the oldest".
// Invariants: size() <= capacity() unless pinned entries forced growth; capacity is at least 1.
//   Single-threaded (UI thread).
#pragma once

#include <cstddef>
#include <deque>
#include <iterator>
#include <utility>
#include <vector>

namespace r1ui::platform {

inline constexpr int kRankCoalescible = 0;
inline constexpr int kRankOrdinary = 1;
inline constexpr int kRankPinned = 2;
inline constexpr size_t kPinnedGrowth = 16;

template <class T>
class BoundedQueue {
 public:
  using RankFn = int (*)(const T&);

  explicit BoundedQueue(size_t capacity, RankFn rank = nullptr)
      : capacity_(capacity == 0 ? 1 : capacity), rank_(rank) {}

  // Appends, discarding according to the rank policy when full (see the file header).
  void push(T item) {
    if (items_.size() >= capacity_ && !makeRoom(rankOf(item))) {
      ++dropped_;  // the incoming entry itself was the lowest-ranked candidate
      return;
    }
    items_.push_back(std::move(item));
  }

  // Newest entry, or nullptr when empty; lets a producer coalesce into it.
  T* back() { return items_.empty() ? nullptr : &items_.back(); }

  // Hands over all entries oldest first and leaves the queue empty. The drop counters persist.
  std::vector<T> drain() {
    std::vector<T> out(std::make_move_iterator(items_.begin()), std::make_move_iterator(items_.end()));
    items_.clear();
    return out;
  }

  size_t size() const { return items_.size(); }
  size_t capacity() const { return capacity_; }
  // Entries discarded for room, of any rank (includes droppedPinned()).
  size_t dropped() const { return dropped_; }
  // Pinned entries lost because the queue was full of them past the growth bound.
  size_t droppedPinned() const { return droppedPinned_; }

 private:
  int rankOf(const T& item) const { return rank_ == nullptr ? kRankOrdinary : rank_(item); }

  // Frees one slot for an entry of rank `incoming`; false when the incoming entry should be the
  // one dropped (every queued entry outranks it).
  bool makeRoom(int incoming) {
    if (rank_ == nullptr) {  // plain drop-oldest, no scan needed
      items_.pop_front();
      ++dropped_;
      return true;
    }
    int lowest = kRankPinned + 1;
    size_t victim = 0;
    for (size_t i = 0; i < items_.size(); ++i) {
      const int r = rankOf(items_[i]);
      if (r < lowest) {
        lowest = r;
        victim = i;
        if (lowest == kRankCoalescible) break;  // the oldest coalescible entry cannot be beaten
      }
    }
    if (lowest < kRankPinned) {
      if (lowest > incoming) return false;
      items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(victim));
      ++dropped_;
      return true;
    }
    // Everything queued is pinned.
    if (incoming < kRankPinned) return false;
    if (items_.size() < capacity_ * kPinnedGrowth) return true;
    items_.pop_front();
    ++dropped_;
    ++droppedPinned_;
    return true;
  }

  std::deque<T> items_;
  size_t capacity_;
  RankFn rank_;
  size_t dropped_ = 0;
  size_t droppedPinned_ = 0;
};

}  // namespace r1ui::platform

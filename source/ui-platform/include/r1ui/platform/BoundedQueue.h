// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: a bounded FIFO that drops the oldest entry when full and counts the drops.
// Why: window event queues must not grow without bound if the application stops draining them
//   (guard register: leaks and memory growth); dropping is visible through dropped().
// Callers: the Win32 window backend (key, click, pointer and unified event queues), tests.
// Invariants: size() <= capacity() always; capacity is at least 1. Single-threaded (UI thread).
#pragma once

#include <cstddef>
#include <deque>
#include <iterator>
#include <utility>
#include <vector>

namespace r1ui::platform {

template <class T>
class BoundedQueue {
 public:
  explicit BoundedQueue(size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

  // Appends; when full the oldest entry is discarded first.
  void push(T item) {
    if (items_.size() >= capacity_) {
      items_.pop_front();
      ++dropped_;
    }
    items_.push_back(std::move(item));
  }

  // Newest entry, or nullptr when empty; lets a producer coalesce into it.
  T* back() { return items_.empty() ? nullptr : &items_.back(); }

  // Hands over all entries oldest first and leaves the queue empty. The drop counter persists.
  std::vector<T> drain() {
    std::vector<T> out(std::make_move_iterator(items_.begin()), std::make_move_iterator(items_.end()));
    items_.clear();
    return out;
  }

  size_t size() const { return items_.size(); }
  size_t capacity() const { return capacity_; }
  size_t dropped() const { return dropped_; }

 private:
  std::deque<T> items_;
  size_t capacity_;
  size_t dropped_ = 0;
};

}  // namespace r1ui::platform

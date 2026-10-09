// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: integer point, size and rectangle value types shared by the platform API.
// Why: window rectangles, monitor rectangles and chrome regions need one plain, OS-neutral
//   representation; no Win32 type may appear in a public header.
// Callers: Window.h, Monitors.h, ChromeHitTest.h, Events.h and every module placing windows.
// Invariants: all values are physical pixels unless a field comment says otherwise. A rectangle
//   with width <= 0 or height <= 0 is empty and contains no point.
#pragma once

#include <cstdint>

namespace r1ui::platform {

struct Point {
  int x = 0;
  int y = 0;
  friend bool operator==(const Point&, const Point&) = default;
};

struct Size {
  int width = 0;
  int height = 0;
  friend bool operator==(const Size&, const Size&) = default;
};

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  bool empty() const { return width <= 0 || height <= 0; }
  // Half-open: left/top edges are inside, right/bottom edges are outside. 64-bit arithmetic so
  // x + width cannot overflow.
  bool contains(Point p) const {
    return !empty() && static_cast<int64_t>(p.x) >= x && static_cast<int64_t>(p.y) >= y &&
           static_cast<int64_t>(p.x) < static_cast<int64_t>(x) + width &&
           static_cast<int64_t>(p.y) < static_cast<int64_t>(y) + height;
  }
  friend bool operator==(const Rect&, const Rect&) = default;
};

}  // namespace r1ui::platform

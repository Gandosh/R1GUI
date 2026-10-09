// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: monitor description, enumeration, and the rule that keeps a window reachable.
// Why: floating panels are saved and restored across sessions and monitor setups; a restored
//   window must never be stranded off-screen (floating-window spec).
// Callers: window placement code in ui-dock and the preview; tests use synthetic monitor lists.
// Invariants: all rectangles are physical pixels in virtual-screen coordinates. clampToVisible is
//   pure (no OS calls) and never throws; enumerateMonitors is the only OS-touching function.
#pragma once

#include <span>
#include <vector>

#include "r1ui/platform/Geometry.h"

namespace r1ui::platform {

struct MonitorInfo {
  Rect bounds;            // full monitor rectangle
  Rect workArea;          // bounds minus taskbar and docked bars
  float dpiScale = 1.0f;  // 1.0 = 96 dpi
  bool primary = false;
};

// Current monitors, primary first. Empty only if the OS reports none (non-interactive session).
std::vector<MonitorInfo> enumerateMonitors();

inline constexpr int kVisibleMarginLogical = 100;

// A window is visible enough when it overlaps some monitor's work area by at least
// `marginLogical` logical pixels (scaled by that monitor's dpiScale) horizontally and vertically;
// the margin never exceeds the window's own size. If so, `window` is returned unchanged.
// Otherwise the window is moved fully onto the nearest monitor's work area (nearest by distance
// between rectangles; ties go to the earlier list entry), shrunk first if it is larger than that
// work area. An empty monitor list or an empty window rectangle returns `window` unchanged.
Rect clampToVisible(const Rect& window, std::span<const MonitorInfo> monitors,
                    int marginLogical = kVisibleMarginLogical);

}  // namespace r1ui::platform

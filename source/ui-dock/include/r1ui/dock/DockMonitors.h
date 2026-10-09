// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: MonitorSet, the description of the connected displays that layout loading needs, and
//   fitWindowRect(), the rule that brings a saved window back onto a visible monitor.
// Why: decision D13 and spec 03 rule 64 / spec 04 rule 47: a stored window that overlaps no monitor
//   by at least the visible margin (100 logical px) is brought fully back, centred on the preferred
//   work area with its size reduced to fit. ui-dock must not depend on the platform module, so the
//   host converts platform::Monitors into this plain struct.
// Callers: DockLayout::fromJson (LoadOptions), DockLayout::fitWindows, LayoutManager, tests.
// Units: every rectangle is in the same logical coordinate space as the window rectangles of the
//   layout (the host converts physical monitor rectangles with each monitor's scale).
// Failure behavior: a MonitorSet with no usable monitor (none, or none with a positive finite work
//   area) cannot judge visibility, so fitWindowRect() only clamps the size and reports no move.
#pragma once

#include <string>
#include <vector>

#include "r1ui/dock/DockTypes.h"

namespace r1ui::dock {

struct MonitorInfo {
  std::string name;
  Rect bounds;          // full monitor rectangle
  Rect workArea;        // without taskbars
  double scale = 1.0;   // display scale (1.0 = 96 dpi)
};

struct MonitorSet {
  std::vector<MonitorInfo> monitors;
  size_t primary = 0;  // index of the preferred work area (the primary monitor)
};

struct FitResult {
  Rect rect;
  bool moved = false;    // the position changed
  bool resized = false;  // the size changed (clamped or reduced to fit)
};

// Clamps the size to [minSize, kMaxCoordinate] (NaN becomes minSize, a NaN position 0) and, when the
// window overlaps no monitor by at least `margin` in both directions (or by its whole size when it
// is smaller than the margin), centres it on the preferred work area with its size reduced to the
// work area. A window that is visible enough is left where it is, even if partly off screen.
FitResult fitWindowRect(const Rect& window, const MonitorSet& monitors, double margin, double minSize);

// True when the set contains at least one monitor with a positive finite work area.
bool hasUsableMonitor(const MonitorSet& monitors);

}  // namespace r1ui::dock

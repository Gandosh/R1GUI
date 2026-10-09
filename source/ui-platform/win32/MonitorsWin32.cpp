// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: monitor enumeration (enumerateMonitors) and the per-monitor DPI query used by window
//   creation.
// Why: monitor layout comes from the OS; the placement rules themselves live in the portable
//   MonitorClamp.cpp.
// Callers: placement code via Monitors.h, Window.cpp via WindowImpl.h (monitorDpi).
// Invariants: the enumeration callback never lets an exception escape; the primary monitor is
//   first in the result.
#include <shellscalingapi.h>

#include <algorithm>

#include "WindowImpl.h"
#include "r1ui/platform/Monitors.h"
#include "r1ui/platform/Utf.h"

namespace r1ui::platform {

namespace {

Rect toRect(const RECT& r) { return {r.left, r.top, r.right - r.left, r.bottom - r.top}; }

BOOL CALLBACK collect(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
  try {
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(monitor, &mi) == FALSE) return TRUE;
    MonitorInfo info;
    info.bounds = toRect(mi.rcMonitor);
    info.workArea = toRect(mi.rcWork);
    info.dpiScale = dpiScaleFromDpi(monitorDpi(monitor));
    info.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    if (auto name = utf16ToUtf8(std::u16string_view(reinterpret_cast<const char16_t*>(mi.szDevice)))) info.name = std::move(*name);
    reinterpret_cast<std::vector<MonitorInfo>*>(param)->push_back(info);
    return TRUE;
  } catch (...) {
    return FALSE;  // stop enumerating; the caller returns what was collected
  }
}

}  // namespace

UINT monitorDpi(HMONITOR monitor) {
  UINT x = 0;
  UINT y = 0;
  if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &x, &y)) || x == 0) return kBaseDpi;
  return x;
}

std::vector<MonitorInfo> enumerateMonitors() {
  std::vector<MonitorInfo> monitors;
  EnumDisplayMonitors(nullptr, nullptr, &collect, reinterpret_cast<LPARAM>(&monitors));
  std::stable_partition(monitors.begin(), monitors.end(), [](const MonitorInfo& m) { return m.primary; });
  return monitors;
}

}  // namespace r1ui::platform

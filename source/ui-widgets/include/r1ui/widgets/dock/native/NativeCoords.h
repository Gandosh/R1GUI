// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the pure geometry of the native floating backend: ScreenSpace (virtual-desktop physical
//   pixels <-> the dock's "screen logical" pixels for any monitor layout and scale), the outer/content
//   rectangle arithmetic of a window with a drawn frame, the size limits and the drop-target query
//   topmostWindow over a bottom-to-top list of window rectangles.
// Why: the contract (FloatingBackend.h) defines screen logical space as the virtual desktop divided by
//   the scale of the monitor under the point; with two monitors of different scale that is a piecewise
//   mapping whose inverse needs rules. Keeping it free of the OS makes every layout testable.
// Callers: NativeFloatingBackend (the only production caller), tests/ui-widgets/dock/native.
// Mapping: toLogical(p) = p / scale(monitor containing p) (the nearest monitor when none contains it).
//   toPhysical(l, hint) = l * scale(M) where M is a monitor whose logical rectangle (bounds / scale)
//   contains l; where several do (a 1.0 and a 2.0 monitor side by side overlap in logical space) the
//   hint monitor wins if it is one of them, else the first in the list (the primary comes first); when
//   none contains l the nearest logical rectangle decides. The two directions agree on every point of
//   a monitor that does not overlap another one in logical space, and for windows the backend passes
//   the monitor the window is on as the hint, so a window never jumps to the other monitor.
// Failure behavior: no exceptions; NaN inputs give NaN outputs (callers validate), an empty monitor
//   list is the identity mapping with scale 1.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "r1ui/dock/DockTypes.h"
#include "r1ui/platform/Monitors.h"
#include "r1ui/widgets/dock/FloatingBackend.h"

namespace r1ui::widgets::native {

class ScreenSpace {
 public:
  ScreenSpace() = default;
  explicit ScreenSpace(std::vector<platform::MonitorInfo> monitors);

  bool empty() const { return monitors_.empty(); }
  const std::vector<platform::MonitorInfo>& monitors() const { return monitors_; }

  // Index of the monitor containing the physical point, else the nearest one; -1 without monitors.
  int monitorOfPhysical(double x, double y) const;
  // Index of the monitor showing the logical point (see the header comment for ties); -1 without monitors.
  int monitorOfLogical(dock::Point logical, int hint = -1) const;
  double scaleOf(int monitor) const;  // 1.0 for an invalid index
  dock::Rect logicalBounds(int monitor) const;
  dock::Rect logicalWorkArea(int monitor) const;

  dock::Point toLogical(double physicalX, double physicalY) const;
  dock::Point toPhysical(dock::Point logical, int hint = -1) const;
  // Name of the monitor showing the logical point (empty when there is none or it has no name).
  std::string nameAt(dock::Point logical, int hint = -1) const;
  // Size of the largest work area in logical pixels (the limit of a window's size); {0, 0} when empty.
  dock::Point largestWorkAreaSize() const;

 private:
  std::vector<platform::MonitorInfo> monitors_;
};

// What a window draws around its content rectangle (logical pixels, the same numbers as
// BackendInfo::frame).
struct FrameMetrics {
  double border = 1.0;
  double titleHeight = 34.0;
  FrameInsets insets() const { return {border, titleHeight, border, border}; }
};

// Outer window rectangle (client area, since the window is borderless) of a content rectangle and back.
dock::Rect outerOfContent(const dock::Rect& content, const FrameMetrics& frame);
dock::Rect contentOfOuter(const dock::Rect& outer, const FrameMetrics& frame);

// Limits a requested content rectangle: finite values only (the caller checked), size between `minimum`
// and `maximum`, never larger than kMaxCoordinate. The position is not limited here.
dock::Rect limitContentSize(dock::Rect content, dock::Point minimum, dock::Point maximum);

// One window as the drop-target query sees it.
struct StackedWindow {
  FloatId id = 0;
  dock::Rect outer;  // content plus the drawn frame, screen logical
  bool visible = true;
};

// The window showing `screen`: floating windows from the last (topmost) to the first, then the main
// window when its content rectangle contains the point; hidden, excluded and minimized windows are
// skipped; a NaN point or none showing it gives nullopt. `mainVisible` false (the main window is
// minimized) removes the main window from the answer.
std::optional<FloatId> topmostWindow(std::span<const StackedWindow> bottomToTop, const dock::Rect& main, bool mainVisible,
                                     dock::Point screen, std::span<const FloatId> exclude);

}  // namespace r1ui::widgets::native

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the fixed layout numbers of the brush popup (header, chips, grid area, footer, popovers), in
//   logical pixels relative to the popup's top-left, as functions of the popup size.
// Why: painting, hit testing and the tests' rectangle queries must agree on every control's position;
//   one place computes them.
// Callers: the BrushLibraryPopup*.cpp files only (not a public header). Calls: BrushTileLayout.h types.
#pragma once

#include <algorithm>

#include "BrushTileLayout.h"

namespace r1ui::widgets::brushes {

inline constexpr double kEdge = 10.0;
inline constexpr double kHeaderTop = 10.0;
inline constexpr double kHeaderHeight = 32.0;
inline constexpr double kModeWidth = 150.0;
inline constexpr double kChipsTop = 48.0;
inline constexpr double kChipsHeight = 26.0;
inline constexpr double kGridTop = 82.0;
inline constexpr double kFooterHeight = 44.0;
inline constexpr double kOptionWidth = 178.0;
inline constexpr double kScrollbarWidth = 6.0;
inline constexpr double kMenuRow = 26.0;
inline constexpr double kMenuWidth = 188.0;
inline constexpr double kAssignWidth = 288.0;
inline constexpr double kAssignHeight = 110.0;

struct Box {
  double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
  bool contains(double px, double py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// The mode switch shrinks to a short label in a narrow popup (a small floating window).
inline double modeWidth(double width) { return width >= 460.0 ? kModeWidth : 78.0; }
inline Box searchBox(double width) { return {kEdge, kHeaderTop, width - 2.0 * kEdge - modeWidth(width) - 8.0, kHeaderHeight}; }
inline Box modeBox(double width) { return {width - kEdge - modeWidth(width), kHeaderTop, modeWidth(width), kHeaderHeight}; }
inline Box chipsBox(double width) { return {kEdge, kChipsTop, width - 2.0 * kEdge, kChipsHeight}; }
inline Box gridBox(double width, double height) { return {0.0, kGridTop, width, height - kGridTop - kFooterHeight}; }
inline Box footerBox(double width, double height) { return {0.0, height - kFooterHeight, width, kFooterHeight}; }
// The option has the whole footer in a narrow popup (no room for the hint and the count then).
inline Box optionBox(double width, double height) {
  const double w = width >= 460.0 ? kOptionWidth : std::max(0.0, width - 2.0 * kEdge);
  return {width - kEdge - w, height - kFooterHeight + 7.0, w, kFooterHeight - 14.0};
}

}  // namespace r1ui::widgets::brushes

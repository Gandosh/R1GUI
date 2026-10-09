// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the measured numbers of TabBar (docs/spec/widgets.md 4.1) shared by its translation units, and
//   the animation slots of a tab.
// Why: layout, painting and the pointer code must agree on a tab's geometry to the pixel; one place
//   names each measured value.
// Callers: TabBar.cpp, TabBarLayout.cpp, TabBarPaint.cpp only (private header, not installed).
#pragma once

#include "r1ui/widgets/tabbar/TabBar.h"

namespace r1ui::widgets {
namespace {

constexpr double kPadX = 12.0;        // measured: padding 0 x 12
constexpr double kIconSize = 12.0;    // leading file icon
constexpr double kGap = 6.0;          // measured gap between icon, label and close button
constexpr double kCloseSize = 16.0;
constexpr double kCloseIcon = 12.0;
constexpr double kBorder = 1.0;
constexpr double kTabHeight = 35.0;   // bar 36 with the 1 px bottom border inside
constexpr double kCloseTop = 9.5;     // measured close button y
constexpr double kWheelStep = 48.0;
constexpr double kButtonIcon = 14.0;
constexpr double kCompactBelow = 100.0;  // narrower tabs (shrunk or overflowing) drop the icon and free the close button's space until needed

int slotOf(TabId id, int sub) { return static_cast<int>((id * 4 + static_cast<TabId>(sub)) & 0x3fffffffu); }

}  // namespace
}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the description of a borderless window's custom title bar regions and the pure
//   classification of a client point into a hit zone.
// Why: with no OS frame, the toolkit tells the platform which areas drag, which are the
//   minimize/maximize/close buttons and how wide the resize bands are; the platform answers the
//   OS non-client hit test from it so snapping, snap layouts and edge resize keep working.
// Callers: Window::setChromeLayout / Window::chromeZoneAt, the Win32 non-client handlers, tests.
// Invariants: rectangles are client-space physical pixels; classification is pure and never
//   throws. Priority at a point: resize band, then buttons, then caption holes (client), then
//   caption, then client.
#pragma once

#include <cstddef>
#include <vector>

#include "r1ui/platform/Geometry.h"

namespace r1ui::platform {

enum class HitZone {
  Client,
  Caption,  // drag to move, double-click to maximize/restore
  MinimizeButton,
  MaximizeButton,  // reporting it as such makes Windows 11 show snap layouts
  CloseButton,
  ResizeLeft,
  ResizeRight,
  ResizeTop,
  ResizeBottom,
  ResizeTopLeft,
  ResizeTopRight,
  ResizeBottomLeft,
  ResizeBottomRight
};

inline constexpr float kDefaultResizeBorderLogical = 6.0f;
inline constexpr float kMaxResizeBorderLogical = 64.0f;
inline constexpr size_t kMaxChromeRects = 256;

struct ChromeLayout {
  std::vector<Rect> captionRects;  // draggable title-bar areas
  std::vector<Rect> captionHoles;  // interactive widgets inside the caption (tabs, menus): client
  Rect minimizeButton;             // empty rect = no such button
  Rect maximizeButton;
  Rect closeButton;
  float resizeBorderLogical = kDefaultResizeBorderLogical;  // band width, scaled by dpi
};

// False if the layout is unusable: more than kMaxChromeRects rectangles in total, or a
// non-finite / negative / larger than kMaxResizeBorderLogical resize border.
bool isValidChromeLayout(const ChromeLayout& layout);

// Classifies a client-space point. Resize bands apply only when `resizable` and not `maximized`.
// A point outside the client rectangle is Client.
HitZone classifyHit(const ChromeLayout& layout, Size clientSize, Point point, float dpiScale,
                    bool resizable, bool maximized);

}  // namespace r1ui::platform

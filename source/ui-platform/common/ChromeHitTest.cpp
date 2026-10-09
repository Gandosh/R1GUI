// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: validation of a ChromeLayout and the pure point classification declared in
//   ChromeHitTest.h.
// Why: the same classification serves the OS non-client hit test and the toolkit's own queries,
//   and is testable without a desktop.
// Callers: Window backend (WM_NCHITTEST), Window::chromeZoneAt, tests.
// Invariants: pure and never throws; resize bands never overlap (each is at most half the
//   client dimension), so corners are well-defined for tiny windows.
#include "r1ui/platform/ChromeHitTest.h"

#include <algorithm>
#include <cmath>

#include "r1ui/platform/Dpi.h"

namespace r1ui::platform {

namespace {

bool anyContains(const std::vector<Rect>& rects, Point p) {
  return std::any_of(rects.begin(), rects.end(), [&](const Rect& r) { return r.contains(p); });
}

// Band width in physical pixels: at least one pixel when the logical width is positive.
int bandWidth(float logical, float scale) {
  if (logical <= 0.0f) return 0;
  return std::max(1, logicalToPhysical(logical, scale));
}

HitZone resizeZone(bool left, bool right, bool top, bool bottom) {
  if (top && left) return HitZone::ResizeTopLeft;
  if (top && right) return HitZone::ResizeTopRight;
  if (bottom && left) return HitZone::ResizeBottomLeft;
  if (bottom && right) return HitZone::ResizeBottomRight;
  if (left) return HitZone::ResizeLeft;
  if (right) return HitZone::ResizeRight;
  if (top) return HitZone::ResizeTop;
  if (bottom) return HitZone::ResizeBottom;
  return HitZone::Client;
}

}  // namespace

bool isValidChromeLayout(const ChromeLayout& layout) {
  if (!std::isfinite(layout.resizeBorderLogical) || layout.resizeBorderLogical < 0.0f ||
      layout.resizeBorderLogical > kMaxResizeBorderLogical) {
    return false;
  }
  return layout.captionRects.size() + layout.captionHoles.size() <= kMaxChromeRects;
}

HitZone classifyHit(const ChromeLayout& layout, Size clientSize, Point point, float dpiScale,
                    bool resizable, bool maximized) {
  const Rect client{0, 0, clientSize.width, clientSize.height};
  if (!client.contains(point)) return HitZone::Client;

  if (resizable && !maximized) {
    const int band = bandWidth(layout.resizeBorderLogical, dpiScale);
    const int bx = std::min(band, clientSize.width / 2);
    const int by = std::min(band, clientSize.height / 2);
    const HitZone zone = resizeZone(point.x < bx, point.x >= clientSize.width - bx, point.y < by,
                                    point.y >= clientSize.height - by);
    if (zone != HitZone::Client) return zone;
  }
  if (layout.closeButton.contains(point)) return HitZone::CloseButton;
  if (layout.maximizeButton.contains(point)) return HitZone::MaximizeButton;
  if (layout.minimizeButton.contains(point)) return HitZone::MinimizeButton;
  if (anyContains(layout.captionHoles, point)) return HitZone::Client;
  if (anyContains(layout.captionRects, point)) return HitZone::Caption;
  return HitZone::Client;
}

}  // namespace r1ui::platform

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for custom-chrome hit classification (ChromeHitTest.h): zone priority, DPI-scaled
//   resize bands, maximized / non-resizable behavior, layout validation and hostile inputs.
// Callers: CTest (label fast). Exit code 0 = pass.
#include <cmath>
#include <limits>

#include "TestSupport.h"
#include "r1ui/platform/ChromeHitTest.h"

using namespace r1ui::platform;
using platform_test::expect;
using platform_test::runCase;

namespace {

constexpr Size kClient{800, 600};

// 32 px title bar: a hole for a tab strip, then minimize / maximize / close buttons.
ChromeLayout titleBar() {
  ChromeLayout l;
  l.captionRects = {{0, 0, 800, 32}};
  l.captionHoles = {{100, 0, 200, 32}};
  l.minimizeButton = {656, 0, 48, 32};
  l.maximizeButton = {704, 0, 48, 32};
  l.closeButton = {752, 0, 48, 32};
  return l;
}

HitZone at(const ChromeLayout& l, int x, int y, float scale = 1.0f, bool resizable = true, bool maximized = false,
           Size size = kClient) {
  return classifyHit(l, size, {x, y}, scale, resizable, maximized);
}

void zonesByPriority() {
  const ChromeLayout l = titleBar();
  expect(at(l, 20, 16) == HitZone::Caption, "empty title bar drags");
  expect(at(l, 150, 16) == HitZone::Client, "a hole inside the caption is client (tabs stay clickable)");
  expect(at(l, 680, 16) == HitZone::MinimizeButton, "minimize");
  expect(at(l, 730, 16) == HitZone::MaximizeButton, "maximize reports as the maximize button (snap layouts)");
  expect(at(l, 770, 16) == HitZone::CloseButton, "close");
  expect(at(l, 400, 300) == HitZone::Client, "body is client");
  expect(at(l, 400, 40) == HitZone::Client, "just below the title bar is client");
  expect(at(l, 655, 16) == HitZone::Caption && at(l, 656, 16) == HitZone::MinimizeButton, "button edge is exact");
}

void resizeBands() {
  const ChromeLayout l = titleBar();
  expect(at(l, 0, 0) == HitZone::ResizeTopLeft && at(l, 5, 5) == HitZone::ResizeTopLeft, "top-left corner, 6 px band");
  expect(at(l, 6, 6) == HitZone::Caption, "inside the band the caption resumes");
  expect(at(l, 799, 0) == HitZone::ResizeTopRight, "top-right corner beats the close button");
  expect(at(l, 400, 2) == HitZone::ResizeTop, "top edge beats the caption");
  expect(at(l, 799, 16) == HitZone::ResizeRight, "right edge beats the close button");
  expect(at(l, 794, 300) == HitZone::ResizeRight && at(l, 793, 300) == HitZone::Client, "band is exactly 6 px");
  expect(at(l, 3, 300) == HitZone::ResizeLeft, "left");
  expect(at(l, 400, 597) == HitZone::ResizeBottom, "bottom");
  expect(at(l, 2, 597) == HitZone::ResizeBottomLeft && at(l, 797, 597) == HitZone::ResizeBottomRight, "bottom corners");
}

void bandScalesWithDpi() {
  const ChromeLayout l = titleBar();
  expect(at(l, 10, 300, 1.0f) == HitZone::Client, "10 px is outside a 6 px band");
  expect(at(l, 10, 300, 2.0f) == HitZone::ResizeLeft, "10 px is inside a 12 px band at 200%");
  expect(at(l, 12, 300, 2.0f) == HitZone::Client, "band ends at 12 px at 200%");
  ChromeLayout thin = titleBar();
  thin.resizeBorderLogical = 0.1f;
  expect(at(thin, 0, 300) == HitZone::ResizeLeft && at(thin, 1, 300) == HitZone::Client, "a positive width is at least 1 px");
  ChromeLayout none = titleBar();
  none.resizeBorderLogical = 0.0f;
  expect(at(none, 0, 300) == HitZone::Client, "zero width disables the bands");
  expect(at(l, 10, 300, std::numeric_limits<float>::quiet_NaN()) == HitZone::Client, "NaN scale treated as 1.0");
}

void maximizedAndFixedWindows() {
  const ChromeLayout l = titleBar();
  expect(at(l, 3, 3, 1.0f, true, true) == HitZone::Caption, "maximized: no resize bands");
  expect(at(l, 799, 16, 1.0f, true, true) == HitZone::CloseButton, "maximized: the close button reaches the edge");
  expect(at(l, 3, 3, 1.0f, false, false) == HitZone::Caption, "non-resizable: no resize bands");
  expect(at(l, 730, 16, 1.0f, false, false) == HitZone::MaximizeButton, "classification still names the button (the window ignores it)");
}

void hostileGeometry() {
  const ChromeLayout l = titleBar();
  expect(at(l, -1, 10) == HitZone::Client && at(l, 800, 10) == HitZone::Client && at(l, 10, 600) == HitZone::Client &&
             at(l, 10, -1) == HitZone::Client,
         "points outside the client rectangle are client");
  expect(at(l, 0, 0, 1.0f, true, false, {0, 0}) == HitZone::Client, "zero-size client");
  expect(at(l, 0, 0, 1.0f, true, false, {-5, -5}) == HitZone::Client, "negative client size");
  expect(at(l, 2, 2, 1.0f, true, false, {5, 5}) == HitZone::Caption, "tiny client: bands halve to 2 px and never overlap, so the middle stays available");
  expect(at(l, 0, 0, 1.0f, true, false, {5, 5}) == HitZone::ResizeTopLeft, "tiny client corner");
  expect(at(l, std::numeric_limits<int>::max(), std::numeric_limits<int>::min()) == HitZone::Client, "extreme point");
  ChromeLayout weird;
  weird.captionRects = {{std::numeric_limits<int>::max() - 5, 0, 100, 100}, {0, 0, -10, 32}, {10, 10, 0, 0}};
  expect(at(weird, 400, 16) == HitZone::Client, "negative / empty / overflowing rectangles contain nothing");
  expect(ChromeLayout{}.minimizeButton.empty() && at(ChromeLayout{}, 0, 0, 1.0f, false) == HitZone::Client,
         "default layout has no absent button capturing the origin");
}

void layoutValidation() {
  expect(isValidChromeLayout(ChromeLayout{}) && isValidChromeLayout(titleBar()), "defaults and a normal title bar are valid");
  ChromeLayout l;
  l.resizeBorderLogical = std::numeric_limits<float>::quiet_NaN();
  expect(!isValidChromeLayout(l), "NaN border");
  l.resizeBorderLogical = std::numeric_limits<float>::infinity();
  expect(!isValidChromeLayout(l), "infinite border");
  l.resizeBorderLogical = -1.0f;
  expect(!isValidChromeLayout(l), "negative border");
  l.resizeBorderLogical = kMaxResizeBorderLogical + 1.0f;
  expect(!isValidChromeLayout(l), "oversized border");
  l.resizeBorderLogical = kMaxResizeBorderLogical;
  expect(isValidChromeLayout(l), "maximum border allowed");
  ChromeLayout many;
  many.captionRects.assign(kMaxChromeRects, Rect{0, 0, 1, 1});
  expect(isValidChromeLayout(many), "exactly the maximum number of rectangles");
  many.captionHoles.push_back({0, 0, 1, 1});
  expect(!isValidChromeLayout(many), "one rectangle over the maximum");
}

}  // namespace

int main() {
  runCase("zones_by_priority", zonesByPriority);
  runCase("resize_bands", resizeBands);
  runCase("band_scales_with_dpi", bandScalesWithDpi);
  runCase("maximized_and_fixed_windows", maximizedAndFixedWindows);
  runCase("hostile_geometry", hostileGeometry);
  runCase("layout_validation", layoutValidation);
  return platform_test::finish("ui-platform chrome hit-test");
}

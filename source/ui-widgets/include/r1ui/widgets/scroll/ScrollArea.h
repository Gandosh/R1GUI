// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: ScrollArea, a clipped viewport over one content container with a vertical and / or
//   horizontal thin scrollbar: wheel scrolling (notches -> lines), thumb drag, track click,
//   keyboard (arrows, Page, Home, End), scroll-into-view, nested scroll chaining and clamping.
// Why: every panel that can overflow (property panels, asset lists, dialogs) needs the same scroll
//   behaviour; the layout engine has no scroll offset, so the area moves its content with a
//   negative margin on a flex child that never shrinks and clips with overflow hidden.
// Callers: application code (add children to content()), TreeView shares ScrollBar but not this
//   class. Calls: ScrollBar, UiContext / PaintContext.
// Structure: ScrollArea (overflow hidden, column flex) -> ScrollContent (flexShrink 0, margin
//   -offset) -> user widgets. The content is as tall as its children want; its width is the
//   viewport width unless setContentMinWidth() asks for horizontal scrolling.
// Scrollbars: policy Auto shows a bar only when the content overflows. Style Gutter (the
//   reference) reserves a 10 px strip for the bar by padding the area, which re-lays the content
//   out once (the frame loop runs that pass in the same frame); style Overlay draws the bar over
//   the content and intercepts presses on it in the capture phase.
// Wheel: notches times wheelStep (default 48 px = 3 lines of 16 px); Shift turns vertical notches
//   into horizontal ones. A wheel event is consumed only when it moves this area, otherwise it
//   bubbles so an enclosing area scrolls (chaining).
// Keys (when the area or a descendant that did not use them has focus): Up / Down one line,
//   PageUp / PageDown one viewport minus a line, Home / End to the ends, Left / Right one line
//   horizontally. setFocusable(true) lets the area itself take focus.
// Invariants: offsets are finite and within [0, content - viewport] after every layout; hostile
//   values (NaN, infinity, huge) are rejected or clamped at the public setters.
#pragma once

#include <functional>
#include <span>

#include "r1ui/widgets/runtime/WidgetObject.h"
#include "r1ui/widgets/scroll/ScrollBar.h"

namespace r1ui::widgets {

// The container the scrolled widgets live in. Public so tests and layouts can find it.
class ScrollContent : public WidgetObject {
 public:
  const char* typeName() const override { return "ScrollContent"; }
  void onAttached() override;
};

enum class ScrollAxes : uint8_t { Vertical, Horizontal, Both };
enum class ScrollbarPolicy : uint8_t { Auto, Always, Never };
enum class ScrollbarStyle : uint8_t { Gutter, Overlay };
enum class ScrollAlign : uint8_t { Nearest, Start, Center, End };

class ScrollArea : public WidgetObject {
 public:
  explicit ScrollArea(ScrollAxes axes = ScrollAxes::Vertical) : axes_(axes) {}
  static std::span<const theme::StyleRuleEntry> styleRows() { return ScrollBar::styleRows(); }

  const char* typeName() const override { return "ScrollArea"; }
  void onAttached() override;
  void onDetached() override;
  void onLayout() override;
  void paintOver(PaintContext& ctx) override;
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onPointerWheel(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onStateChanged(uint16_t previous) override;
  void onKeyDown(Event& e) override;
  uint8_t phases() const override;

  // ---- structure ----
  // Add the scrolled widgets as children of this widget.
  core::tree::WidgetId content() const { return content_; }
  // Minimum width / height of the content in logical px; a width larger than the viewport is what
  // makes the area scroll horizontally. Non-finite or negative values are rejected.
  void setContentMinWidth(double width);
  void setContentMinHeight(double height);
  void setVerticalPolicy(ScrollbarPolicy policy);
  void setHorizontalPolicy(ScrollbarPolicy policy);
  void setScrollbarStyle(ScrollbarStyle style);
  // Pixels per wheel notch; non-positive or non-finite values are rejected.
  void setWheelStep(double pixels);
  double lineStep() const { return lineStep_; }

  // ---- scroll position (logical px; every setter clamps to the scrollable range) ----
  double offsetX() const { return offsetX_; }
  double offsetY() const { return offsetY_; }
  double maxOffsetX() const;
  double maxOffsetY() const;
  double viewportWidth() const { return viewportW_; }
  double viewportHeight() const { return viewportH_; }
  double contentWidth() const { return contentW_; }
  double contentHeight() const { return contentH_; }
  bool verticalBarVisible() const { return showV_; }
  bool horizontalBarVisible() const { return showH_; }
  // Returns true when the position changed.
  bool scrollTo(double x, double y);
  bool scrollBy(double dx, double dy);
  bool scrollByLines(double lines);
  bool scrollByPages(double pages);
  // Brings `child` (any descendant of the content) into view; false for a stale or foreign widget.
  bool scrollIntoView(core::tree::WidgetId child, ScrollAlign align = ScrollAlign::Nearest, double margin = 0.0);
  // The same for a rectangle in content coordinates.
  bool scrollRectIntoView(double x, double y, double w, double h, ScrollAlign align = ScrollAlign::Nearest, double margin = 0.0);
  // Called after every change of the offset (wheel, drag, API, clamping by layout).
  void setOnScroll(std::function<void(ScrollArea&)> callback) { onScroll_ = std::move(callback); }

 private:
  struct Geometry {
    double x = 0, y = 0, w = 0, h = 0;  // area absolute rectangle
  };
  bool vertical() const { return axes_ != ScrollAxes::Horizontal; }
  bool horizontal() const { return axes_ != ScrollAxes::Vertical; }
  Geometry area() const;
  void placeBars();
  void applyOffset(double x, double y);
  double alignedOffset(double start, double length, double view, double current, ScrollAlign align, double margin) const;
  bool barHit(double x, double y) const;

  ScrollAxes axes_;
  core::tree::WidgetId content_;
  ScrollBar vbar_{ScrollAxis::Vertical};
  ScrollBar hbar_{ScrollAxis::Horizontal};
  ScrollbarPolicy vPolicy_ = ScrollbarPolicy::Auto;
  ScrollbarPolicy hPolicy_ = ScrollbarPolicy::Auto;
  ScrollbarStyle barStyle_ = ScrollbarStyle::Gutter;
  double wheelStep_ = 48.0;
  double lineStep_ = 16.0;
  double minContentW_ = 0.0;
  double offsetX_ = 0.0;
  double offsetY_ = 0.0;
  double viewportW_ = 0.0;
  double viewportH_ = 0.0;
  double contentW_ = 0.0;
  double contentH_ = 0.0;
  bool showV_ = false;
  bool showH_ = false;
  bool draggingBar_ = false;
  std::function<void(ScrollArea&)> onScroll_;
};

}  // namespace r1ui::widgets

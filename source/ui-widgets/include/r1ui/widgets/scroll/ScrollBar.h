// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: ScrollBar, the thin scrollbar of the reference (a 10 px gutter holding a 6 px thumb with
//   a 3 px radius, transparent track): thumb geometry from viewport / content / offset, hit
//   testing, thumb dragging, track clicks (one page towards the click), hover state and painting.
// Why: ScrollArea (two bars) and TreeView (one bar, virtualized rows) scroll differently but must
//   look and behave the same; the bar is a plain object owned by its widget (no tree node), so a
//   100k row tree pays nothing for it.
// Callers: ScrollArea, TreeView (and any widget that scrolls itself). Calls: PaintContext.
// Units: logical pixels; the track rectangle is in window coordinates (the owner updates it from
//   its absolute rectangle before every use). Offsets are in content pixels, 0 .. content - viewport.
// Invariants: update() sanitises its input (NaN, negative or infinite values become 0, offsets are
//   clamped), the thumb never leaves the track and is at least kMinThumb long (or the whole
//   track when the track is shorter), pointer calls return the new offset only when it changed.
// Style rows (registered by styleRows()): scroll.thumb (border colour, radius 3; hover and active
//   use muted at 50%, which measures as #606060 over the dark panel like the reference).
#pragma once

#include <optional>
#include <span>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/runtime/PaintContext.h"

namespace r1ui::widgets {

enum class ScrollAxis : uint8_t { Vertical, Horizontal };

class ScrollBar {
 public:
  static constexpr double kGutter = 10.0;     // reserved width / height of the bar
  static constexpr double kThumbSize = 6.0;   // thumb thickness, centred in the gutter
  static constexpr double kMinThumb = 20.0;   // shortest thumb

  explicit ScrollBar(ScrollAxis axis = ScrollAxis::Vertical) : axis_(axis) {}
  static std::span<const theme::StyleRuleEntry> styleRows();

  ScrollAxis axis() const { return axis_; }
  // Track rectangle in window coordinates; the bar is empty (never hit, never painted) for a
  // non-positive size.
  void setTrack(double x, double y, double w, double h);
  // New viewport / content / offset (logical px along the axis). Returns the clamped offset.
  double update(double viewport, double content, double offset);
  // False when everything fits (content <= viewport): nothing to scroll, nothing to draw.
  bool scrollable() const { return content_ > viewport_ + 1e-6 && trackLength() > 0.0; }
  double offset() const { return offset_; }
  double maxOffset() const { return content_ > viewport_ ? content_ - viewport_ : 0.0; }

  // Thumb rectangle {x, y, w, h} in window coordinates (zeros when not scrollable).
  struct Box {
    double x = 0, y = 0, w = 0, h = 0;
    bool contains(double px, double py) const { return w > 0 && h > 0 && px >= x && py >= y && px < x + w && py < y + h; }
  };
  Box thumb() const;
  Box track() const { return {trackX_, trackY_, trackW_, trackH_}; }
  bool inTrack(double x, double y) const { return scrollable() && track().contains(x, y); }

  // ---- interaction (each returns the new offset when it changed) ----
  std::optional<double> pointerDown(double x, double y);
  std::optional<double> pointerMove(double x, double y);
  void pointerUp() { dragging_ = false; }
  void cancel() { dragging_ = false; }
  bool dragging() const { return dragging_; }
  // Hover tracks the thumb; returns true when the state changed (the owner repaints).
  bool setPointer(double x, double y, bool inside);
  bool thumbHovered() const { return hover_; }

  void paint(PaintContext& ctx) const;

 private:
  double trackLength() const { return axis_ == ScrollAxis::Vertical ? trackH_ : trackW_; }
  double thumbLength() const;
  double offsetForThumbStart(double start) const;

  ScrollAxis axis_;
  double trackX_ = 0, trackY_ = 0, trackW_ = 0, trackH_ = 0;
  double viewport_ = 0, content_ = 0, offset_ = 0;
  bool dragging_ = false;
  bool hover_ = false;
  double grab_ = 0;  // pointer distance from the thumb start while dragging
};

}  // namespace r1ui::widgets

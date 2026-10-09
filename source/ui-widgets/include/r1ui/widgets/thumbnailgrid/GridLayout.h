// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the pure maths of the virtualised asset grid and list (spec 12): how many columns fit and how
//   tiles stretch to fill the width, the position of every item without creating anything per item,
//   the window of items to draw for a scroll offset, hit testing, keyboard navigation between tiles,
//   scrolling an item into view (centred or with the least movement), and the zoom stops.
// Why: with 100 000 items nothing may be created or measured per item; every query is O(1) or
//   O(visible). Keeping the maths free of widgets makes the spec's numbers testable (one column when
//   narrower than a tile plus the scrollbar, even stretching, page jumps of whole rows, 100 000
//   items) and keeps the widget thin.
// Callers: ThumbnailGrid, tests. Calls: nothing.
// Units: logical pixels; `content` coordinates start at the top-left of the scrolled content, view
//   coordinates at the top-left of the viewport (content y minus the scroll offset).
// Limits: item counts above kMaxItems are clamped (a clamped count is the caller's bug, never a
//   crash); every position is a double so 2^30 items at 200 px do not overflow.
// Invariants: columns >= 1; for 0 items every range is empty and every hit test misses; an index
//   returned by a query is always < the item count or npos.
#pragma once

#include <cstddef>
#include <cstdint>

namespace r1ui::widgets::thumbs {

inline constexpr size_t kNone = static_cast<size_t>(-1);
inline constexpr size_t kMaxItems = size_t{1} << 30;

enum class ViewMode : uint8_t { Grid, List };

struct LayoutParams {
  double viewportWidth = 0.0;
  double viewportHeight = 0.0;
  ViewMode mode = ViewMode::Grid;
  double thumbEdge = 128.0;       // the zoom value: edge of the thumbnail square in a grid tile
  double tilePadding = 6.0;       // around the thumbnail inside a tile
  double gap = 4.0;               // between tiles
  double outerPadding = 8.0;      // around the whole grid
  double scrollbarWidth = 17.0;   // spec 12: width allowance; reserved only when the content overflows
  size_t itemCount = 0;
};

struct Metrics {
  ViewMode mode = ViewMode::Grid;
  int columns = 1;
  double cellW = 0.0;             // stretched tile width
  double cellH = 0.0;             // tile height (thumbnail + label area), or the row height in a list
  double thumb = 0.0;             // edge of the thumbnail square
  double labelHeight = 0.0;       // under the thumbnail (grid), 0 in a list
  int labelLines = 1;
  double gap = 0.0;
  double padding = 0.0;
  double tilePad = 0.0;
  double rowStride = 1.0;
  double contentHeight = 0.0;
  double reservedWidth = 0.0;     // taken by the scrollbar
  double availableWidth = 0.0;
  size_t itemCount = 0;
  size_t rows() const { return columns > 0 ? (itemCount + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns) : 0; }
};

struct Rect {
  double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
};

Metrics computeMetrics(const LayoutParams& p);
// Content-coordinate rectangle of item `index` (empty for an index >= count).
Rect itemRect(const Metrics& m, size_t index);
// The thumbnail square inside a tile rectangle (content coordinates).
Rect thumbRect(const Metrics& m, const Rect& tile);
// The label area under the thumbnail (grid) or beside it (list).
Rect labelRect(const Metrics& m, const Rect& tile);

struct VisibleRange {
  size_t first = 0;
  size_t last = 0;  // exclusive
  size_t count() const { return last > first ? last - first : 0; }
};
// Items whose rows intersect [scroll, scroll + viewportHeight), widened by `overscanRows` rows each side.
VisibleRange visibleRange(const Metrics& m, double scroll, double viewportHeight, size_t overscanRows);

// Item under a content-space point, kNone in gaps, padding and past the last item.
size_t hitTest(const Metrics& m, double x, double y);

double maxScroll(const Metrics& m, double viewportHeight);
double clampScroll(const Metrics& m, double scroll, double viewportHeight);
// New scroll offset that shows `index` fully: the same offset when it is already fully inside; centred
// when `center` is set (spec 08 rule 67), else moved by the least amount (rule 68).
double revealScroll(const Metrics& m, double scroll, double viewportHeight, size_t index, bool center);

enum class Nav : uint8_t { Left, Right, Up, Down, Home, End, PageUp, PageDown };
// The item a navigation key leads to from `current` (kNone current = nothing yet: Down / Right / Home
// go to the first item, Up / Left / End to the last). Clamped at the ends; never wraps.
size_t navigate(const Metrics& m, size_t current, Nav key, double viewportHeight);

// ---- zoom (spec 12 rules 7-14) ----
inline constexpr double kMinZoom = 48.0;
inline constexpr double kMaxZoom = 320.0;
inline constexpr double kDefaultZoom = 128.0;  // the middle stop
double clampZoom(double value);                // NaN -> default
// The next stop above (direction > 0) or below (< 0) `value`; the value itself clamped when there is none.
double stepZoom(double value, int direction);
const double* zoomStops(size_t& count);

}  // namespace r1ui::widgets::thumbs

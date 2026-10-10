// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the geometry of the brush popup's tile grid: how many columns fit, where the (at most two)
//   sections with their headers sit, the rectangle of every tile, hit testing, the tile range that
//   intersects a scroll window and keyboard moves between tiles across sections.
// Why: with 2000 brushes nothing may be created or measured per tile; every query is O(1) or
//   O(visible). Keeping the maths free of widgets makes the numbers testable and the widget thin.
// Callers: BrushLibraryPopup (src/brushes only, not a public header). Calls: nothing.
// Units: logical pixels; content coordinates start at the top-left of the scrolled content, view
//   coordinates at the top-left of the grid area (content y minus the scroll offset).
// Invariants: columns >= 1; a tile index returned by a query is below the tile count; sections
//   partition the tile list in order (Recent first when present); zero or negative widths give one column.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace r1ui::widgets::brushes {

inline constexpr double kCellW = 88.0;
inline constexpr double kCellH = 102.0;
inline constexpr double kThumb = 64.0;
inline constexpr double kGap = 6.0;
inline constexpr double kGridPad = 8.0;
inline constexpr double kSectionHeader = 22.0;
inline constexpr double kSectionGap = 6.0;

struct TileRect {
  double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
  bool contains(double px, double py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

enum class Move : uint8_t { Left, Right, Up, Down, Home, End };

class TileLayout {
 public:
  // `recentCount` tiles form the first section (with a header) when headers are shown; the remaining
  // tiles the second. Without headers there is one section.
  void build(double width, uint32_t recentCount, uint32_t tileCount, bool showHeaders);

  int columns() const { return columns_; }
  double contentHeight() const { return contentHeight_; }
  uint32_t tileCount() const { return tileCount_; }
  TileRect tileRect(uint32_t tile) const;                 // content coordinates, empty for a bad index
  TileRect thumbRect(uint32_t tile) const;                // the picture square inside the tile
  std::optional<uint32_t> hit(double x, double y) const;  // content coordinates; gaps and headers miss
  // Section headers: top (content y) of the header of section 0/1, -1 when absent.
  double headerTop(int section) const { return section >= 0 && section < 2 ? sections_[section].headerTop : -1.0; }
  // First and one past the last tile whose rows intersect [scroll, scroll + height); two ranges at most.
  struct Range {
    uint32_t first = 0;
    uint32_t last = 0;
  };
  int visible(double scroll, double height, Range out[2]) const;
  // The tile a navigation key leads to from `current` (clamped, never wraps). `current` must be a valid tile.
  uint32_t move(uint32_t current, Move key) const;
  // Scroll offset that shows `tile` fully with the least movement.
  double reveal(double scroll, double height, uint32_t tile) const;
  double maxScroll(double height) const { return contentHeight_ > height ? contentHeight_ - height : 0.0; }

 private:
  struct Section {
    uint32_t first = 0;
    uint32_t count = 0;
    double headerTop = -1.0;
    double top = 0.0;  // y of the first row
  };
  uint32_t rowsOf(const Section& section) const;
  int sectionOf(uint32_t tile) const;

  Section sections_[2];
  int sectionCount_ = 0;
  int columns_ = 1;
  double contentHeight_ = 0.0;
  double xOffset_ = 0.0;  // left edge of the first column: the block of columns is centred
  uint32_t tileCount_ = 0;
};

}  // namespace r1ui::widgets::brushes

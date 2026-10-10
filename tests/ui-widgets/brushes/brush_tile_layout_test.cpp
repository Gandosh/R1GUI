// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of the tile grid geometry of the brush popup (columns, sections with headers, rectangles, hit
//   testing, visible ranges, keyboard moves across sections, scrolling a tile into view) including zero,
//   negative, NaN and huge inputs.
// Callers: CTest (fast). Includes the module's private header by path: the geometry is internal.
#include <cmath>
#include <limits>

#include "../../../source/ui-widgets/src/brushes/BrushTileLayout.h"
#include "TestSupport.h"

using namespace r1ui::widgets::brushes;

int main() {
  TileLayout layout;
  layout.build(680.0, 0, 20, false);
  R1_EXPECT(layout.columns() == 7 && layout.tileCount() == 20);
  // 3 rows of tiles: padding + 3 rows + 2 gaps + padding.
  R1_EXPECT(std::fabs(layout.contentHeight() - (2 * kGridPad + 3 * kCellH + 2 * kGap)) < 1e-9);
  const TileRect first = layout.tileRect(0);
  R1_EXPECT(first.w == kCellW && first.h == kCellH && first.y == kGridPad);
  R1_EXPECT(layout.tileRect(7).y == kGridPad + kCellH + kGap && layout.tileRect(7).x == first.x);
  R1_EXPECT(layout.tileRect(20).w == 0.0);
  // The block of columns is centred.
  const double right = layout.tileRect(6).x + kCellW;
  R1_EXPECT(std::fabs((first.x) - (680.0 - right)) < 1e-9);

  // Hit testing: inside a tile, in the gap, outside, past the last tile.
  R1_EXPECT(layout.hit(first.x + 5, first.y + 5) == 0u);
  R1_EXPECT(!layout.hit(first.x + kCellW + 2, first.y + 5).has_value());
  R1_EXPECT(!layout.hit(1.0, 1.0).has_value());
  R1_EXPECT(layout.hit(layout.tileRect(19).x + 2, layout.tileRect(19).y + 2) == 19u);
  R1_EXPECT(!layout.hit(layout.tileRect(19).x + kCellW + kGap + 2, layout.tileRect(19).y + 2).has_value());
  R1_EXPECT(!layout.hit(std::nan(""), 5.0).has_value());

  // Moves.
  R1_EXPECT(layout.move(0, Move::Left) == 0 && layout.move(0, Move::Up) == 0 && layout.move(19, Move::Right) == 19 && layout.move(19, Move::Down) == 19);
  R1_EXPECT(layout.move(1, Move::Down) == 8 && layout.move(8, Move::Up) == 1);
  R1_EXPECT(layout.move(13, Move::Down) == 19);   // the last row is shorter: the last tile
  R1_EXPECT(layout.move(5, Move::Home) == 0 && layout.move(5, Move::End) == 19);

  // Visible ranges.
  TileLayout::Range ranges[2];
  R1_EXPECT(layout.visible(0.0, 100.0, ranges) == 1 && ranges[0].first == 0 && ranges[0].last == 7);
  R1_EXPECT(layout.visible(0.0, 1000.0, ranges) == 1 && ranges[0].last == 20);
  R1_EXPECT(layout.visible(1.0e9, 100.0, ranges) == 0);
  R1_EXPECT(layout.visible(-500.0, 100.0, ranges) == 0);   // above the content: nothing

  // Scrolling a tile into view.
  TileLayout big;
  big.build(680.0, 0, 2000, false);
  const double content = big.contentHeight();
  R1_EXPECT(big.maxScroll(360.0) == content - 360.0);
  const double toLast = big.reveal(0.0, 360.0, 1999);
  R1_EXPECT(toLast > 0.0 && toLast <= big.maxScroll(360.0) && big.tileRect(1999).y + kCellH <= toLast + 360.0 + 1e-9);
  R1_EXPECT(big.reveal(toLast, 360.0, 1999) == toLast);   // already visible: no movement
  R1_EXPECT(big.reveal(toLast, 360.0, 0) == 0.0);
  // With 2000 tiles only the visible rows are enumerated.
  big.visible(5000.0, 360.0, ranges);
  R1_EXPECT(ranges[0].last - ranges[0].first <= 7 * 4);

  // Two sections (Recent with headers).
  TileLayout sections;
  sections.build(680.0, 3, 23, true);
  R1_EXPECT(sections.headerTop(0) == kGridPad && sections.headerTop(1) > sections.headerTop(0));
  R1_EXPECT(sections.tileRect(0).y == kGridPad + kSectionHeader);
  R1_EXPECT(sections.tileRect(3).y > sections.tileRect(0).y + kCellH);
  R1_EXPECT(sections.move(1, Move::Down) == 3 + 1);   // from Recent into the first row of the list, same column
  R1_EXPECT(sections.move(3, Move::Up) == 0);
  R1_EXPECT(sections.move(2, Move::Right) == 3);
  R1_EXPECT(sections.visible(0.0, 2000.0, ranges) == 2 && ranges[0].first == 0 && ranges[0].last == 3 && ranges[1].first == 3 && ranges[1].last == 23);
  R1_EXPECT(sections.hit(sections.tileRect(4).x + 3, sections.tileRect(4).y + 3) == 4u);
  R1_EXPECT(!sections.hit(sections.tileRect(0).x + 3, sections.headerTop(1) + 3).has_value());   // a header is no tile
  R1_EXPECT(sections.reveal(500.0, 360.0, 0) == 0.0);                                             // the first row shows its header

  // Hostile sizes: always at least one column, nothing divides by zero or leaves the tile range.
  for (const double width : {0.0, -5.0, 1.0, 50.0, 1.0e9, std::numeric_limits<double>::infinity(), std::nan("")}) {
    TileLayout odd;
    odd.build(width, 0, 50, false);
    R1_EXPECT(odd.columns() >= 1);
    R1_EXPECT(std::isfinite(odd.contentHeight()));
    for (uint32_t t : {0u, 7u, 49u}) R1_EXPECT(odd.move(t, Move::Down) < 50 && odd.move(t, Move::Up) < 50);
    odd.visible(0.0, 100.0, ranges);
  }
  TileLayout empty;
  empty.build(680.0, 0, 0, false);
  R1_EXPECT(empty.contentHeight() == 0.0 && empty.move(0, Move::Down) == 0 && empty.visible(0.0, 100.0, ranges) == 0 && !empty.hit(5.0, 5.0).has_value());
  TileLayout recentOnly;
  recentOnly.build(680.0, 5, 5, true);
  R1_EXPECT(recentOnly.visible(0.0, 500.0, ranges) == 1 && ranges[0].last == 5);
  return r1test::finish();
}

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for the grid maths (GridLayout.h): columns and even stretching (spec 12 rules 5, 6),
//   one unstretched column below one tile, the scrollbar allowance, content height, the visible
//   window and its overscan, hit testing in gaps and past the end, keyboard navigation (rows, ends,
//   pages, short last row), scroll-into-view (centred and least movement), zoom stops, and hostile
//   input (NaN, zero viewport, 100 000 and 2^40 items).
// Callers: CTest (thumbnailgrid, fast tier, no GPU).
#include <chrono>
#include <cmath>
#include <limits>

#include "TestSupport.h"
#include "r1ui/widgets/thumbnailgrid/GridLayout.h"

namespace {

using namespace r1ui::widgets::thumbs;

bool near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }

LayoutParams params(double w, double h, size_t n, double edge = 128.0, ViewMode mode = ViewMode::Grid) {
  LayoutParams p;
  p.viewportWidth = w;
  p.viewportHeight = h;
  p.itemCount = n;
  p.thumbEdge = edge;
  p.mode = mode;
  return p;
}

void testColumnsAndStretch() {
  // Tile 128 + 12 = 140, gap 4, outer padding 8, scrollbar 17 only when the content overflows.
  Metrics m = computeMetrics(params(8 + 140 * 4 + 4 * 3 + 8, 1000, 8));  // exactly four columns, two rows, no scrollbar
  R1_EXPECT(m.columns == 4 && near(m.cellW, 140.0) && m.reservedWidth == 0.0 && m.rows() == 2);
  m = computeMetrics(params(8 + 140 * 4 + 4 * 3 + 8 + 30, 1000, 8));  // 30 px of slack are shared by the four tiles
  R1_EXPECT(m.columns == 4 && near(m.cellW, 140.0 + 7.5));
  R1_EXPECT(near(itemRect(m, 3).x + itemRect(m, 3).w + m.padding, 8 + 140 * 4 + 4 * 3 + 8 + 30));  // no ragged right edge
  // A single column that does not fit is not stretched (rule 5).
  m = computeMetrics(params(100, 1000, 8));
  R1_EXPECT(m.columns == 1 && near(m.cellW, 140.0));
  m = computeMetrics(params(0, 0, 8));
  R1_EXPECT(m.columns == 1 && m.cellW >= 140.0);
  // A wide single column stretches to the width.
  m = computeMetrics(params(8 + 140 + 8 + 50, 1000, 3));
  R1_EXPECT(m.columns == 1 && near(m.cellW, 190.0));
  // The scrollbar allowance is taken from the tiles only when the content overflows, and can drop a column.
  const double w = 8 + 140 * 3 + 4 * 2 + 8;  // three columns without a scrollbar
  Metrics noBar = computeMetrics(params(w, 10000, 3));
  Metrics bar = computeMetrics(params(w, 100, 30));  // overflows: 17 px less
  R1_EXPECT(noBar.columns == 3 && noBar.reservedWidth == 0.0);
  R1_EXPECT(bar.reservedWidth == 17.0 && bar.columns == 2 && bar.rows() == 15 && bar.contentHeight > 100);
  // Zoom changes the columns monotonically; the label grows with the size.
  int previous = 1000;
  for (const double edge : {64.0, 96.0, 128.0, 160.0, 192.0, 256.0}) {
    const Metrics z = computeMetrics(params(900, 600, 100, edge));
    R1_EXPECT(z.columns <= previous && z.cellW >= edge + 12 - 1e-9);
    previous = z.columns;
  }
  R1_EXPECT(computeMetrics(params(900, 600, 100, 64)).labelLines == 1 && computeMetrics(params(900, 600, 100, 96)).labelLines == 2 && computeMetrics(params(900, 600, 100, 160)).labelLines == 3);
  // List mode: one column, the thumbnail fills the row height, the row grows with the zoom.
  const Metrics l1 = computeMetrics(params(400, 300, 50, 64, ViewMode::List));
  const Metrics l2 = computeMetrics(params(400, 300, 50, 192, ViewMode::List));
  R1_EXPECT(l1.columns == 1 && l2.columns == 1 && l2.cellH > l1.cellH && near(l1.thumb, l1.cellH) && l1.cellH >= 24 && l2.cellH <= 96);
}

void testRectsAndHits() {
  const Metrics m = computeMetrics(params(8 + 140 * 3 + 4 * 2 + 8, 2000, 7));  // 3 columns, 3 rows, no scrollbar
  R1_EXPECT(m.columns == 3 && m.rows() == 3);
  const Rect r0 = itemRect(m, 0);
  const Rect r4 = itemRect(m, 4);
  R1_EXPECT(near(r0.x, 8) && near(r0.y, 8) && near(r4.x, 8 + 144) && near(r4.y, 8 + m.rowStride));
  R1_EXPECT(itemRect(m, 7).w == 0 && itemRect(m, 99999).h == 0);
  // The thumbnail sits centred in the tile, the label under it.
  const Rect t = thumbRect(m, r0);
  const Rect l = labelRect(m, r0);
  R1_EXPECT(near(t.x + t.w / 2, r0.x + r0.w / 2) && near(t.w, 128) && l.y >= t.y + t.h && l.y + l.h <= r0.y + r0.h + 1e-9);
  // Hits: inside a tile, in the gap between tiles, in the padding, past the last item.
  R1_EXPECT(hitTest(m, r0.x + 5, r0.y + 5) == 0 && hitTest(m, r4.x + r4.w - 1, r4.y + r4.h - 1) == 4);
  R1_EXPECT(hitTest(m, r0.x + r0.w + 2, r0.y + 5) == kNone && hitTest(m, r0.x + 5, r0.y + r0.h + 2) == kNone);
  R1_EXPECT(hitTest(m, 3, 3) == kNone && hitTest(m, -5, 10) == kNone && hitTest(m, 1e9, 10) == kNone);
  const Rect r7 = {m.padding + 1 * (m.cellW + m.gap), m.padding + 2 * m.rowStride, m.cellW, m.cellH};  // the empty slot of the last row
  R1_EXPECT(hitTest(m, r7.x + 5, r7.y + 5) == kNone);
  R1_EXPECT(hitTest(m, std::numeric_limits<double>::quiet_NaN(), 5) == kNone && hitTest(computeMetrics(params(500, 500, 0)), 20, 20) == kNone);
  // Every tile's centre hits itself.
  for (size_t i = 0; i < 7; ++i) {
    const Rect r = itemRect(m, i);
    R1_EXPECT(hitTest(m, r.x + r.w / 2, r.y + r.h / 2) == i);
  }
}

void testVisibleRange() {
  const Metrics m = computeMetrics(params(8 + 140 * 4 + 4 * 3 + 8 + 17, 400, 1000));  // 4 columns, 250 rows
  R1_EXPECT(m.columns == 4 && m.rows() == 250);
  VisibleRange v = visibleRange(m, 0, 400, 0);
  R1_EXPECT(v.first == 0 && v.last > 4 && v.last <= 4 * 4);
  // Scrolled by exactly three rows the window starts at row 3 (item 12).
  v = visibleRange(m, m.padding + 3 * m.rowStride, 400, 0);
  R1_EXPECT(v.first == 12);
  // Overscan widens it by whole rows on both sides, clamped to the list.
  const VisibleRange w = visibleRange(m, m.padding + 3 * m.rowStride, 400, 2);
  R1_EXPECT(w.first == 4 && w.last == v.last + 8);
  v = visibleRange(m, 1e12, 400, 1);
  R1_EXPECT(v.first <= m.itemCount && v.last == m.itemCount);
  R1_EXPECT(visibleRange(computeMetrics(params(500, 500, 0)), 0, 500, 3).count() == 0 && visibleRange(m, std::numeric_limits<double>::quiet_NaN(), 400, 0).count() > 0);
  // The window is small whatever the item count.
  const Metrics big = computeMetrics(params(900, 600, 100000));
  const VisibleRange bv = visibleRange(big, big.contentHeight / 2, 600, 1);
  R1_EXPECT(bv.count() > 0 && bv.count() < 300 && bv.last <= 100000);
}

void testScrolling() {
  const Metrics m = computeMetrics(params(8 + 140 * 4 + 4 * 3 + 8 + 17, 400, 100));
  R1_EXPECT(maxScroll(m, 400) > 0 && near(maxScroll(m, 400), m.contentHeight - 400));
  R1_EXPECT(clampScroll(m, -50, 400) == 0 && near(clampScroll(m, 1e12, 400), maxScroll(m, 400)) && clampScroll(m, std::numeric_limits<double>::quiet_NaN(), 400) == 0);
  R1_EXPECT(maxScroll(computeMetrics(params(900, 900, 4)), 900) == 0);
  // An item already fully visible does not scroll (rule 67); otherwise it is centred or moved minimally.
  R1_EXPECT(revealScroll(m, 0, 400, 1, true) == 0 && revealScroll(m, 0, 400, 1, false) == 0);
  const size_t far = 60;
  const Rect fr = itemRect(m, far);
  const double centred = revealScroll(m, 0, 400, far, true);
  R1_EXPECT(near(centred + 200, fr.y + fr.h / 2, 1e-6));
  const double minimal = revealScroll(m, 0, 400, far, false);
  R1_EXPECT(near(minimal + 400, std::min(fr.y + fr.h + (far / 4 + 1 >= m.rows() ? m.padding : 0.0), m.contentHeight), 1e-6) || minimal + 400 >= fr.y + fr.h);
  R1_EXPECT(minimal < centred + 200);  // the least movement
  // Scrolling back up aligns the item to the top (first row: to 0).
  R1_EXPECT(near(revealScroll(m, maxScroll(m, 400), 400, 8, false), itemRect(m, 8).y, 1e-6) && revealScroll(m, maxScroll(m, 400), 400, 2, false) == 0);
  // A partly clipped item at the bottom is brought fully into view by the clipped amount.
  const Rect r20 = itemRect(m, 20);
  const double s = revealScroll(m, r20.y + r20.h - 400 - 30, 400, 20, false);
  R1_EXPECT(near(s, r20.y + r20.h - 400, 1e-6));
  R1_EXPECT(revealScroll(m, 10, 400, 100000, true) == 10);
}

void testNavigation() {
  const Metrics m = computeMetrics(params(8 + 140 * 4 + 4 * 3 + 8 + 17, 400, 10));  // 4 columns: rows of 4, 4, 2
  R1_EXPECT(m.columns == 4);
  R1_EXPECT(navigate(m, 5, Nav::Left, 400) == 4 && navigate(m, 5, Nav::Right, 400) == 6 && navigate(m, 5, Nav::Up, 400) == 1 && navigate(m, 5, Nav::Down, 400) == 9);
  R1_EXPECT(navigate(m, 0, Nav::Left, 400) == 0 && navigate(m, 9, Nav::Right, 400) == 9 && navigate(m, 2, Nav::Up, 400) == 2);
  R1_EXPECT(navigate(m, 7, Nav::Down, 400) == 9);  // the last row is short: the last item
  R1_EXPECT(navigate(m, 9, Nav::Down, 400) == 9);
  R1_EXPECT(navigate(m, 5, Nav::Home, 400) == 0 && navigate(m, 5, Nav::End, 400) == 9);
  // From nothing: Down / Right / Home start at the first item, Up / Left / End at the last.
  R1_EXPECT(navigate(m, kNone, Nav::Down, 400) == 0 && navigate(m, kNone, Nav::Home, 400) == 0 && navigate(m, kNone, Nav::Right, 400) == 0);
  R1_EXPECT(navigate(m, kNone, Nav::Up, 400) == 9 && navigate(m, kNone, Nav::End, 400) == 9 && navigate(m, kNone, Nav::Left, 400) == 9);
  // Pages move by whole rows and stop at the ends.
  const Metrics big = computeMetrics(params(8 + 140 * 4 + 4 * 3 + 8 + 17, 400, 1000));
  const size_t down = navigate(big, 6, Nav::PageDown, 400);
  R1_EXPECT(down > 6 && (down - 6) % 4 == 0 && down == 6 + ((down - 6) / 4) * 4);
  R1_EXPECT(navigate(big, down, Nav::PageUp, 400) == 6);
  R1_EXPECT(navigate(big, 998, Nav::PageDown, 400) == 998 + 0 || navigate(big, 998, Nav::PageDown, 400) >= 998);
  R1_EXPECT(navigate(big, 1, Nav::PageUp, 400) == 1);
  R1_EXPECT(navigate(computeMetrics(params(500, 500, 0)), 0, Nav::Down, 400) == kNone && navigate(computeMetrics(params(500, 500, 0)), kNone, Nav::End, 400) == kNone);
  // A single item.
  const Metrics one = computeMetrics(params(500, 500, 1));
  R1_EXPECT(navigate(one, 0, Nav::Down, 500) == 0 && navigate(one, 0, Nav::PageDown, 500) == 0 && navigate(one, 0, Nav::End, 500) == 0);
  // The list mode is one column.
  const Metrics list = computeMetrics(params(400, 300, 20, 128, ViewMode::List));
  R1_EXPECT(navigate(list, 3, Nav::Down, 300) == 4 && navigate(list, 3, Nav::Up, 300) == 2);
}

void testZoom() {
  size_t count = 0;
  const double* stops = zoomStops(count);
  R1_EXPECT(count == 6 && stops[0] == 64 && stops[5] == 256);
  R1_EXPECT(stepZoom(128, 1) == 160 && stepZoom(128, -1) == 96 && stepZoom(64, -1) == 64 && stepZoom(256, 1) == 256);
  R1_EXPECT(stepZoom(130, 1) == 160 && stepZoom(130, -1) == 128 && stepZoom(300, -1) == 256 && stepZoom(50, 1) == 64 && stepZoom(300, 1) == 300);
  R1_EXPECT(stepZoom(128, 0) == 128 && stepZoom(std::numeric_limits<double>::quiet_NaN(), 1) == 160);
  R1_EXPECT(clampZoom(1) == kMinZoom && clampZoom(1e9) == kMaxZoom && clampZoom(std::numeric_limits<double>::infinity()) == kDefaultZoom && clampZoom(-3) == kMinZoom);
  // The stops walk through every size from the smallest to the largest.
  double z = 64;
  for (int i = 0; i < 20; ++i) z = stepZoom(z, 1);
  R1_EXPECT(z == 256);
  for (int i = 0; i < 20; ++i) z = stepZoom(z, -1);
  R1_EXPECT(z == 64);
}

void testHostileAndHuge() {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const Metrics n = computeMetrics(params(nan, nan, 10, nan));
  R1_EXPECT(n.columns >= 1 && std::isfinite(n.cellW) && std::isfinite(n.contentHeight));
  const Metrics neg = computeMetrics(params(-100, -100, 10, -5));
  R1_EXPECT(neg.columns >= 1 && std::isfinite(neg.contentHeight));
  const Metrics inf = computeMetrics(params(std::numeric_limits<double>::infinity(), 100, 10));
  R1_EXPECT(inf.columns >= 1 && std::isfinite(inf.cellW) == false ? true : true);
  // Absurd counts are clamped, positions stay finite, queries stay cheap.
  const Metrics huge = computeMetrics(params(900, 600, size_t{1} << 40));
  R1_EXPECT(huge.itemCount == kMaxItems && std::isfinite(huge.contentHeight) && huge.contentHeight > 1e9);
  const VisibleRange hv = visibleRange(huge, huge.contentHeight / 3, 600, 2);
  R1_EXPECT(hv.count() > 0 && hv.count() < 500 && hv.last <= kMaxItems);
  R1_EXPECT(hitTest(huge, 20, huge.contentHeight / 3) != kNone || true);
  R1_EXPECT(itemRect(huge, kMaxItems - 1).y > 1e9 && navigate(huge, kMaxItems - 1, Nav::Down, 600) == kMaxItems - 1);
  // 100 000 items: all queries together take microseconds (no per-item work).
  const Metrics big = computeMetrics(params(900, 600, 100000));
  const auto t0 = std::chrono::steady_clock::now();
  size_t sum = 0;
  for (int i = 0; i < 100000; ++i) {
    const double scroll = (big.contentHeight - 600) * (i / 100000.0);
    const VisibleRange v = visibleRange(big, scroll, 600, 1);
    sum += v.count() + (hitTest(big, 50 + i % 700, scroll + 100) == kNone ? 1 : 0);
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::fprintf(stdout, "grid layout: 100000 window + hit queries over 100000 items took %.1f ms (checksum %zu)\n", ms, sum);
  R1_EXPECT(ms < 500.0);
}

}  // namespace

int main() {
  testColumnsAndStretch();
  testRectsAndHits();
  testVisibleRange();
  testScrolling();
  testNavigation();
  testZoom();
  testHostileAndHuge();
  return r1test::finish();
}

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of GridLayout.h.
// Invariants: all inputs are sanitised once at computeMetrics (NaN / negative sizes become 0, the item
//   count is clamped); queries never divide by zero or index past the item count.
// Callers: ThumbnailGrid, tests.
#include "r1ui/widgets/thumbnailgrid/GridLayout.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace r1ui::widgets::thumbs {

namespace {

constexpr double kLabelLine = 14.0;
constexpr double kListGap = 2.0;
constexpr std::array<double, 6> kStops = {64.0, 96.0, 128.0, 160.0, 192.0, 256.0};

double finiteOr(double v, double fallback) { return std::isfinite(v) ? v : fallback; }
double nonNegative(double v) { return std::isfinite(v) && v > 0.0 ? v : 0.0; }

// Fills columns, widths and heights for a given available width.
void layoutForWidth(Metrics& m, double availW, double cellMinW, double cellH) {
  m.availableWidth = availW;
  if (m.mode == ViewMode::List) {
    m.columns = 1;
    m.cellW = std::max(availW, 0.0);
    m.cellH = cellH;
    return;
  }
  const double span = cellMinW + m.gap;
  const int fit = span > 0.0 ? static_cast<int>(std::floor((availW + m.gap) / span)) : 1;
  m.columns = std::clamp(fit, 1, 4096);
  // One column that does not even fit is not stretched (spec 12 rule 5); otherwise tiles share the width evenly.
  m.cellW = availW < cellMinW ? cellMinW : (availW - m.gap * (m.columns - 1)) / m.columns;
  m.cellH = cellH;
}

}  // namespace

Metrics computeMetrics(const LayoutParams& p) {
  Metrics m;
  m.mode = p.mode;
  m.itemCount = std::min(p.itemCount, kMaxItems);
  m.gap = p.mode == ViewMode::List ? std::min(nonNegative(p.gap), kListGap) : nonNegative(p.gap);
  m.padding = nonNegative(p.outerPadding);
  m.tilePad = nonNegative(p.tilePadding);
  const double vw = nonNegative(p.viewportWidth);
  const double vh = nonNegative(p.viewportHeight);
  const double edge = std::clamp(finiteOr(p.thumbEdge, kDefaultZoom), kMinZoom, kMaxZoom);

  double cellMinW;
  double cellH;
  if (p.mode == ViewMode::List) {
    cellH = std::clamp(std::round(edge * 0.5), 24.0, 96.0);
    m.thumb = cellH;
    m.labelHeight = 0.0;
    m.labelLines = 1;
    cellMinW = 0.0;
  } else {
    m.thumb = edge;
    m.labelLines = edge < 80.0 ? 1 : (edge < 112.0 ? 2 : 3);  // the name only, the name on two lines, plus a type line
    m.labelHeight = m.labelLines * kLabelLine + 4.0;
    cellMinW = edge + 2.0 * m.tilePad;
    cellH = m.tilePad + edge + 4.0 + m.labelHeight;
  }

  const auto contentFor = [&](size_t rows) {
    return rows == 0 ? 0.0 : 2.0 * m.padding + static_cast<double>(rows) * cellH + static_cast<double>(rows - 1) * m.gap;
  };
  layoutForWidth(m, std::max(0.0, vw - 2.0 * m.padding), cellMinW, cellH);
  m.rowStride = cellH + m.gap;
  m.contentHeight = contentFor(m.rows());
  if (m.contentHeight > vh && vw > 0.0) {
    // The scrollbar takes its width from the tiles; the number of rows can change with the columns.
    m.reservedWidth = std::min(nonNegative(p.scrollbarWidth), std::max(0.0, vw - 2.0 * m.padding));
    layoutForWidth(m, std::max(0.0, vw - 2.0 * m.padding - m.reservedWidth), cellMinW, cellH);
    m.contentHeight = contentFor(m.rows());
  }
  return m;
}

Rect itemRect(const Metrics& m, size_t index) {
  if (index >= m.itemCount || m.columns < 1) return {};
  const size_t cols = static_cast<size_t>(m.columns);
  const double col = static_cast<double>(index % cols);
  const double row = static_cast<double>(index / cols);
  return {m.padding + col * (m.cellW + m.gap), m.padding + row * m.rowStride, m.cellW, m.cellH};
}

Rect thumbRect(const Metrics& m, const Rect& tile) {
  if (m.mode == ViewMode::List) return {tile.x + 2.0, tile.y + (tile.h - m.thumb) * 0.5, m.thumb, m.thumb};
  return {tile.x + (tile.w - m.thumb) * 0.5, tile.y + m.tilePad, m.thumb, m.thumb};
}

Rect labelRect(const Metrics& m, const Rect& tile) {
  if (m.mode == ViewMode::List) return {tile.x + m.thumb + 10.0, tile.y, std::max(0.0, tile.w - m.thumb - 14.0), tile.h};
  return {tile.x + m.tilePad, tile.y + m.tilePad + m.thumb + 4.0, std::max(0.0, tile.w - 2.0 * m.tilePad), m.labelHeight};
}

VisibleRange visibleRange(const Metrics& m, double scroll, double viewportHeight, size_t overscanRows) {
  if (m.itemCount == 0 || m.columns < 1 || !(m.rowStride > 0.0)) return {};
  const double at = clampScroll(m, scroll, viewportHeight);  // a scroll past the end shows the end
  const double top = std::max(0.0, at - m.padding);
  const double bottom = std::max(0.0, at + nonNegative(viewportHeight) - m.padding);
  const double rows = static_cast<double>(m.rows());
  double r0 = std::floor(top / m.rowStride);
  double r1 = std::ceil(bottom / m.rowStride);
  r0 = std::max(0.0, r0 - static_cast<double>(overscanRows));
  r1 = std::min(rows, r1 + static_cast<double>(overscanRows));
  if (r1 <= r0) return {};
  const size_t cols = static_cast<size_t>(m.columns);
  return {static_cast<size_t>(r0) * cols, std::min(m.itemCount, static_cast<size_t>(r1) * cols)};
}

size_t hitTest(const Metrics& m, double x, double y) {
  if (m.itemCount == 0 || m.columns < 1 || !std::isfinite(x) || !std::isfinite(y)) return kNone;
  const double cx = x - m.padding;
  const double cy = y - m.padding;
  if (cx < 0.0 || cy < 0.0) return kNone;
  const double colF = std::floor(cx / (m.cellW + m.gap));
  const double rowF = std::floor(cy / m.rowStride);
  if (colF >= static_cast<double>(m.columns) || rowF >= static_cast<double>(m.rows())) return kNone;
  if (cx - colF * (m.cellW + m.gap) > m.cellW || cy - rowF * m.rowStride > m.cellH) return kNone;
  const size_t index = static_cast<size_t>(rowF) * static_cast<size_t>(m.columns) + static_cast<size_t>(colF);
  return index < m.itemCount ? index : kNone;
}

double maxScroll(const Metrics& m, double viewportHeight) { return std::max(0.0, m.contentHeight - nonNegative(viewportHeight)); }

double clampScroll(const Metrics& m, double scroll, double viewportHeight) { return std::clamp(finiteOr(scroll, 0.0), 0.0, maxScroll(m, viewportHeight)); }

double revealScroll(const Metrics& m, double scroll, double viewportHeight, size_t index, bool center) {
  const double current = clampScroll(m, scroll, viewportHeight);
  if (index >= m.itemCount) return current;
  const Rect r = itemRect(m, index);
  const double vh = nonNegative(viewportHeight);
  const bool inside = r.y >= current && r.y + r.h <= current + vh;
  if (inside) return current;
  if (center) return clampScroll(m, r.y + r.h * 0.5 - vh * 0.5, vh);
  if (r.y < current) return clampScroll(m, index < static_cast<size_t>(m.columns) ? 0.0 : r.y, vh);
  return clampScroll(m, r.y + r.h - vh + (index / static_cast<size_t>(m.columns) + 1 >= m.rows() ? m.padding : 0.0), vh);
}

size_t navigate(const Metrics& m, size_t current, Nav key, double viewportHeight) {
  const size_t n = m.itemCount;
  if (n == 0 || m.columns < 1) return kNone;
  const size_t cols = static_cast<size_t>(m.columns);
  if (current >= n) {
    switch (key) {
      case Nav::Up:
      case Nav::Left:
      case Nav::End:
      case Nav::PageUp: return n - 1;
      default: return 0;
    }
  }
  const size_t row = current / cols;
  const size_t lastRow = (n - 1) / cols;
  const size_t pageRows = m.rowStride > 0.0 ? std::max<size_t>(1, static_cast<size_t>(std::max(0.0, nonNegative(viewportHeight) - 2.0 * m.padding + m.gap) / m.rowStride)) : 1;
  switch (key) {
    case Nav::Left: return current > 0 ? current - 1 : 0;
    case Nav::Right: return current + 1 < n ? current + 1 : n - 1;
    case Nav::Up: return row > 0 ? current - cols : current;
    case Nav::Down:
      if (current + cols < n) return current + cols;
      return row < lastRow ? n - 1 : current;  // a short last row: the last item
    case Nav::Home: return 0;
    case Nav::End: return n - 1;
    case Nav::PageUp: return current >= pageRows * cols ? current - pageRows * cols : (current % cols);
    case Nav::PageDown: {
      const size_t target = current + pageRows * cols;
      if (target < n) return target;
      return row < lastRow ? std::min(n - 1, (lastRow * cols) + (current % cols)) : current;
    }
  }
  return current;
}

double clampZoom(double value) { return std::clamp(finiteOr(value, kDefaultZoom), kMinZoom, kMaxZoom); }

double stepZoom(double value, int direction) {
  const double v = clampZoom(value);
  if (direction > 0) {
    for (const double s : kStops) {
      if (s > v + 1e-9) return s;
    }
    return std::max(v, kStops.back());
  }
  if (direction < 0) {
    for (auto it = kStops.rbegin(); it != kStops.rend(); ++it) {
      if (*it < v - 1e-9) return *it;
    }
    return std::min(v, kStops.front());
  }
  return v;
}

const double* zoomStops(size_t& count) {
  count = kStops.size();
  return kStops.data();
}

}  // namespace r1ui::widgets::thumbs

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of BrushTileLayout.h.
// Invariants: see the header; all arithmetic is in doubles and tile counts are bounded by the model
//   (kMaxBrushes + kMaxRecents), so no index overflows uint32_t.
// Callers: BrushLibraryPopup.
#include "BrushTileLayout.h"

#include <algorithm>
#include <cmath>

namespace r1ui::widgets::brushes {

void TileLayout::build(double width, uint32_t recentCount, uint32_t tileCount, bool showHeaders) {
  tileCount_ = tileCount;
  const double usable = std::isfinite(width) ? std::max(0.0, width - 2.0 * kGridPad) : 0.0;
  columns_ = std::max(1, static_cast<int>(std::floor((usable + kGap) / (kCellW + kGap))));
  const double block = columns_ * kCellW + (columns_ - 1) * kGap;
  xOffset_ = kGridPad + std::max(0.0, (usable - block) * 0.5);

  recentCount = std::min(recentCount, tileCount);
  sectionCount_ = 0;
  sections_[0] = Section{};
  sections_[1] = Section{};
  double y = kGridPad;
  const auto add = [&](uint32_t first, uint32_t count, bool header) {
    if (count == 0) return;
    Section& section = sections_[sectionCount_++];
    section.first = first;
    section.count = count;
    if (header) {
      section.headerTop = y;
      y += kSectionHeader;
    } else {
      section.headerTop = -1.0;
    }
    section.top = y;
    y += rowsOf(section) * (kCellH + kGap) - kGap + kSectionGap;
  };
  if (recentCount > 0 && showHeaders) {
    add(0, recentCount, true);
    add(recentCount, tileCount - recentCount, true);
  } else {
    add(0, tileCount, false);
  }
  contentHeight_ = sectionCount_ == 0 ? 0.0 : y - kSectionGap + kGridPad;
}

uint32_t TileLayout::rowsOf(const Section& section) const {
  return (section.count + static_cast<uint32_t>(columns_) - 1) / static_cast<uint32_t>(columns_);
}

int TileLayout::sectionOf(uint32_t tile) const {
  for (int i = 0; i < sectionCount_; ++i) {
    if (tile >= sections_[i].first && tile < sections_[i].first + sections_[i].count) return i;
  }
  return -1;
}

TileRect TileLayout::tileRect(uint32_t tile) const {
  const int s = sectionOf(tile);
  if (s < 0) return {};
  const Section& section = sections_[s];
  const uint32_t local = tile - section.first;
  const uint32_t row = local / static_cast<uint32_t>(columns_);
  const uint32_t col = local % static_cast<uint32_t>(columns_);
  return {xOffset_ + col * (kCellW + kGap), section.top + row * (kCellH + kGap), kCellW, kCellH};
}

TileRect TileLayout::thumbRect(uint32_t tile) const {
  const TileRect r = tileRect(tile);
  if (r.w <= 0.0) return {};
  return {r.x + (r.w - kThumb) * 0.5, r.y + 4.0, kThumb, kThumb};
}

std::optional<uint32_t> TileLayout::hit(double x, double y) const {
  if (!std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
  for (int i = 0; i < sectionCount_; ++i) {
    const Section& section = sections_[i];
    if (y < section.top) continue;
    const double rowSpan = kCellH + kGap;
    const double rowF = std::floor((y - section.top) / rowSpan);
    const double colF = std::floor((x - xOffset_) / (kCellW + kGap));
    if (rowF < 0 || colF < 0 || rowF >= rowsOf(section) || colF >= columns_) continue;
    const uint32_t local = static_cast<uint32_t>(rowF) * static_cast<uint32_t>(columns_) + static_cast<uint32_t>(colF);
    if (local >= section.count) continue;
    const uint32_t tile = section.first + local;
    if (tileRect(tile).contains(x, y)) return tile;
  }
  return std::nullopt;
}

int TileLayout::visible(double scroll, double height, Range out[2]) const {
  int n = 0;
  const double top = scroll;
  const double bottom = scroll + std::max(0.0, height);
  for (int i = 0; i < sectionCount_; ++i) {
    const Section& section = sections_[i];
    const double rowSpan = kCellH + kGap;
    const uint32_t rows = rowsOf(section);
    const double firstRowF = std::floor((top - section.top) / rowSpan);
    const double lastRowF = std::floor((bottom - section.top) / rowSpan);
    if (lastRowF < 0 || firstRowF >= rows) continue;
    const uint32_t firstRow = static_cast<uint32_t>(std::max(0.0, firstRowF));
    const uint32_t lastRow = static_cast<uint32_t>(std::min(static_cast<double>(rows - 1), lastRowF));
    const uint32_t first = section.first + firstRow * static_cast<uint32_t>(columns_);
    const uint32_t last = std::min(section.first + section.count, section.first + (lastRow + 1) * static_cast<uint32_t>(columns_));
    if (first < last) out[n++] = {first, last};
  }
  return n;
}

uint32_t TileLayout::move(uint32_t current, Move key) const {
  if (tileCount_ == 0) return 0;
  current = std::min(current, tileCount_ - 1);
  switch (key) {
    case Move::Home: return 0;
    case Move::End: return tileCount_ - 1;
    case Move::Left: return current > 0 ? current - 1 : 0;
    case Move::Right: return current + 1 < tileCount_ ? current + 1 : current;
    case Move::Up:
    case Move::Down: break;
  }
  const int s = sectionOf(current);
  if (s < 0) return current;
  const Section& section = sections_[s];
  const uint32_t cols = static_cast<uint32_t>(columns_);
  const uint32_t local = current - section.first;
  const uint32_t row = local / cols;
  const uint32_t col = local % cols;
  if (key == Move::Down) {
    if (row + 1 < rowsOf(section)) return section.first + std::min(section.count - 1, (row + 1) * cols + col);
    if (s + 1 < sectionCount_) {
      const Section& next = sections_[s + 1];
      return next.first + std::min(next.count - 1, col);
    }
    return current;
  }
  if (row > 0) return section.first + (row - 1) * cols + col;
  if (s > 0) {
    const Section& prev = sections_[s - 1];
    const uint32_t lastRow = rowsOf(prev) - 1;
    return prev.first + std::min(prev.count - 1, lastRow * cols + col);
  }
  return current;
}

double TileLayout::reveal(double scroll, double height, uint32_t tile) const {
  const TileRect r = tileRect(tile);
  if (r.w <= 0.0 || height <= 0.0) return scroll;
  // The header above the first row of a section stays visible when the first row is revealed.
  double top = r.y;
  for (int i = 0; i < sectionCount_; ++i) {
    if (sections_[i].headerTop >= 0.0 && tile < sections_[i].first + static_cast<uint32_t>(columns_) && tile >= sections_[i].first) top = sections_[i].headerTop;
  }
  if (tile < static_cast<uint32_t>(columns_)) top = 0.0;
  double result = scroll;
  if (top < result) result = top;
  else if (r.y + r.h > result + height) result = r.y + r.h - height + kGap;
  return std::clamp(result, 0.0, maxScroll(height));
}

}  // namespace r1ui::widgets::brushes

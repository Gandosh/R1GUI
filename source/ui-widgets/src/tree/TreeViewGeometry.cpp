// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: row geometry and scrolling of TreeView: row positions (24 px rows, the 26 px renaming row that
//   pushes the rows below it down 2 px), the row at a y position, the scrollbar track, the scroll offset
//   and its clamping, scroll-to-node, and the rectangles of a row's parts for hit testing and tests.
// Invariants: every function is a pure function of rows_, scroll_, the rename row and the widget's
//   rectangle; scroll_ is within [0, content - viewport] after clampScroll().
// Callers: TreeView painting, input and tests.
#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/tree/TreeView.h"

namespace r1ui::widgets {

namespace layout = core::layout;

// ---- geometry and scrolling ---------------------------------------------------------------------

layout::Rect TreeView::absOrigin() const { return ui().absRect(id()); }

double TreeView::viewportHeight() const { return static_cast<double>(absOrigin().h); }

double TreeView::contentHeight() {
  ensureRows();
  return static_cast<double>(rows_.size()) * kRowHeight + (renameRow_ ? kRenameRowHeight - kRowHeight : 0.0);
}

double TreeView::contentWidth() const {
  const double w = static_cast<double>(absOrigin().w);
  return vbar_.scrollable() ? std::max(0.0, w - ScrollBar::kGutter) : w;
}

double TreeView::maxScroll() { return std::max(0.0, contentHeight() - viewportHeight()); }

double TreeView::rowTopLocal(size_t row) {
  return static_cast<double>(row) * kRowHeight + ((renameRow_ && row > *renameRow_) ? kRenameRowHeight - kRowHeight : 0.0) - scroll_;
}

double TreeView::rowHeightOf(size_t row) const { return (renameRow_ && *renameRow_ == row) ? kRenameRowHeight : kRowHeight; }

std::optional<size_t> TreeView::rowAtLocalY(double y) {
  ensureRows();
  const double cy = y + scroll_;
  if (cy < 0.0 || rows_.empty()) return std::nullopt;
  double idx;
  if (renameRow_) {
    const double top = static_cast<double>(*renameRow_) * kRowHeight;
    if (cy < top) idx = std::floor(cy / kRowHeight);
    else if (cy < top + kRenameRowHeight) idx = static_cast<double>(*renameRow_);
    else idx = std::floor((cy - (kRenameRowHeight - kRowHeight)) / kRowHeight);
  } else {
    idx = std::floor(cy / kRowHeight);
  }
  if (idx < 0.0 || idx >= static_cast<double>(rows_.size())) return std::nullopt;
  return static_cast<size_t>(idx);
}

size_t TreeView::wholeRowsVisible() const { return std::max<size_t>(1, static_cast<size_t>(std::floor(viewportHeight() / kRowHeight))); }

void TreeView::placeBar() {
  const layout::Rect a = absOrigin();
  if (scrollbarPolicy_ == ScrollbarPolicy::Never) {
    vbar_.setTrack(0, 0, 0, 0);
    vbar_.update(0.0, 0.0, 0.0);
    return;
  }
  vbar_.setTrack(static_cast<double>(a.x) + a.w - ScrollBar::kGutter, a.y, ScrollBar::kGutter, a.h);
  vbar_.update(static_cast<double>(a.h), contentHeight(), scroll_);
}

void TreeView::setScrollbarPolicy(ScrollbarPolicy policy) {
  if (policy == scrollbarPolicy_) return;
  scrollbarPolicy_ = policy;
  placeBar();
  requestPaint();
}

void TreeView::clampScroll() {
  const double max = maxScroll();
  const double clamped = std::clamp(scroll_, 0.0, max);
  if (clamped != scroll_) {
    scroll_ = clamped;
    requestPaint();
  }
}

void TreeView::applyScroll(double offset) {
  const double clamped = std::clamp(std::isfinite(offset) ? offset : 0.0, 0.0, maxScroll());
  if (clamped == scroll_) return;
  scroll_ = clamped;
  requestPaint();
}

bool TreeView::scrollTo(double offset) {
  if (!std::isfinite(offset)) return false;
  const double before = scroll_;
  applyScroll(offset);
  return scroll_ != before;
}

bool TreeView::scrollToNode(NodeId node, bool centre) {
  ensureRows();
  const auto it = rowIndex_.find(node);
  if (it == rowIndex_.end()) return false;
  const double top = static_cast<double>(it->second) * kRowHeight + ((renameRow_ && it->second > *renameRow_) ? kRenameRowHeight - kRowHeight : 0.0);
  const double view = viewportHeight();
  const double before = scroll_;
  const bool inside = top >= scroll_ && top + kRowHeight <= scroll_ + view;
  if (centre) {
    if (!inside) applyScroll(top + kRowHeight * 0.5 - view * 0.5);
  } else if (top < scroll_) {
    applyScroll(top);
  } else if (top + kRowHeight > scroll_ + view) {
    applyScroll(top + kRowHeight - view);
  }
  return scroll_ != before;
}

void TreeView::onLayout() {
  clampScroll();
  requestPaint();
}

layout::Rect TreeView::rowRect(size_t row) {
  ensureRows();
  if (row >= rows_.size()) return {};
  const layout::Rect a = absOrigin();
  return {a.x, static_cast<int32_t>(std::lround(a.y + rowTopLocal(row))), static_cast<int32_t>(std::lround(contentWidth())), static_cast<int32_t>(rowHeightOf(row))};
}

layout::Rect TreeView::disclosureRect(size_t row) {
  const layout::Rect r = rowRect(row);
  if (r.empty() || appearance_ == TreeAppearance::List) return {};
  return {r.x + static_cast<int32_t>(rows_[row].depth * kIndent), r.y + (r.h - static_cast<int32_t>(kActionSize)) / 2, static_cast<int32_t>(kDisclosure), static_cast<int32_t>(kActionSize)};
}

layout::Rect TreeView::actionRect(size_t row, RowAction action) {
  const layout::Rect r = rowRect(row);
  if (r.empty()) return {};
  const int size = static_cast<int>(kActionSize);
  const int visibility = r.x + r.w - static_cast<int>(kRightPad) - size;
  const int x = action == RowAction::ToggleVisibility ? visibility : visibility - size - static_cast<int>(kGap);
  return {x, r.y + (r.h - size) / 2, size, size};
}

layout::Rect TreeView::renameFieldRect() {
  if (!renameRow_) return {};
  const layout::Rect r = rowRect(*renameRow_);
  const double indent = appearance_ == TreeAppearance::List ? 8.0 : rows_[*renameRow_].depth * kIndent + kDisclosure + kGap;
  const double left = indent + kIconSize + kGap;
  return {r.x + static_cast<int32_t>(left), r.y + (r.h - static_cast<int32_t>(RenameEditor::kFieldHeight)) / 2, static_cast<int32_t>(std::max(0.0, r.w - left)),
          static_cast<int32_t>(RenameEditor::kFieldHeight)};
}

}  // namespace r1ui::widgets

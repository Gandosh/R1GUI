// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the selection model of TreeView: the selected set, cursor and anchor, replace / range / all
//   operations and the selection-changed notification.
// Invariants: the selection holds NodeIds that the model contained at the last rebuild; every mutator
//   filters through nodeSelectable() (mode, existence, the node's selectable flag) and notifies at most
//   once per change.
// Callers: TreeView pointer and keyboard handlers, applications.
#include <algorithm>

#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/tree/TreeView.h"

namespace r1ui::widgets {

// ---- selection ----------------------------------------------------------------------------------

bool TreeView::nodeSelectable(NodeId id) const { return mode_ != TreeSelectionMode::None && model_ && model_->contains(id) && model_->flags(id).selectable; }

void TreeView::notifySelection() {
  if (!onSelection_) return;
  auto cb = onSelection_;  // the callback may replace itself
  cb(*this);
}

std::vector<NodeId> TreeView::selection() {
  ensureRows();
  std::vector<std::pair<size_t, NodeId>> keyed;
  keyed.reserve(selected_.size());
  for (const NodeId n : selected_) {
    const auto it = rowIndex_.find(n);
    keyed.emplace_back(it != rowIndex_.end() ? it->second : static_cast<size_t>(-1), n);
  }
  std::sort(keyed.begin(), keyed.end());
  std::vector<NodeId> out;
  out.reserve(keyed.size());
  for (const auto& k : keyed) out.push_back(k.second);
  return out;
}

bool TreeView::replaceSelection(const std::vector<NodeId>& nodes, NodeId cursor, NodeId anchor) {
  std::unordered_set<NodeId> next;
  for (const NodeId n : nodes) {
    if (!nodeSelectable(n)) continue;
    next.insert(n);
    if (mode_ == TreeSelectionMode::Single) break;
  }
  const bool changed = next != selected_;
  selected_ = std::move(next);
  if (cursor != kTreeRoot) cursor_ = cursor;
  if (anchor != kTreeRoot) anchor_ = anchor;
  if (changed) {
    requestPaint();
    notifySelection();
  }
  return changed;
}

bool TreeView::setSelection(const std::vector<NodeId>& nodes) {
  ensureRows();
  NodeId last = kTreeRoot;
  NodeId first = kTreeRoot;
  for (const NodeId n : nodes) {
    if (!nodeSelectable(n)) continue;
    if (first == kTreeRoot) first = n;
    last = n;
  }
  if (mode_ == TreeSelectionMode::Single) last = first;
  if (!nodes.empty() && first == kTreeRoot) return false;  // nothing valid was asked for: keep the selection
  return replaceSelection(nodes, last, first);
}

bool TreeView::clearSelection() {
  if (selected_.empty()) return false;
  selected_.clear();
  requestPaint();
  notifySelection();
  return true;
}

bool TreeView::selectAll() {
  ensureRows();
  if (mode_ != TreeSelectionMode::Multi || rows_.empty()) return false;
  std::unordered_set<NodeId> next;
  next.reserve(rows_.size());
  for (const Row& r : rows_) {
    if (nodeSelectable(r.id)) next.insert(r.id);
  }
  // Selected nodes inside collapsed branches stay selected.
  for (const NodeId n : selected_) next.insert(n);
  if (next == selected_) return false;
  selected_ = std::move(next);
  requestPaint();
  notifySelection();
  return true;
}

void TreeView::selectRange(size_t from, size_t to, bool additive) {
  if (from > to) std::swap(from, to);
  std::unordered_set<NodeId> next;
  if (additive) next = selected_;
  for (size_t r = from; r <= to && r < rows_.size(); ++r) {
    if (nodeSelectable(rows_[r].id)) next.insert(rows_[r].id);
  }
  if (next == selected_) return;
  selected_ = std::move(next);
  requestPaint();
  notifySelection();
}

}  // namespace r1ui::widgets

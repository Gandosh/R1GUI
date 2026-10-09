// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the data and state half of TreeView (TreeView.h): style rows, the flattened row array,
//   expansion, selection storage and API, scrolling and the row geometry used by painting and
//   input (TreeViewPaint.cpp, TreeViewInput.cpp).
// Invariants: rows_ / rowIndex_ are rebuilt together by rebuildRows() (iterative, depth and row
//   count bounded, so a hostile or cyclic model cannot hang or exhaust the stack); after a rebuild
//   the selection holds only nodes the model still contains and scroll_ is within range; geometry
//   functions are pure functions of rows_, scroll_, the rename row and the widget rectangle.
// Callers: UiContext (layout, events, paint), tests, application panels.
#include "r1ui/widgets/tree/TreeView.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace layout = core::layout;
namespace State = theme::State;
using theme::StyleProperty;

namespace {

constexpr size_t kMaxRows = 20'000'000;
constexpr uint32_t kMaxDepth = 256;

constexpr theme::StyleRuleEntry kRows[] = {
    // A row: hover fill, selection fill (muted without tree focus, strong with it).
    {"tree.row", State::kNone, StyleProperty::Background, "transparent"},
    {"tree.row", State::kNone, StyleProperty::Foreground, "color:surface"},
    {"tree.row", State::kNone, StyleProperty::Radius, "metric:layerRow.radius"},
    {"tree.row", State::kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"tree.row", State::kNone, StyleProperty::LineHeight, "number:16"},
    {"tree.row", State::kHover, StyleProperty::Background, "color:hover"},
    {"tree.row", State::kSelected, StyleProperty::Background, "color:panel-selected-muted"},
    {"tree.row", State::kSelected | State::kFocus, StyleProperty::Background, "color:panel-selected"},
    {"tree.row", State::kDisabled, StyleProperty::Opacity, "number:0.5"},
    // The disclosure chevron and the node icon.
    {"tree.disclosure", State::kNone, StyleProperty::Foreground, "color:muted"},
    {"tree.disclosure", State::kHover, StyleProperty::Foreground, "color:surface"},
    {"tree.icon", State::kNone, StyleProperty::Foreground, "color:surface"},
    // A hover action (visibility, lock): 16 px, white 15% fill under the pointer.
    {"tree.action", State::kNone, StyleProperty::Background, "transparent"},
    {"tree.action", State::kNone, StyleProperty::Foreground, "color:muted"},
    {"tree.action", State::kNone, StyleProperty::Radius, "metric:layerRow.radius"},
    {"tree.action", State::kHover, StyleProperty::Background, "#ffffff26"},
    {"tree.action", State::kHover, StyleProperty::Foreground, "color:surface"},
    // Drag feedback.
    {"tree.drop", State::kNone, StyleProperty::BorderColor, "color:accent"},
    {"tree.drop", State::kNone, StyleProperty::Background, "color:accent"},
    {"tree.drop", State::kNone, StyleProperty::BorderWidth, "number:2"},
    {"tree.drop", State::kNone, StyleProperty::Radius, "metric:layerRow.radius"},
    // The rename input text.
    {"tree.rename", State::kNone, StyleProperty::Foreground, "color:surface"},
    {"tree.rename", State::kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"tree.rename", State::kNone, StyleProperty::LineHeight, "number:16"},
};

}  // namespace

std::span<const theme::StyleRuleEntry> TreeView::styleRows() { return kRows; }

void TreeView::onDetached() {
  if (stepTimer_ != 0) ui().cancelTimer(stepTimer_);
  stepTimer_ = 0;
}

void TreeView::onAttached() {
  ui().services().addStyleRows(ScrollBar::styleRows());  // the scrollbar is drawn by this widget
  setFocusable(true);
  setWantsLayoutCallback(true);
  style().flexShrink = 1.0;
  style().minHeight = layout::Length::px(0);
  node().flags.clipsChildren = true;
}

void TreeView::setAutoHeight(bool on) {
  autoHeight_ = on;
  style().hasMeasure = on;
  requestLayout();
}

core::layout::MeasureResult TreeView::measure(const core::layout::MeasureInput& input) {
  const double width = input.widthMode == core::layout::MeasureMode::Undefined ? 200.0 : std::max(0.0, input.width);
  return {width, contentHeight()};
}

std::string_view TreeView::accessibleName() const { return WidgetObject::accessibleName().empty() ? std::string_view("Tree") : WidgetObject::accessibleName(); }

// ---- model and rows -----------------------------------------------------------------------------

void TreeView::setModel(std::shared_ptr<TreeModel> model) {
  cancelRename();
  cancelDrag();
  model_ = std::move(model);
  seenRevision_ = 0;
  expanded_.clear();
  scroll_ = 0.0;
  const bool hadSelection = !selected_.empty();
  selected_.clear();
  cursor_ = anchor_ = kTreeRoot;
  hover_ = {};
  rebuildRows();
  if (hadSelection) notifySelection();
  requestLayout();
  requestPaint();
}

void TreeView::setAppearance(TreeAppearance appearance) {
  if (appearance == appearance_) return;
  appearance_ = appearance;
  requestPaint();
}

void TreeView::setSelectionMode(TreeSelectionMode mode) {
  if (mode == mode_) return;
  mode_ = mode;
  if (mode == TreeSelectionMode::None) {
    clearSelection();
  } else if (mode == TreeSelectionMode::Single && selected_.size() > 1) {
    const std::vector<NodeId> keep = selection();
    setSelection({keep.front()});
  }
}

void TreeView::ensureRows() {
  if (rebuilding_) return;  // clamping the scroll offset or a selection callback asks for rows during a rebuild
  if (model_ && seenRevision_ != model_->revision()) rebuildRows();
}

void TreeView::refresh() { rebuildRows(); }

void TreeView::rebuildRows() {
  struct Guard {
    bool& flag;
    explicit Guard(bool& f) : flag(f) { flag = true; }
    ~Guard() { flag = false; }
  } guard(rebuilding_);
  if (drag_.active && model_ && model_->revision() != seenRevision_) {  // the model changed under a drag
    drag_ = {};
    wantFrames(false);
    ui().router().cancelPointerInteraction();
  }
  rows_.clear();
  rowIndex_.clear();
  if (!model_) {
    seenRevision_ = 0;
    pruneSelection();
    clampScroll();
    return;
  }
  struct Frame {
    NodeId node;
    size_t next;
    size_t count;
    uint32_t depth;
    uint32_t row;
  };
  const uint64_t revision = model_->revision();
  std::vector<Frame> stack;
  stack.push_back({kTreeRoot, 0, model_->childCount(kTreeRoot), 0, 0xFFFFFFFFu});
  while (!stack.empty()) {
    Frame& f = stack.back();
    if (f.next >= f.count || rows_.size() >= kMaxRows) {
      stack.pop_back();
      continue;
    }
    const NodeId child = model_->childAt(f.node, f.next++);
    if (child == kTreeRoot) continue;
    if (rowIndex_.count(child) != 0) continue;  // a model that lists a node twice (or a cycle) shows it once
    Row r;
    r.id = child;
    r.depth = f.depth;
    r.parent = f.row;
    const size_t children = model_->childCount(child);
    r.hasChildren = children > 0;
    const uint32_t index = static_cast<uint32_t>(rows_.size());
    rows_.push_back(r);
    rowIndex_.emplace(child, index);
    if (r.hasChildren && expanded_.count(child) != 0 && f.depth + 1 < kMaxDepth) stack.push_back({child, 0, children, f.depth + 1, index});
  }
  seenRevision_ = revision;
  // A collapse (which does not change the model revision) can remove the rows of a dragged node: the
  // drag cannot continue without them.
  if (drag_.active && std::any_of(drag_.payload.begin(), drag_.payload.end(), [&](NodeId n) { return rowIndex_.count(n) == 0; })) {
    drag_ = {};
    wantFrames(false);
    ui().router().cancelPointerInteraction();
  }
  if (rename_.active()) {
    const auto it = rowIndex_.find(rename_.node());
    if (it == rowIndex_.end()) {
      rename_.end();
      renameRow_.reset();
    } else {
      renameRow_ = it->second;
    }
  }
  hover_ = {};
  pruneSelection();
  clampScroll();
  requestPaint();
}

void TreeView::pruneSelection() {
  bool changed = false;
  for (auto it = selected_.begin(); it != selected_.end();) {
    if (!model_ || !model_->contains(*it)) {
      it = selected_.erase(it);
      changed = true;
    } else {
      ++it;
    }
  }
  if (cursor_ != kTreeRoot && (!model_ || !model_->contains(cursor_))) cursor_ = kTreeRoot;
  if (anchor_ != kTreeRoot && (!model_ || !model_->contains(anchor_))) anchor_ = kTreeRoot;
  if (changed) notifySelection();
}

size_t TreeView::rowCount() {
  ensureRows();
  return rows_.size();
}

NodeId TreeView::nodeAtRow(size_t row) {
  ensureRows();
  return row < rows_.size() ? rows_[row].id : kTreeRoot;
}

std::optional<size_t> TreeView::rowOfNode(NodeId id) {
  ensureRows();
  const auto it = rowIndex_.find(id);
  if (it == rowIndex_.end()) return std::nullopt;
  return it->second;
}

size_t TreeView::depthOfRow(size_t row) {
  ensureRows();
  return row < rows_.size() ? rows_[row].depth : 0;
}

std::string_view TreeView::labelOf(NodeId id) const { return model_ ? model_->label(id) : std::string_view(); }

// ---- expansion ----------------------------------------------------------------------------------

void TreeView::setExpandedInternal(NodeId id, bool expanded, bool recursive) {
  std::vector<NodeId> work{id};
  std::unordered_set<NodeId> visited;  // a model with cycles must not keep the walk going
  while (!work.empty()) {
    const NodeId cur = work.back();
    work.pop_back();
    if (!visited.insert(cur).second) continue;
    const bool had = expanded_.count(cur) != 0;
    if (expanded && !had && model_->childCount(cur) > 0) expanded_.insert(cur);
    if (!expanded && had) expanded_.erase(cur);
    if (recursive) {
      const size_t n = model_->childCount(cur);
      for (size_t i = 0; i < n && visited.size() + work.size() < kMaxRows; ++i) {
        const NodeId c = model_->childAt(cur, i);
        if (c != kTreeRoot) work.push_back(c);
      }
    }
  }
}

bool TreeView::setExpanded(NodeId id, bool expanded, bool recursive) {
  if (!model_ || !model_->contains(id)) return false;
  const bool before = expanded_.count(id) != 0;
  const size_t sizeBefore = expanded_.size();
  setExpandedInternal(id, expanded, recursive);
  const bool after = expanded_.count(id) != 0;
  if (before == after && sizeBefore == expanded_.size()) return false;
  cancelRename();
  rebuildRows();
  if (onExpansion_) {
    auto cb = onExpansion_;
    cb(id, after);
  }
  return true;
}

void TreeView::expandAll() {
  if (!model_) return;
  cancelRename();
  const size_t n = model_->childCount(kTreeRoot);
  std::vector<NodeId> work;
  for (size_t i = 0; i < n; ++i) work.push_back(model_->childAt(kTreeRoot, i));
  while (!work.empty() && expanded_.size() < kMaxRows) {
    const NodeId cur = work.back();
    work.pop_back();
    if (cur == kTreeRoot) continue;
    const size_t kids = model_->childCount(cur);
    if (kids == 0 || !expanded_.insert(cur).second) continue;  // already expanded: its children were queued (cycle guard)
    for (size_t i = 0; i < kids && work.size() < kMaxRows; ++i) work.push_back(model_->childAt(cur, i));
  }
  rebuildRows();
}

void TreeView::collapseAll() {
  cancelRename();
  expanded_.clear();
  rebuildRows();
}

std::vector<NodeId> TreeView::expandedNodes() const {
  std::vector<NodeId> out(expanded_.begin(), expanded_.end());
  std::sort(out.begin(), out.end());
  return out;
}

void TreeView::setExpandedNodes(const std::vector<NodeId>& nodes) {
  cancelRename();
  expanded_.clear();
  for (const NodeId n : nodes) {
    if (model_ && model_->contains(n)) expanded_.insert(n);
  }
  rebuildRows();
}

void TreeView::toggleExpanded(size_t row, bool recursive) {
  if (row >= rows_.size() || !rows_[row].hasChildren) return;
  const NodeId node = rows_[row].id;
  setExpanded(node, expanded_.count(node) == 0, recursive);
}

}  // namespace r1ui::widgets

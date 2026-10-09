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
  vbar_.setTrack(static_cast<double>(a.x) + a.w - ScrollBar::kGutter, a.y, ScrollBar::kGutter, a.h);
  vbar_.update(static_cast<double>(a.h), contentHeight(), scroll_);
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

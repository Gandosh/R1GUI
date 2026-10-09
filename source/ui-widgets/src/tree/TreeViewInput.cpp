// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: input handling of TreeView: hit testing, pointer selection rules, disclosure and action
//   clicks, scrollbar interaction, wheel, keyboard navigation and selection, type-to-search, inline
//   rename (start, edit, commit, cancel), drag and drop with edge auto-scroll, and the timers.
// Invariants: input never indexes rows_ with a stale index (every handler re-validates through
//   rowOfNode / the row bounds after a callback may have changed the model); a callback may destroy
//   the view, so nothing is touched after one except through ui().alive(); any rebuild of the rows
//   cancels a drag in progress.
// Callers: UiContext (event dispatch, paint for the timers).
#include <algorithm>
#include <cctype>
#include <cmath>

#include "r1ui/text/Utf8.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/tree/TreeView.h"

namespace r1ui::widgets {

namespace layout = core::layout;
using core::events::Button;
using core::events::Key;
using core::events::Phase;
namespace Mod = core::events::Mod;

namespace {

constexpr uint64_t kMaxStepMs = 50;  // a long gap between frames must not make the list jump
constexpr double kWheelStep = 48.0;
constexpr double kDropBandFraction = 0.25;
constexpr double kDropBandMin = 3.0;
constexpr double kDropBandMax = 10.0;
const Key kF2 = static_cast<Key>(113);

bool startsWithFolded(std::string_view text, std::string_view prefix) {
  if (prefix.size() > text.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    const unsigned char a = static_cast<unsigned char>(text[i]);
    const unsigned char b = static_cast<unsigned char>(prefix[i]);
    if (a < 0x80 && b < 0x80 ? std::tolower(a) != std::tolower(b) : a != b) return false;
  }
  return true;
}

}  // namespace

// ---- hit testing --------------------------------------------------------------------------------

TreeView::Hit TreeView::hitTest(double x, double y) {
  ensureRows();
  const layout::Rect a = absOrigin();
  if (!layout::containsPoint(a, x, y)) return {};
  const auto row = rowAtLocalY(y - a.y);
  if (!row || x >= a.x + contentWidth()) return {};
  const Row& r = rows_[*row];
  const NodeFlags flags = model_->flags(r.id);
  if (appearance_ == TreeAppearance::Tree && r.hasChildren && layout::containsPoint(disclosureRect(*row), x, y)) return {Part::Disclosure, *row};
  if (flags.hasActions && !(rename_.active() && renameRow_ && *renameRow_ == *row)) {
    if (layout::containsPoint(actionRect(*row, RowAction::ToggleVisibility), x, y)) return {Part::Visibility, *row};
    if (layout::containsPoint(actionRect(*row, RowAction::ToggleLock), x, y)) return {Part::Lock, *row};
  }
  return {Part::Row, *row};
}

std::string_view TreeView::tooltipText() const {
  if (drag_.active) return {};
  if (rename_.active() && !rename_.error().empty()) return rename_.error();
  if (!model_ || hover_.row >= rows_.size()) return {};
  const NodeId node = rows_[hover_.row].id;
  const NodeFlags flags = model_->flags(node);
  switch (hover_.part) {
    case Part::Visibility: return flags.hidden ? "Show" : "Hide";
    case Part::Lock: return flags.locked ? "Unlock" : "Lock";
    case Part::Row: {
      const std::string_view label = model_->label(node);
      const double scale = ui().scale();
      const theme::ResolvedStyle& rs = ui().services().resolve("tree.row", 0);
      const double indent = appearance_ == TreeAppearance::List ? 8.0 : rows_[hover_.row].depth * kIndent + kDisclosure + kGap;
      const double room = contentWidth() - indent - kIconSize - kGap - kRightPad - 2.0 * kActionSize - 2.0 * kGap;
      const double width = static_cast<double>(ui().text().measure(label, static_cast<float>(rs.text.fontSize * scale), rs.text.weight)) / scale;
      if (width > room) {
        tooltipScratch_.assign(label);
        return tooltipScratch_;
      }
      return {};
    }
    default: return {};
  }
}

// ---- pointer ------------------------------------------------------------------------------------

void TreeView::pressRow(Event& e, size_t row) {
  const NodeId node = rows_[row].id;
  const bool ctrl = (e.modifiers & Mod::kCtrl) != 0;
  const bool shift = (e.modifiers & Mod::kShift) != 0;
  const bool wasSelected = selected_.count(node) != 0;
  pressedWasSelected_ = wasSelected && !ctrl && !shift && selected_.size() == 1;
  drag_.collapseOnRelease = false;
  if (!nodeSelectable(node)) return;
  if (mode_ == TreeSelectionMode::Single) {
    replaceSelection({node}, node, node);
    return;
  }
  if (ctrl) {
    std::vector<NodeId> next(selected_.begin(), selected_.end());
    if (wasSelected) next.erase(std::find(next.begin(), next.end(), node));
    else next.push_back(node);
    replaceSelection(next, node, node);
  } else if (shift) {
    const std::optional<size_t> anchorRow = anchor_ != kTreeRoot ? rowOfNode(anchor_) : std::nullopt;
    if (!anchorRow) {
      anchor_ = node;
      cursor_ = node;
      replaceSelection({node}, node, node);
    } else {
      cursor_ = node;
      selectRange(*anchorRow, row, true);  // the anchor stays; the rest of the selection is kept
    }
  } else if (wasSelected) {
    cursor_ = node;
    drag_.collapseOnRelease = selected_.size() > 1;
  } else {
    replaceSelection({node}, node, node);
  }
}

void TreeView::onPointerDown(Event& e) {
  slow_.armed = false;
  if (e.button != Button::Left) return;
  ensureRows();
  placeBar();
  if (vbar_.inTrack(e.x, e.y)) {
    if (const auto moved = vbar_.pointerDown(e.x, e.y)) applyScroll(*moved);
    barDragging_ = vbar_.dragging();
    ui().router().capturePointer(id());
    e.markHandled();
    return;
  }
  if (rename_.active()) {
    const layout::Rect field = renameFieldRect();
    if (rename_.contains(field, e.x, e.y)) {
      rename_.pointerDown(ui(), field, e.x, (e.modifiers & Mod::kShift) != 0, e.clickCount);
      renameDrag_ = true;
      ui().router().capturePointer(id());
      e.markHandled();
      requestPaint();
      return;
    }
    if (!commitRename()) cancelRename();  // an invalid text is discarded when the user clicks elsewhere (spec 08 rule 80)
  }
  const Hit hit = hitTest(e.x, e.y);
  press_ = hit;
  ui().router().focus(id(), core::events::FocusReason::Pointer);
  e.markHandled();
  switch (hit.part) {
    case Part::None:
      if ((e.modifiers & (Mod::kCtrl | Mod::kShift)) == 0) clearSelection();
      return;
    case Part::Disclosure: toggleExpanded(hit.row, (e.modifiers & Mod::kShift) != 0); return;
    case Part::Visibility:
    case Part::Lock: return;
    case Part::Row: break;
  }
  drag_ = {};
  drag_.armed = true;
  drag_.pressed = rows_[hit.row].id;
  pressRow(e, hit.row);
  ui().router().capturePointer(id());
}

void TreeView::onPointerMove(Event& e) {
  if (barDragging_) {
    placeBar();
    if (const auto moved = vbar_.pointerMove(e.x, e.y)) applyScroll(*moved);
    e.markHandled();
    return;
  }
  if (renameDrag_) {
    rename_.pointerDrag(ui(), renameFieldRect(), e.x);
    requestPaint();
    e.markHandled();
    return;
  }
  if (drag_.active) {
    drag_.pointerX = e.x;
    drag_.pointerY = e.y;
    updateDrop(e.x, e.y);
    wantFrames(true);
    e.markHandled();
    return;
  }
  const Hit hit = hitTest(e.x, e.y);
  bool changed = hit.part != hover_.part || hit.row != hover_.row;
  hover_ = hit;
  placeBar();
  changed = vbar_.setPointer(e.x, e.y, layout::containsPoint(absOrigin(), e.x, e.y)) || changed;
  if (changed) requestPaint();
}

void TreeView::onPointerLeave(Event&) {
  if (drag_.active || barDragging_) return;
  const bool had = hover_.part != Part::None || vbar_.thumbHovered();
  hover_ = {};
  vbar_.setPointer(0, 0, false);
  if (had) requestPaint();
}

void TreeView::onPointerWheel(Event& e) {
  const double dy = -e.wheelY * kWheelStep;
  if (dy == 0.0) return;
  const double before = scroll_;
  applyScroll(scroll_ + dy);
  if (scroll_ != before) {
    e.stopPropagation();
    e.markHandled();
  }
}

void TreeView::onPointerUp(Event& e) {
  const core::tree::WidgetId self = id();
  if (e.button == Button::Right) {
    ensureRows();
    const Hit hit = hitTest(e.x, e.y);
    NodeId node = kTreeRoot;
    if (hit.part != Part::None && hit.row < rows_.size()) {
      node = rows_[hit.row].id;
      if (selected_.count(node) == 0 && nodeSelectable(node)) replaceSelection({node}, node, node);
    } else if ((e.modifiers & (Mod::kCtrl | Mod::kShift)) == 0) {
      clearSelection();
    }
    if (onContext_ && ui().alive(self)) {
      auto cb = onContext_;
      cb(node, e.x, e.y);
    }
    return;
  }
  if (e.button != Button::Left) return;
  if (barDragging_) {
    barDragging_ = false;
    vbar_.pointerUp();
    requestPaint();
    return;
  }
  if (renameDrag_) {
    renameDrag_ = false;
    return;
  }
  const bool dragged = drag_.active;
  if (drag_.active) finishDrag(true);
  if (!ui().alive(self)) return;
  if (!dragged && drag_.armed && drag_.collapseOnRelease && mode_ == TreeSelectionMode::Multi) {
    const NodeId pressed = drag_.pressed;
    replaceSelection({pressed}, pressed, pressed);
  }
  drag_.armed = false;
  drag_.collapseOnRelease = false;
}

void TreeView::onClick(Event& e) {
  if (e.button != Button::Left) return;
  const Hit hit = hitTest(e.x, e.y);
  if (hit.part == Part::None || hit.part != press_.part || hit.row != press_.row || hit.row >= rows_.size()) return;
  const NodeId node = rows_[hit.row].id;
  if (hit.part == Part::Visibility || hit.part == Part::Lock) {
    e.markHandled();
    if (onAction_ && model_->flags(node).hasActions) {
      auto cb = onAction_;
      cb(node, hit.part == Part::Visibility ? RowAction::ToggleVisibility : RowAction::ToggleLock);
    }
    return;
  }
  if (hit.part == Part::Row && pressedWasSelected_ && selected_.size() == 1 && selected_.count(node) != 0 && !rename_.active()) {
    // A second click on the label of the selected row arms the slow-click rename (spec 08 rule 75).
    const layout::Rect r = rowRect(hit.row);
    const double labelLeft = r.x + (appearance_ == TreeAppearance::List ? 8.0 : rows_[hit.row].depth * kIndent + kDisclosure + kGap) + kIconSize + kGap;
    if (e.x >= labelLeft && model_->flags(node).renamable) armRename(node);
  }
}

void TreeView::onDoubleClick(Event& e) {
  slow_.armed = false;
  if (e.button != Button::Left) return;
  const Hit hit = hitTest(e.x, e.y);
  if (hit.part != Part::Row || hit.row >= rows_.size()) return;
  e.markHandled();
  const NodeId node = rows_[hit.row].id;
  if (onActivate_) {
    auto cb = onActivate_;
    cb(node);
  } else {
    toggleExpanded(hit.row, false);
  }
}

void TreeView::onCaptureLost(Event&) {
  barDragging_ = false;
  renameDrag_ = false;
  vbar_.cancel();
  if (drag_.active) finishDrag(false);
  drag_.armed = false;
}

void TreeView::onStateChanged(uint16_t) {
  if (enabled()) return;
  cancelRename();
  if (drag_.active) finishDrag(false);
  drag_ = {};
  hover_ = {};
  barDragging_ = false;
  renameDrag_ = false;
  slow_.armed = false;
  vbar_.cancel();
}

void TreeView::onFocusOut(Event&) {
  if (rename_.active()) {
    if (!ui().windowActive() || !commitRename()) cancelRename();
  }
  requestPaint();
}

// ---- keyboard -----------------------------------------------------------------------------------

void TreeView::moveCursor(size_t row, bool shift, bool ctrl) {
  if (row >= rows_.size()) return;
  const NodeId node = rows_[row].id;
  if (!nodeSelectable(node)) return;
  if (mode_ == TreeSelectionMode::Single || (!shift && !ctrl)) {
    replaceSelection({node}, node, node);
  } else if (shift) {
    const std::optional<size_t> anchorRow = anchor_ != kTreeRoot ? rowOfNode(anchor_) : std::nullopt;
    const size_t from = anchorRow.value_or(cursor_ != kTreeRoot ? rowOfNode(cursor_).value_or(row) : row);
    if (!anchorRow) anchor_ = rows_[from].id;
    cursor_ = node;
    selectRange(from, row, ctrl);
  } else {  // Ctrl only: add the item, the anchor stays
    std::vector<NodeId> next(selected_.begin(), selected_.end());
    next.push_back(node);
    replaceSelection(next, node, kTreeRoot);
  }
  cursor_ = node;
  scrollToNode(node, false);
  requestPaint();
}

void TreeView::keyNavigate(Event& e) {
  if (rows_.empty() || mode_ == TreeSelectionMode::None) return;
  const bool shift = (e.modifiers & Mod::kShift) != 0;
  const bool ctrl = (e.modifiers & Mod::kCtrl) != 0;
  const std::optional<size_t> cur = cursor_ != kTreeRoot ? rowOfNode(cursor_) : std::nullopt;
  const auto selectable = [&](size_t r) { return nodeSelectable(rows_[r].id); };
  // Nearest selectable row from `from` (exclusive) in a direction; npos when there is none.
  const auto step = [&](long from, int dir) -> long {
    for (long r = from + dir; r >= 0 && r < static_cast<long>(rows_.size()); r += dir) {
      if (selectable(static_cast<size_t>(r))) return r;
    }
    return -1;
  };
  const size_t whole = wholeRowsVisible();
  long target = -1;
  bool known = true;
  switch (e.key) {
    case Key::Down: target = step(cur ? static_cast<long>(*cur) : -1, +1); break;
    case Key::Up: target = step(cur ? static_cast<long>(*cur) : static_cast<long>(rows_.size()), -1); break;
    case Key::Home: target = step(-1, +1); break;
    case Key::End: target = step(static_cast<long>(rows_.size()), -1); break;
    case Key::PageDown: {
      const long base = cur ? static_cast<long>(*cur) : -1;
      const long want = std::min<long>(static_cast<long>(rows_.size()) - 1, base + static_cast<long>(whole));
      target = selectable(static_cast<size_t>(std::max(0L, want))) ? std::max(0L, want) : step(want, -1);
      break;
    }
    case Key::PageUp: {
      const long base = cur ? static_cast<long>(*cur) : static_cast<long>(rows_.size());
      const long want = std::max<long>(0, base - static_cast<long>(whole));
      target = selectable(static_cast<size_t>(want)) ? want : step(want, +1);
      break;
    }
    case Key::Left:
    case Key::Right: {
      if (appearance_ == TreeAppearance::List || !cur || shift || ctrl) {
        known = false;
        break;
      }
      const Row& r = rows_[*cur];
      const NodeId node = r.id;
      if (e.key == Key::Left) {
        if (r.hasChildren && expanded_.count(node) != 0) {
          setExpanded(node, false);
        } else if (r.parent != 0xFFFFFFFFu && selectable(r.parent)) {
          target = static_cast<long>(r.parent);
        }
      } else if (r.hasChildren) {
        if (expanded_.count(node) == 0) setExpanded(node, true);
        else if (*cur + 1 < rows_.size() && selectable(*cur + 1)) target = static_cast<long>(*cur + 1);
      }
      break;
    }
    case Key::Space: {
      if (!cur || !selectable(*cur)) {
        known = false;
        break;
      }
      const NodeId node = rows_[*cur].id;
      if (ctrl && mode_ == TreeSelectionMode::Multi) {
        std::vector<NodeId> next(selected_.begin(), selected_.end());
        const auto it = std::find(next.begin(), next.end(), node);
        if (it != next.end()) next.erase(it);
        else next.push_back(node);
        replaceSelection(next, node, kTreeRoot);
      } else if (selected_.count(node) == 0) {
        replaceSelection({node}, node, node);
      } else {
        known = false;  // already selected: the key is not used (spec 08 rule 61)
      }
      scrollToNode(node, false);
      break;
    }
    case Key::Enter: {
      if (!cur) {
        known = false;
        break;
      }
      const NodeId node = rows_[*cur].id;
      if (onActivate_) {
        auto cb = onActivate_;
        cb(node);
      } else {
        toggleExpanded(*cur, false);
      }
      break;
    }
    case Key::A:
      if (ctrl && !shift && mode_ == TreeSelectionMode::Multi) selectAll();
      else known = false;
      break;
    default: known = false; break;
  }
  if (!known) return;
  e.markHandled();
  if (target >= 0) {
    const bool navigation = e.key == Key::Down || e.key == Key::Up || e.key == Key::Home || e.key == Key::End || e.key == Key::PageDown || e.key == Key::PageUp;
    if (navigation) moveCursor(static_cast<size_t>(target), shift, ctrl);
    else moveCursor(static_cast<size_t>(target), false, false);  // Left / Right: the parent or child becomes the sole selection
  }
}

void TreeView::onKeyDown(Event& e) {
  slow_.armed = false;
  ensureRows();
  const core::tree::WidgetId self = id();
  if (rename_.active()) {
    const RenameEditor::Outcome outcome = rename_.keyDown(e.key, e.modifiers);
    switch (outcome) {
      case RenameEditor::Outcome::Commit:
        commitRename();
        e.markHandled();
        break;
      case RenameEditor::Outcome::Cancel:
        cancelRename();
        e.markHandled();
        break;
      case RenameEditor::Outcome::Handled:
        e.markHandled();
        requestPaint();
        break;
      case RenameEditor::Outcome::Ignored:
        if (e.key != Key::Tab) e.markHandled();  // nothing else may act while the field is open
        break;
    }
    return;
  }
  if (drag_.active && e.key == Key::Escape) {
    cancelDrag();
    ui().router().cancelPointerInteraction();
    e.markHandled();
    return;
  }
  if (e.modifiers & (Mod::kAlt | Mod::kMeta)) return;  // Alt disables the list keys (rule 63)
  if (e.key == kF2 && e.modifiers == 0) {
    const NodeId node = cursor_ != kTreeRoot ? cursor_ : (selected_.size() == 1 ? *selected_.begin() : kTreeRoot);
    if (node != kTreeRoot && beginRename(node)) e.markHandled();
    return;
  }
  keyNavigate(e);
  (void)self;
}

void TreeView::typeAhead(char32_t cp) {
  const uint64_t now = ui().now();
  if (!searchPrefix_.empty() && now > searchMs_ + kSearchResetMs) searchPrefix_.clear();
  searchMs_ = now;
  text::appendUtf8(searchPrefix_, cp);
  if (rows_.empty() || mode_ == TreeSelectionMode::None) return;
  const std::optional<size_t> cur = cursor_ != kTreeRoot ? rowOfNode(cursor_) : std::nullopt;
  const size_t n = rows_.size();
  // The current row stays when it already matches the longer prefix; otherwise search after it, wrapping.
  size_t found = n;
  if (cur && startsWithFolded(model_->label(rows_[*cur].id), searchPrefix_)) {
    found = *cur;
  } else {
    const size_t start = cur ? *cur + 1 : 0;
    for (size_t k = 0; k < n; ++k) {
      const size_t r = (start + k) % n;
      if (nodeSelectable(rows_[r].id) && startsWithFolded(model_->label(rows_[r].id), searchPrefix_)) {
        found = r;
        break;
      }
    }
  }
  if (found == n) return;
  const NodeId node = rows_[found].id;
  replaceSelection({node}, node, node);
  scrollToNode(node, false);
  requestPaint();
}

void TreeView::onTextInput(Event& e) {
  if (rename_.active()) {
    if (rename_.textInput(e.codePoint)) {
      e.markHandled();
      requestPaint();
    }
    return;
  }
  if ((e.modifiers & (Mod::kCtrl | Mod::kAlt | Mod::kMeta)) != 0 || e.codePoint < 0x20 || e.codePoint == 0x7F) return;
  ensureRows();
  e.markHandled();
  typeAhead(e.codePoint);
}

// ---- rename -------------------------------------------------------------------------------------

bool TreeView::beginRename(NodeId node) {
  ensureRows();
  if (!model_ || drag_.active || !enabled()) return false;
  const auto it = rowIndex_.find(node);
  if (it == rowIndex_.end() || !model_->flags(node).renamable) return false;
  if (rename_.active()) cancelRename();
  slow_.armed = false;
  scrollToNode(node, false);
  rename_.begin(ui(), node, model_->label(node));
  renameRow_ = it->second;
  ui().router().focus(id(), core::events::FocusReason::Program);
  clampScroll();
  scrollToNode(node, false);
  requestPaint();
  return true;
}

bool TreeView::commitRename() {
  if (!rename_.active()) return false;
  const core::tree::WidgetId self = id();
  const NodeId node = rename_.node();
  const std::string text = rename_.text();
  if (text.empty()) {
    rename_.setError("The name cannot be empty");
    requestPaint();
    return false;
  }
  if (text != labelOf(node) && onRename_) {
    auto cb = onRename_;
    const RenameResult result = cb(node, text);
    if (!ui().alive(self)) return true;
    if (!result.ok) {
      rename_.setError(result.error.empty() ? std::string("Invalid name") : result.error);
      requestPaint();
      return false;
    }
  }
  rename_.end();
  renameRow_.reset();
  clampScroll();
  requestPaint();
  return true;
}

void TreeView::cancelRename() {
  if (!rename_.active()) return;
  rename_.end();
  renameRow_.reset();
  clampScroll();
  requestPaint();
}

void TreeView::armRename(NodeId node) {
  slow_.armed = true;
  slow_.node = node;
  slow_.dueMs = ui().now() + kSlowClickMs;
  wantFrames(true);
}

// ---- drag and drop ------------------------------------------------------------------------------

std::vector<NodeId> TreeView::dragPayload() {
  std::vector<size_t> selectedRows;
  for (const NodeId n : selected_) {
    const auto it = rowIndex_.find(n);
    if (it != rowIndex_.end() && model_->flags(n).draggable) selectedRows.push_back(it->second);
  }
  std::sort(selectedRows.begin(), selectedRows.end());
  std::vector<NodeId> payload;
  size_t coveredUntil = 0;  // rows below a dragged node travel with it
  for (const size_t r : selectedRows) {
    if (r < coveredUntil) continue;
    payload.push_back(rows_[r].id);
    size_t end = r + 1;
    while (end < rows_.size() && rows_[end].depth > rows_[r].depth) ++end;
    coveredUntil = end;
  }
  return payload;
}

void TreeView::onDragStart(Event& e) {
  if (!drag_.armed || drag_.active || mode_ == TreeSelectionMode::None || rename_.active() || barDragging_) return;
  slow_.armed = false;
  ensureRows();
  const auto pressedRow = rowIndex_.find(drag_.pressed);
  if (pressedRow == rowIndex_.end() || !model_->flags(drag_.pressed).draggable) return;
  if (selected_.count(drag_.pressed) == 0) return;
  drag_.payload = dragPayload();
  if (drag_.payload.empty()) return;
  drag_.active = true;
  drag_.collapseOnRelease = false;
  drag_.pointerX = e.x;
  drag_.pointerY = e.y;
  drag_.lastMs = ui().now();
  hover_ = {};
  e.markHandled();
  updateDrop(e.x, e.y);
  wantFrames(true);
  requestPaint();
}

bool TreeView::dropAllowed(NodeId target, DropZone zone) {
  const auto it = rowIndex_.find(target);
  if (it == rowIndex_.end() || drag_.payload.empty()) return false;
  const size_t t = it->second;
  // A node cannot be dropped relative to itself or into its own subtree.
  for (const NodeId p : drag_.payload) {
    const size_t r = rowIndex_.at(p);
    size_t end = r + 1;
    while (end < rows_.size() && rows_[end].depth > rows_[r].depth) ++end;
    if (t >= r && t < end) return false;
  }
  if (zone == DropZone::Onto && !model_->flags(target).acceptsDrops) return false;
  if (acceptDrop_) return acceptDrop_(buildRequest(target, zone));
  return true;
}

DropRequest TreeView::buildRequest(NodeId target, DropZone zone) const {
  DropRequest req;
  req.nodes = drag_.payload;
  req.target = target;
  req.zone = zone;
  return req;
}

void TreeView::updateDrop(double x, double y) {
  (void)x;
  drag_.preview = {};
  ensureRows();
  if (rows_.empty()) return;
  const layout::Rect a = absOrigin();
  const double ly = y - a.y;
  size_t row;
  DropZone zone;
  const auto at = rowAtLocalY(ly);
  if (!at) {
    // Past the last row: below it; above the first row: above it.
    if (ly < 0.0) {
      row = 0;
      zone = DropZone::Above;
    } else {
      row = rows_.size() - 1;
      zone = DropZone::Below;
    }
  } else {
    row = *at;
    const double height = rowHeightOf(row);
    const double inRow = ly - rowTopLocal(row);
    const double band = std::clamp(height * kDropBandFraction, kDropBandMin, kDropBandMax);
    if (inRow < band) zone = DropZone::Above;
    else if (inRow >= height - band) zone = DropZone::Below;
    else zone = DropZone::Onto;
    if (zone == DropZone::Onto && !model_->flags(rows_[row].id).acceptsDrops) zone = inRow < height * 0.5 ? DropZone::Above : DropZone::Below;
  }
  const NodeId target = rows_[row].id;
  if (dropAllowed(target, zone)) drag_.preview = {true, target, zone};
  requestPaint();
}

void TreeView::finishDrag(bool commit) {
  if (!drag_.active) return;
  const core::tree::WidgetId self = id();
  const DropPreview preview = drag_.preview;
  const std::vector<NodeId> payload = drag_.payload;
  drag_ = {};
  wantFrames(false);
  requestPaint();
  if (!commit || !preview.valid || !onDrop_) return;
  DropRequest req;
  req.nodes = payload;
  req.target = preview.target;
  req.zone = preview.zone;
  auto cb = onDrop_;
  cb(req);
  if (!ui().alive(self)) return;
  rebuildRows();
  if (req.zone == DropZone::Onto && model_ && model_->contains(req.target)) setExpanded(req.target, true);
}

void TreeView::cancelDrag() {
  const bool was = drag_.active;
  if (was) finishDrag(false);
  drag_ = {};
  if (was) ui().router().cancelPointerInteraction();
}

// ---- time ---------------------------------------------------------------------------------------

void TreeView::wantFrames(bool on) {
  if (on == framesWanted_) return;
  framesWanted_ = on;
  if (on) ui().invalidator().requestAnimation(id());
  else ui().invalidator().cancelAnimation(id());
}

void TreeView::advance(uint64_t now) {
  const uint64_t dt = lastAdvanceMs_ != 0 && now > lastAdvanceMs_ ? std::min(now - lastAdvanceMs_, kMaxStepMs) : 0;
  lastAdvanceMs_ = now;
  bool frames = false;
  if (slow_.armed) {
    if (now >= slow_.dueMs) {
      const NodeId node = slow_.node;
      slow_.armed = false;
      beginRename(node);
    } else {
      frames = true;
    }
  }
  if (drag_.active) {
    const layout::Rect a = absOrigin();
    const double top = a.y;
    const double bottom = static_cast<double>(a.y) + a.h;
    double direction = 0.0;
    double depth = 0.0;
    if (drag_.pointerY < top + kEdgeScrollZone) {
      direction = -1.0;
      depth = (top + kEdgeScrollZone - drag_.pointerY) / kEdgeScrollZone;
    } else if (drag_.pointerY > bottom - kEdgeScrollZone) {
      direction = 1.0;
      depth = (drag_.pointerY - (bottom - kEdgeScrollZone)) / kEdgeScrollZone;
    }
    if (direction != 0.0) {
      frames = true;
      const double speed = kEdgeScrollMax * std::clamp(depth, 0.0, 1.0);  // up to 600 px/s, reached at the edge and beyond
      const double before = scroll_;
      applyScroll(scroll_ + direction * speed * static_cast<double>(dt) / 1000.0);
      if (scroll_ != before) updateDrop(drag_.pointerX, drag_.pointerY);
    }
  }
  wantFrames(frames);
}

}  // namespace r1ui::widgets

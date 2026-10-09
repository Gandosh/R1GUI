// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: keyboard handling of TreeView: cursor movement and selection (spec 01 rules 31-39, spec 08 rules
//   57-65), expand / collapse keys, Enter activation, F2, the rename field's keys while it is open and
//   type-to-search.
// Invariants: Alt (and Meta) disable every list key; keys are used only when the tree has rows and a
//   cursor to act on, so an empty tree lets them travel on.
// Callers: UiContext (key and text dispatch).
#include <algorithm>
#include <cctype>

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

}  // namespace r1ui::widgets

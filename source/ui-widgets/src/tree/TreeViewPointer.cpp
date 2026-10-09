// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: pointer handling of TreeView: hit testing, tooltips, the pointer selection rules (spec 08 rules
//   45-53), disclosure and action clicks, scrollbar interaction, wheel, double click, context menu and the
//   focus / enable bookkeeping of a gesture.
// Invariants: handlers re-validate rows through rowOfNode / the row bounds (a callback may change the
//   model) and use ui().alive() after a callback, because a callback may destroy the view.
// Callers: UiContext (event dispatch).
#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/tree/TreeView.h"

namespace r1ui::widgets {

namespace layout = core::layout;
using core::events::Button;
using core::events::Key;
using core::events::Phase;
namespace Mod = core::events::Mod;

namespace {
constexpr double kWheelStep = 48.0;  // px per wheel notch
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

}  // namespace r1ui::widgets

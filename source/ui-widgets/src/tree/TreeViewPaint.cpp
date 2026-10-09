// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: painting of TreeView: visible rows only (background, disclosure, icon, label, hover
//   actions, the inline rename field), drop indicators, the scrollbar and the time step that
//   paint() runs before drawing (slow-click rename, edge auto-scroll).
// Invariants: paint() never touches the widget tree and never requests layout; it only advances the
//   view's own timers and scroll offset (documented in TreeView.h), draws at most the rows that
//   intersect the viewport, and balances every clip and opacity push.
// Callers: UiContext (paint traversal).
#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/tree/TreeView.h"

namespace r1ui::widgets {

namespace layout = core::layout;
namespace State = theme::State;

namespace {
constexpr float kHiddenOpacity = 0.5f;   // measured: a hidden layer row
constexpr float kDraggedOpacity = 0.3f;  // source-derived: the dragged row
constexpr float kIconOpacity = 0.7f;     // measured: node icons
}  // namespace

float TreeView::paintOpacity() const { return static_cast<float>(ui().services().resolve("tree.row", styleState() & State::kDisabled).opacity); }

Cursor TreeView::cursor() const {
  if (!enabled()) return Cursor::Default;
  if (drag_.active) return Cursor::Move;
  return hover_.part == Part::None ? Cursor::Default : Cursor::Pointer;
}

void TreeView::paint(PaintContext& ctx) {
  ensureRows();
  clampScroll();
  placeBar();
  if (rows_.empty()) return;
  const layout::Rect a = ctx.rect();
  const double viewH = static_cast<double>(a.h);
  render::Painter& painter = ctx.painter();
  painter.pushClip(ctx.box());
  const size_t first = rowAtLocalY(0.0).value_or(0);
  const size_t last = std::min(rows_.size(), rowAtLocalY(std::max(0.0, viewH - 1.0)).value_or(rows_.size() - 1) + 1);
  const double contentW = contentWidth();
  for (size_t i = first; i < last; ++i) {
    const Row& row = rows_[i];
    const NodeFlags flags = model_->flags(row.id);
    const double top = a.y + rowTopLocal(i);
    const double height = rowHeightOf(i);
    const bool renamingRow = renameRow_ && *renameRow_ == i;
    const bool selected = selected_.count(row.id) != 0;
    const bool hot = hover_.part != Part::None && hover_.row == i && !drag_.active && enabled();
    const bool dragged = drag_.active && std::find(drag_.payload.begin(), drag_.payload.end(), row.id) != drag_.payload.end();
    float opacity = 1.0f;
    if (flags.hidden) opacity *= kHiddenOpacity;
    if (dragged) opacity *= kDraggedOpacity;
    if (opacity < 1.0f) painter.pushOpacity(opacity);
    const uint8_t bits = (selected ? State::kSelected : 0) | (hot && !renamingRow ? State::kHover : 0) | (focused() ? State::kFocus : 0);
    const theme::ResolvedStyle& rs = ctx.resolve("tree.row", bits);
    const render::Rect box = ctx.toPhysical(a.x, top, contentW, height);
    if (!renamingRow && rs.background.a > 0) painter.fillRoundedRect(box, render::CornerRadii::uniform(ctx.px(rs.radius)), ctx.color(rs.background));
    // Keyboard focus shows through the selection colour; only a cursor row that is not selected needs a ring (spec 01 rule 6).
    if (focusVisible() && cursor_ == row.id && !renamingRow && !selected) ctx.focusRing(box, ctx.px(rs.radius));

    double x = appearance_ == TreeAppearance::List ? 8.0 : static_cast<double>(row.depth) * kIndent;
    if (appearance_ == TreeAppearance::Tree) {
      if (row.hasChildren) {
        const bool overDisclosure = hover_.part == Part::Disclosure && hover_.row == i;
        const theme::ResolvedStyle& ds = ctx.resolve("tree.disclosure", overDisclosure ? State::kHover : State::kNone);
        const render::Rect dbox = ctx.toPhysical(a.x + x, top + (height - kActionSize) * 0.5, kDisclosure, kActionSize);
        ctx.drawIcon(expanded_.count(row.id) != 0 ? "chevron-down" : "chevron-right", kIconSize, dbox, ctx.color(ds.text.color));
      }
      x += kDisclosure + kGap;
    }
    const std::string_view iconName = model_->icon(row.id);
    if (!iconName.empty()) {
      const theme::ResolvedStyle& is = ctx.resolve("tree.icon", 0);
      render::Color tint = flags.component ? ctx.color("component") : ctx.color(is.text.color);
      tint.a *= kIconOpacity;
      ctx.drawIcon(iconName, kIconSize, ctx.toPhysical(a.x + x, top, kIconSize, height), tint);
    }
    x += kIconSize + kGap;
    if (renamingRow) {
      rename_.paint(ctx, renameFieldRect());
    } else {
      const bool showActions = flags.hasActions && (hot || flags.hidden || flags.locked);
      const double actionsWidth = showActions ? 2.0 * kActionSize + kGap + kGap : 0.0;
      TextOptions options;
      options.padLeft = x;
      options.padRight = kRightPad + actionsWidth;
      ctx.drawText(model_->label(row.id), rs.text, box, options);
      if (showActions) {
        for (const RowAction action : {RowAction::ToggleLock, RowAction::ToggleVisibility}) {
          const layout::Rect r = actionRect(i, action);
          const Part part = action == RowAction::ToggleVisibility ? Part::Visibility : Part::Lock;
          const bool over = hover_.part == part && hover_.row == i;
          const theme::ResolvedStyle& as = ctx.resolve("tree.action", over ? State::kHover : State::kNone);
          const render::Rect abox = ctx.toPhysical(r.x, r.y, r.w, r.h);
          if (as.background.a > 0) painter.fillRoundedRect(abox, render::CornerRadii::uniform(ctx.px(as.radius)), ctx.color(as.background));
          const char* icon = action == RowAction::ToggleVisibility ? (flags.hidden ? "eye-off" : "eye") : (flags.locked ? "lock" : "unlock");
          ctx.drawIcon(icon, kIconSize, abox, ctx.color(as.text.color));
        }
      }
    }
    if (opacity < 1.0f) painter.popOpacity();
  }

  if (drag_.active && drag_.preview.valid) {
    const auto it = rowIndex_.find(drag_.preview.target);
    if (it != rowIndex_.end()) {
      const theme::ResolvedStyle& ds = ctx.resolve("tree.drop", 0);
      const size_t r = it->second;
      const double top = a.y + rowTopLocal(r);
      const double height = rowHeightOf(r);
      const double indent = appearance_ == TreeAppearance::List ? 8.0 : static_cast<double>(rows_[r].depth) * kIndent;
      const render::Color accent = ctx.color(ds.border.color);
      const float line = ctx.px(ds.border.width);
      if (drag_.preview.zone == DropZone::Onto) {
        painter.border(ctx.toPhysical(a.x, top, contentW, height), render::CornerRadii::uniform(ctx.px(ds.radius)), line, accent);
      } else {
        const double y = drag_.preview.zone == DropZone::Above ? top - ds.border.width * 0.5 : top + height - ds.border.width * 0.5;
        painter.fillRoundedRect(ctx.toPhysical(a.x + indent, y, std::max(0.0, contentW - indent), ds.border.width), render::CornerRadii::uniform(line * 0.5f), accent);
      }
    }
  }
  painter.popClip();
}

void TreeView::paintOver(PaintContext& ctx) {
  placeBar();
  vbar_.paint(ctx);
}

}  // namespace r1ui::widgets

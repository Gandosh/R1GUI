// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of ActionListView (ActionList.h): grouping, filtering, the flattened row list
//   with prefix-sum row tops, selection, scrolling, painting of the visible rows only, pointer and
//   keyboard handling, and the drag out of a row.
// Invariants: rows_ and tops_ always agree (tops_.size() == rows_.size() + 1); the scroll offset is in
//   [0, max(0, content - viewport)]; selected_ is -1 or a valid row; the drag hub is touched only
//   between a left press on an action row and the release, Escape or capture loss; no callback runs
//   while the row vectors are being rebuilt (callbacks get copies of the action).
// Callers: ActionList, tests.
#include <algorithm>
#include <cmath>

#include "r1ui/commands/Text.h"
#include "r1ui/widgets/actions/ActionList.h"
#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace layout = core::layout;
using core::events::Button;
using core::events::Key;
namespace Mod = core::events::Mod;

namespace {

constexpr double kThumbWidth = 6.0;
constexpr double kThumbGrabWidth = 12.0;
constexpr double kMinThumb = 24.0;
constexpr double kIconColumn = 34.0;   // left edge of the label column
constexpr double kColumnGap = 10.0;
constexpr double kMinDescriptionWidth = 60.0;

std::string lowerCaseKey(const std::string& text) { return foldActionText(text); }

void drawIconSafe(PaintContext& ctx, std::string_view name, double size, const render::Rect& box, const render::Color& tint) {
  try {
    ctx.drawIcon(name.empty() ? std::string_view("circle") : name, size, box, tint);
  } catch (const std::exception&) {
    ctx.drawIcon("circle", size, box, tint);
  }
}

// Logical width of `text` in `style`.
double widthOf(PaintContext& ctx, std::string_view text, const theme::TextStyle& style) {
  const double scale = ctx.ui().scale();
  return static_cast<double>(ctx.ui().text().measure(text, static_cast<float>(style.fontSize * scale), style.weight)) / scale;
}

}  // namespace

DragPayload makeActionDragPayload(const ActionInfo& action) {
  DragPayload payload;
  payload.kind = DragPayload::Kind::Command;
  payload.commandId = action.id;
  payload.text = action.label;
  return payload;
}

// ---- lifecycle -----------------------------------------------------------------------------------

void ActionListView::onAttached() {
  setFocusable(true);
  style().flexGrow = 1.0;
  style().flexShrink = 1.0;
  style().minHeight = layout::Length::px(kRowHeight * 3);
  style().overflow = layout::Overflow::Hidden;
}

void ActionListView::onDetached() {
  if (dragging_ && hub_ != nullptr) hub_->cancel();
  dragging_ = false;
}

// ---- data and grouping -----------------------------------------------------------------------------

void ActionListView::setActions(std::vector<ActionInfo> actions) {
  // Remember the selection by content so a refresh (a rebound shortcut) keeps it.
  std::string keepId, keepLabel, keepCategory;
  bool keepHeader = false;
  if (selected_ >= 0 && selected_ < static_cast<int>(rows_.size())) {
    const ActionRow& row = rows_[static_cast<size_t>(selected_)];
    keepHeader = row.header;
    if (row.header) {
      keepCategory = groupNames_[row.index];
    } else {
      keepId = actions_[row.index].id;
      keepLabel = actions_[row.index].label;
    }
  }
  if (actions.size() > kMaxActions) actions.resize(kMaxActions);
  actions_.clear();
  actions_.reserve(actions.size());
  for (const ActionInfo& a : actions) actions_.push_back(sanitizedAction(a));
  actions.clear();

  haystacks_.clear();
  haystacks_.reserve(actions_.size());
  for (const ActionInfo& a : actions_) haystacks_.push_back(actionHaystack(a, options_.searchShortcuts));

  // Groups: sorted by folded category name, actions in the order given.
  std::vector<std::string> names;
  names.reserve(16);
  for (const ActionInfo& a : actions_) names.push_back(a.category);
  std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
    const std::string fa = lowerCaseKey(a), fb = lowerCaseKey(b);
    return fa != fb ? fa < fb : a < b;
  });
  names.erase(std::unique(names.begin(), names.end()), names.end());
  std::vector<uint8_t> oldCollapsed;
  std::vector<std::string> oldNames = std::move(groupNames_);
  oldCollapsed = std::move(collapsed_);
  groupNames_ = std::move(names);
  collapsed_.assign(groupNames_.size(), 0);
  for (size_t g = 0; g < groupNames_.size(); ++g) {
    const auto it = std::find(oldNames.begin(), oldNames.end(), groupNames_[g]);
    if (it != oldNames.end()) collapsed_[g] = oldCollapsed[static_cast<size_t>(it - oldNames.begin())];
  }
  groups_.assign(groupNames_.size(), {});
  groupOf_.assign(actions_.size(), 0);
  for (uint32_t i = 0; i < actions_.size(); ++i) {
    const auto it = std::lower_bound(groupNames_.begin(), groupNames_.end(), actions_[i].category, [](const std::string& a, const std::string& b) {
      const std::string fa = lowerCaseKey(a), fb = lowerCaseKey(b);
      return fa != fb ? fa < fb : a < b;
    });
    const uint32_t g = static_cast<uint32_t>(it - groupNames_.begin());
    groups_[g].push_back(i);
    groupOf_[i] = g;
  }

  selected_ = -1;
  hover_ = -1;
  rebuildRows();
  for (size_t r = 0; r < rows_.size(); ++r) {
    const ActionRow& row = rows_[r];
    const bool same = keepHeader ? (row.header && groupNames_[row.index] == keepCategory) : (!row.header && !keepId.empty() && actions_[row.index].id == keepId && actions_[row.index].label == keepLabel);
    if (same) {
      selected_ = static_cast<int>(r);
      break;
    }
  }
  setScrollOffset(scroll_);
  requestPaint();
}

void ActionListView::setQuery(std::string_view text) {
  const std::string clean = commands::sanitizeText(text, kMaxActionQueryBytes);
  if (clean == queryText_) return;
  queryText_ = clean;
  query_ = parseActionQuery(queryText_);
  // Keep the selected action when it still matches, else a search selects its first hit.
  const ActionInfo* keep = selectedAction();
  const std::string keepId = keep != nullptr ? keep->id : std::string();
  const std::string keepLabel = keep != nullptr ? keep->label : std::string();
  const int before = selected_;
  selected_ = -1;
  rebuildRows();
  if (!keepId.empty()) {
    for (size_t r = 0; r < rows_.size(); ++r) {
      if (!rows_[r].header && actions_[rows_[r].index].id == keepId && actions_[rows_[r].index].label == keepLabel) selected_ = static_cast<int>(r);
    }
  }
  if (selected_ < 0 && !query_.empty()) {
    for (size_t r = 0; r < rows_.size(); ++r) {
      if (!rows_[r].header) {
        setSelectedRow(static_cast<int>(r), true);
        break;
      }
    }
  }
  if (before != selected_ || selected_ >= 0) scrollIntoView(selected_);
  setScrollOffset(scroll_);
  requestPaint();
}

void ActionListView::setCategoryFilter(std::string category) {
  if (category == categoryFilter_) return;
  categoryFilter_ = std::move(category);
  selected_ = -1;
  rebuildRows();
  setScrollOffset(0.0);
  requestPaint();
}

void ActionListView::rebuildRows() {
  rows_.clear();
  matches_ = 0;
  const bool searching = !query_.empty();
  for (uint32_t g = 0; g < groups_.size(); ++g) {
    if (!categoryFilter_.empty() && groupNames_[g] != categoryFilter_) continue;
    const size_t headerAt = rows_.size();
    rows_.push_back({true, g, 0});
    const bool expanded = searching || collapsed_[g] == 0;
    uint32_t count = 0;
    for (const uint32_t i : groups_[g]) {
      if (searching && !matchesAction(query_, haystacks_[i])) continue;
      ++count;
      if (expanded) rows_.push_back({false, i, 0});
    }
    if (count == 0) {
      rows_.resize(headerAt);
      continue;
    }
    rows_[headerAt].count = count;
    matches_ += count;
  }
  tops_.assign(rows_.size() + 1, 0.0);
  for (size_t r = 0; r < rows_.size(); ++r) tops_[r + 1] = tops_[r] + rowHeight(rows_[r]);
  if (selected_ >= static_cast<int>(rows_.size())) selected_ = -1;
  hover_ = -1;
}

void ActionListView::setCollapsed(const std::string& category, bool collapsed) {
  const auto it = std::find(groupNames_.begin(), groupNames_.end(), category);
  if (it == groupNames_.end()) return;
  const size_t g = static_cast<size_t>(it - groupNames_.begin());
  if ((collapsed_[g] != 0) == collapsed) return;
  toggleGroup(static_cast<uint32_t>(g));
}

bool ActionListView::isCollapsed(const std::string& category) const {
  const auto it = std::find(groupNames_.begin(), groupNames_.end(), category);
  return it != groupNames_.end() && collapsed_[static_cast<size_t>(it - groupNames_.begin())] != 0;
}

void ActionListView::toggleGroup(uint32_t group) {
  if (group >= collapsed_.size()) return;
  collapsed_[group] ^= 1;
  // The header stays selected when it was.
  const bool headerSelected = selected_ >= 0 && selected_ < static_cast<int>(rows_.size()) && rows_[static_cast<size_t>(selected_)].header && rows_[static_cast<size_t>(selected_)].index == group;
  const ActionInfo* sel = selectedAction();
  const std::string keepId = sel != nullptr ? sel->id : std::string();
  const uint32_t keepIndex = selected_ >= 0 && selected_ < static_cast<int>(rows_.size()) ? rows_[static_cast<size_t>(selected_)].index : 0;
  selected_ = -1;
  rebuildRows();
  for (size_t r = 0; r < rows_.size(); ++r) {
    if (headerSelected && rows_[r].header && rows_[r].index == group) selected_ = static_cast<int>(r);
    if (!headerSelected && !keepId.empty() && !rows_[r].header && rows_[r].index == keepIndex) selected_ = static_cast<int>(r);
  }
  if (selected_ < 0 && !keepId.empty()) {  // the selected action was inside the collapsed group: select its header
    for (size_t r = 0; r < rows_.size(); ++r) {
      if (rows_[r].header && rows_[r].index == group) selected_ = static_cast<int>(r);
    }
  }
  setScrollOffset(scroll_);
  requestPaint();
}

// ---- selection -----------------------------------------------------------------------------------------

const ActionInfo* ActionListView::selectedAction() const {
  if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size())) return nullptr;
  const ActionRow& row = rows_[static_cast<size_t>(selected_)];
  return row.header ? nullptr : &actions_[row.index];
}

const std::string& ActionListView::rowCategory(int row) const {
  const ActionRow& r = rows_[static_cast<size_t>(row)];
  return r.header ? groupNames_[r.index] : actions_[r.index].category;
}

bool ActionListView::selectAction(std::string_view id) {
  for (size_t r = 0; r < rows_.size(); ++r) {
    if (!rows_[r].header && actions_[rows_[r].index].id == id) {
      setSelectedRow(static_cast<int>(r), true);
      return true;
    }
  }
  // Inside a collapsed group: expand it, then select.
  for (uint32_t i = 0; i < actions_.size(); ++i) {
    if (actions_[i].id != id) continue;
    const uint32_t g = groupOf_[i];
    if (!categoryFilter_.empty() && groupNames_[g] != categoryFilter_) return false;
    if (collapsed_[g] == 0) return false;  // filtered out by the search
    toggleGroup(g);
    return selectAction(id);
  }
  return false;
}

void ActionListView::selectRow(int row) { setSelectedRow(row, true); }

void ActionListView::setSelectedRow(int row, bool notify) {
  if (row < -1 || row >= static_cast<int>(rows_.size())) return;
  if (row == selected_) return;
  selected_ = row;
  scrollIntoView(row);
  requestPaint();
  if (notify && onSelect_) {
    if (const ActionInfo* a = selectedAction()) {
      const ActionInfo copy = *a;
      auto callback = onSelect_;
      callback(copy);
    }
  }
}

void ActionListView::moveSelection(int delta) {
  if (rows_.empty() || delta == 0) return;
  const int last = static_cast<int>(rows_.size()) - 1;
  int target = selected_ < 0 ? (delta > 0 ? 0 : last) : std::clamp(selected_ + delta, 0, last);
  setSelectedRow(target, true);
}

void ActionListView::activateSelected() {
  const ActionInfo* a = selectedAction();
  if (a == nullptr || !onActivate_) return;
  const ActionInfo copy = *a;
  auto callback = onActivate_;
  callback(copy);
}

// ---- geometry ----------------------------------------------------------------------------------------------

double ActionListView::viewTop() const {
  return ui().absRect(id()).y + (options_.columnHeaders ? kColumnHeaderHeight : 0.0);
}

double ActionListView::viewportHeight() const {
  return std::max(0.0, static_cast<double>(ui().absRect(id()).h) - (options_.columnHeaders ? kColumnHeaderHeight : 0.0));
}

void ActionListView::setScrollOffset(double offset) {
  const double max = std::max(0.0, contentHeight() - viewportHeight());
  const double clamped = std::isfinite(offset) ? std::clamp(offset, 0.0, max) : 0.0;
  if (clamped == scroll_) return;
  scroll_ = clamped;
  requestPaint();
}

void ActionListView::scrollIntoView(int row) {
  if (row < 0 || row >= static_cast<int>(rows_.size())) return;
  const double top = tops_[static_cast<size_t>(row)];
  const double bottom = tops_[static_cast<size_t>(row) + 1];
  const double viewport = viewportHeight();
  if (top < scroll_) {
    setScrollOffset(top);
  } else if (bottom > scroll_ + viewport) {
    setScrollOffset(bottom - viewport);
  }
}

layout::RectD ActionListView::rowRect(int row) const {
  if (row < 0 || row >= static_cast<int>(rows_.size())) return {};
  const layout::Rect r = ui().absRect(id());
  const double top = viewTop();
  const double y = top + tops_[static_cast<size_t>(row)] - scroll_;
  const double h = tops_[static_cast<size_t>(row) + 1] - tops_[static_cast<size_t>(row)];
  if (y + h <= top || y >= top + viewportHeight()) return {};
  return {static_cast<double>(r.x), y, static_cast<double>(r.w), h};
}

int ActionListView::rowAt(double x, double y) const {
  const layout::Rect r = ui().absRect(id());
  const double top = viewTop();
  if (x < r.x || x >= r.x + r.w || y < top || y >= top + viewportHeight()) return -1;
  const double content = y - top + scroll_;
  if (content < 0.0 || content >= contentHeight()) return -1;
  const auto it = std::upper_bound(tops_.begin(), tops_.end(), content);
  const long index = static_cast<long>(it - tops_.begin()) - 1;
  return index >= 0 && index < static_cast<long>(rows_.size()) ? static_cast<int>(index) : -1;
}

ActionListView::Columns ActionListView::columns() const {
  const layout::Rect r = ui().absRect(id());
  Columns c{};
  c.shortcutW = options_.shortcutWidth;
  c.shortcutX = r.x + r.w - c.shortcutW - 12.0;
  c.labelX = r.x + kIconColumn;
  const double room = std::max(0.0, c.shortcutX - c.labelX - kColumnGap);
  if (!options_.showDescriptions || room < 140.0 + kMinDescriptionWidth) {
    c.labelW = room;
    c.descX = c.descW = 0.0;
    return c;
  }
  c.labelW = std::clamp(room * 0.42, 120.0, 300.0);
  c.descX = c.labelX + c.labelW + kColumnGap;
  c.descW = std::max(0.0, c.shortcutX - c.descX - kColumnGap);
  return c;
}

layout::RectD ActionListView::thumbRect() const {
  const double viewport = viewportHeight();
  const double content = contentHeight();
  if (content <= viewport || viewport <= 0.0) return {};
  const layout::Rect r = ui().absRect(id());
  const double h = std::max(kMinThumb, viewport * viewport / content);
  const double travel = std::max(0.0, viewport - h);
  const double max = content - viewport;
  const double y = viewTop() + (max > 0.0 ? scroll_ / max * travel : 0.0);
  return {r.x + r.w - kThumbWidth - 2.0, y, kThumbWidth, h};
}

bool ActionListView::thumbHit(double x, double y) const {
  const layout::RectD t = thumbRect();
  if (t.h <= 0.0) return false;
  const layout::Rect r = ui().absRect(id());
  return x >= r.x + r.w - kThumbGrabWidth && x < r.x + r.w && y >= viewTop() && y < viewTop() + viewportHeight();
}

void ActionListView::dragThumbTo(double y) {
  const layout::RectD t = thumbRect();
  const double viewport = viewportHeight();
  const double travel = viewport - t.h;
  if (travel <= 0.0) return;
  const double fraction = std::clamp((y - thumbGrab_ - viewTop()) / travel, 0.0, 1.0);
  setScrollOffset(fraction * (contentHeight() - viewport));
}

std::string_view ActionListView::tooltipText() const {
  if (hover_ < 0 || hover_ >= static_cast<int>(rows_.size())) return {};
  const ActionRow& row = rows_[static_cast<size_t>(hover_)];
  if (row.header) return {};
  return actions_[row.index].description;
}

// ---- painting -------------------------------------------------------------------------------------------------

void ActionListView::paint(PaintContext& ctx) {
  const layout::Rect area = ctx.rect();
  render::Painter& painter = ctx.painter();
  painter.fillRect(ctx.box(), ctx.color("panel"));
  painter.pushClip(ctx.box());
  const theme::TextStyle body = ctx.style("label.body").text;
  const theme::TextStyle muted = ctx.style("label.muted").text;
  const render::Color white{1.0f, 1.0f, 1.0f, 1.0f};
  const Columns cols = columns();
  const double top = viewTop();
  const double bottom = top + viewportHeight();

  // Rows: binary search for the first visible one, stop at the first below the viewport.
  const auto first = std::upper_bound(tops_.begin(), tops_.end(), scroll_);
  size_t r = first == tops_.begin() ? 0 : static_cast<size_t>(first - tops_.begin()) - 1;
  painter.pushClip(ctx.toPhysical(area.x, top, area.w, std::max(0.0, bottom - top)));
  for (; r < rows_.size(); ++r) {
    const double y = top + tops_[r] - scroll_;
    if (y >= bottom) break;
    const double h = tops_[r + 1] - tops_[r];
    const ActionRow& row = rows_[r];
    const bool selected = static_cast<int>(r) == selected_;
    const bool hover = static_cast<int>(r) == hover_;
    if (row.header) {
      painter.fillRect(ctx.toPhysical(area.x, y, area.w, h), ctx.color("hover", selected ? 1.0 : 0.55));
      if (selected) painter.border(ctx.toPhysical(area.x + 1, y + 1, area.w - 2, h - 2), render::CornerRadii::uniform(0.0f), ctx.hairline(), ctx.color("accent"));
      const bool collapsed = collapsed_[row.index] != 0 && query_.empty();
      drawIconSafe(ctx, collapsed ? "chevron-right" : "chevron-down", 14.0, ctx.toPhysical(area.x + 8, y, 18, h), ctx.color("muted"));
      TextOptions name;
      name.weight = 600;
      name.padLeft = 0.0;
      const double nameW = widthOf(ctx, groupNames_[row.index], body) + 8.0;
      ctx.drawText(groupNames_[row.index], body, ctx.toPhysical(area.x + kIconColumn - 6, y, std::min(nameW, area.w - kIconColumn), h), name);
      TextOptions count;
      count.padLeft = 0.0;
      ctx.drawText("(" + std::to_string(row.count) + ")", muted, ctx.toPhysical(area.x + kIconColumn - 6 + nameW + 2, y, 80, h), count);
      continue;
    }
    const ActionInfo& action = actions_[row.index];
    if (selected) {
      painter.fillRoundedRect(ctx.toPhysical(area.x + 4, y + 1, area.w - 8, h - 2), render::CornerRadii::uniform(ctx.px(6.0)), ctx.color("accent"));
    } else if (hover) {
      painter.fillRoundedRect(ctx.toPhysical(area.x + 4, y + 1, area.w - 8, h - 2), render::CornerRadii::uniform(ctx.px(6.0)), ctx.color("hover"));
    }
    const double dim = action.enabled ? 1.0 : 0.5;
    const render::Color text = selected ? white : ctx.color(body.color, dim);
    const render::Color grey = selected ? render::Color{1.0f, 1.0f, 1.0f, 0.75f} : ctx.color(muted.color, dim);
    if (options_.showIcons) drawIconSafe(ctx, action.icon, 16.0, ctx.toPhysical(area.x + 10, y, 16, h), selected ? white : ctx.color("muted", dim));
    const render::Color mark = selected ? render::Color{1.0f, 1.0f, 1.0f, 0.30f} : ctx.color("accent", 0.35);
    const auto highlight = [&](std::string_view s, const theme::TextStyle& st, double colX, double colW) {
      for (const std::string& term : query_.terms) {
        const size_t pos = findFolded(s, term);
        if (pos == std::string_view::npos) continue;
        const double x0 = widthOf(ctx, s.substr(0, pos), st);
        if (x0 >= colW) continue;
        const double w = std::min(widthOf(ctx, s.substr(pos, term.size()), st), colW - x0);
        painter.fillRoundedRect(ctx.toPhysical(colX + x0, y + 5, w, h - 10), render::CornerRadii::uniform(ctx.px(2.0)), mark);
      }
    };
    TextOptions label;
    label.color = text;
    label.padRight = 4.0;
    if (!query_.empty()) highlight(action.label, body, cols.labelX, cols.labelW - 4.0);
    ctx.drawText(action.label, body, ctx.toPhysical(cols.labelX, y, cols.labelW, h), label);
    if (cols.descW > 0.0 && !action.description.empty()) {
      TextOptions desc;
      desc.color = grey;
      desc.padRight = 4.0;
      if (!query_.empty()) highlight(action.description, muted, cols.descX, cols.descW - 4.0);
      ctx.drawText(action.description, muted, ctx.toPhysical(cols.descX, y, cols.descW, h), desc);
    }
    if (!action.shortcut.empty()) {
      TextOptions chord;
      chord.color = selected ? white : ctx.color(body.color, dim);
      chord.weight = 500;
      ctx.drawText(action.shortcut, body, ctx.toPhysical(cols.shortcutX, y, cols.shortcutW, h), chord);
    }
  }
  painter.popClip();

  // Scroll thumb.
  const layout::RectD thumb = thumbRect();
  if (thumb.h > 0.0) {
    painter.fillRoundedRect(ctx.toPhysical(thumb.x, thumb.y, thumb.w, thumb.h), render::CornerRadii::uniform(ctx.px(3.0)), ctx.color("border-strong", thumbDrag_ ? 1.0 : 0.7));
  }

  // Column header strip (fixed).
  if (options_.columnHeaders) {
    painter.fillRect(ctx.toPhysical(area.x, area.y, area.w, kColumnHeaderHeight), ctx.color("panel"));
    painter.fillRect(ctx.toPhysical(area.x, area.y + kColumnHeaderHeight - 1, area.w, 1), ctx.color("border"));
    theme::TextStyle head = muted;
    head.weight = 600;
    TextOptions o;
    ctx.drawText(options_.labelHeader, head, ctx.toPhysical(cols.labelX, area.y, std::max(0.0, cols.labelW), kColumnHeaderHeight), o);
    if (cols.descW > 0.0) ctx.drawText(options_.descriptionHeader, head, ctx.toPhysical(cols.descX, area.y, cols.descW, kColumnHeaderHeight), o);
    ctx.drawText(options_.shortcutHeader, head, ctx.toPhysical(cols.shortcutX, area.y, cols.shortcutW, kColumnHeaderHeight), o);
  }
  painter.popClip();
}

void ActionListView::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.px(4.0));
}

// ---- pointer ---------------------------------------------------------------------------------------------------

void ActionListView::onPointerDown(Event& e) {
  if (e.button != Button::Left) return;
  ui().router().focus(id(), core::events::FocusReason::Pointer);
  e.markHandled();
  if (thumbHit(e.x, e.y)) {
    const layout::RectD t = thumbRect();
    thumbGrab_ = e.y >= t.y && e.y < t.y + t.h ? e.y - t.y : t.h * 0.5;
    thumbDrag_ = true;
    dragThumbTo(e.y);
    ui().router().capturePointer(id());
    return;
  }
  const int row = rowAt(e.x, e.y);
  if (row < 0) return;
  setSelectedRow(row, true);
  if (rows_[static_cast<size_t>(row)].header) {
    toggleGroup(rows_[static_cast<size_t>(row)].index);
    return;
  }
  pressed_ = row;
  armed_ = true;
  ui().router().capturePointer(id());
}

void ActionListView::onDragStart(Event& e) {
  if (!armed_ || dragging_ || hub_ == nullptr || pressed_ < 0 || pressed_ >= static_cast<int>(rows_.size())) return;
  const ActionRow& row = rows_[static_cast<size_t>(pressed_)];
  if (row.header) return;
  dragging_ = hub_->begin(makeActionDragPayload(actions_[row.index]), e.x, e.y);
  e.markHandled();
}

void ActionListView::onPointerMove(Event& e) {
  if (thumbDrag_) {
    dragThumbTo(e.y);
    e.markHandled();
    return;
  }
  if (dragging_ && hub_ != nullptr) {
    hub_->move(e.x, e.y);
    e.markHandled();
    return;
  }
  const int hover = rowAt(e.x, e.y);
  if (hover != hover_) {
    hover_ = hover;
    requestPaint();
  }
}

void ActionListView::onPointerUp(Event& e) {
  if (e.button != Button::Left) return;
  const bool wasDragging = dragging_;
  dragging_ = false;
  armed_ = false;
  pressed_ = -1;
  thumbDrag_ = false;
  if (wasDragging && hub_ != nullptr) hub_->end(e.x, e.y);
}

void ActionListView::onCaptureLost(Event&) {
  if (dragging_ && hub_ != nullptr) hub_->cancel();
  dragging_ = false;
  armed_ = false;
  thumbDrag_ = false;
  pressed_ = -1;
}

void ActionListView::onPointerLeave(Event&) {
  if (hover_ != -1) {
    hover_ = -1;
    requestPaint();
  }
}

void ActionListView::onPointerWheel(Event& e) {
  const double before = scroll_;
  setScrollOffset(scroll_ - e.wheelY * 48.0);
  if (scroll_ != before) {
    e.markHandled();
    e.stopPropagation();
  }
}

void ActionListView::onDoubleClick(Event& e) {
  const int row = rowAt(e.x, e.y);
  if (row < 0 || rows_[static_cast<size_t>(row)].header) return;
  setSelectedRow(row, true);
  e.markHandled();
  activateSelected();
}

// ---- keyboard -----------------------------------------------------------------------------------------------------

void ActionListView::onKeyDown(Event& e) {
  if (e.key == Key::Escape && dragging_) {
    dragging_ = false;
    armed_ = false;
    if (hub_ != nullptr) hub_->cancel();
    ui().router().cancelPointerInteraction();
    e.markHandled();
    return;
  }
  if (e.key == static_cast<Key>('F') && (e.modifiers & Mod::kCtrl) != 0 && onSearchRequest_) {
    onSearchRequest_(0);
    e.markHandled();
    return;
  }
  if ((e.modifiers & (Mod::kCtrl | Mod::kAlt | Mod::kMeta)) != 0) return;
  const int page = std::max(1, static_cast<int>(viewportHeight() / kRowHeight) - 1);
  const ActionRow* current = selected_ >= 0 && selected_ < static_cast<int>(rows_.size()) ? &rows_[static_cast<size_t>(selected_)] : nullptr;
  switch (e.key) {
    case Key::Down: moveSelection(1); break;
    case Key::Up: moveSelection(-1); break;
    case Key::PageDown: moveSelection(page); break;
    case Key::PageUp: moveSelection(-page); break;
    case Key::Home: setSelectedRow(rows_.empty() ? -1 : 0, true); break;
    case Key::End: setSelectedRow(static_cast<int>(rows_.size()) - 1, true); break;
    case Key::Enter:
    case Key::Space:
      if (current != nullptr && current->header) {
        toggleGroup(current->index);
      } else if (e.key == Key::Enter) {
        activateSelected();
      } else {
        return;
      }
      break;
    case Key::Left:
    case Key::Right: {
      if (current == nullptr) return;
      const bool collapse = e.key == Key::Left;
      if (current->header) {
        if ((collapsed_[current->index] != 0) == collapse) return;
        toggleGroup(current->index);
      } else if (collapse) {  // on an action: jump to its header
        for (int r = selected_; r >= 0; --r) {
          if (rows_[static_cast<size_t>(r)].header) {
            setSelectedRow(r, true);
            break;
          }
        }
      } else {
        return;
      }
      break;
    }
    default: return;
  }
  e.markHandled();
}

void ActionListView::onTextInput(Event& e) {
  if (e.codePoint < 0x20 || e.codePoint == 0x7F || !onSearchRequest_) return;
  onSearchRequest_(e.codePoint);
  e.markHandled();
}

}  // namespace r1ui::widgets

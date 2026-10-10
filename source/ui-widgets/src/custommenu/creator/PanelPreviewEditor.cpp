// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of PanelPreviewEditor.h: the grid geometry, painting, pointer, keys, drag source
//   and drop target.
// Invariants: all geometry derives from grid() (one function), so painting, hit tests and the drop
//   indicator agree; selected_/dropIndex_/hover_ are -1 or inside the entry count (clamped in refresh);
//   the hub is touched only between a press on a button and the release, Escape or capture loss.
// Callers: CreateCustomMenuWindow, the gallery, tests.
#include "r1ui/widgets/custommenu/creator/PanelPreviewEditor.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/custommenu/creator/PiePreviewEditor.h"
#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace cm = commands::custommenu;
namespace layout = core::layout;
using core::events::Button;
using core::events::Key;
namespace Mod = core::events::Mod;
using theme::State::kHover;
using theme::State::kNone;
using theme::State::kSelected;
using theme::StyleProperty;

namespace {

constexpr theme::StyleRuleEntry kRows[] = {
    {"panelpreview.frame", kNone, StyleProperty::Background, "color:panel-secondary"},
    {"panelpreview.frame", kNone, StyleProperty::BorderColor, "color:border"},
    {"panelpreview.frame", kNone, StyleProperty::BorderWidth, "number:1"},
    {"panelpreview.frame", kNone, StyleProperty::Radius, "radius:md"},
    {"panelpreview.cell", kNone, StyleProperty::Background, "color:panel-field"},
    {"panelpreview.cell", kNone, StyleProperty::Foreground, "color:surface"},
    {"panelpreview.cell", kNone, StyleProperty::BorderColor, "color:border"},
    {"panelpreview.cell", kNone, StyleProperty::BorderWidth, "number:1"},
    {"panelpreview.cell", kNone, StyleProperty::Radius, "radius:md"},
    {"panelpreview.cell", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"panelpreview.cell", kNone, StyleProperty::LineHeight, "number:16"},
    {"panelpreview.cell", kNone, StyleProperty::PaddingX, "number:8"},
    {"panelpreview.cell", kHover, StyleProperty::Background, "color:panel-field-hover"},
    {"panelpreview.cell", kSelected, StyleProperty::Background, "color:accent@0.18"},
    {"panelpreview.cell", kSelected, StyleProperty::BorderColor, "color:accent"},
};

constexpr double kIconSize = 16.0;
constexpr double kTallIconSize = 24.0;
constexpr double kIconGap = 6.0;
constexpr double kTallThreshold = 56.0;

bool parseEntryDrag(const std::string& nodeId, int& index) {
  const std::string prefix = kEntryDragPrefix;
  if (nodeId.size() <= prefix.size() || nodeId.compare(0, prefix.size(), prefix) != 0) return false;
  int value = 0;
  for (size_t i = prefix.size(); i < nodeId.size(); ++i) {
    if (nodeId[i] < '0' || nodeId[i] > '9' || value > 100000) return false;
    value = value * 10 + (nodeId[i] - '0');
  }
  index = value;
  return true;
}

}  // namespace

std::span<const theme::StyleRuleEntry> PanelPreviewEditor::styleRows() { return kRows; }

PanelPreviewEditor::PanelPreviewEditor(CommandServices services, CreatorSession& session) : services_(services), session_(session) {}

void PanelPreviewEditor::onAttached() {
  setFocusable(true);
  style().flexGrow = 1.0;
  style().flexShrink = 1.0;
  style().minWidth = layout::Length::px(220.0);
  style().minHeight = layout::Length::px(160.0);
  style().overflow = layout::Overflow::Hidden;
  menu_ = std::make_unique<MenuController>(ui());
}

void PanelPreviewEditor::onDetached() {
  if (dragging_ && hub_ != nullptr) hub_->cancel();
  dragging_ = armed_ = false;
  if (hub_ != nullptr) hub_->removeTarget(id());
  hub_ = nullptr;
  if (menu_) menu_->close();
}

void PanelPreviewEditor::setDragHub(DragHub* hub) {
  if (hub_ != nullptr && hub_ != hub) hub_->removeTarget(id());
  hub_ = hub;
  if (hub_ != nullptr) hub_->addTarget(id(), static_cast<DragTarget*>(this));
}

// ---- geometry ---------------------------------------------------------------------------------------

PanelPreviewEditor::Grid PanelPreviewEditor::grid() const {
  Grid g;
  const cm::MenuDraft* d = draft();
  if (d == nullptr) return g;
  const cm::CustomMenu& menu = d->menu();
  const layout::Rect self = ui().absRect(id());
  g.columns = std::clamp(menu.panel.columns, cm::kMinColumns, cm::kMaxColumns);
  g.cellH = static_cast<double>(std::clamp(menu.panel.buttonSize, cm::kMinButtonSize, cm::kMaxButtonSize));
  const double inner = std::max(0.0, self.w - 2.0 * kPadding);
  g.cellW = std::max(8.0, (inner - kGap * (g.columns - 1)) / g.columns);
  g.entries = static_cast<int>(menu.entries.size());
  g.rows = (g.entries + 1 + g.columns - 1) / g.columns;  // the drop-here cell follows the last button
  g.contentHeight = 2.0 * kPadding + g.rows * g.cellH + (g.rows - 1) * kGap;
  return g;
}

double PanelPreviewEditor::maxScroll() const { return std::max(0.0, grid().contentHeight - ui().absRect(id()).h); }

void PanelPreviewEditor::setScroll(double offset) {
  const double clamped = std::clamp(std::isfinite(offset) ? offset : 0.0, 0.0, maxScroll());
  if (clamped == scroll_) return;
  scroll_ = clamped;
  requestPaint();
}

bool PanelPreviewEditor::cellRect(int index, double& x, double& y, double& w, double& h) const {
  const Grid g = grid();
  if (index < 0 || index > g.entries) return false;
  const layout::Rect self = ui().absRect(id());
  x = self.x + kPadding + (index % g.columns) * (g.cellW + kGap);
  y = self.y + kPadding + (index / g.columns) * (g.cellH + kGap) - scroll_;
  w = g.cellW;
  h = g.cellH;
  return true;
}

int PanelPreviewEditor::entryAt(double x, double y) const {
  const Grid g = grid();
  for (int i = 0; i < g.entries; ++i) {
    double cx, cy, cw, ch;
    if (cellRect(i, cx, cy, cw, ch) && x >= cx && x < cx + cw && y >= cy && y < cy + ch) return i;
  }
  return -1;
}

int PanelPreviewEditor::insertionAt(double x, double y) const {
  const Grid g = grid();
  const layout::Rect self = ui().absRect(id());
  if (g.entries == 0 || !std::isfinite(x) || !std::isfinite(y)) return 0;
  const double top = self.y + kPadding - scroll_;
  const int row = std::clamp(static_cast<int>(std::floor((y - top) / (g.cellH + kGap))), 0, g.rows - 1);
  const int col = std::clamp(static_cast<int>(std::floor((x - (self.x + kPadding)) / (g.cellW + kGap))), 0, g.columns - 1);
  const int index = row * g.columns + col;
  if (index >= g.entries) return g.entries;
  double cx, cy, cw, ch;
  if (!cellRect(index, cx, cy, cw, ch)) return g.entries;
  return x > cx + cw * 0.5 ? index + 1 : index;
}

// ---- drawing ----------------------------------------------------------------------------------------

void PanelPreviewEditor::refresh() {
  const cm::MenuDraft* d = draft();
  const int count = d != nullptr ? static_cast<int>(d->menu().entries.size()) : 0;
  if (selected_ >= count) selected_ = -1;
  if (dropIndex_ > count) dropIndex_ = -1;
  hover_ = -1;
  setScroll(scroll_);
  requestPaint();
}

void PanelPreviewEditor::paint(PaintContext& ctx) {
  const cm::MenuDraft* d = draft();
  render::Painter& painter = ctx.painter();
  const theme::ResolvedStyle& frame = ctx.style("panelpreview.frame");
  painter.fillRoundedRect(ctx.box(), render::CornerRadii::uniform(ctx.px(frame.radius)), ctx.color(frame.background));
  painter.border(ctx.box(), render::CornerRadii::uniform(ctx.px(frame.radius)), ctx.px(frame.border.width), ctx.color(frame.border.color));
  if (d == nullptr || d->kind() != cm::MenuKind::Panel) return;
  painter.pushClip(ctx.box());
  const cm::CustomMenu& menu = d->menu();
  const Grid g = grid();
  const theme::ResolvedStyle& base = ctx.style("panelpreview.cell");
  const double scale = ctx.scale();
  for (int i = 0; i <= g.entries; ++i) {
    double x, y, w, h;
    if (!cellRect(i, x, y, w, h)) continue;
    const layout::Rect clipCheck = ctx.rect();
    if (y + h < clipCheck.y || y > clipCheck.y + clipCheck.h) continue;
    const render::Rect box = ctx.toPhysical(x, y, w, h);
    const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(base.radius));
    if (i == g.entries) {
      painter.border(box, radii, ctx.px(1.5), ctx.color("muted", 0.5));
      TextOptions hint;
      hint.align = TextAlign::Center;
      hint.color = ctx.color("muted", 0.8);
      ctx.drawText(g.entries == 0 ? "Drop actions here" : "Drop here", base.text, box, hint);
      continue;
    }
    const cm::MenuEntry& entry = menu.entries[static_cast<size_t>(i)];
    const commands::CommandDef* def = services_.registry.find(entry.commandId);
    const bool missing = def == nullptr;
    const uint8_t bits = i == selected_ ? kSelected : (i == hover_ ? kHover : kNone);
    const theme::ResolvedStyle& rs = ctx.resolve("panelpreview.cell", bits);
    painter.fillRoundedRect(box, radii, ctx.color(rs.background));
    painter.border(box, radii, ctx.px(i == selected_ ? 2.0 : rs.border.width), ctx.color(rs.border.color));
    const float fade = missing ? 0.5f : 1.0f;
    render::Color fg = ctx.color(rs.text.color);
    fg.a *= fade;
    std::string label = !entry.label.empty() ? entry.label : (def != nullptr ? def->label : entry.commandId);
    const std::string icon = !entry.icon.empty() ? entry.icon : (def != nullptr ? def->icon : std::string());
    const bool hasIcon = !icon.empty();
    const bool hasLabel = menu.panel.showLabels && !label.empty();
    TextOptions options;
    options.color = fg;
    const auto textWidth = [&](double room) {
      return std::min(room, static_cast<double>(ctx.ui().text().measure(label, static_cast<float>(rs.text.fontSize * scale), rs.text.weight)) / scale);
    };
    if (h >= kTallThreshold && hasIcon) {
      const double room = w - 2.0 * rs.paddingX;
      const double labelH = hasLabel ? rs.text.lineHeight : 0.0;
      const double gap = hasLabel ? 4.0 : 0.0;
      const double top = (h - (kTallIconSize + gap + labelH)) * 0.5;
      ctx.drawIcon(icon, kTallIconSize, ctx.toPhysical(x + (w - kTallIconSize) * 0.5, y + top, kTallIconSize, kTallIconSize), fg, "circle");
      if (hasLabel) {
        const double lw = textWidth(room);
        options.align = TextAlign::Center;
        ctx.drawText(label, rs.text, ctx.toPhysical(x + (w - lw) * 0.5 - 1.0, y + top + kTallIconSize + gap, lw + 2.0, labelH), options);
      }
      continue;
    }
    const double room = w - 2.0 * rs.paddingX;
    const double iconW = hasIcon ? kIconSize : 0.0;
    const double gap = hasIcon && hasLabel ? kIconGap : 0.0;
    const double lw = hasLabel ? textWidth(std::max(0.0, room - iconW - gap)) : 0.0;
    const double left = x + (w - (iconW + gap + lw)) * 0.5;
    if (hasIcon) ctx.drawIcon(icon, kIconSize, ctx.toPhysical(left, y, kIconSize, h), fg, "circle");
    if (hasLabel) ctx.drawText(label, rs.text, ctx.toPhysical(left + iconW + gap, y, lw + 1.0, h), options);
  }
  painter.popClip();
}

void PanelPreviewEditor::paintOver(PaintContext& ctx) {
  const cm::MenuDraft* d = draft();
  if (d == nullptr || d->kind() != cm::MenuKind::Panel) return;
  render::Painter& painter = ctx.painter();
  if (dropIndex_ >= 0) {
    double x, y, w, h;
    if (cellRect(dropIndex_, x, y, w, h)) {
      painter.fillRoundedRect(ctx.toPhysical(x - kGap * 0.5 - 1.5, y, 3.0, h), render::CornerRadii::uniform(ctx.px(1.5)), ctx.color("accent"));
    }
  }
  const double overflow = maxScroll();
  if (overflow > 0.0) {
    const layout::Rect self = ctx.rect();
    const double content = grid().contentHeight;
    const double thumbH = std::max(24.0, self.h * self.h / content);
    const double thumbY = self.y + (self.h - thumbH) * (scroll_ / overflow);
    painter.fillRoundedRect(ctx.toPhysical(self.x + self.w - 8.0, thumbY, 5.0, thumbH), render::CornerRadii::uniform(ctx.px(2.5)), ctx.color("border-strong", 0.7));
  }
  if (focusVisible()) ctx.focusRing(ctx.px(4.0));
}

// ---- selection and editing --------------------------------------------------------------------------

void PanelPreviewEditor::say(const std::string& text) const {
  if (onMessage_) onMessage_(text);
}

void PanelPreviewEditor::changed() {
  refresh();
  if (onChanged_) onChanged_();
}

void PanelPreviewEditor::select(int index) {
  const cm::MenuDraft* d = draft();
  const int count = d != nullptr ? static_cast<int>(d->menu().entries.size()) : 0;
  const int value = index >= 0 && index < count ? index : -1;
  if (value == selected_) return;
  selected_ = value;
  if (value >= 0) {  // keep it in view
    double x, y, w, h;
    const layout::Rect self = ui().absRect(id());
    if (cellRect(value, x, y, w, h)) {
      if (y < self.y) setScroll(scroll_ - (self.y - y) - kPadding);
      else if (y + h > self.y + self.h) setScroll(scroll_ + (y + h - (self.y + self.h)) + kPadding);
    }
  }
  requestPaint();
  if (onSelect_) onSelect_(selected_);
}

bool PanelPreviewEditor::dropCommand(int insertAt, const std::string& commandId) {
  cm::MenuDraft* d = draft();
  if (d == nullptr) return false;
  if (commandId.empty() || services_.registry.find(commandId) == nullptr) {
    say("That action is not available.");
    return false;
  }
  const size_t at = insertAt < 0 ? cm::MenuDraft::npos : static_cast<size_t>(insertAt);
  const cm::MenuEditResult result = d->addEntry(commandId, at);
  if (!result.ok) {
    say(result.reason.empty() ? "The panel did not accept that action." : result.reason);
    return false;
  }
  const commands::CommandDef* def = services_.registry.find(commandId);
  say("Added " + (def != nullptr ? def->label : commandId) + " to the panel.");
  select(static_cast<int>(result.index));
  changed();
  return true;
}

bool PanelPreviewEditor::removeEntry(int index) {
  cm::MenuDraft* d = draft();
  if (d == nullptr || index < 0 || index >= static_cast<int>(d->menu().entries.size())) return false;
  const cm::MenuEditResult result = d->clearSlot(static_cast<size_t>(index));
  if (!result.ok) {
    say(result.reason);
    return false;
  }
  say("Removed the button.");
  if (selected_ >= static_cast<int>(d->menu().entries.size())) selected_ = static_cast<int>(d->menu().entries.size()) - 1;
  changed();
  if (onSelect_) onSelect_(selected_);
  return true;
}

bool PanelPreviewEditor::moveSelected(int delta) {
  cm::MenuDraft* d = draft();
  if (d == nullptr || selected_ < 0) return false;
  const int target = selected_ + delta;
  if (target < 0 || target >= static_cast<int>(d->menu().entries.size())) return false;
  const cm::MenuEditResult result = d->moveEntry(static_cast<size_t>(selected_), static_cast<size_t>(target));
  if (!result.ok) {
    say(result.reason);
    return false;
  }
  selected_ = target;
  changed();
  if (onSelect_) onSelect_(selected_);
  return true;
}

void PanelPreviewEditor::openContextMenu(int index, double x, double y) {
  const cm::MenuDraft* d = draft();
  if (!menu_ || d == nullptr || index < 0 || index >= static_cast<int>(d->menu().entries.size())) return;
  const int count = static_cast<int>(d->menu().entries.size());
  MenuSpec spec;
  spec.items.push_back(menuAction("remove", "Remove from panel"));
  spec.items.push_back(menuSeparator());
  MenuItemSpec left = menuAction("left", "Move left");
  left.enabled = index > 0;
  MenuItemSpec right = menuAction("right", "Move right");
  right.enabled = index + 1 < count;
  spec.items.push_back(std::move(left));
  spec.items.push_back(std::move(right));
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  spec.onCommand = [context, self](const MenuItemSpec& item) {
    PanelPreviewEditor* editor = context->objectAs<PanelPreviewEditor>(self);
    if (editor == nullptr) return;
    if (item.id == "remove") editor->removeEntry(editor->selected());
    else if (item.id == "left") editor->moveSelected(-1);
    else if (item.id == "right") editor->moveSelected(1);
  };
  menu_->openContextMenu(std::move(spec), x, y);
}

// ---- pointer and keys -------------------------------------------------------------------------------

void PanelPreviewEditor::onPointerDown(Event& e) {
  ui().router().focus(id(), core::events::FocusReason::Pointer);
  const int index = entryAt(e.x, e.y);
  if (e.button == Button::Right) {
    if (index >= 0) {
      select(index);
      openContextMenu(index, e.x, e.y);
    }
    e.markHandled();
    return;
  }
  if (e.button != Button::Left) return;
  e.markHandled();
  select(index);
  if (index >= 0) {
    pressed_ = index;
    armed_ = true;
    ui().router().capturePointer(id());
  }
}

void PanelPreviewEditor::onDragStart(Event& e) {
  const cm::MenuDraft* d = draft();
  if (!armed_ || dragging_ || hub_ == nullptr || d == nullptr || pressed_ < 0 || pressed_ >= static_cast<int>(d->menu().entries.size())) return;
  const cm::MenuEntry& entry = d->menu().entries[static_cast<size_t>(pressed_)];
  DragPayload payload;
  payload.kind = DragPayload::Kind::Node;
  payload.nodeId = kEntryDragPrefix + std::to_string(pressed_);
  const commands::CommandDef* def = services_.registry.find(entry.commandId);
  payload.text = !entry.label.empty() ? entry.label : (def != nullptr ? def->label : entry.commandId);
  dragSource_ = pressed_;
  dragging_ = hub_->begin(std::move(payload), e.x, e.y);
  e.markHandled();
}

void PanelPreviewEditor::onPointerMove(Event& e) {
  if (dragging_ && hub_ != nullptr) {
    hub_->move(e.x, e.y);
    e.markHandled();
    return;
  }
  const int hover = entryAt(e.x, e.y);
  if (hover != hover_) {
    hover_ = hover;
    requestPaint();
  }
}

void PanelPreviewEditor::onPointerUp(Event& e) {
  if (e.button != Button::Left) return;
  if (dragging_ && hub_ != nullptr) hub_->end(e.x, e.y);  // dragging_ stays set: the hub asks dragOver at the release point
  dragging_ = armed_ = false;
  pressed_ = dragSource_ = -1;
}

void PanelPreviewEditor::onCaptureLost(Event&) {
  if (dragging_ && hub_ != nullptr) hub_->cancel();
  dragging_ = armed_ = false;
  pressed_ = dragSource_ = -1;
}

void PanelPreviewEditor::onPointerWheel(Event& e) {
  const double before = scroll_;
  setScroll(scroll_ - e.wheelY * 48.0);
  if (scroll_ != before) {
    e.markHandled();
    e.stopPropagation();
  }
}

void PanelPreviewEditor::onKeyDown(Event& e) {
  const cm::MenuDraft* d = draft();
  if (d == nullptr) return;
  const int count = static_cast<int>(d->menu().entries.size());
  const int columns = std::max(1, d->menu().panel.columns);
  const bool ctrl = (e.modifiers & Mod::kCtrl) != 0;
  switch (e.key) {
    case Key::Escape:
      if (dragging_) {
        dragging_ = armed_ = false;
        if (hub_ != nullptr) hub_->cancel();
        ui().router().cancelPointerInteraction();
        e.markHandled();
      }
      return;
    case Key::Delete:
    case Key::Backspace:
      if (selected_ >= 0) removeEntry(selected_);
      break;
    case Key::Left:
      if (ctrl) moveSelected(-1);
      else select(selected_ <= 0 ? (count > 0 ? 0 : -1) : selected_ - 1);
      break;
    case Key::Right:
      if (ctrl) moveSelected(1);
      else select(selected_ < 0 ? (count > 0 ? 0 : -1) : std::min(count - 1, selected_ + 1));
      break;
    case Key::Up: select(selected_ < 0 ? (count > 0 ? 0 : -1) : std::max(0, selected_ - columns)); break;
    case Key::Down: select(selected_ < 0 ? (count > 0 ? 0 : -1) : std::min(count - 1, selected_ + columns)); break;
    case Key::Home: select(count > 0 ? 0 : -1); break;
    case Key::End: select(count - 1); break;
    default: return;
  }
  e.markHandled();
}

// ---- drop target ------------------------------------------------------------------------------------

int PanelPreviewEditor::acceptedInsertion(const DragPayload& payload, double x, double y) {
  cm::MenuDraft* d = draft();
  if (d == nullptr) return -1;
  const int insertion = insertionAt(x, y);
  if (payload.kind == DragPayload::Kind::Command) {
    if (payload.commandId.empty() || services_.registry.find(payload.commandId) == nullptr) return -1;
    return d->canAddEntry(payload.commandId, static_cast<size_t>(insertion)).ok ? insertion : -1;
  }
  int from = -1;
  if (!dragging_ || !parseEntryDrag(payload.nodeId, from) || from != dragSource_) return -1;
  const int to = insertion > from ? insertion - 1 : insertion;
  if (to == from) return -1;
  return d->canMoveEntry(static_cast<size_t>(from), static_cast<size_t>(to)).ok ? insertion : -1;
}

bool PanelPreviewEditor::dragOver(const DragPayload& payload, double x, double y) {
  const int accepted = acceptedInsertion(payload, x, y);
  if (accepted != dropIndex_) {
    dropIndex_ = accepted;
    requestPaint();
  }
  return accepted >= 0;
}

void PanelPreviewEditor::dragLeave() {
  if (dropIndex_ != -1) {
    dropIndex_ = -1;
    requestPaint();
  }
}

bool PanelPreviewEditor::dragDrop(const DragPayload& payload, double x, double y) {
  dropIndex_ = -1;
  const int insertion = insertionAt(x, y);
  if (payload.kind == DragPayload::Kind::Command) return dropCommand(insertion, payload.commandId);
  cm::MenuDraft* d = draft();
  int from = -1;
  if (d == nullptr || !parseEntryDrag(payload.nodeId, from) || from != dragSource_) return false;
  const int to = insertion > from ? insertion - 1 : insertion;
  if (to == from) return false;
  const cm::MenuEditResult result = d->moveEntry(static_cast<size_t>(from), static_cast<size_t>(to));
  if (!result.ok) {
    say(result.reason);
    return false;
  }
  say("Moved the button.");
  selected_ = to;
  changed();
  if (onSelect_) onSelect_(selected_);
  return true;
}

}  // namespace r1ui::widgets

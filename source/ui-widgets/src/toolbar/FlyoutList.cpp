// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of FlyoutList.h and the flyout.* style rows.
// Invariants: highlight_ is -1 or the index of a selectable row; rowTop() is the single source of
//   row geometry for measure, paint, hit testing and rowRect(); a pick closes the overlay before the
//   callback so the callback may open another popup.
// Callers: Toolbar, TabBar (through openFlyout), UiContext (events, paint).
#include "r1ui/widgets/toolbar/FlyoutList.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace layout = core::layout;
namespace State = theme::State;
using core::events::Button;
using core::events::Key;
using theme::StyleProperty;

namespace {

constexpr double kIconSize = 14.0;
constexpr double kIconSlot = 14.0;

constexpr theme::StyleRuleEntry kRows[] = {
    {"flyout.item", State::kNone, StyleProperty::Background, "transparent"},
    {"flyout.item", State::kNone, StyleProperty::Foreground, "color:surface"},
    {"flyout.item", State::kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"flyout.item", State::kNone, StyleProperty::FontWeight, "weight:medium"},
    {"flyout.item", State::kNone, StyleProperty::LineHeight, "number:16"},
    {"flyout.item", State::kNone, StyleProperty::Radius, "radius:md"},
    {"flyout.item", State::kHover, StyleProperty::Background, "color:hover"},
    {"flyout.item", State::kDisabled, StyleProperty::Foreground, "color:muted@0.5"},
    {"flyout.shortcut", State::kNone, StyleProperty::Foreground, "color:muted"},
    {"flyout.shortcut", State::kNone, StyleProperty::FontSize, "fontSize:11"},
    {"flyout.shortcut", State::kNone, StyleProperty::LineHeight, "number:16"},
    {"flyout.separator", State::kNone, StyleProperty::Background, "color:border"},
};

}  // namespace

std::span<const theme::StyleRuleEntry> FlyoutList::styleRows() { return kRows; }

FlyoutList::FlyoutList(std::vector<FlyoutItem> items, std::function<void(size_t)> onPick) : items_(std::move(items)), onPick_(std::move(onPick)) {
  if (items_.size() > kMaxItems) items_.resize(kMaxItems);
}

void FlyoutList::onAttached() {
  style().hasMeasure = true;
  style().flexShrink = 0.0;
  setFocusable(true);
}

double FlyoutList::rowTop(size_t index) const {
  double y = 0.0;
  for (size_t i = 0; i < index && i < items_.size(); ++i) y += items_[i].separator ? kSeparatorHeight : kRowHeight;
  return y;
}

layout::Rect FlyoutList::rowRect(size_t index) const {
  if (index >= items_.size()) return {};
  const layout::Rect me = ui().absRect(id());
  const int top = static_cast<int>(std::lround(rowTop(index)));
  return {0, top, me.w, static_cast<int>(items_[index].separator ? kSeparatorHeight : kRowHeight)};
}

layout::MeasureResult FlyoutList::measure(const layout::MeasureInput&) {
  const double scale = ui().scale();
  const theme::ResolvedStyle& item = ui().services().resolve("flyout.item", 0);
  const theme::ResolvedStyle& hint = ui().services().resolve("flyout.shortcut", 0);
  bool anyIcon = false;
  for (const FlyoutItem& i : items_) anyIcon = anyIcon || (!i.separator && (!i.icon.empty() || i.checked));
  double width = 0.0;
  for (const FlyoutItem& i : items_) {
    if (i.separator) continue;
    double w = 2.0 * kPadX;
    if (anyIcon) w += kIconSlot + kGap;
    w += static_cast<double>(ui().text().measure(i.label, static_cast<float>(item.text.fontSize * scale), item.text.weight)) / scale;
    if (!i.shortcut.empty()) w += kGap + static_cast<double>(ui().text().measure(i.shortcut, static_cast<float>(hint.text.fontSize * scale), hint.text.weight)) / scale;
    width = std::max(width, w);
  }
  return {std::ceil(width), rowTop(items_.size())};
}

void FlyoutList::paint(PaintContext& ctx) {
  bool anyIcon = false;
  for (const FlyoutItem& i : items_) anyIcon = anyIcon || (!i.separator && (!i.icon.empty() || i.checked));
  const layout::Rect me = ctx.rect();
  for (size_t n = 0; n < items_.size(); ++n) {
    const FlyoutItem& it = items_[n];
    const double top = me.y + rowTop(n);
    if (it.separator) {
      const theme::ResolvedStyle& sep = ctx.resolve("flyout.separator", 0);
      ctx.painter().fillRect(ctx.toPhysical(me.x, top + (kSeparatorHeight - 1.0) * 0.5, me.w, 1.0), ctx.color(sep.background));
      continue;
    }
    const bool hot = static_cast<int>(n) == highlight_ && it.enabled;
    const uint8_t bits = (hot ? State::kHover : State::kNone) | (it.enabled ? State::kNone : State::kDisabled);
    const theme::ResolvedStyle& rs = ctx.resolve("flyout.item", bits);
    const render::Rect row = ctx.toPhysical(me.x, top, me.w, kRowHeight);
    if (rs.background.a > 0) ctx.painter().fillRoundedRect(row, render::CornerRadii::uniform(ctx.px(rs.radius)), ctx.color(rs.background));
    const render::Color tint = ctx.color(rs.text.color);
    double x = kPadX;
    if (anyIcon) {
      const render::Rect iconBox = ctx.toPhysical(me.x + x, top, kIconSlot, kRowHeight);
      if (!it.icon.empty()) ctx.drawIcon(it.icon, kIconSize, iconBox, tint);
      else if (it.checked) ctx.drawIcon("check", 12.0, iconBox, tint);
      x += kIconSlot + kGap;
    }
    const theme::ResolvedStyle& hint = ctx.resolve("flyout.shortcut", 0);
    float hintWidth = 0.0f;
    if (!it.shortcut.empty()) {
      const float size = static_cast<float>(hint.text.fontSize) * ctx.scale();
      hintWidth = ui().text().measure(it.shortcut, size, hint.text.weight);
      const render::Rect hintBox = ctx.toPhysical(me.x, top, me.w, kRowHeight);
      TextOptions o;
      o.padRight = kPadX;
      o.align = TextAlign::End;
      o.ellipsis = false;
      if (!it.enabled) o.color = ctx.color(rs.text.color);
      ctx.drawText(it.shortcut, hint.text, hintBox, o);
    }
    const render::Rect labelBox = ctx.toPhysical(me.x, top, me.w, kRowHeight);
    TextOptions o;
    o.padLeft = x;
    o.padRight = kPadX + (hintWidth > 0.0f ? hintWidth / ctx.scale() + kGap : 0.0);
    ctx.drawText(it.label, rs.text, labelBox, o);
  }
}

// ---- highlight and picking ----------------------------------------------------------------------

int FlyoutList::rowAt(double localY) const {
  if (localY < 0.0) return -1;
  double y = 0.0;
  for (size_t i = 0; i < items_.size(); ++i) {
    const double h = items_[i].separator ? kSeparatorHeight : kRowHeight;
    if (localY < y + h) return items_[i].separator || !items_[i].enabled ? -1 : static_cast<int>(i);
    y += h;
  }
  return -1;
}

void FlyoutList::setHighlighted(int index) {
  if (index < -1 || index >= static_cast<int>(items_.size())) index = -1;
  if (index >= 0 && (items_[static_cast<size_t>(index)].separator || !items_[static_cast<size_t>(index)].enabled)) index = -1;
  if (index == highlight_) return;
  highlight_ = index;
  requestPaint();
}

int FlyoutList::step(int from, int direction) const {
  for (int i = from + direction; i >= 0 && i < static_cast<int>(items_.size()); i += direction) {
    const FlyoutItem& it = items_[static_cast<size_t>(i)];
    if (!it.separator && it.enabled) return i;
  }
  return from;
}

void FlyoutList::onPointerMove(Event& e) {
  const layout::Rect me = ui().absRect(id());
  setHighlighted(rowAt(e.y - me.y));
}

void FlyoutList::onPointerLeave(Event&) { setHighlighted(-1); }

bool FlyoutList::pick(size_t index) {
  if (index >= items_.size() || items_[index].separator || !items_[index].enabled) return false;
  auto callback = onPick_;
  auto close = close_;
  if (close) close();
  if (callback) callback(index);
  return true;
}

void FlyoutList::onClick(Event& e) {
  if (e.button != Button::Left) return;
  const layout::Rect me = ui().absRect(id());
  const int row = rowAt(e.y - me.y);
  if (row < 0) return;
  e.markHandled();
  pick(static_cast<size_t>(row));
}

void FlyoutList::onKeyDown(Event& e) {
  if (items_.empty()) return;
  if (e.modifiers & (core::events::Mod::kCtrl | core::events::Mod::kAlt | core::events::Mod::kMeta)) return;
  switch (e.key) {
    case Key::Down: setHighlighted(highlight_ < 0 ? step(-1, 1) : step(highlight_, 1)); break;
    case Key::Up: setHighlighted(highlight_ < 0 ? step(static_cast<int>(items_.size()), -1) : step(highlight_, -1)); break;
    case Key::Home: setHighlighted(step(-1, 1)); break;
    case Key::End: setHighlighted(step(static_cast<int>(items_.size()), -1)); break;
    case Key::Enter:
    case Key::Space:
      if (highlight_ >= 0) pick(static_cast<size_t>(highlight_));
      break;
    default: return;
  }
  e.markHandled();
}

OverlayHandle openFlyout(UiContext& ui, std::vector<FlyoutItem> items, std::function<void(size_t)> onPick, const FlyoutOpenOptions& options) {
  if (items.empty()) return {};
  OverlayOptions o;
  o.anchor = options.anchor;
  o.placement = options.placement;
  o.gap = options.gap;
  o.anchorWidget = options.anchorWidget;
  o.surface = OverlaySurface::Menu;
  o.dismissOnWindowDeactivate = true;
  o.focusOnOpen = true;
  o.onClosed = options.onClosed;
  const OverlayHandle handle = ui.overlays().open(o);
  if (!handle.valid()) return {};
  FlyoutList& list = ui.create<FlyoutList>(handle.host, std::move(items), std::move(onPick));
  const OverlayId overlay = handle.id;
  UiContext* context = &ui;
  list.setCloseHook([context, overlay] { context->overlays().close(overlay); });
  list.setHighlighted(options.initialHighlight);
  return handle;
}

}  // namespace r1ui::widgets

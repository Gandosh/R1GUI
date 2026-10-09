// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Segmented.h: style rows, item geometry, hit testing, keyboard selection
//   and painting.
// Invariants: selected_ and hover_ are -1 or valid indices; pressedItem_ is -1 unless a left press
//   began on an enabled item; geometry for hit testing and painting comes from one function
//   (layoutFor), so what is drawn is what is hit.
// Callers: UiContext (rows registered through create<T>), tests.
#include "r1ui/widgets/segmented/Segmented.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/button/Button.h"  // isValidIconName
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

using core::events::Key;
using theme::State::kDisabled;
using theme::State::kHover;
using theme::State::kNone;
using theme::State::kSelected;
using theme::StyleProperty;

constexpr double kIconSize = 12.0;
constexpr double kIconGap = 4.0;

constexpr theme::StyleRuleEntry kRows[] = {
    {"segmented.container", kNone, StyleProperty::Background, "color:panel-field"},
    {"segmented.container", kNone, StyleProperty::Radius, "metric:field.radius"},
    {"segmented.container", kDisabled, StyleProperty::Opacity, "number:0.5"},
    {"segmented.item", kNone, StyleProperty::Background, "transparent"},
    {"segmented.item", kNone, StyleProperty::Foreground, "color:muted"},
    {"segmented.item", kNone, StyleProperty::Radius, "radius:sm"},
    {"segmented.item", kHover, StyleProperty::Background, "color:hover"},
    {"segmented.item", kHover, StyleProperty::Foreground, "color:surface"},
    {"segmented.item", kSelected, StyleProperty::Background, "color:panel-selected-muted"},
    {"segmented.item", kSelected, StyleProperty::Foreground, "color:surface"},
    {"segmented.item", kDisabled, StyleProperty::Opacity, "number:0.5"},
    {"segmented.item.sm", kNone, StyleProperty::FontSize, "fontSize:11"},
    {"segmented.item.sm", kNone, StyleProperty::PaddingX, "number:6"},
    {"segmented.item.md", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"segmented.item.md", kNone, StyleProperty::PaddingX, "number:8"},
};

const char* sizeKey(SegmentedSize s) { return s == SegmentedSize::Sm ? "segmented.item.sm" : "segmented.item.md"; }

}  // namespace

std::span<const theme::StyleRuleEntry> Segmented::styleRows() { return kRows; }

void Segmented::onAttached() {
  setFocusable(true);
  style().hasMeasure = true;
  style().flexShrink = 1.0;
}

Segmented::Layout Segmented::layoutFor(double width) const {
  const theme::Tokens& tokens = ui().services().tokens();
  Layout l;
  l.padding = tokens.widgetMetric("segmented", "padding").value_or(2.0);
  l.gap = tokens.widgetMetric("segmented", "gap").value_or(2.0);
  l.itemH = tokens.widgetMetric("segmented", "itemHeight").value_or(22.0);
  const double n = static_cast<double>(items_.size());
  l.itemW = n > 0.0 ? std::max(0.0, (width - 2.0 * l.padding - (n - 1.0) * l.gap) / n) : 0.0;
  return l;
}

// ---- items and value -----------------------------------------------------------------------------

bool Segmented::setItems(std::vector<SegmentItem> items) {
  if (items.size() > kMaxItems) return false;
  for (const SegmentItem& item : items) {
    if (!item.icon.empty() && !isValidIconName(item.icon)) return false;
  }
  items_ = std::move(items);
  if (selected_ >= static_cast<int>(items_.size())) selected_ = -1;
  hover_ = -1;
  pressedItem_ = -1;
  requestLayout();
  requestPaint();
  return true;
}

bool Segmented::setItemEnabled(int index, bool enabled) {
  if (index < 0 || index >= static_cast<int>(items_.size())) return false;
  items_[static_cast<size_t>(index)].enabled = enabled;
  requestPaint();
  return true;
}

bool Segmented::setSelectedIndex(int index) {
  if (index < -1 || index >= static_cast<int>(items_.size())) return false;
  if (index == selected_) return true;
  selected_ = index;
  requestPaint();
  return true;
}

void Segmented::setSize(SegmentedSize size) {
  if (size == size_) return;
  size_ = size;
  requestLayout();
  requestPaint();
}

int Segmented::itemAt(double localX) const {
  if (items_.empty() || !std::isfinite(localX)) return -1;
  const double width = ui().absRect(id()).w;
  const Layout l = layoutFor(width);
  const double rel = localX - l.padding;
  if (rel < 0.0 || l.itemW <= 0.0) return -1;
  const double stride = l.itemW + l.gap;
  const int index = static_cast<int>(std::floor(rel / stride));
  if (index < 0 || index >= static_cast<int>(items_.size())) return -1;
  return rel - index * stride <= l.itemW ? index : -1;  // the gap between items hits nothing
}

int Segmented::stepEnabled(int from, int dir) const {
  const int n = static_cast<int>(items_.size());
  for (int i = from + dir; i >= 0 && i < n; i += dir) {
    if (items_[static_cast<size_t>(i)].enabled) return i;
  }
  return -1;
}

void Segmented::selectByUser(int index) {
  if (index < 0 || index >= static_cast<int>(items_.size()) || index == selected_) return;
  selected_ = index;
  requestPaint();
  // A copy is called: the handler may replace this callback (setOn...) while it runs, or destroy this
  // control, so nothing is touched afterwards.
  if (onChange_) {
    const auto callback = onChange_;
    callback(index);
  }
}

// ---- measure and paint ---------------------------------------------------------------------------

core::layout::MeasureResult Segmented::measure(const core::layout::MeasureInput& input) {
  const theme::ResolvedStyle& sz = ui().services().resolve(sizeKey(size_), 0);
  const double scale = ui().scale();
  const Layout l = layoutFor(0.0);
  double widest = 0.0;
  for (const SegmentItem& item : items_) {
    double w = 2.0 * sz.paddingX;
    if (!item.icon.empty()) w += kIconSize + (item.text.empty() ? 0.0 : kIconGap);
    if (!item.text.empty()) w += std::ceil(static_cast<double>(ui().text().measure(item.text, static_cast<float>(sz.text.fontSize * scale), 400)) / scale);  // whole px: layout rounds
    widest = std::max(widest, w);
  }
  const double n = static_cast<double>(items_.size());
  double width = 2.0 * l.padding + (n > 0.0 ? n * widest + (n - 1.0) * l.gap : 0.0);
  if (input.widthMode == core::layout::MeasureMode::AtMost) width = std::min(width, input.width);
  return {width, 2.0 * l.padding + l.itemH};
}

float Segmented::paintOpacity() const { return static_cast<float>(ui().services().resolve("segmented.container", styleState()).opacity); }

void Segmented::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& container = ctx.resolve("segmented.container", 0);
  const theme::ResolvedStyle& sz = ctx.resolve(sizeKey(size_), 0);
  ctx.painter().fillRoundedRect(ctx.box(), render::CornerRadii::uniform(ctx.px(container.radius)), ctx.color(container.background));
  const core::layout::Rect& r = ctx.rect();
  const Layout l = layoutFor(r.w);
  for (size_t i = 0; i < items_.size(); ++i) {
    const SegmentItem& item = items_[i];
    const int index = static_cast<int>(i);
    uint8_t bits = kNone;
    if (index == selected_) bits |= kSelected;
    if (item.enabled && (index == hover_ || (index == pressedItem_ && hover_ == index))) bits |= kHover;
    const theme::ResolvedStyle& rs = ctx.resolve("segmented.item", bits);
    const double x = r.x + l.padding + static_cast<double>(index) * (l.itemW + l.gap);
    const render::Rect box = ctx.toPhysical(x, r.y + l.padding, l.itemW, l.itemH);
    const int slot = index * 2;
    const render::Color fill = ctx.animatedColor(slot, ctx.color(rs.background));
    const render::Color fg = ctx.animatedColor(slot + 1, ctx.color(rs.text.color));
    const float opacity = item.enabled ? 1.0f : static_cast<float>(ctx.resolve("segmented.item", kDisabled).opacity);
    if (opacity < 1.0f) ctx.painter().pushOpacity(opacity);
    if (fill.a > 0.0f) ctx.painter().fillRoundedRect(box, render::CornerRadii::uniform(ctx.px(rs.radius)), fill);

    // Content: icon then text, centred; the text is shortened first.
    const float pad = ctx.px(sz.paddingX);
    const float avail = std::max(0.0f, box.w - 2.0f * pad);
    const bool hasIcon = !item.icon.empty();
    const float iconPx = hasIcon ? ctx.px(kIconSize) : 0.0f;
    const float gap = hasIcon && !item.text.empty() ? ctx.px(kIconGap) : 0.0f;
    const float fontPx = static_cast<float>(sz.text.fontSize) * ctx.scale();
    float textW = item.text.empty() ? 0.0f : ui().text().measure(item.text, fontPx, 400);
    if (textW > avail - iconPx - gap + ctx.px(1.0)) textW = std::max(0.0f, avail - iconPx - gap);  // layout rounds widths
    float cx = box.x + pad + std::max(0.0f, (avail - (iconPx + gap + textW)) * 0.5f);
    if (hasIcon) {
      ctx.drawIcon(item.icon, kIconSize, {cx, box.y, iconPx, box.h}, fg);
      cx += iconPx + gap;
    }
    if (textW > 0.0f) {
      TextOptions options;
      options.color = fg;
      ctx.drawText(item.text, sz.text, {cx, box.y, textW, box.h}, options);
    }
    if (opacity < 1.0f) ctx.painter().popOpacity();
  }
}

void Segmented::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.px(ctx.resolve("segmented.container", 0).radius));
}

Cursor Segmented::cursor() const {
  if (!enabled()) return Cursor::Default;
  return hover_ >= 0 && items_[static_cast<size_t>(hover_)].enabled ? Cursor::Pointer : Cursor::Default;
}

std::string_view Segmented::tooltipText() const {
  if (hover_ >= 0 && !items_[static_cast<size_t>(hover_)].tooltip.empty()) return items_[static_cast<size_t>(hover_)].tooltip;
  return WidgetObject::tooltipText();
}

std::string_view Segmented::accessibleName() const {
  if (!WidgetObject::accessibleName().empty()) return WidgetObject::accessibleName();
  return selected_ >= 0 ? std::string_view(items_[static_cast<size_t>(selected_)].text) : std::string_view();
}

uint8_t Segmented::styleState() const {
  uint8_t s = WidgetObject::styleState();
  if (!focusVisible()) s &= static_cast<uint8_t>(~theme::State::kFocus);
  return s;
}

// ---- input ---------------------------------------------------------------------------------------

void Segmented::onPointerMove(Event& e) {
  const int index = itemAt(e.localX);
  if (index == hover_) return;
  hover_ = index;
  requestPaint();
}

void Segmented::onPointerLeave(Event&) {
  if (hover_ < 0) return;
  hover_ = -1;
  requestPaint();
}

void Segmented::onPointerDown(Event& e) {
  if (e.button != core::events::Button::Left) return;
  const int index = itemAt(e.localX);
  pressedItem_ = index >= 0 && items_[static_cast<size_t>(index)].enabled ? index : -1;
  hover_ = index;
  requestPaint();
}

void Segmented::onPointerUp(Event& e) {
  if (e.button != core::events::Button::Left) return;
  // The press stays known until the Click that follows the release is handled.
  requestPaint();
}

void Segmented::onClick(Event& e) {
  if (e.button != core::events::Button::Left) return;
  const int pressed = pressedItem_;
  pressedItem_ = -1;
  const int index = itemAt(e.localX);
  if (!enabled() || pressed < 0 || index != pressed) return;
  e.markHandled();
  selectByUser(index);
}

void Segmented::onKeyDown(Event& e) {
  if (e.modifiers != core::events::Mod::kNone || !enabled() || items_.empty()) return;
  int target = -1;
  switch (e.key) {
    case Key::Left:
    case Key::Up:
      target = selected_ < 0 ? stepEnabled(static_cast<int>(items_.size()), -1) : stepEnabled(selected_, -1);
      break;
    case Key::Right:
    case Key::Down:
      target = selected_ < 0 ? stepEnabled(-1, 1) : stepEnabled(selected_, 1);
      break;
    case Key::Home: target = stepEnabled(-1, 1); break;
    case Key::End: target = stepEnabled(static_cast<int>(items_.size()), -1); break;
    default: return;
  }
  e.markHandled();  // at an edge the key is used without effect (spec 01 rule 11)
  selectByUser(target);
}

void Segmented::onStateChanged(uint16_t previous) {
  if (!hasState(StateFlag::kPressed) && (previous & StateFlag::kPressed) != 0 && !hovered()) pressedItem_ = -1;
}

}  // namespace r1ui::widgets

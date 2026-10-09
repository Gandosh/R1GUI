// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of PickerButton.h.
// Invariants: onActivate may destroy the button (the owner rebuilds its UI); nothing touches the
//   button after the callback returns unless it is still alive.
// Callers: the colour picker and gradient editor.
#include "r1ui/widgets/colorpicker/PickerButton.h"

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

using theme::State::kActive;
using theme::State::kDisabled;
using theme::State::kHover;
using theme::State::kNone;
using theme::State::kSelected;
using theme::StyleProperty;
using theme::StyleRuleEntry;

const StyleRuleEntry kRows[] = {
    {"picker.button.tab", kNone, StyleProperty::Background, "transparent"},
    {"picker.button.tab", kHover, StyleProperty::Background, "color:hover"},
    {"picker.button.tab", kSelected, StyleProperty::Background, "color:hover"},
    {"picker.button.tab", kNone, StyleProperty::Foreground, "color:muted"},
    {"picker.button.tab", kHover, StyleProperty::Foreground, "color:surface"},
    {"picker.button.tab", kSelected, StyleProperty::Foreground, "color:surface"},
    {"picker.button.tab", kNone, StyleProperty::Radius, "radius:panel"},
    {"picker.button.tab", kDisabled, StyleProperty::Opacity, "number:0.5"},

    {"picker.button.field", kNone, StyleProperty::Background, "color:panel-field"},
    {"picker.button.field", kHover, StyleProperty::Background, "color:panel-field-hover"},
    {"picker.button.field", kNone, StyleProperty::Foreground, "color:muted"},
    {"picker.button.field", kHover, StyleProperty::Foreground, "color:surface"},
    {"picker.button.field", kActive, StyleProperty::Foreground, "color:accent"},
    {"picker.button.field", kNone, StyleProperty::Radius, "radius:panel"},
    {"picker.button.field", kDisabled, StyleProperty::Opacity, "number:0.5"},

    {"picker.button.plain", kNone, StyleProperty::Background, "transparent"},
    {"picker.button.plain", kHover, StyleProperty::Background, "color:hover"},
    {"picker.button.plain", kNone, StyleProperty::Foreground, "color:muted"},
    {"picker.button.plain", kHover, StyleProperty::Foreground, "color:surface"},
    {"picker.button.plain", kNone, StyleProperty::Radius, "radius:panel"},
    {"picker.button.plain", kDisabled, StyleProperty::Opacity, "number:0.5"},
};

}  // namespace

std::span<const StyleRuleEntry> PickerButton::styleRows() { return kRows; }

PickerButton::PickerButton(std::string icon, Look look, double sizeLogical, double iconLogical)
    : icon_(std::move(icon)), look_(look), size_(sizeLogical), iconSize_(iconLogical) {}

const char* PickerButton::rowKey() const {
  switch (look_) {
    case Look::Tab: return "picker.button.tab";
    case Look::Field: return "picker.button.field";
    case Look::Plain: return "picker.button.plain";
  }
  return "picker.button.tab";
}

void PickerButton::onAttached() {
  core::layout::Style& s = style();
  s.width = core::layout::Length::px(size_);
  s.height = core::layout::Length::px(size_);
  s.flexShrink = 0.0;
  setFocusable(true);
}

float PickerButton::paintOpacity() const { return static_cast<float>(ui().services().resolve(rowKey(), styleState()).opacity); }

std::string_view PickerButton::accessibleName() const {
  return WidgetObject::accessibleName().empty() ? std::string_view(icon_) : WidgetObject::accessibleName();
}

void PickerButton::setIcon(std::string icon) {
  if (icon == icon_) return;
  icon_ = std::move(icon);
  requestPaint();
}

void PickerButton::setTooltipAndName(std::string text) {
  setAccessibleName(text);
  setTooltip(std::move(text));
}

void PickerButton::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.style(rowKey());
  const render::Rect box = ctx.box();
  const render::Color bg = ctx.animatedColor(0, ctx.color(rs.background));
  if (bg.a > 0.0f) ctx.painter().fillRoundedRect(box, render::CornerRadii::uniform(ctx.px(rs.radius)), bg);
  const render::Color fg = ctx.animatedColor(1, ctx.color(rs.text.color));
  if (!icon_.empty()) ctx.drawIcon(icon_, iconSize_, box, fg);
}

void PickerButton::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.box(), ctx.px(ctx.style(rowKey()).radius));
}

void PickerButton::activate() {
  const core::tree::WidgetId self = id();
  if (onActivate) onActivate();
  if (ui().alive(self)) requestPaint();
}

void PickerButton::onClick(Event& e) {
  if (e.button != core::events::Button::Left) return;
  e.markHandled();
  activate();
}

void PickerButton::onKeyDown(Event& e) {
  if (e.key != core::events::Key::Enter && e.key != core::events::Key::Space) return;
  if (e.repeat) {
    e.markHandled();
    return;
  }
  e.markHandled();
  activate();
}

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of ActionButton.h.
// Invariants: keyPressed_ is set only by a Space / Enter key-down on this button and cleared by the
//   key-up, a focus loss or a disable, so a key-up that began elsewhere never activates it.
// Callers: UiContext (events, paint).
#include "r1ui/widgets/section/ActionButton.h"

#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

using core::events::Button;
using core::events::Key;

void ActionButton::onAttached() {
  setFocusable(true);
  style().flexShrink = 0.0;
}

void ActionButton::setIcon(std::string icon) {
  if (icon == icon_) return;
  icon_ = std::move(icon);
  requestPaint();
}

void ActionButton::setSize(double width, double height) {
  if (!std::isfinite(width) || !std::isfinite(height) || width < 0.0 || height < 0.0) return;
  style().width = core::layout::Length::px(width);
  style().height = core::layout::Length::px(height);
  requestLayout();
}

std::string_view ActionButton::accessibleName() const {
  return WidgetObject::accessibleName().empty() ? std::string_view(tooltipText()) : WidgetObject::accessibleName();
}

// Focus is shown through the border row only for keyboard focus (focus rule 5, spec 01).
uint8_t ActionButton::styleState() const {
  uint8_t s = WidgetObject::styleState();
  if (!focusVisible()) s &= static_cast<uint8_t>(~theme::State::kFocus);
  if (keyPressed_) s |= theme::State::kActive;
  return s;
}

float ActionButton::paintOpacity() const { return static_cast<float>(ui().services().resolve(styleKey_, styleState()).opacity); }

void ActionButton::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.style(styleKey_);
  const render::Color bg = ctx.animatedColor(0, ctx.color(rs.background));
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(rs.radius));
  const render::Rect box = ctx.box();
  if (bg.a > 0.0f) ctx.painter().fillRoundedRect(box, radii, bg);
  if (rs.border.width > 0.0 && rs.border.color.a > 0) ctx.painter().border(box, radii, ctx.px(rs.border.width), ctx.color(rs.border.color));
  const render::Color tint = ctx.animatedColor(1, ctx.color(rs.text.color));
  if (!icon_.empty()) ctx.drawIcon(icon_, iconSize_, box, tint);
}

void ActionButton::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.px(ctx.style(styleKey_).radius));
}

bool ActionButton::activate() {
  if (!enabled() || !onActivate_) return false;
  auto callback = onActivate_;  // the callback may replace its own std::function or destroy this button
  callback(*this);
  return true;
}

void ActionButton::onClick(Event& e) {
  if (e.button != Button::Left) return;
  e.markHandled();
  activate();
}

void ActionButton::onKeyDown(Event& e) {
  if (e.key != Key::Space && e.key != Key::Enter) return;
  if (e.modifiers & (core::events::Mod::kCtrl | core::events::Mod::kAlt | core::events::Mod::kMeta)) return;
  e.markHandled();
  if (e.repeat || keyPressed_) return;
  keyPressed_ = true;
  requestPaint();
}

void ActionButton::onKeyUp(Event& e) {
  if (e.key != Key::Space && e.key != Key::Enter) return;
  if (!keyPressed_) return;
  keyPressed_ = false;
  requestPaint();
  e.markHandled();
  activate();
}

void ActionButton::onStateChanged(uint16_t) {
  if (enabled() || !keyPressed_) return;
  keyPressed_ = false;
}

void ActionButton::onFocusOut(Event&) {
  if (!keyPressed_) return;
  keyPressed_ = false;
  requestPaint();
}

}  // namespace r1ui::widgets

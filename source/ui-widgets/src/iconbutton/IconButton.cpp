// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of IconButton.h: style rows and painting.
// Invariants: the node's width and height always equal the box size (fixed style, no measure).
// Callers: UiContext (rows registered through create<T>), tests.
#include "r1ui/widgets/iconbutton/IconButton.h"

#include <cmath>

#include "r1ui/widgets/button/Button.h"  // isValidIconName
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

using theme::State::kDisabled;
using theme::State::kHover;
using theme::State::kNone;
using theme::State::kSelected;
using theme::StyleProperty;

constexpr theme::StyleRuleEntry kRows[] = {
    {"iconbtn", kNone, StyleProperty::Background, "transparent"},
    {"iconbtn", kNone, StyleProperty::Foreground, "color:muted"},
    {"iconbtn", kNone, StyleProperty::BorderColor, "transparent"},
    {"iconbtn", kNone, StyleProperty::BorderWidth, "number:1"},
    {"iconbtn", kNone, StyleProperty::Radius, "metric:iconButton.md.radius"},
    {"iconbtn", kHover, StyleProperty::Background, "color:hover"},
    {"iconbtn", kHover, StyleProperty::Foreground, "color:surface"},
    {"iconbtn", kSelected, StyleProperty::BorderColor, "color:accent"},
    {"iconbtn", kSelected, StyleProperty::Foreground, "color:accent"},
    {"iconbtn", kDisabled, StyleProperty::Opacity, "number:0.5"},
    {"iconbtn.sm", kNone, StyleProperty::Height, "metric:iconButton.sm.size"},
    {"iconbtn.md", kNone, StyleProperty::Height, "metric:iconButton.md.size"},
};

}  // namespace

IconButton::IconButton(std::string icon, IconButtonSize size) : icon_(isValidIconName(icon) ? std::move(icon) : std::string()), sizeClass_(size) {}

std::span<const theme::StyleRuleEntry> IconButton::styleRows() { return kRows; }

void IconButton::onAttached() {
  Pressable::onAttached();
  hoverFill_ = sizeClass_ == IconButtonSize::Md;
  box_ = ui().services().resolve(sizeClass_ == IconButtonSize::Sm ? "iconbtn.sm" : "iconbtn.md", 0).height;
  applySize();
}

void IconButton::applySize() {
  style().width = core::layout::Length::px(box_);
  style().height = core::layout::Length::px(box_);
  style().flexShrink = 0.0;
  requestLayout();
  requestPaint();
}

bool IconButton::setIcon(std::string name) {
  if (!name.empty() && !isValidIconName(name)) return false;
  if (name == icon_) return true;
  icon_ = std::move(name);
  requestPaint();
  return true;
}

void IconButton::setSizeClass(IconButtonSize size) {
  sizeClass_ = size;
  hoverFill_ = size == IconButtonSize::Md;
  box_ = ui().services().resolve(size == IconButtonSize::Sm ? "iconbtn.sm" : "iconbtn.md", 0).height;
  applySize();
}

bool IconButton::setBoxSize(double size) {
  if (!std::isfinite(size) || size < 8.0 || size > 128.0) return false;
  box_ = size;
  applySize();
  return true;
}

bool IconButton::setIconSize(double size) {
  if (!std::isfinite(size) || size < 4.0 || size > 64.0) return false;
  iconSize_ = size;
  requestPaint();
  return true;
}

bool IconButton::setRadius(double radius) {
  if (!std::isfinite(radius) || radius < 0.0) return false;
  radius_ = radius;
  requestPaint();
  return true;
}

void IconButton::setHoverFill(bool fill) {
  hoverFill_ = fill;
  requestPaint();
}

bool IconButton::click() {
  if (!enabled()) return false;
  activate();
  return true;
}

void IconButton::activate() {
  // A copy is called: the handler may replace this callback (setOn...) while it runs.
  if (onClick_) {
    const auto callback = onClick_;
    callback();
  }
}

float IconButton::paintOpacity() const { return static_cast<float>(ui().services().resolve("iconbtn", styleState()).opacity); }

void IconButton::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.style("iconbtn");
  const render::Rect box = ctx.box();
  // Sm: the hover look changes the glyph only (measured); drop the hover bit for the fill.
  render::Color fill = ctx.color(rs.background);
  if (!hoverFill_) fill = ctx.color(ctx.resolve("iconbtn", static_cast<uint8_t>(styleState() & ~kHover)).background);
  fill = ctx.animatedColor(0, fill);
  const render::Color fg = ctx.animatedColor(1, ctx.color(rs.text.color));
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(radius_ >= 0.0 ? radius_ : rs.radius));
  if (fill.a > 0.0f) ctx.painter().fillRoundedRect(box, radii, fill);
  if (active()) ctx.painter().border(box, radii, ctx.px(rs.border.width), ctx.color(rs.border.color));
  if (!icon_.empty()) ctx.drawIcon(icon_, iconSize_, box, fg);
}

void IconButton::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.px(radius_ >= 0.0 ? radius_ : ctx.style("iconbtn").radius));
}

std::string_view IconButton::accessibleName() const {
  return WidgetObject::accessibleName().empty() ? std::string_view(icon_) : WidgetObject::accessibleName();
}

}  // namespace r1ui::widgets

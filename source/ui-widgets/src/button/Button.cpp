// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Button.h: style rows, content measurement and painting.
// Invariants: measure() and paint() use the same font size, weight and icon size, so a button that
//   gets its natural width never truncates its text.
// Callers: UiContext (rows registered through create<T>, measure / paint dispatch), tests.
#include "r1ui/widgets/button/Button.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

using theme::State::kDisabled;
using theme::State::kFocus;
using theme::State::kHover;
using theme::State::kNone;
using theme::StyleProperty;

constexpr double kContentGap = 6.0;

// Tone rows (colours) and size rows (height, side padding, font). Hover rows come after the base
// values; the style sheet ignores them while disabled.
constexpr theme::StyleRuleEntry kRows[] = {
    {"btn.ghost", kNone, StyleProperty::Background, "transparent"},
    {"btn.ghost", kNone, StyleProperty::Foreground, "color:muted"},
    {"btn.ghost", kNone, StyleProperty::Radius, "radius:sm"},
    {"btn.ghost", kNone, StyleProperty::FontWeight, "weight:normal"},
    {"btn.ghost", kHover, StyleProperty::Background, "color:hover"},
    {"btn.ghost", kHover, StyleProperty::Foreground, "color:surface"},
    {"btn.ghost", kDisabled, StyleProperty::Opacity, "number:0.5"},

    {"btn.accent", kNone, StyleProperty::Background, "color:accent"},
    {"btn.accent", kNone, StyleProperty::Foreground, "#ffffff"},
    {"btn.accent", kNone, StyleProperty::Radius, "radius:sm"},
    {"btn.accent", kNone, StyleProperty::FontWeight, "weight:medium"},
    {"btn.accent", kHover, StyleProperty::Background, "color:accent@0.9"},
    {"btn.accent", kDisabled, StyleProperty::Opacity, "number:0.5"},

    {"btn.panel", kNone, StyleProperty::Background, "color:panel@0.7"},
    {"btn.panel", kNone, StyleProperty::Foreground, "color:surface"},
    {"btn.panel", kNone, StyleProperty::BorderColor, "#ffffff1a"},
    {"btn.panel", kNone, StyleProperty::BorderWidth, "number:1"},
    {"btn.panel", kNone, StyleProperty::Radius, "radius:md"},
    {"btn.panel", kNone, StyleProperty::FontWeight, "weight:medium"},
    {"btn.panel", kHover, StyleProperty::Background, "color:hover"},
    {"btn.panel", kDisabled, StyleProperty::Opacity, "number:0.5"},

    {"btn.panelAccent", kNone, StyleProperty::Background, "color:panel@0.7"},
    {"btn.panelAccent", kNone, StyleProperty::Foreground, "color:accent"},
    {"btn.panelAccent", kNone, StyleProperty::BorderColor, "color:accent@0.2"},
    {"btn.panelAccent", kNone, StyleProperty::BorderWidth, "number:1"},
    {"btn.panelAccent", kNone, StyleProperty::Radius, "radius:md"},
    {"btn.panelAccent", kNone, StyleProperty::FontWeight, "weight:medium"},
    {"btn.panelAccent", kHover, StyleProperty::Background, "color:accent@0.1"},
    {"btn.panelAccent", kDisabled, StyleProperty::Opacity, "number:0.5"},

    {"btn.neutral", kNone, StyleProperty::Background, "color:hover"},
    {"btn.neutral", kNone, StyleProperty::Foreground, "color:surface"},
    {"btn.neutral", kNone, StyleProperty::Radius, "radius:sm"},
    {"btn.neutral", kNone, StyleProperty::FontWeight, "weight:normal"},
    {"btn.neutral", kHover, StyleProperty::Background, "color:border"},
    {"btn.neutral", kDisabled, StyleProperty::Opacity, "number:0.5"},

    {"btn.size.sm", kNone, StyleProperty::Height, "metric:button.sm.height"},
    {"btn.size.sm", kNone, StyleProperty::PaddingX, "number:12"},
    {"btn.size.sm", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"btn.size.md", kNone, StyleProperty::Height, "metric:button.md.height"},
    {"btn.size.md", kNone, StyleProperty::PaddingX, "number:12"},
    {"btn.size.md", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"btn.size.icon", kNone, StyleProperty::Height, "metric:button.icon"},
    {"btn.size.icon", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"btn.size.iconSm", kNone, StyleProperty::Height, "metric:button.iconSm"},
    {"btn.size.iconSm", kNone, StyleProperty::FontSize, "fontSize:xs"},
};

bool iconOnly(ButtonSize s) { return s == ButtonSize::Icon || s == ButtonSize::IconSm; }

}  // namespace

bool isValidIconName(std::string_view name) {
  if (name.empty() || name.size() > 64) return false;
  return std::all_of(name.begin(), name.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
  });
}

std::span<const theme::StyleRuleEntry> Button::styleRows() { return kRows; }

const char* Button::toneKey(ButtonTone tone) {
  switch (tone) {
    case ButtonTone::Ghost: return "btn.ghost";
    case ButtonTone::Accent: return "btn.accent";
    case ButtonTone::Panel: return "btn.panel";
    case ButtonTone::PanelAccent: return "btn.panelAccent";
    case ButtonTone::Neutral: return "btn.neutral";
  }
  return "btn.ghost";
}

const char* Button::sizeKey(ButtonSize size) {
  switch (size) {
    case ButtonSize::Sm: return "btn.size.sm";
    case ButtonSize::Md: return "btn.size.md";
    case ButtonSize::Icon: return "btn.size.icon";
    case ButtonSize::IconSm: return "btn.size.iconSm";
  }
  return "btn.size.sm";
}

void Button::onAttached() {
  Pressable::onAttached();
  style().hasMeasure = true;
  style().flexShrink = 0.0;
}

double Button::sizeHeight() const { return ui().services().resolve(sizeKey(size_), 0).height; }
double Button::iconSize() const {
  if (iconSize_ > 0.0) return iconSize_;
  return size_ == ButtonSize::Icon ? 16.0 : 14.0;
}
double Button::paddingX() const { return paddingX_ >= 0.0 ? paddingX_ : ui().services().resolve(sizeKey(size_), 0).paddingX; }
double Button::radius() const { return radius_ >= 0.0 ? radius_ : ui().services().resolve(toneKey(tone_), 0).radius; }

// ---- setters (each requests the layout or paint it needs) ---------------------------------------

void Button::setText(std::string text) {
  if (text == text_) return;
  text_ = std::move(text);
  requestLayout();
  requestPaint();
}

bool Button::setIcon(std::string name) {
  if (!name.empty() && !isValidIconName(name)) return false;
  if (name == icon_) return true;
  icon_ = std::move(name);
  requestLayout();
  requestPaint();
  return true;
}

bool Button::setTrailingIcon(std::string name) {
  if (!name.empty() && !isValidIconName(name)) return false;
  if (name == trailingIcon_) return true;
  trailingIcon_ = std::move(name);
  requestLayout();
  requestPaint();
  return true;
}

bool Button::setIconSize(double size) {
  if (!std::isfinite(size) || size < 4.0 || size > 64.0) return false;
  iconSize_ = size;
  requestLayout();
  requestPaint();
  return true;
}

void Button::setTone(ButtonTone tone) {
  if (tone == tone_) return;
  tone_ = tone;
  requestLayout();
  requestPaint();
}

void Button::setSize(ButtonSize size) {
  if (size == size_) return;
  size_ = size;
  requestLayout();
  requestPaint();
}

bool Button::setRadius(double radius) {
  if (!std::isfinite(radius) || radius < 0.0) return false;
  radius_ = radius;
  requestPaint();
  return true;
}

bool Button::setPaddingX(double padding) {
  if (!std::isfinite(padding) || padding < 0.0 || padding > 1000.0) return false;
  paddingX_ = padding;
  requestLayout();
  requestPaint();
  return true;
}

void Button::setWeight(int weight) {
  weight_ = std::clamp(weight, 100, 900);
  requestLayout();
  requestPaint();
}

bool Button::click() {
  if (!enabled()) return false;
  activate();
  return true;
}

void Button::activate() {
  if (onClick_) onClick_();
}

// ---- measure and paint ---------------------------------------------------------------------------

core::layout::MeasureResult Button::measure(const core::layout::MeasureInput& input) {
  const double height = sizeHeight();
  if (iconOnly(size_)) return {height, height};
  const theme::ResolvedStyle& tone = ui().services().resolve(toneKey(tone_), 0);
  const theme::ResolvedStyle& sz = ui().services().resolve(sizeKey(size_), 0);
  const double scale = ui().scale();
  const int weight = weight_ >= 0 ? weight_ : tone.text.weight;
  double content = 0.0;
  int parts = 0;
  if (!icon_.empty()) { content += iconSize(); ++parts; }
  if (!text_.empty()) {
    content += std::ceil(static_cast<double>(ui().text().measure(text_, static_cast<float>(sz.text.fontSize * scale), weight)) / scale);  // whole px: layout rounds
    ++parts;
  }
  if (!trailingIcon_.empty()) { content += iconSize(); ++parts; }
  if (parts > 1) content += kContentGap * (parts - 1);
  double width = content + 2.0 * paddingX();
  if (input.widthMode == core::layout::MeasureMode::AtMost) width = std::min(width, input.width);
  return {width, height};
}

float Button::paintOpacity() const { return static_cast<float>(ui().services().resolve(toneKey(tone_), styleState()).opacity); }

void Button::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& tone = ctx.style(toneKey(tone_));
  const theme::ResolvedStyle& sz = ctx.style(sizeKey(size_));
  const render::Rect box = ctx.box();
  const bool animate = tone_ != ButtonTone::Neutral;
  render::Color fill = ctx.color(tone.background);
  render::Color fg = ctx.color(tone.text.color);
  if (animate) {
    fill = ctx.animatedColor(0, fill);
    fg = ctx.animatedColor(1, fg);
  }
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(radius()));
  if (fill.a > 0.0f) ctx.painter().fillRoundedRect(box, radii, fill);
  if (tone.border.width > 0.0 && tone.border.color.a > 0) ctx.painter().border(box, radii, ctx.px(tone.border.width), ctx.color(tone.border.color));

  // Content row: [icon] [text] [trailing icon], centred; the text gives way first.
  const float iconPx = ctx.px(iconSize());
  const float gap = ctx.px(kContentGap);
  const float padX = iconOnly(size_) ? 0.0f : ctx.px(paddingX());
  const float avail = std::max(0.0f, box.w - 2.0f * padX);
  const bool hasIcon = !icon_.empty();
  const bool hasTrail = !trailingIcon_.empty();
  const bool hasText = !text_.empty() && !iconOnly(size_);
  const int weight = weight_ >= 0 ? weight_ : tone.text.weight;
  const float fontPx = static_cast<float>(sz.text.fontSize) * ctx.scale();
  float textW = hasText ? ui().text().measure(text_, fontPx, weight) : 0.0f;
  const int parts = (hasIcon ? 1 : 0) + (hasText ? 1 : 0) + (hasTrail ? 1 : 0);
  const float fixedW = (hasIcon ? iconPx : 0.0f) + (hasTrail ? iconPx : 0.0f) + (parts > 1 ? gap * static_cast<float>(parts - 1) : 0.0f);
  // Layout rounds the width to whole pixels: up to one logical pixel of overflow is not truncated.
  if (textW > avail - fixedW + ctx.px(1.0)) textW = std::max(0.0f, avail - fixedW);
  const float total = fixedW + textW;
  float x = box.x + padX + std::max(0.0f, (avail - total) * 0.5f);
  if (hasIcon) {
    ctx.drawIcon(icon_, iconSize(), {x, box.y, iconPx, box.h}, fg);
    x += iconPx + gap;
  }
  if (hasText) {
    TextOptions options;
    options.color = fg;
    options.weight = weight;
    ctx.drawText(text_, sz.text, {x, box.y, textW, box.h}, options);
    x += textW + gap;
  }
  if (hasTrail) ctx.drawIcon(trailingIcon_, iconSize(), {x, box.y, iconPx, box.h}, fg);
}

void Button::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.px(radius()));
}

std::string_view Button::accessibleName() const {
  if (!WidgetObject::accessibleName().empty()) return WidgetObject::accessibleName();
  return text_.empty() ? std::string_view(icon_) : std::string_view(text_);
}

}  // namespace r1ui::widgets

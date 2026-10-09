// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of PropControls.h: the props.* style rows, painting and input of PropResetButton,
//   PropSlider and ColorChip.
// Why: see PropControls.h. Geometry constants carry the numbers they come from: the slider and chip
//   share the 26 px field height of the reference panel, the reset glyph is 12 px in a 16 px box so it
//   fits the caption line (11 px text, 16 px line) without making the row taller.
// Invariants: no colour is hard-coded (rows and tokens only) except the white slider thumb, which the
//   switch thumb also uses; hidden reset buttons are hit-test transparent and not focusable.
// Callers: PropertyRowView, tests.
#include "r1ui/widgets/props/PropControls.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

using core::events::Button;
using core::events::FocusReason;
using core::events::Key;
namespace Mod = core::events::Mod;
using theme::State::kDisabled;
using theme::State::kHover;
using theme::State::kMixed;
using theme::State::kNone;
using theme::StyleProperty;

constexpr double kResetGlyph = 12.0;
constexpr double kThumb = 12.0;       // slider thumb diameter (same as the small switch thumb)
constexpr double kTrackHeight = 4.0;
constexpr double kChipInset = 3.0;    // the 26 px chip draws a 20 px swatch, as the reference paint field does

constexpr theme::StyleRuleEntry kRows[] = {
    {"props.reset", kNone, StyleProperty::Background, "transparent"},
    {"props.reset", kNone, StyleProperty::Foreground, "color:muted"},
    {"props.reset", kNone, StyleProperty::Radius, "metric:iconButton.md.radius"},
    {"props.reset", kHover, StyleProperty::Background, "color:hover"},
    {"props.reset", kHover, StyleProperty::Foreground, "color:surface"},

    {"props.slider.track", kNone, StyleProperty::Background, "color:border-strong"},
    {"props.slider.track", kDisabled, StyleProperty::Opacity, "number:0.5"},
    {"props.slider.fill", kNone, StyleProperty::Background, "color:accent"},
    {"props.slider.fill", kMixed, StyleProperty::Background, "color:accent@0.3"},
    {"props.slider.thumb", kNone, StyleProperty::Background, "#ffffff"},
    {"props.slider.thumb", kMixed, StyleProperty::Background, "color:accent"},

    {"props.chip", kNone, StyleProperty::BorderColor, "color:border"},
    {"props.chip", kNone, StyleProperty::Background, "color:panel-field"},
    {"props.chip", kNone, StyleProperty::Foreground, "color:muted"},
    {"props.chip", kDisabled, StyleProperty::Opacity, "number:0.5"},
};

render::Color toRender(const color::Rgba& c) {
  const color::Rgba s = color::sanitized(c);
  return render::Color::fromRgba8(static_cast<uint8_t>(color::to8(s.rgb.r)), static_cast<uint8_t>(color::to8(s.rgb.g)), static_cast<uint8_t>(color::to8(s.rgb.b)),
                                  static_cast<uint8_t>(color::to8(s.a)));
}

}  // namespace

// ---- PropResetButton -----------------------------------------------------------------------------------

std::span<const theme::StyleRuleEntry> PropResetButton::styleRows() { return kRows; }

void PropResetButton::onAttached() {
  Pressable::onAttached();
  core::layout::Style& s = style();
  s.width = core::layout::Length::px(kSize);
  s.height = core::layout::Length::px(kSize);
  s.flexShrink = 0.0;
  setFocusable(false);
  node().flags.hitTestTransparent = true;
}

void PropResetButton::setShown(bool shown) {
  if (shown == shown_) return;
  shown_ = shown;
  node().flags.hitTestTransparent = !shown;
  setFocusable(shown);
  requestPaint();
}

void PropResetButton::setReset(std::string tooltip, std::function<void()> onReset) {
  tooltip_ = std::move(tooltip);
  onReset_ = std::move(onReset);
}

void PropResetButton::paint(PaintContext& ctx) {
  if (!shown_) return;
  const theme::ResolvedStyle& rs = ctx.style("props.reset");
  const render::Rect box = ctx.box();
  const render::Color fill = ctx.color(rs.background);
  if (fill.a > 0.0f) ctx.painter().fillRoundedRect(box, render::CornerRadii::uniform(ctx.px(rs.radius)), fill);
  ctx.drawIcon("undo2", kResetGlyph, box, ctx.color(rs.text.color));
}

void PropResetButton::paintOver(PaintContext& ctx) {
  if (shown_ && focusVisible()) ctx.focusRing(ctx.px(ctx.style("props.reset").radius));
}

void PropResetButton::activate() {
  if (shown_ && onReset_) onReset_();
}

// ---- PropSlider ---------------------------------------------------------------------------------------

std::span<const theme::StyleRuleEntry> PropSlider::styleRows() { return kRows; }

void PropSlider::onAttached() {
  core::layout::Style& s = style();
  s.height = core::layout::Length::px(kHeight);
  s.flexGrow = 1.0;
  s.flexShrink = 1.0;
  s.flexBasis = core::layout::Length::px(0.0);
  s.minWidth = core::layout::Length::px(0.0);
  setFocusable(true);
}

void PropSlider::setRange(double min, double max) {
  if (!std::isfinite(min) || !std::isfinite(max) || min >= max) {
    min = 0.0;
    max = 1.0;
  }
  min_ = min;
  max_ = max;
  value_ = std::clamp(value_, min_, max_);
  requestPaint();
}

void PropSlider::setValue(double value) {
  if (!std::isfinite(value)) return;
  value = std::clamp(value, min_, max_);
  if (value == value_) return;
  value_ = value;
  requestPaint();
}

double PropSlider::fraction() const { return (value_ - min_) / (max_ - min_); }

double PropSlider::valueAt(double x) const {
  const core::layout::Rect r = ui().absRect(id());
  const double span = std::max(1.0, r.w - kThumb);
  const double t = std::clamp((x - (r.x + kThumb * 0.5)) / span, 0.0, 1.0);
  return min_ + t * (max_ - min_);
}

void PropSlider::paint(PaintContext& ctx) {
  const core::layout::Rect& r = ctx.rect();
  const double trackY = r.y + (r.h - kTrackHeight) * 0.5;
  const double left = r.x + kThumb * 0.5;
  const double width = std::max(0.0, r.w - kThumb);
  const render::CornerRadii pill = render::CornerRadii::uniform(ctx.px(kTrackHeight * 0.5));
  ctx.painter().fillRoundedRect(ctx.toPhysical(left, trackY, width, kTrackHeight), pill, ctx.color(ctx.style("props.slider.track").background));
  const double t = std::clamp(fraction(), 0.0, 1.0);
  if (t > 0.0 && !hasState(StateFlag::kMixed)) {
    ctx.painter().fillRoundedRect(ctx.toPhysical(left, trackY, width * t, kTrackHeight), pill, ctx.color(ctx.style("props.slider.fill").background));
  }
  const double cx = left + width * (hasState(StateFlag::kMixed) ? 0.5 : t);
  const render::Rect thumb = ctx.toPhysical(cx - kThumb * 0.5, r.y + (r.h - kThumb) * 0.5, kThumb, kThumb);
  const render::CornerRadii round = render::CornerRadii::uniform(thumb.w * 0.5f);
  if (const auto layers = ctx.ui().services().tokens().shadow("sm")) {
    for (auto it = layers->rbegin(); it != layers->rend(); ++it) {
      render::ShadowSpec spec;
      spec.offsetX = ctx.px(it->offsetX);
      spec.offsetY = ctx.px(it->offsetY);
      spec.blur = ctx.px(it->blur);
      spec.spread = ctx.px(it->spread);
      spec.color = ctx.color(it->color);
      ctx.painter().shadow(thumb, round, spec);
    }
  }
  ctx.painter().fillRoundedRect(thumb, round, ctx.color(ctx.style("props.slider.thumb").background));
}

void PropSlider::paintOver(PaintContext& ctx) {
  if (!focusVisible()) return;
  ctx.focusRing(ctx.toPhysical(ctx.rect().x, ctx.rect().y + 2.0, ctx.rect().w, ctx.rect().h - 4.0), ctx.px(4.0));
}

void PropSlider::onPointerDown(Event& e) {
  if (e.button != Button::Left || !enabled()) return;
  e.markHandled();
  ui().router().focus(id(), FocusReason::Pointer);
  if (!ui().alive(id())) return;
  ui().router().capturePointer(id());
  dragging_ = true;
  startValue_ = value_;
  if (onBegin_) onBegin_();
  if (!ui().alive(id())) return;
  onPointerMove(e);
}

void PropSlider::onPointerMove(Event& e) {
  if (!dragging_) return;
  const double next = valueAt(e.x);
  if (next == value_) return;
  value_ = next;
  requestPaint();
  if (onChanged_) onChanged_(next, true);
}

void PropSlider::onPointerUp(Event& e) {
  if (e.button == Button::Left && dragging_) finish(false);
}

void PropSlider::onCaptureLost(Event&) {
  if (dragging_) finish(true);
}

void PropSlider::finish(bool cancelled) {
  dragging_ = false;
  if (cancelled && value_ != startValue_) {
    value_ = startValue_;
    requestPaint();
    if (onChanged_) onChanged_(startValue_, false);
    if (!ui().alive(id())) return;
  }
  if (onEnd_) onEnd_(cancelled, value_ != startValue_);
}

void PropSlider::onKeyDown(Event& e) {
  if (e.key == Key::Escape && dragging_) {
    e.markHandled();
    finish(true);
    if (ui().alive(id())) ui().router().cancelPointerInteraction();
    return;
  }
  if (dragging_ || !enabled()) return;
  double next = value_;
  double step = (max_ - min_) / 100.0;
  if ((e.modifiers & Mod::kCtrl) != 0) step *= 0.1;
  else if ((e.modifiers & Mod::kShift) != 0) step *= 10.0;
  switch (e.key) {
    case Key::Left:
    case Key::Down: next = value_ - step; break;
    case Key::Right:
    case Key::Up: next = value_ + step; break;
    case Key::Home: next = min_; break;
    case Key::End: next = max_; break;
    default: return;
  }
  e.markHandled();
  next = std::clamp(next, min_, max_);
  if (onBegin_) onBegin_();
  if (!ui().alive(id())) return;
  const bool changed = next != value_;
  if (changed) {
    value_ = next;
    requestPaint();
    if (onChanged_) onChanged_(next, false);
    if (!ui().alive(id())) return;
  }
  if (onEnd_) onEnd_(false, changed);
}

// ---- ColorChip ----------------------------------------------------------------------------------------

std::span<const theme::StyleRuleEntry> ColorChip::styleRows() { return kRows; }

void ColorChip::onAttached() {
  Pressable::onAttached();
  style().width = core::layout::Length::px(kSize);
  style().height = core::layout::Length::px(kSize);
  style().flexShrink = 0.0;
}

void ColorChip::setColor(const color::Rgba& color) {
  color_ = color::sanitized(color);
  requestPaint();
}

float ColorChip::paintOpacity() const { return static_cast<float>(ui().services().resolve("props.chip", styleState()).opacity); }

void ColorChip::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.style("props.chip");
  const render::Rect box = ctx.box();
  const render::Rect chip{box.x + ctx.px(kChipInset), box.y + ctx.px(kChipInset), box.w - ctx.px(2 * kChipInset), box.h - ctx.px(2 * kChipInset)};
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(3));
  if (hasState(StateFlag::kMixed)) {
    // Several different colours: an empty field with a minus, like the indeterminate checkbox.
    ctx.painter().fillRoundedRect(chip, radii, ctx.color(rs.background));
    ctx.drawIcon("minus", 12, chip, ctx.color(rs.text.color));
  } else {
    if (color_.a < 1.0) {
      // A translucent colour is shown over a two-tone checkerboard so its alpha can be read.
      ctx.painter().fillRoundedRect(chip, radii, ctx.color("checkerboard"));
      const float cell = chip.w / 4.0f;
      for (int cy = 0; cy < 4; ++cy) {
        for (int cx = 0; cx < 4; ++cx) {
          if ((cx + cy) % 2 == 0) continue;
          ctx.painter().fillRect({chip.x + cell * static_cast<float>(cx), chip.y + cell * static_cast<float>(cy), cell, cell}, ctx.color("checkerboard-muted"));
        }
      }
    }
    ctx.painter().fillRoundedRect(chip, radii, toRender(color_));
  }
  ctx.painter().border(chip, radii, ctx.hairline(), ctx.color(rs.border.color));
}

void ColorChip::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.px(4));
}

void ColorChip::activate() {
  if (onActivate_) onActivate_();
}

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Checkbox.h: the label style row, measurement and painting of the box.
// Invariants: the box is kBoxSize logical px, vertically centred in the widget; the widget is at
//   least as tall as the box and the label line.
// Callers: UiContext (rows registered through create<T>), tests.
#include "r1ui/widgets/checkbox/Checkbox.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

using theme::State::kDisabled;
using theme::State::kNone;
using theme::StyleProperty;

constexpr double kLabelGap = 8.0;

constexpr theme::StyleRuleEntry kRows[] = {
    {"checkbox.label", kNone, StyleProperty::Foreground, "color:surface"},
    {"checkbox.label", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"checkbox.label", kNone, StyleProperty::LineHeight, "number:16"},
    {"checkbox.label", kDisabled, StyleProperty::Opacity, "number:0.5"},
};

// The browser's native checkbox colours as measured (see Checkbox.h).
struct NativeColors {
  render::Color fill, border, borderHover, mark, checkedHover;
};
NativeColors nativeColors(bool dark) {
  using render::Color;
  if (dark) return {Color::fromHex(0x3b3b3bff), Color::fromHex(0x7f7f7fff), Color::fromHex(0xa4a4a4ff), Color::fromHex(0x3b3b3bff), Color::fromHex(0x71a4f8ff)};
  return {Color::fromHex(0xffffffff), Color::fromHex(0x808080ff), Color::fromHex(0x5c5c5cff), Color::fromHex(0xffffffff), Color::fromHex(0x124ac6ff)};
}

}  // namespace

std::span<const theme::StyleRuleEntry> Checkbox::styleRows() { return kRows; }

void Checkbox::onAttached() {
  Pressable::onAttached();
  style().hasMeasure = true;
  style().flexShrink = 0.0;
}

void Checkbox::setChecked(bool checked) {
  setMixed(false);
  setSelected(checked);
}

void Checkbox::setLabel(std::string label) {
  if (label == label_) return;
  label_ = std::move(label);
  requestLayout();
  requestPaint();
}

void Checkbox::activate() {
  // A mixed box becomes checked (as the native control does); otherwise the value flips.
  const bool next = mixed() ? true : !checked();
  setChecked(next);
  if (onChange_) onChange_(next);
}

core::layout::MeasureResult Checkbox::measure(const core::layout::MeasureInput& input) {
  const theme::ResolvedStyle& rs = ui().services().resolve("checkbox.label", 0);
  const double scale = ui().scale();
  double width = kBoxSize;
  double height = kBoxSize;
  if (!label_.empty()) {
    width += kLabelGap + std::ceil(static_cast<double>(ui().text().measure(label_, static_cast<float>(rs.text.fontSize * scale), rs.text.weight)) / scale);  // whole px: layout rounds
    height = std::max(height, rs.text.lineHeight);
  }
  if (input.widthMode == core::layout::MeasureMode::AtMost) width = std::min(width, input.width);
  return {width, height};
}

float Checkbox::paintOpacity() const { return static_cast<float>(ui().services().resolve("checkbox.label", styleState()).opacity); }

void Checkbox::paint(PaintContext& ctx) {
  const core::layout::Rect& r = ctx.rect();
  const double top = std::floor((r.h - kBoxSize) * 0.5);
  const render::Rect box = ctx.toPhysical(r.x, r.y + top, kBoxSize, kBoxSize);
  const NativeColors native = nativeColors(ui().theme().id() == theme::ThemeId::Dark);
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(2.0));
  const bool on = checked() || mixed();
  render::Painter& p = ctx.painter();
  if (on) {
    p.fillRoundedRect(box, radii, ctx.animatedColor(0, hovered() ? native.checkedHover : ctx.color("accent")));
    const float s = ctx.scale();
    const render::Color mark = native.mark;
    const float w = 1.9f * s;
    if (mixed()) {
      p.line(box.x + 3.0f * s, box.y + 6.5f * s, box.x + 10.0f * s, box.y + 6.5f * s, w, mark);
    } else {
      p.line(box.x + 2.9f * s, box.y + 6.8f * s, box.x + 5.1f * s, box.y + 9.9f * s, w, mark);
      p.line(box.x + 5.1f * s, box.y + 9.9f * s, box.x + 10.4f * s, box.y + 2.6f * s, w, mark);
    }
  } else {
    p.fillRoundedRect(box, radii, native.fill);
    p.border(box, radii, ctx.hairline(), ctx.animatedColor(0, hovered() ? native.borderHover : native.border));
  }
  if (!label_.empty()) {
    const theme::ResolvedStyle& rs = ctx.style("checkbox.label");
    const double x = r.x + kBoxSize + kLabelGap;
    ctx.drawText(label_, rs.text, ctx.toPhysical(x, r.y, std::max(0.0, r.w - kBoxSize - kLabelGap), r.h));
  }
}

void Checkbox::paintOver(PaintContext& ctx) {
  if (!focusVisible()) return;
  const core::layout::Rect& r = ctx.rect();
  const double top = std::floor((r.h - kBoxSize) * 0.5);
  ctx.focusRing(ctx.toPhysical(r.x - 1.0, r.y + top - 1.0, kBoxSize + 2.0, kBoxSize + 2.0), ctx.px(3.0));
}

std::string_view Checkbox::accessibleName() const {
  return WidgetObject::accessibleName().empty() ? std::string_view(label_) : WidgetObject::accessibleName();
}

}  // namespace r1ui::widgets

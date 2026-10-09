// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Label.h.
// Invariants: measure() and paint() use the same font size (style size x display scale) and the
//   same engine, so a label that fits its measured width never truncates.
// Callers: UiContext (measure/paint dispatch), tests.
#include "r1ui/widgets/label/Label.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

const char* Label::styleKeyFor(LabelRole role) {
  switch (role) {
    case LabelRole::Body: return "label.body";
    case LabelRole::Muted: return "label.muted";
    case LabelRole::Caption: return "label.caption";
    case LabelRole::Heading: return "label.heading";
    case LabelRole::Title: return "label.title";
    case LabelRole::Danger: return "label.danger";
  }
  return "label.body";
}

const theme::ResolvedStyle& Label::resolved() const { return ui().services().resolve(styleKeyFor(role_), styleState()); }

void Label::onAttached() {
  core::layout::Style& s = style();
  s.hasMeasure = true;
  s.flexShrink = 1.0;
  node().flags.hitTestTransparent = true;
}

void Label::setText(std::string text) {
  if (text == text_) return;
  text_ = std::move(text);
  requestLayout();
  requestPaint();
}

void Label::setRole(LabelRole role) {
  if (role == role_) return;
  role_ = role;
  requestLayout();
  requestPaint();
}

void Label::setColorToken(std::string token) {
  if (token == colorToken_) return;
  colorToken_ = std::move(token);
  requestPaint();
}

void Label::setAlign(TextAlign align) {
  if (align == align_) return;
  align_ = align;
  requestPaint();
}

void Label::setEllipsis(bool ellipsis) {
  if (ellipsis == ellipsis_) return;
  ellipsis_ = ellipsis;
  requestPaint();
}

void Label::setInteractive(bool interactive) { node().flags.hitTestTransparent = !interactive; }

core::layout::MeasureResult Label::measure(const core::layout::MeasureInput& input) {
  const theme::ResolvedStyle& rs = resolved();
  const double scale = ui().scale();
  const float px = static_cast<float>(rs.text.fontSize * scale);
  double width = static_cast<double>(ui().text().measure(text_, px, rs.text.weight)) / scale;
  if (input.widthMode == core::layout::MeasureMode::AtMost) width = std::min(width, input.width);
  return {width, rs.text.lineHeight};
}

float Label::paintOpacity() const { return static_cast<float>(resolved().opacity); }

void Label::paint(PaintContext& ctx) {
  if (text_.empty()) return;
  const theme::ResolvedStyle& rs = resolved();
  TextOptions options;
  options.padLeft = style().padding[core::layout::kLeft];
  options.padRight = style().padding[core::layout::kRight];
  options.align = align_;
  options.ellipsis = ellipsis_;
  if (!colorToken_.empty()) options.color = ctx.color(colorToken_);
  ctx.drawText(text_, rs.text, ctx.box(), options);
}

std::string_view Label::accessibleName() const { return WidgetObject::accessibleName().empty() ? std::string_view(text_) : WidgetObject::accessibleName(); }

}  // namespace r1ui::widgets

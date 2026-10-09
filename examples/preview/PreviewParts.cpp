// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of PreviewParts.h.
// Callers: ComposedApp*.cpp, GalleryApp.cpp.
#include "PreviewParts.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace preview {

namespace layout = r1ui::core::layout;
namespace render = r1ui::render;
using r1ui::widgets::PaintContext;

// ---- Surface ------------------------------------------------------------------------------

void Surface::paint(PaintContext& ctx) { ctx.painter().fillRect(ctx.box(), ctx.color(token_)); }

void Surface::paintOver(PaintContext& ctx) {
  if (edges_ == 0) return;
  const render::Rect b = ctx.box();
  const float line = ctx.hairline();
  const render::Color border = ctx.color("border");
  if ((edges_ & kLeft) != 0) ctx.painter().fillRect({b.x, b.y, line, b.h}, border);
  if ((edges_ & kRight) != 0) ctx.painter().fillRect({b.x + b.w - line, b.y, line, b.h}, border);
  if ((edges_ & kTop) != 0) ctx.painter().fillRect({b.x, b.y, b.w, line}, border);
  if ((edges_ & kBottom) != 0) ctx.painter().fillRect({b.x, b.y + b.h - line, b.w, line}, border);
}

// ---- PassThrough --------------------------------------------------------------------------

void PassThrough::onAttached() {
  layout::Style& s = style();
  s.position = layout::Position::Absolute;
  for (int e = 0; e < 4; ++e) s.inset[e] = layout::Length::px(0);
  s.direction = layout::FlexDirection::Column;
  node().flags.hitTestTransparent = true;
}

// ---- ColorSwatch --------------------------------------------------------------------------

void ColorSwatch::onAttached() {
  Pressable::onAttached();
  style().width = layout::Length::px(26);
  style().height = layout::Length::px(26);
  style().flexShrink = 0.0;
}

void ColorSwatch::setColor(const r1ui::widgets::color::Rgba& color) {
  color_ = r1ui::widgets::color::sanitized(color);
  requestPaint();
}

void ColorSwatch::paint(PaintContext& ctx) {
  const render::Rect box = ctx.box();
  const render::Rect chip{box.x + ctx.px(3), box.y + ctx.px(3), box.w - ctx.px(6), box.h - ctx.px(6)};
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(3));
  const auto to8 = [](double v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
  ctx.painter().fillRoundedRect(chip, radii, render::Color::fromRgba8(to8(color_.rgb.r), to8(color_.rgb.g), to8(color_.rgb.b), to8(color_.a)));
  ctx.painter().border(chip, radii, ctx.hairline(), ctx.color("border"));
}

void ColorSwatch::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.px(4));
}

void ColorSwatch::activate() {
  if (onClick_) onClick_();
}

}  // namespace preview

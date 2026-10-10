// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: PieMenu (PieMenu.h): style rows, geometry and painting.
// Invariants: paint() only draws; the slot list never exceeds eight entries; all positions derive from
//   pieSlotOffset so the drawn slots are exactly the sectors PieGesture selects.
// Callers: PieTrigger, gallery, tests.
#include "r1ui/widgets/pie/PieMenu.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

using theme::State::kDisabled;
using theme::State::kNone;
using theme::State::kSelected;
using theme::StyleProperty;

constexpr theme::StyleRuleEntry kRows[] = {
    {"pie.backdrop", kNone, StyleProperty::Background, "color:panel@0.98"},
    {"pie.backdrop", kNone, StyleProperty::BorderColor, "color:border"},
    {"pie.backdrop", kNone, StyleProperty::BorderWidth, "number:1"},
    {"pie.center", kNone, StyleProperty::Background, "color:panel-secondary"},
    {"pie.center", kNone, StyleProperty::BorderColor, "color:border-strong"},
    {"pie.center", kNone, StyleProperty::BorderWidth, "number:1"},
    {"pie.slot", kNone, StyleProperty::Background, "color:panel-field"},
    {"pie.slot", kNone, StyleProperty::Foreground, "color:surface"},
    {"pie.slot", kNone, StyleProperty::BorderColor, "color:border-strong"},
    {"pie.slot", kNone, StyleProperty::BorderWidth, "number:1"},
    {"pie.slot", kNone, StyleProperty::Radius, "number:15"},
    {"pie.slot", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"pie.slot", kNone, StyleProperty::LineHeight, "number:16"},
    {"pie.slot", kNone, StyleProperty::PaddingX, "number:10"},
    {"pie.slot", kSelected, StyleProperty::Background, "color:accent"},
    {"pie.slot", kSelected, StyleProperty::Foreground, "#ffffff"},
    {"pie.slot", kSelected, StyleProperty::BorderColor, "color:accent"},
    {"pie.slot", kDisabled, StyleProperty::Background, "color:panel-field@0.55"},
    {"pie.slot", kDisabled, StyleProperty::Foreground, "color:muted@0.6"},
    {"pie.slot", kDisabled, StyleProperty::BorderColor, "color:border@0.7"},
    {"pie.empty", kNone, StyleProperty::BorderColor, "color:muted@0.4"},
    {"pie.empty", kNone, StyleProperty::BorderWidth, "number:1"},
    {"pie.empty", kSelected, StyleProperty::BorderColor, "color:muted@0.8"},
};

constexpr double kIconSize = 14.0;
constexpr double kIconGap = 6.0;

render::Color withAlpha(render::Color c, float factor) {
  c.a *= factor;
  return c;
}

// A filled circle (a rounded square with the maximum radius).
void disc(PaintContext& ctx, double cx, double cy, double radius, const render::Color& color) {
  const render::Rect box = ctx.toPhysical(cx - radius, cy - radius, 2.0 * radius, 2.0 * radius);
  ctx.painter().fillRoundedRect(box, render::CornerRadii::uniform(ctx.px(radius)), color);
}

void ring(PaintContext& ctx, double cx, double cy, double radius, double width, const render::Color& color) {
  const render::Rect box = ctx.toPhysical(cx - radius, cy - radius, 2.0 * radius, 2.0 * radius);
  ctx.painter().border(box, render::CornerRadii::uniform(ctx.px(radius)), ctx.px(width), color);
}

}  // namespace

std::span<const theme::StyleRuleEntry> PieMenu::styleRows() { return kRows; }

PieMenu::PieMenu(std::vector<PieSlotView> slots, double deadZone) : slots_(std::move(slots)), deadZone_(std::isfinite(deadZone) ? std::clamp(deadZone, 4.0, 200.0) : 24.0) {
  if (slots_.size() > 8) slots_.resize(8);
}

void PieMenu::onAttached() {
  core::layout::Style& s = style();
  s.width = core::layout::Length::px(extent());
  s.height = core::layout::Length::px(extent());
  s.flexShrink = 0.0;
  node().flags.hitTestTransparent = true;
}

void PieMenu::setHighlight(int slot) {
  const int value = slot >= 0 && slot < slotCount() ? slot : -1;
  if (value == highlight_) return;
  highlight_ = value;
  requestPaint();
}

core::layout::RectD PieMenu::slotRect(int index) const {
  if (index < 0 || index >= slotCount()) return {};
  const PiePoint p = pieSlotOffset(slotCount(), index);
  const double c = kBackdropRadius;
  return {c + p.x - kSlotWidth * 0.5, c + p.y - kSlotHeight * 0.5, kSlotWidth, kSlotHeight};
}

void PieMenu::paint(PaintContext& ctx) {
  const core::layout::Rect self = ctx.rect();
  const double cx = static_cast<double>(self.x) + kBackdropRadius;
  const double cy = static_cast<double>(self.y) + kBackdropRadius;

  // Backdrop disc with the md shadow, a faint ring where the slots sit, the dead zone at the centre.
  const theme::ResolvedStyle& back = ctx.style("pie.backdrop");
  const render::Rect backBox = ctx.toPhysical(cx - kBackdropRadius + 6.0, cy - kBackdropRadius + 6.0, 2.0 * (kBackdropRadius - 6.0), 2.0 * (kBackdropRadius - 6.0));
  const render::CornerRadii backRadii = render::CornerRadii::uniform(ctx.px(kBackdropRadius - 6.0));
  if (const auto layers = ctx.ui().services().tokens().shadow("md")) {
    for (auto it = layers->rbegin(); it != layers->rend(); ++it) {
      render::ShadowSpec spec;
      spec.offsetX = ctx.px(it->offsetX);
      spec.offsetY = ctx.px(it->offsetY);
      spec.blur = ctx.px(it->blur);
      spec.spread = ctx.px(it->spread);
      spec.color = ctx.color(it->color);
      ctx.painter().shadow(backBox, backRadii, spec);
    }
  }
  ctx.painter().fillRoundedRect(backBox, backRadii, ctx.color(back.background));
  ctx.painter().border(backBox, backRadii, ctx.px(back.border.width), ctx.color(back.border.color));
  const theme::ResolvedStyle& center = ctx.style("pie.center");
  ring(ctx, cx, cy, kPieSlotRadius, 1.0, withAlpha(ctx.color(back.border.color), 0.6f));
  disc(ctx, cx, cy, deadZone_, ctx.color(center.background));
  ring(ctx, cx, cy, deadZone_, center.border.width, ctx.color(center.border.color));

  // Line from the centre towards the highlighted slot, ending at the edge of its pill.
  if (highlight_ >= 0) {
    const theme::ResolvedStyle& hot = ctx.resolve("pie.slot", kSelected);
    const PiePoint p = pieSlotOffset(slotCount(), highlight_);
    const double ux = p.x / kPieSlotRadius;
    const double uy = p.y / kPieSlotRadius;
    const double tx = std::fabs(ux) > 1e-6 ? kSlotWidth * 0.5 / std::fabs(ux) : 1e9;
    const double ty = std::fabs(uy) > 1e-6 ? kSlotHeight * 0.5 / std::fabs(uy) : 1e9;
    const double end = kPieSlotRadius - std::min(tx, ty) - 2.0;
    const double start = deadZone_;
    if (end > start) {
      const render::Rect a = ctx.toPhysical(cx + ux * start, cy + uy * start, 0.0, 0.0);
      const render::Rect b = ctx.toPhysical(cx + ux * end, cy + uy * end, 0.0, 0.0);
      ctx.painter().line(a.x, a.y, b.x, b.y, ctx.px(3.0), ctx.color(hot.background));
    }
    disc(ctx, cx, cy, 4.5, ctx.color(hot.background));
  } else {
    disc(ctx, cx, cy, 3.0, withAlpha(ctx.color(center.border.color), 0.9f));
  }

  // Slots.
  for (int i = 0; i < slotCount(); ++i) {
    const PieSlotView& slot = slots_[static_cast<size_t>(i)];
    const PiePoint p = pieSlotOffset(slotCount(), i);
    const double sx = cx + p.x;
    const double sy = cy + p.y;
    const bool hot = i == highlight_;
    if (!slot.filled) {
      const theme::ResolvedStyle& empty = ctx.resolve("pie.empty", hot ? kSelected : kNone);
      disc(ctx, sx, sy, 5.0, withAlpha(ctx.color(empty.border.color), 0.35f));
      ring(ctx, sx, sy, 9.0, empty.border.width, ctx.color(empty.border.color));
      continue;
    }
    const uint8_t bits = hot ? kSelected : (slot.selectable ? kNone : kDisabled);
    const theme::ResolvedStyle& rs = ctx.resolve("pie.slot", bits);
    const render::Rect box = ctx.toPhysical(sx - kSlotWidth * 0.5, sy - kSlotHeight * 0.5, kSlotWidth, kSlotHeight);
    const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(rs.radius));
    ctx.painter().fillRoundedRect(box, radii, ctx.color(rs.background));
    const render::Color outline = slot.checked && !hot ? ctx.color("accent") : ctx.color(rs.border.color);
    ctx.painter().border(box, radii, ctx.px(slot.checked && !hot ? 2.0 : rs.border.width), outline);

    const render::Color fg = ctx.color(rs.text.color);
    const double inner = kSlotWidth - 2.0 * rs.paddingX;
    const bool hasIcon = !slot.icon.empty();
    const bool hasLabel = !slot.label.empty();
    const double iconW = hasIcon ? kIconSize : 0.0;
    const double gap = hasIcon && hasLabel ? kIconGap : 0.0;
    const double labelRoom = std::max(0.0, inner - iconW - gap);
    double labelW = 0.0;
    if (hasLabel) {
      const double scale = ctx.scale();
      labelW = std::min(labelRoom, static_cast<double>(ctx.ui().text().measure(slot.label, static_cast<float>(rs.text.fontSize * scale), rs.text.weight)) / scale);
    }
    // The icon and label are centred as a group.
    const double groupW = iconW + gap + labelW;
    const double left = sx - groupW * 0.5;
    if (hasIcon) {
      const render::Rect iconBox = ctx.toPhysical(left, sy - kSlotHeight * 0.5, kIconSize, kSlotHeight);
      ctx.drawIcon(slot.icon, kIconSize, iconBox, fg, "circle");
    }
    if (hasLabel) {
      TextOptions options;
      options.color = fg;
      const render::Rect textBox = ctx.toPhysical(left + iconW + gap, sy - kSlotHeight * 0.5, labelW + 1.0, kSlotHeight);
      ctx.drawText(slot.label, rs.text, textBox, options);
    }
  }
}

}  // namespace r1ui::widgets

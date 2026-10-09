// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of FieldChrome.h: the row table and the field box helpers.
// Invariants: the table is a constexpr array, so its address identifies it for Services::addStyleRows;
//   every widget of the group returns this same table.
// Callers: TextInput, NumberField, Select.
#include "r1ui/widgets/textinput/FieldChrome.h"

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

using theme::State::kBound;
using theme::State::kDisabled;
using theme::State::kFocus;
using theme::State::kHover;
using theme::State::kInvalid;
using theme::State::kMixed;
using theme::State::kNone;
using theme::StyleProperty;

constexpr theme::StyleRuleEntry kRows[] = {
    // ---- text input, default tone, md (12 px) and sm (11 px) --------------------------------------
    {"input.default", kNone, StyleProperty::Background, "color:input"},
    {"input.default", kNone, StyleProperty::Foreground, "color:surface"},
    {"input.default", kNone, StyleProperty::BorderColor, "color:border"},
    {"input.default", kNone, StyleProperty::BorderWidth, "number:1"},
    {"input.default", kNone, StyleProperty::Radius, "metric:field.radius"},
    {"input.default", kNone, StyleProperty::Height, "metric:field.height"},
    {"input.default", kNone, StyleProperty::PaddingX, "number:8"},
    {"input.default", kNone, StyleProperty::PaddingY, "number:4"},
    {"input.default", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"input.default", kNone, StyleProperty::LineHeight, "lineHeight:xs"},
    {"input.default", kFocus, StyleProperty::BorderColor, "color:accent"},
    {"input.default", kMixed, StyleProperty::Foreground, "color:muted"},
    {"input.default", kBound, StyleProperty::Foreground, "color:component"},
    {"input.default", kInvalid, StyleProperty::BorderColor, "color:danger"},
    {"input.default", kDisabled, StyleProperty::Opacity, "number:0.5"},

    {"input.default.sm", kNone, StyleProperty::Background, "color:input"},
    {"input.default.sm", kNone, StyleProperty::Foreground, "color:surface"},
    {"input.default.sm", kNone, StyleProperty::BorderColor, "color:border"},
    {"input.default.sm", kNone, StyleProperty::BorderWidth, "number:1"},
    {"input.default.sm", kNone, StyleProperty::Radius, "metric:field.radius"},
    {"input.default.sm", kNone, StyleProperty::Height, "metric:field.height"},
    {"input.default.sm", kNone, StyleProperty::PaddingX, "number:8"},
    {"input.default.sm", kNone, StyleProperty::PaddingY, "number:4"},
    {"input.default.sm", kNone, StyleProperty::FontSize, "fontSize:11"},
    {"input.default.sm", kNone, StyleProperty::LineHeight, "number:16.5"},
    {"input.default.sm", kFocus, StyleProperty::BorderColor, "color:accent"},
    {"input.default.sm", kMixed, StyleProperty::Foreground, "color:muted"},
    {"input.default.sm", kBound, StyleProperty::Foreground, "color:component"},
    {"input.default.sm", kInvalid, StyleProperty::BorderColor, "color:danger"},
    {"input.default.sm", kDisabled, StyleProperty::Opacity, "number:0.5"},

    // ---- panel tone: the properties-panel field (text input, number field and select trigger) --------
    {"input.panel", kNone, StyleProperty::Background, "color:panel-field"},
    {"input.panel", kNone, StyleProperty::Foreground, "color:surface"},
    {"input.panel", kNone, StyleProperty::BorderColor, "transparent"},
    {"input.panel", kNone, StyleProperty::BorderWidth, "metric:field.borderWidth"},
    {"input.panel", kNone, StyleProperty::Radius, "metric:field.radius"},
    {"input.panel", kNone, StyleProperty::Height, "metric:field.height"},
    {"input.panel", kNone, StyleProperty::PaddingX, "metric:field.padX"},
    {"input.panel", kNone, StyleProperty::PaddingY, "number:4"},
    {"input.panel", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"input.panel", kNone, StyleProperty::LineHeight, "lineHeight:xs"},
    {"input.panel", kHover, StyleProperty::Background, "color:panel-field-hover"},
    {"input.panel", kFocus, StyleProperty::BorderColor, "color:panel-focus"},
    {"input.panel", kMixed, StyleProperty::Foreground, "color:muted"},
    {"input.panel", kBound, StyleProperty::Foreground, "color:component"},
    {"input.panel", kInvalid, StyleProperty::BorderColor, "color:danger"},
    {"input.panel", kDisabled, StyleProperty::Opacity, "number:0.6"},

    {"input.panel.sm", kNone, StyleProperty::Background, "color:panel-field"},
    {"input.panel.sm", kNone, StyleProperty::Foreground, "color:surface"},
    {"input.panel.sm", kNone, StyleProperty::BorderColor, "transparent"},
    {"input.panel.sm", kNone, StyleProperty::BorderWidth, "metric:field.borderWidth"},
    {"input.panel.sm", kNone, StyleProperty::Radius, "metric:field.radius"},
    {"input.panel.sm", kNone, StyleProperty::Height, "metric:field.height"},
    {"input.panel.sm", kNone, StyleProperty::PaddingX, "metric:field.padX"},
    {"input.panel.sm", kNone, StyleProperty::PaddingY, "number:4"},
    {"input.panel.sm", kNone, StyleProperty::FontSize, "fontSize:11"},
    {"input.panel.sm", kNone, StyleProperty::LineHeight, "number:16"},
    {"input.panel.sm", kHover, StyleProperty::Background, "color:panel-field-hover"},
    {"input.panel.sm", kFocus, StyleProperty::BorderColor, "color:panel-focus"},
    {"input.panel.sm", kMixed, StyleProperty::Foreground, "color:muted"},
    {"input.panel.sm", kBound, StyleProperty::Foreground, "color:component"},
    {"input.panel.sm", kInvalid, StyleProperty::BorderColor, "color:danger"},
    {"input.panel.sm", kDisabled, StyleProperty::Opacity, "number:0.6"},

    // Browser highlight measured in the reference crops (white text on it).
    {"input.selection.dark", kNone, StyleProperty::Background, "#0c40aa"},
    {"input.selection.dark", kNone, StyleProperty::Foreground, "#ffffff"},
    {"input.selection.light", kNone, StyleProperty::Background, "#2e63cd"},
    {"input.selection.light", kNone, StyleProperty::Foreground, "#ffffff"},

    // Clear button inside a text input: full text strength, as the reference's native clear glyph.
    {"input.glyph", kNone, StyleProperty::Foreground, "color:surface"},

    // ---- number field parts -----------------------------------------------------------------------
    // Value text: 12 px with tabular digits (measured: digit advance 7.4 px at 12 px).
    {"numberfield.value", kNone, StyleProperty::Foreground, "color:surface"},
    {"numberfield.value", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"numberfield.value", kNone, StyleProperty::LineHeight, "number:16"},
    {"numberfield.value", kMixed, StyleProperty::Foreground, "color:muted"},
    {"numberfield.value", kBound, StyleProperty::Foreground, "color:component"},
    {"numberfield.label", kNone, StyleProperty::Foreground, "color:muted"},
    {"numberfield.label", kNone, StyleProperty::FontSize, "fontSize:10"},
    {"numberfield.label", kNone, StyleProperty::LineHeight, "number:16"},
    {"numberfield.suffix", kNone, StyleProperty::Foreground, "color:muted"},
    {"numberfield.suffix", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"numberfield.suffix", kNone, StyleProperty::LineHeight, "lineHeight:xs"},
    {"numberfield.pill", kNone, StyleProperty::Foreground, "color:component"},
    {"numberfield.pill", kNone, StyleProperty::FontSize, "fontSize:11"},
    {"numberfield.pill", kNone, StyleProperty::FontWeight, "weight:medium"},
    {"numberfield.pill", kNone, StyleProperty::LineHeight, "number:16"},
    {"numberfield.pill", kNone, StyleProperty::Radius, "metric:field.radius"},
    {"numberfield.button", kNone, StyleProperty::Foreground, "color:muted"},
    {"numberfield.button", kHover, StyleProperty::Foreground, "color:surface"},
    {"numberfield.button", kBound, StyleProperty::Foreground, "color:component"},

    // ---- select trigger, list and parts ---------------------------------------------------------------
    {"select.item", kNone, StyleProperty::Foreground, "color:surface"},
    {"select.item", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"select.item", kNone, StyleProperty::LineHeight, "lineHeight:xs"},
    {"select.item", kNone, StyleProperty::Radius, "radius:sm"},
    {"select.item", kNone, StyleProperty::PaddingY, "number:6"},
    {"select.item", kHover, StyleProperty::Background, "color:hover"},
    {"select.item", kDisabled, StyleProperty::Foreground, "color:muted@0.5"},
    {"select.item.sm", kNone, StyleProperty::Foreground, "color:surface"},
    {"select.item.sm", kNone, StyleProperty::FontSize, "fontSize:11"},
    {"select.item.sm", kNone, StyleProperty::LineHeight, "number:16"},
    {"select.item.sm", kNone, StyleProperty::Radius, "radius:sm"},
    {"select.item.sm", kNone, StyleProperty::PaddingY, "number:6"},
    {"select.item.sm", kHover, StyleProperty::Background, "color:hover"},
    {"select.item.sm", kDisabled, StyleProperty::Foreground, "color:muted@0.5"},
    {"select.group", kNone, StyleProperty::Foreground, "color:muted"},
    {"select.group", kNone, StyleProperty::FontSize, "fontSize:10"},
    {"select.group", kNone, StyleProperty::LineHeight, "number:16"},
    {"select.check", kNone, StyleProperty::Foreground, "color:accent"},
    {"select.separator", kNone, StyleProperty::BorderColor, "color:border"},
    {"select.separator", kNone, StyleProperty::BorderWidth, "number:1"},
    {"select.hint", kNone, StyleProperty::Foreground, "color:muted"},
    {"select.hint", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"select.hint", kNone, StyleProperty::LineHeight, "lineHeight:xs"},
    {"select.scrollbar", kNone, StyleProperty::Background, "color:border"},
    {"select.scrollbar", kNone, StyleProperty::Radius, "metric:scrollbarThin.radius"},
    {"select.scrollbar", kNone, StyleProperty::Height, "metric:scrollbarThin.size"},
    {"select.scrollbar", kHover, StyleProperty::Background, "color:muted"},
    {"select.search", kNone, StyleProperty::Foreground, "color:surface"},
    {"select.search", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"select.search", kNone, StyleProperty::LineHeight, "lineHeight:xs"},
    {"select.search", kNone, StyleProperty::BorderColor, "color:border"},
    {"select.search", kNone, StyleProperty::BorderWidth, "number:1"},
};

}  // namespace

std::span<const theme::StyleRuleEntry> fieldStyleRows() { return kRows; }

FieldColors animatedFieldColors(PaintContext& ctx, const theme::ResolvedStyle& rs, int baseSlot) {
  return {ctx.animatedColor(baseSlot, ctx.color(rs.background)), ctx.animatedColor(baseSlot + 1, ctx.color(rs.border.color))};
}

void paintFieldBox(PaintContext& ctx, const theme::ResolvedStyle& rs, const FieldColors& colors, const render::Rect& box) {
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(rs.radius));
  if (colors.background.a > 0.0f) ctx.painter().fillRoundedRect(box, radii, colors.background);
  if (rs.border.width > 0.0 && colors.border.a > 0.0f) ctx.painter().border(box, radii, ctx.px(rs.border.width), colors.border);
}

render::Color fieldSelectionBackground(PaintContext& ctx) {
  const bool light = ctx.ui().theme().id() == theme::ThemeId::Light;
  return ctx.color(ctx.resolve(light ? "input.selection.light" : "input.selection.dark", 0).background);
}

render::Color fieldSelectionText(PaintContext& ctx) {
  const bool light = ctx.ui().theme().id() == theme::ThemeId::Light;
  return ctx.color(ctx.resolve(light ? "input.selection.light" : "input.selection.dark", 0).text.color);
}

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of DialogParts.h.
// Invariants: DialogText measures and paints the same wrapped lines for the same width; a pressable
//   widget activates at most once per input gesture and never while disabled.
// Callers: Dialog.cpp, UiContext (dispatch).
#include "r1ui/widgets/dialog/DialogParts.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/tooltip/TooltipContent.h"

namespace r1ui::widgets {

namespace events = core::events;

namespace {

using theme::State::kActive;
using theme::State::kDisabled;
using theme::State::kHover;
using theme::State::kNone;
using theme::StyleProperty;

constexpr double kTextWrapCap = 480.0;   // wrap width when the parent does not constrain the text
constexpr size_t kTextMaxLines = 24;

constexpr theme::StyleRuleEntry kTextRows[] = {
    {"dialog.title", kNone, StyleProperty::Foreground, "color:surface"},
    {"dialog.title", kNone, StyleProperty::FontSize, "fontSize:sm"},
    {"dialog.title", kNone, StyleProperty::LineHeight, "lineHeight:sm"},
    {"dialog.title", kNone, StyleProperty::FontWeight, "weight:semibold"},
    {"dialog.description", kNone, StyleProperty::Foreground, "color:muted"},
    {"dialog.description", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"dialog.description", kNone, StyleProperty::LineHeight, "number:16"},
};

constexpr theme::StyleRuleEntry kButtonRows[] = {
    {"dialog.button", kNone, StyleProperty::Background, "color:hover"},
    {"dialog.button", kNone, StyleProperty::Foreground, "color:surface"},
    {"dialog.button", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"dialog.button", kNone, StyleProperty::LineHeight, "number:16"},
    {"dialog.button", kNone, StyleProperty::PaddingX, "number:12"},
    {"dialog.button", kNone, StyleProperty::PaddingY, "number:6"},
    {"dialog.button", kNone, StyleProperty::Radius, "radius:sm"},
    {"dialog.button", kHover, StyleProperty::Background, "color:border"},
    {"dialog.button", kActive, StyleProperty::Background, "color:border"},
    {"dialog.button", kDisabled, StyleProperty::Opacity, "number:0.5"},
    {"dialog.button.primary", kNone, StyleProperty::Background, "color:accent"},
    {"dialog.button.primary", kNone, StyleProperty::Foreground, "#ffffff"},
    {"dialog.button.primary", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"dialog.button.primary", kNone, StyleProperty::LineHeight, "number:16"},
    {"dialog.button.primary", kNone, StyleProperty::PaddingX, "number:12"},
    {"dialog.button.primary", kNone, StyleProperty::PaddingY, "number:6"},
    {"dialog.button.primary", kNone, StyleProperty::Radius, "radius:sm"},
    {"dialog.button.primary", kHover, StyleProperty::Background, "color:accent@0.9"},
    {"dialog.button.primary", kActive, StyleProperty::Background, "color:accent@0.9"},
    {"dialog.button.primary", kDisabled, StyleProperty::Opacity, "number:0.5"},
    {"dialog.button.danger", kNone, StyleProperty::Background, "#e7000b"},
    {"dialog.button.danger", kNone, StyleProperty::Foreground, "#ffffff"},
    {"dialog.button.danger", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"dialog.button.danger", kNone, StyleProperty::LineHeight, "number:16"},
    {"dialog.button.danger", kNone, StyleProperty::PaddingX, "number:12"},
    {"dialog.button.danger", kNone, StyleProperty::PaddingY, "number:6"},
    {"dialog.button.danger", kNone, StyleProperty::Radius, "radius:sm"},
    {"dialog.button.danger", kHover, StyleProperty::Background, "#e7000be6"},
    {"dialog.button.danger", kActive, StyleProperty::Background, "#e7000be6"},
    {"dialog.button.danger", kDisabled, StyleProperty::Opacity, "number:0.5"},
};

constexpr theme::StyleRuleEntry kCloseRows[] = {
    {"dialog.close", kNone, StyleProperty::Background, "transparent"},
    {"dialog.close", kNone, StyleProperty::Foreground, "color:muted"},
    {"dialog.close", kNone, StyleProperty::Radius, "radius:sm"},
    {"dialog.close", kHover, StyleProperty::Background, "color:hover"},
    {"dialog.close", kHover, StyleProperty::Foreground, "color:surface"},
    {"dialog.close", kDisabled, StyleProperty::Opacity, "number:0.5"},
};

double widthOf(UiContext& ui, std::string_view text, double fontSize, int weight) {
  if (text.empty()) return 0.0;
  const double scale = ui.scale();
  return static_cast<double>(ui.text().measure(text, static_cast<float>(fontSize * scale), weight)) / scale;
}

}  // namespace

// ---- DialogBox ----------------------------------------------------------------------------------

void DialogBox::setDividerBelow(bool on) {
  if (divider_ == on) return;
  divider_ = on;
  requestPaint();
}

void DialogBox::paint(PaintContext& ctx) {
  if (!divider_) return;
  const render::Rect box = ctx.box();
  const float line = ctx.hairline();
  ctx.painter().fillRect({box.x, box.y + box.h - line, box.w, line}, ctx.color("border"));
}

// ---- DialogText ---------------------------------------------------------------------------------

std::span<const theme::StyleRuleEntry> DialogText::styleRows() { return kTextRows; }

DialogText::DialogText(std::string text, const char* styleKey) : text_(std::move(text)), key_(styleKey) {}

void DialogText::onAttached() {
  style().hasMeasure = true;
  style().flexShrink = 0.0;
  node().flags.hitTestTransparent = true;
}

std::vector<std::string> DialogText::wrapped(double width) const {
  const theme::ResolvedStyle& rs = ui().services().resolve(key_, 0);
  return wrapTooltipText(ui(), text_, rs.text.fontSize, rs.text.weight, width, kTextMaxLines);
}

core::layout::MeasureResult DialogText::measure(const core::layout::MeasureInput& input) {
  const theme::ResolvedStyle& rs = ui().services().resolve(key_, 0);
  const double room = input.widthMode == core::layout::MeasureMode::Undefined ? kTextWrapCap : input.width;
  const std::vector<std::string> lines = wrapped(room);
  double width = 0.0;
  for (const std::string& line : lines) width = std::max(width, widthOf(ui(), line, rs.text.fontSize, rs.text.weight));
  return {width, rs.text.lineHeight * static_cast<double>(lines.size())};
}

void DialogText::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.resolve(key_, 0);
  const std::vector<std::string> lines = wrapped(ctx.rect().w);
  const render::Rect box = ctx.box();
  const float lineH = ctx.px(rs.text.lineHeight);
  TextOptions o;
  o.ellipsis = false;
  float y = box.y;
  for (const std::string& line : lines) {
    ctx.drawText(line, rs.text, {box.x, y, box.w, lineH}, o);
    y += lineH;
  }
}

// ---- DialogPressable ----------------------------------------------------------------------------

void DialogPressable::onAttached() { setFocusable(true); }

uint8_t DialogPressable::styleState() const {
  return static_cast<uint8_t>(WidgetObject::styleState() & ~theme::State::kFocus);  // focus is a ring, not a fill
}

void DialogPressable::activate() {
  if (!enabled() || !onActivate_) return;
  const std::function<void()> fn = onActivate_;  // the callback may destroy this widget
  fn();
}

void DialogPressable::onPointerDown(Event& e) {
  if (e.button == events::Button::Left && enabled()) ui().router().capturePointer(id());
}

void DialogPressable::onPointerUp(Event&) {}

void DialogPressable::onClick(Event& e) {
  e.markHandled();
  activate();
}

void DialogPressable::onKeyDown(Event& e) {
  if (e.repeat || (e.modifiers & (events::Mod::kCtrl | events::Mod::kAlt | events::Mod::kMeta)) != 0) return;
  if (e.key != events::Key::Enter && e.key != events::Key::Space) return;
  e.markHandled();
  activate();
}

// ---- DialogButton -------------------------------------------------------------------------------

std::span<const theme::StyleRuleEntry> DialogButton::styleRows() { return kButtonRows; }

void DialogButton::onAttached() {
  DialogPressable::onAttached();
  style().hasMeasure = true;
  style().flexShrink = 0.0;
  if (!action_.enabled) setEnabled(false);
}

const char* DialogButton::styleKey() const {
  switch (action_.kind) {
    case DialogActionKind::Primary: return "dialog.button.primary";
    case DialogActionKind::Danger: return "dialog.button.danger";
    case DialogActionKind::Neutral: break;
  }
  return "dialog.button";
}

core::layout::MeasureResult DialogButton::measure(const core::layout::MeasureInput&) {
  const theme::ResolvedStyle& rs = ui().services().resolve(styleKey(), 0);
  return {2.0 * rs.paddingX + widthOf(ui(), action_.label, rs.text.fontSize, rs.text.weight), rs.text.lineHeight + 2.0 * rs.paddingY};
}

float DialogButton::paintOpacity() const { return static_cast<float>(ui().services().resolve(styleKey(), styleState()).opacity); }

void DialogButton::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.style(styleKey());
  ctx.fillBox(rs);
  const render::Rect box = ctx.box();
  const render::Rect line{box.x, box.y + ctx.px(rs.paddingY), box.w, ctx.px(rs.text.lineHeight)};
  TextOptions o;
  o.align = TextAlign::Center;
  o.padLeft = rs.paddingX;
  o.padRight = rs.paddingX;
  o.ellipsis = false;  // the box is the measured width rounded to whole pixels: never shorten a label that fits
  ctx.drawText(action_.label, rs.text, line, o);
}

void DialogButton::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.px(ctx.resolve(styleKey(), 0).radius));
}

// ---- DialogCloseButton --------------------------------------------------------------------------

std::span<const theme::StyleRuleEntry> DialogCloseButton::styleRows() { return kCloseRows; }

void DialogCloseButton::onAttached() {
  DialogPressable::onAttached();
  core::layout::Style& s = style();
  s.width = core::layout::Length::px(24);
  s.height = core::layout::Length::px(24);
  s.flexShrink = 0.0;
  s.position = core::layout::Position::Absolute;
  s.inset[core::layout::kTop] = core::layout::Length::px(12);
  s.inset[core::layout::kRight] = core::layout::Length::px(16);
}

void DialogCloseButton::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.style("dialog.close");
  ctx.fillBox(rs);
  ctx.drawIcon("x", 14.0, ctx.box(), ctx.color(rs.text.color));
}

void DialogCloseButton::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.px(ctx.resolve("dialog.close", 0).radius));
}

// ---- DialogContent ------------------------------------------------------------------------------

void DialogContent::onAttached() {
  core::layout::Style& s = style();
  s.direction = core::layout::FlexDirection::Column;
  s.alignItems = core::layout::Align::Stretch;
  s.flexShrink = 1.0;
  s.minHeight = core::layout::Length::px(0);
  for (int e = 0; e < 4; ++e) s.margin[e] = core::layout::Length::px(1.0);  // the host's 1 px border
}

void DialogContent::onKeyDown(Event& e) {
  if (e.key != events::Key::Enter || e.repeat || (e.modifiers & (events::Mod::kCtrl | events::Mod::kAlt | events::Mod::kMeta)) != 0) return;
  // Reached only when the focused widget did not use Enter: run the default action.
  if (runDefault_) {
    e.markHandled();
    runDefault_();
  }
}

}  // namespace r1ui::widgets

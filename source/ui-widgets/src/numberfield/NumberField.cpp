// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of NumberField.h, part 1: construction, value and range configuration, chrome
//   options, the layout of the parts (label, value, suffix, buttons) and painting. The gesture logic
//   (stepping, edit mode, scrubbing, pointer, keyboard, focus) is in NumberFieldInput.cpp.
// Invariants: value_ always lies in the hard range and is an integer in integer mode.
// Callers: UiContext (paint dispatch), tests.
#include "r1ui/widgets/numberfield/NumberField.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/textinput/FieldChrome.h"

namespace r1ui::widgets {

namespace {

constexpr double kSidePad = 6.0;        // left padding before the label or glyph
constexpr double kRightPad = 5.0;       // right padding after the last button
constexpr double kGlyphGap = 6.0;       // between the leading label and the value
constexpr double kIconGap = 5.0;        // between the leading glyph and the value
constexpr double kLeadingIcon = 12.0;
constexpr double kButtonSize = 20.0;    // apply-variable button (20 x 20)
constexpr double kDropdownWidth = 16.0;
constexpr double kButtonGap = 4.0;
constexpr double kVariableGlyph = 12.0;
constexpr double kDropdownGlyph = 12.0;
constexpr double kSuffixPad = 6.0;
constexpr double kPillPad = 4.0;

}  // namespace

NumberField::NumberField() : lineEditor_(text::EditorConfig{kMaxExpressionBytes, false}) {}

std::span<const theme::StyleRuleEntry> NumberField::styleRows() { return fieldStyleRows(); }

void NumberField::onAttached() {
  lineEditor_.bind(ui());
  setFocusable(true);
  const theme::ResolvedStyle& rs = ui().services().resolve("input.panel", 0);
  style().height = core::layout::Length::px(rs.height);
  style().minHeight = core::layout::Length::px(rs.height);
}

// ---- value and ranges ----------------------------------------------------------------------------

double NumberField::normalise(double value) const {
  if (integer_) value = std::round(value);
  return std::clamp(value, min_, max_);
}

void NumberField::setValue(double value) {
  if (!std::isfinite(value)) return;
  const double next = normalise(value);
  if (next == value_) return;
  value_ = next;
  requestPaint();
}

void NumberField::setRange(double min, double max) {
  if (!std::isfinite(min) || !std::isfinite(max) || min > max) {
    min = -1.0e15;
    max = 1.0e15;
  }
  min_ = min;
  max_ = max;
  value_ = normalise(value_);
  requestPaint();
}

void NumberField::setSoftRange(double min, double max) {
  if (!std::isfinite(min) || !std::isfinite(max) || min >= max) {
    soft_.reset();
    return;
  }
  soft_ = std::make_pair(std::max(min, min_), std::min(max, max_));
  if (soft_->first >= soft_->second) soft_.reset();
}

void NumberField::clearSoftRange() { soft_.reset(); }

void NumberField::setIncrement(double increment) { increment_ = std::isfinite(increment) && increment > 0.0 ? increment : 0.0; }

void NumberField::setInteger(bool integer) {
  integer_ = integer;
  value_ = normalise(value_);
  requestPaint();
}

void NumberField::setFractionDigits(int minDigits, int maxDigits) {
  maxDigits_ = std::clamp(maxDigits, 0, 9);
  minDigits_ = std::clamp(minDigits, 0, maxDigits_);
  requestPaint();
}

std::string NumberField::displayText() const { return formatNumber(value_, minDigits_, maxDigits_); }

// ---- chrome ----------------------------------------------------------------------------------------

void NumberField::setLabel(std::string label) {
  if (label == label_) return;
  label_ = std::move(label);
  requestPaint();
}

void NumberField::setLeadingIcon(std::string iconName) {
  if (iconName == icon_) return;
  icon_ = std::move(iconName);
  requestPaint();
}

void NumberField::setSuffix(std::string suffix) {
  if (suffix == suffix_) return;
  suffix_ = std::move(suffix);
  requestPaint();
}

void NumberField::setUnits(std::vector<NumberUnit> units) { units_ = std::move(units); }

void NumberField::setBoundVariable(std::string name) {
  boundName_ = std::move(name);
  setBound(!boundName_.empty());
  requestPaint();
}

void NumberField::setVariableButton(bool visible, std::string tooltip) {
  variableButton_ = visible;
  variableTooltip_ = std::move(tooltip);
  requestPaint();
}

void NumberField::setDropdownButton(bool visible) {
  dropdownButton_ = visible;
  requestPaint();
}

// ---- geometry ---------------------------------------------------------------------------------------

const theme::ResolvedStyle& NumberField::fieldStyle() const { return ui().services().resolve("input.panel", styleState()); }

NumberField::Parts NumberField::parts() const {
  const theme::ResolvedStyle& rs = fieldStyle();
  const core::layout::Rect r = ui().absRect(id());
  const double scale = ui().scale();
  Parts p;
  p.lineTop = r.y + rs.border.width + rs.paddingY;
  double x = r.x + kSidePad;
  if (!icon_.empty()) {
    p.iconX = x;
    x += kLeadingIcon + kIconGap;
  } else if (!label_.empty()) {
    const theme::ResolvedStyle& ls = ui().services().resolve("numberfield.label", 0);
    p.labelX = x;
    p.labelWidth = static_cast<double>(ui().text().measure(label_, static_cast<float>(ls.text.fontSize * scale))) / scale;
    x += p.labelWidth + kGlyphGap;
  }
  p.valueLeft = x;
  double right = static_cast<double>(r.right()) - kRightPad;
  const bool anyButton = variableButton_ || dropdownButton_;
  if (dropdownButton_) {
    p.dropdown = {static_cast<int32_t>(std::lround(right - kDropdownWidth)), static_cast<int32_t>(std::lround(r.y + (r.h - kButtonSize) / 2.0)),
                  static_cast<int32_t>(kDropdownWidth), static_cast<int32_t>(kButtonSize)};
    right -= kDropdownWidth + kButtonGap;
  }
  if (variableButton_) {
    p.variable = {static_cast<int32_t>(std::lround(right - kButtonSize)), static_cast<int32_t>(std::lround(r.y + (r.h - kButtonSize) / 2.0)),
                  static_cast<int32_t>(kButtonSize), static_cast<int32_t>(kButtonSize)};
    right -= kButtonSize;
  }
  if (!suffix_.empty()) {
    const theme::ResolvedStyle& ss = ui().services().resolve("numberfield.suffix", 0);
    p.suffixWidth = static_cast<double>(ui().text().measure(suffix_, static_cast<float>(ss.text.fontSize * scale))) / scale;
    const double edge = anyButton ? right - kSuffixPad : static_cast<double>(r.right()) - kSuffixPad;
    p.suffixX = edge - p.suffixWidth;
    right = p.suffixX;
  }
  p.valueRight = std::max(p.valueLeft, right - kButtonGap);
  return p;
}

NumberField::Part NumberField::partAt(double x, double y) const {
  const Parts p = parts();
  if (!p.variable.empty() && core::layout::containsPoint(p.variable, x, y)) return Part::Variable;
  if (!p.dropdown.empty() && core::layout::containsPoint(p.dropdown, x, y)) return Part::Dropdown;
  return Part::None;
}

// ---- WidgetObject -----------------------------------------------------------------------------------------------

float NumberField::paintOpacity() const { return static_cast<float>(fieldStyle().opacity); }

Cursor NumberField::cursor() const {
  if (hoverPart_ != Part::None) return Cursor::Pointer;
  return editing_ ? Cursor::Text : Cursor::ResizeHorizontal;
}

std::string_view NumberField::tooltipText() const {
  if (hoverPart_ == Part::Variable && !variableTooltip_.empty()) return variableTooltip_;
  return WidgetObject::tooltipText();
}

void NumberField::onStateChanged(uint16_t previous) {
  if (hasState(StateFlag::kDisabled) && (previous & StateFlag::kDisabled) == 0) {
    pressed_ = false;
    pressedPart_ = Part::None;
    hoverPart_ = Part::None;
  }
}

void NumberField::paint(PaintContext& ctx) {
  settleScroll();
  const theme::ResolvedStyle& rs = ctx.style("input.panel");
  const theme::ResolvedStyle& vs = ctx.style("numberfield.value");
  const render::Rect box = ctx.box();
  paintFieldBox(ctx, rs, animatedFieldColors(ctx, rs, 0), box);
  const Parts p = parts();
  const float scale = ctx.scale();
  const float lineTop = static_cast<float>(p.lineTop) * scale;
  const float lineHeight = ctx.px(vs.text.lineHeight);
  const float borderPx = ctx.px(rs.border.width);
  const bool animate = ctx.ui().animationsActive();
  if (editing_ && focused() && animate) ui().invalidator().requestAnimation(id());

  const render::Color muted = ctx.color("muted");
  if (!icon_.empty()) {
    ctx.drawIcon(icon_, kLeadingIcon, ctx.toPhysical(p.iconX, p.lineTop, kLeadingIcon, vs.text.lineHeight), muted);
  } else if (!label_.empty()) {
    const theme::ResolvedStyle& ls = ctx.resolve("numberfield.label", 0);
    TextOptions options;
    options.ellipsis = false;
    ctx.drawText(label_, ls.text, {static_cast<float>(p.labelX) * scale, lineTop, static_cast<float>(p.labelWidth) * scale + 2.0f, lineHeight}, options);
  }

  const render::Rect valueBox{static_cast<float>(p.valueLeft) * scale, lineTop, static_cast<float>(p.valueRight - p.valueLeft) * scale, lineHeight};
  if (editing_) {
    LineEditorPaint lp;
    lp.content = {valueBox.x, box.y + borderPx, valueBox.w, box.h - 2.0f * borderPx};
    lp.lineTop = lineTop;
    lp.lineHeight = lineHeight;
    lp.pixelSize = ctx.px(vs.text.fontSize);
    lp.weight = vs.text.weight;
    lp.text = ctx.color("surface");
    lp.selectionBackground = fieldSelectionBackground(ctx);
    lp.selectionText = fieldSelectionText(ctx);
    lp.caret = ctx.color("surface");
    lp.showSelection = true;
    lp.showCaret = focused() && lineEditor_.caretPhaseOn(ui().now(), animate);
    lineEditor_.paint(ctx, lp);
  } else if (!boundName_.empty()) {
    const theme::ResolvedStyle& ps = ctx.resolve("numberfield.pill", 0);
    TextOptions options;
    options.padLeft = kPillPad;
    options.padRight = kPillPad;
    ctx.drawText(boundName_, ps.text, valueBox, options);
  } else {
    const std::string shown = mixed() ? std::string("Mixed") : displayText();
    TextOptions options;
    options.tabular = true;
    ctx.drawText(shown, vs.text, valueBox, options);
  }

  if (!suffix_.empty()) {
    const theme::ResolvedStyle& ss = ctx.resolve("numberfield.suffix", 0);
    TextOptions options;
    options.ellipsis = false;
    ctx.drawText(suffix_, ss.text, {static_cast<float>(p.suffixX) * scale, lineTop, static_cast<float>(p.suffixWidth) * scale + 2.0f, lineHeight}, options);
  }

  const auto buttonColor = [&](int slot, Part part) {
    uint8_t bits = hoverPart_ == part ? theme::State::kHover : theme::State::kNone;
    if (hasState(StateFlag::kBound) && part == Part::Variable) bits |= theme::State::kBound;
    return ctx.animatedColor(slot, ctx.color(ctx.resolve("numberfield.button", bits).text.color));
  };
  if (!p.variable.empty()) {
    // A bound field shows the plain diamond (the reference drops the centre dot).
    ctx.drawIcon(boundName_.empty() ? "apply-variable" : "diamond", kVariableGlyph, ctx.toPhysical(p.variable.x, p.variable.y, p.variable.w, p.variable.h), buttonColor(2, Part::Variable));
  }
  if (!p.dropdown.empty()) {
    ctx.drawIcon("chevron-down", kDropdownGlyph, ctx.toPhysical(p.dropdown.x, p.dropdown.y, p.dropdown.w, p.dropdown.h), buttonColor(3, Part::Dropdown));
  }
}

}  // namespace r1ui::widgets

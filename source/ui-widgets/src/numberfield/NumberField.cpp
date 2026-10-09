// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of NumberField.h.
// Invariants: value_ always lies in the hard range and is an integer in integer mode; every callback
//   is followed by a liveness check (a handler may destroy the field); a gesture that called
//   onBeginInteraction always ends with onEndInteraction (commit, cancel, blur or capture loss); the
//   typed text is never applied when it equals the text edit mode started with, so focusing a field
//   and pressing Enter cannot round the stored value to the displayed digits.
// Callers: UiContext (event and paint dispatch), tests.
#include "r1ui/widgets/numberfield/NumberField.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/textinput/FieldChrome.h"

namespace r1ui::widgets {

namespace {

using core::events::Button;
using core::events::FocusReason;
using core::events::Key;
namespace Mod = core::events::Mod;

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
constexpr double kPixelsPerIncrement = 5.0;
constexpr double kScrubBasisWidth = 100.0;
constexpr int kEditDigits = 6;
constexpr double kHugeRange = 1.0e14;

bool isPlainTextKey(Key k, uint8_t mods) {
  if ((mods & Mod::kCtrl) != 0 && (mods & Mod::kAlt) == 0) return false;
  const auto v = static_cast<uint16_t>(k);
  return (v >= 'A' && v <= 'Z') || (v >= '0' && v <= '9') || k == Key::Space;
}

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

// ---- step arithmetic ---------------------------------------------------------------------------------

double NumberField::baseStep() const {
  double span = std::numeric_limits<double>::infinity();
  if (soft_) span = soft_->second - soft_->first;
  else if (max_ - min_ < kHugeRange) span = max_ - min_;
  return (integer_ || span > 10.0) ? 1.0 : 0.1;
}

double NumberField::stepMultiplier(uint8_t modifiers) {
  if ((modifiers & Mod::kCtrl) != 0) return 0.1;
  if ((modifiers & Mod::kShift) != 0) return 10.0;
  return 1.0;
}

double NumberField::scrubPerPixel(uint8_t modifiers) const {
  const double multiplier = stepMultiplier(modifiers);
  const double widthPx = std::max(kScrubBasisWidth, static_cast<double>(ui().absRect(id()).w));
  if (soft_) return baseStep() * multiplier * (soft_->second - soft_->first) / widthPx;
  if (increment_ > 0.0) return increment_ * multiplier / kPixelsPerIncrement;
  // No soft range: the change per pixel grows with the size of the value (one more unit per 100 units).
  return baseStep() * multiplier * std::max(1.0, std::fabs(value_) / 100.0);
}

// ---- change protocol -----------------------------------------------------------------------------------

bool NumberField::fireBegin() {
  if (onBegin_) {
    const auto callback = onBegin_;
    callback();
  }
  return ui().alive(id());
}

bool NumberField::fireChanged(double value, bool interactive) {
  if (onChanged_) {
    const auto callback = onChanged_;
    callback(value, interactive);
  }
  return ui().alive(id());
}

bool NumberField::fireEnd(bool cancelled, bool changed) {
  if (onEnd_) {
    const auto callback = onEnd_;
    callback(InteractionEnd{cancelled, changed});
  }
  return ui().alive(id());
}

bool NumberField::commitValue(double value) {
  if (!fireBegin()) return false;
  value_ = value;
  setMixed(false);
  requestPaint();
  if (!fireChanged(value, false)) return false;
  return fireEnd(false, true);
}

void NumberField::stepBy(double notches, uint8_t modifiers) {
  if (mixed() || !enabled() || !std::isfinite(notches)) return;
  const double step = (increment_ > 0.0 ? increment_ : baseStep()) * stepMultiplier(modifiers);
  double next = value_ + notches * step;
  // Soft range first, but never pull a value that lies beyond it back across the wrong direction.
  if (soft_) {
    if (notches > 0.0) next = std::min(next, std::max(soft_->second, value_));
    else next = std::max(next, std::min(soft_->first, value_));
  }
  next = normalise(next);
  if (next == value_) return;
  commitValue(next);
}

// ---- edit mode -------------------------------------------------------------------------------------------

void NumberField::enterEdit(bool selectAll) {
  if (editing_ || !enabled()) return;
  editing_ = true;
  editStart_ = mixed() ? std::string("Mixed") : formatNumber(value_, minDigits_, std::max(maxDigits_, kEditDigits));
  lineEditor_.setText(editStart_);
  if (selectAll) lineEditor_.model().selectAll();
  lineEditor_.noteActivity(ui().now());
  if (ui().animationsActive()) ui().invalidator().requestAnimation(id());
  requestPaint();
}

void NumberField::leaveEdit() {
  if (!editing_) return;
  editing_ = false;
  textDragging_ = false;
  lineEditor_.model().cancelPreedit();
  ui().invalidator().cancelAnimation(id());
  requestPaint();
}

void NumberField::refreshEditText() {
  editStart_ = formatNumber(value_, minDigits_, std::max(maxDigits_, kEditDigits));
  lineEditor_.setText(editStart_);
  lineEditor_.model().selectAll();
  lineEditor_.noteActivity(ui().now());
  requestPaint();
}

void NumberField::revertEdit() { leaveEdit(); }

void NumberField::commitEdit() {
  if (!editing_) return;
  const std::string typed = lineEditor_.text();
  leaveEdit();
  if (typed == editStart_) return;
  std::vector<NumberUnit> units = units_;
  if (!suffix_.empty()) units.push_back({suffix_, 1.0});
  NumberParseOptions options;
  options.units = units;
  options.allowMixedToken = mixed();
  const std::optional<NumberExpression> expression = parseNumberExpression(typed, options);
  if (!expression) return;  // rule 15: unparsable text changes nothing
  if (expression->usesMixed()) {
    if (!onMixedExpression_ || !fireBegin()) return;
    const auto callback = onMixedExpression_;
    callback(*expression);
    if (!ui().alive(id())) return;
    fireEnd(false, true);
    return;
  }
  const double typedValue = expression->evaluate();
  if (!std::isfinite(typedValue)) return;
  const double next = normalise(typedValue);
  if (!mixed() && next == value_) return;  // rule 16: no change, no undo step
  commitValue(next);
}

void NumberField::scrollEditIntoView() {
  const Parts p = parts();
  const theme::ResolvedStyle& rs = ui().services().resolve("numberfield.value", 0);
  if (lineEditor_.ensureLayout(static_cast<float>(rs.text.fontSize * ui().scale()))) {
    lineEditor_.scrollCaretIntoView(static_cast<float>((p.valueRight - p.valueLeft) * ui().scale()));
  }
}

void NumberField::applyEditEdit(const LineEdit& edit) {
  if (!edit.caretMoved) return;
  lineEditor_.noteActivity(ui().now());
  scrollEditIntoView();
  requestPaint();
}

// ---- scrub ---------------------------------------------------------------------------------------------------

void NumberField::beginScrub(const Event&) {
  scrubbing_ = true;
  scrubRaw_ = value_;
  startValue_ = value_;
  lastX_ = pressX_;
  requestPaint();
  fireBegin();
}

void NumberField::scrubTo(double x, uint8_t modifiers) {
  if (!std::isfinite(x)) return;
  lastModifiers_ = modifiers;
  const double delta = (x - lastX_) * scrubPerPixel(modifiers);
  lastX_ = x;
  double raw = scrubRaw_ + delta;
  const bool beyondSoft = (modifiers & (Mod::kCtrl | Mod::kShift)) != 0;
  if (soft_ && !beyondSoft) {
    if ((modifiers & Mod::kAlt) != 0) {
      // Alt widens the soft range as the pointer keeps moving (rule 29), up to the hard range.
      soft_->first = std::clamp(std::min(soft_->first, raw), min_, max_);
      soft_->second = std::clamp(std::max(soft_->second, raw), min_, max_);
    }
    raw = std::clamp(raw, soft_->first, soft_->second);
  }
  raw = std::clamp(raw, min_, max_);
  scrubRaw_ = raw;
  const double applied = normalise(raw);
  if (applied == value_) return;
  value_ = applied;
  requestPaint();
  fireChanged(applied, true);
}

void NumberField::endScrub() {
  if (!scrubbing_) return;
  scrubbing_ = false;
  pressed_ = false;
  const bool beyondSoft = (lastModifiers_ & (Mod::kCtrl | Mod::kShift)) != 0;
  if (increment_ > 0.0 && !beyondSoft) {
    const double snapped = normalise(std::round(value_ / increment_) * increment_);
    if (snapped != value_) {
      value_ = snapped;
      requestPaint();
      if (!fireChanged(snapped, false)) return;
    }
  }
  const bool changed = value_ != startValue_;
  const double px = pressX_;
  const double py = pressY_;
  if (!fireEnd(false, changed)) return;
  if (onRestorePointer_) {
    const auto callback = onRestorePointer_;
    callback(px, py);
  }
}

void NumberField::cancelScrub() {
  if (!scrubbing_) return;
  scrubbing_ = false;
  pressed_ = false;
  const bool restored = value_ != startValue_;
  value_ = startValue_;
  requestPaint();
  if (restored && !fireChanged(startValue_, false)) return;
  if (!fireEnd(true, false)) return;
  ui().router().cancelPointerInteraction();
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

// ---- pointer ------------------------------------------------------------------------------------------------------

void NumberField::onPointerDown(Event& e) {
  if (e.button != Button::Left || !enabled()) return;
  e.markHandled();
  const Part part = partAt(e.x, e.y);
  if (part != Part::None) {
    pressedPart_ = part;
    ui().router().capturePointer(id());
    requestPaint();
    return;
  }
  ui().router().focus(id(), FocusReason::Pointer);
  if (!ui().alive(id())) return;
  ui().router().capturePointer(id());
  if (editing_) {
    const Parts p = parts();
    const theme::ResolvedStyle& rs = ui().services().resolve("numberfield.value", 0);
    lineEditor_.ensureLayout(static_cast<float>(rs.text.fontSize * ui().scale()));
    textDragging_ = true;
    const int clicks = static_cast<int>(std::clamp<uint32_t>(e.clickCount, 1, 3));
    applyEditEdit(lineEditor_.pointerPress(static_cast<float>((e.x - p.valueLeft) * ui().scale()), clicks, (e.modifiers & Mod::kShift) != 0));
    return;
  }
  pressed_ = true;
  pressX_ = lastX_ = e.x;
  pressY_ = e.y;
  startValue_ = value_;
  lastModifiers_ = e.modifiers;
}

void NumberField::onDragStart(Event& e) {
  if (!pressed_ || editing_ || scrubbing_ || mixed() || !enabled()) return;
  e.markHandled();  // accepting the drag suppresses the click that would enter edit mode
  beginScrub(e);
  // The move that crossed the threshold may already have been delivered: apply its distance now.
  if (scrubbing_ && ui().alive(id())) scrubTo(e.x, e.modifiers);
}

void NumberField::onPointerMove(Event& e) {
  const Part hovered = partAt(e.x, e.y);
  if (hovered != hoverPart_ && !scrubbing_) {
    hoverPart_ = hovered;
    requestPaint();
  }
  if (scrubbing_) {
    scrubTo(e.x, e.modifiers);
  } else if (textDragging_ && (e.buttons & core::events::buttonBit(Button::Left)) != 0) {
    const Parts p = parts();
    applyEditEdit(lineEditor_.pointerDrag(static_cast<float>((e.x - p.valueLeft) * ui().scale())));
  }
}

void NumberField::onPointerUp(Event& e) {
  if (e.button != Button::Left) return;
  if (pressedPart_ != Part::None) {
    const Part part = pressedPart_;
    pressedPart_ = Part::None;
    requestPaint();
    if (partAt(e.x, e.y) != part) return;
    const std::function<void()> callback = part == Part::Variable ? onVariable_ : onDropdown_;
    if (callback) callback();
    return;
  }
  textDragging_ = false;
  if (scrubbing_) endScrub();
}

void NumberField::onPointerLeave(Event&) {
  if (hoverPart_ != Part::None) {
    hoverPart_ = Part::None;
    requestPaint();
  }
}

void NumberField::onClick(Event& e) {
  if (e.button != Button::Left || !pressed_ || scrubbing_ || editing_) return;
  pressed_ = false;
  enterEdit(true);
}

void NumberField::onCaptureLost(Event&) {
  textDragging_ = false;
  pressedPart_ = Part::None;
  if (scrubbing_) cancelScrub();  // a normal release ends the scrub in onPointerUp first
}

void NumberField::onPointerWheel(Event& e) {
  if (!enabled() || !focused() || mixed() || scrubbing_ || e.wheelY == 0.0) return;
  e.markHandled();
  e.stopPropagation();
  stepBy(e.wheelY, e.modifiers);
  if (editing_ && ui().alive(id())) refreshEditText();
}

// ---- keyboard and focus -------------------------------------------------------------------------------------------

void NumberField::onKeyDown(Event& e) {
  if (!enabled()) return;
  if (scrubbing_) {
    if (e.key == Key::Escape) {
      e.markHandled();
      cancelScrub();
    }
    return;
  }
  if (editing_) {
    switch (e.key) {
      case Key::Enter:
        e.markHandled();
        commitEdit();
        return;
      case Key::Escape:
        e.markHandled();
        revertEdit();
        return;
      case Key::Up:
      case Key::Down: {
        e.markHandled();
        if (mixed()) return;
        std::vector<NumberUnit> units = units_;
        if (!suffix_.empty()) units.push_back({suffix_, 1.0});
        NumberParseOptions options;
        options.units = units;
        const std::optional<double> typed = parseNumber(lineEditor_.text(), options);
        if (typed) value_ = normalise(*typed);  // a typed value is kept when stepping from it
        stepBy(e.key == Key::Up ? 1.0 : -1.0, e.modifiers);
        if (ui().alive(id())) refreshEditText();
        return;
      }
      default: break;
    }
    const LineEdit edit = lineEditor_.handleKeyDown(e);
    if (edit.handled) e.markHandled();
    applyEditEdit(edit);
    return;
  }
  switch (e.key) {
    case Key::Enter:
      e.markHandled();
      enterEdit(true);
      return;
    case Key::Up:
    case Key::Right:
      e.markHandled();
      stepBy(1.0, e.modifiers);
      return;
    case Key::Down:
    case Key::Left:
      e.markHandled();
      stepBy(-1.0, e.modifiers);
      return;
    default: break;
  }
  if (isPlainTextKey(e.key, e.modifiers)) e.markHandled();  // typing starts edit mode; shortcuts must not see it
}

void NumberField::onTextInput(Event& e) {
  if (!enabled() || scrubbing_) return;
  if (!editing_) {
    if ((e.modifiers & (Mod::kCtrl | Mod::kMeta)) != 0 && (e.modifiers & Mod::kAlt) == 0) return;
    enterEdit(false);
    lineEditor_.setText("");
  }
  const LineEdit edit = lineEditor_.handleChar(e.codePoint, e.modifiers);
  if (edit.handled) e.markHandled();
  applyEditEdit(edit);
}

void NumberField::onFocusIn(Event& e) {
  if (e.focusReason != FocusReason::Pointer) enterEdit(true);  // rule 11: Tab and program focus edit at once
}

void NumberField::onFocusOut(Event&) {
  pressed_ = false;
  pressedPart_ = Part::None;
  if (scrubbing_) cancelScrub();
  if (!ui().alive(id())) return;
  commitEdit();
}

}  // namespace r1ui::widgets

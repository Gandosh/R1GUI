// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of NumberField.h, part 2: the gestures. Step arithmetic, the change protocol
//   (begin / changed / end brackets), text edit mode, scrubbing, and the pointer, keyboard and focus
//   handlers. Layout, configuration and painting are in NumberField.cpp.
// Invariants: every callback is followed by a liveness check (a handler may destroy the field); a gesture
//   that called onBeginInteraction always ends with onEndInteraction (commit, cancel, blur or capture
//   loss); the typed text is never applied when it equals the text edit mode started with, so focusing a
//   field and pressing Enter cannot round the stored value to the displayed digits.
// Callers: UiContext (event dispatch), tests.
#include <algorithm>
#include <cmath>
#include <limits>

#include "r1ui/widgets/numberfield/NumberField.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

using core::events::Button;
using core::events::FocusReason;
using core::events::Key;
namespace Mod = core::events::Mod;

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

void NumberField::stepBy(double notches, uint8_t modifiers) { stepFrom(value_, notches, modifiers); }

void NumberField::stepFrom(double base, double notches, uint8_t modifiers) {
  if (mixed() || !enabled() || !std::isfinite(notches) || !std::isfinite(base)) return;
  const double step = (increment_ > 0.0 ? increment_ : baseStep()) * stepMultiplier(modifiers);
  double next = base + notches * step;
  // Soft range first, but never pull a value that lies beyond it back across the wrong direction.
  if (soft_) {
    if (notches > 0.0) next = std::min(next, std::max(soft_->second, base));
    else next = std::max(next, std::min(soft_->first, base));
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

void NumberField::settleScroll() {
  if (!scrollPending_) return;
  scrollPending_ = false;
  const Parts p = parts();
  const theme::ResolvedStyle& rs = ui().services().resolve("numberfield.value", 0);
  if (lineEditor_.ensureLayout(static_cast<float>(rs.text.fontSize * ui().scale()))) {
    lineEditor_.scrollCaretIntoView(static_cast<float>((p.valueRight - p.valueLeft) * ui().scale()));
  }
}

void NumberField::applyEditEdit(const LineEdit& edit) {
  if (!edit.caretMoved) return;
  lineEditor_.noteActivity(ui().now());
  scrollPending_ = true;  // settled at the next paint
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

// A field destroyed in the middle of a scrub ends the gesture the host was told about (cancelled: the
// value goes back to where the scrub began), so no undo transaction stays open.
void NumberField::onDetached() {
  if (scrubbing_) cancelScrub();
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
        // Stepping starts from the typed value, and the result goes through the change protocol (begin,
        // changed, end) like any other edit, so the host sees the typed value too.
        stepFrom(typed ? normalise(*typed) : value_, e.key == Key::Up ? 1.0 : -1.0, e.modifiers);
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

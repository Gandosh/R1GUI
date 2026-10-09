// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: NumberField, the properties-panel number field (docs/spec/widgets.md 2.4): a 26 px field with
//   an optional leading label or glyph, the value, an optional suffix, a "Mixed" display, a bound
//   (variable) display, an apply-variable button and a dropdown button; drag-scrubbing, click-to-edit,
//   arrow and wheel stepping, hard and soft ranges, integer mode, units and undo-grouping callbacks.
// Why: the property panel edits most values with this one control; its interaction is specified in
//   docs/spec/interaction/09-property-binding-undo.md (rules 10-42, 74-77) and must be identical for
//   every property. The widget owns no document: it reports changes through callbacks and the host
//   applies them (and calls setValue when the property changes elsewhere).
// Callers: application code (property panels), tests, GalleryFields. Calls: LineEditor (edit mode),
//   NumberParse (typed text), FieldChrome (rows, box), UiContext (focus, capture, frames).
// Modes and state machine (spec 09, "States and transitions"):
//   Idle (shows the value; ew-resize cursor) -> Pressed (left down, under the drag threshold) -> a release
//   enters Text editing (rule 10); movement past the threshold (Router DragStart, strictly more than
//   the larger of 1 mm and 5 px) enters Scrubbing (rule 21). Tab or program focus enters Text editing at
//   once (rule 11); Enter on a focused, idle field enters it (rule 20). Editing: Enter or blur commit
//   (rules 12-13), Escape reverts (14), unparsable text changes nothing (15), equal value changes nothing
//   (16), typed values are clamped to the hard range (17), rounded when integer (18), units converted (19).
//   While editing a drag selects text instead of scrubbing (a deliberate deviation from rule 21 that keeps
//   text selection usable).
// Scrubbing (rules 21-33): the value follows the pointer by `step x soft span` per max(100, width) px
//   (the step is 1 for a soft range wider than 10, else 0.1; Ctrl x0.1 fine, Shift x10 coarse, Ctrl wins);
//   without a soft range the change per pixel grows with the magnitude; a fixed increment moves one
//   increment per 5 px and snaps on release. Values clamp to the soft range unless Ctrl or Shift is held
//   (then only the hard range applies); Alt at the end of the soft range widens it. Escape restores the
//   pre-drag value. A mixed field cannot be scrubbed or stepped (rule 39).
// Undo grouping: every user change is bracketed by onBeginInteraction / onEndInteraction (a scrub is ONE
//   bracket with many interactive onValueChanged calls; a typed commit, an arrow or a wheel step is a
//   bracket with one call), so the host can open and close one undo step per bracket (rules 74-77).
//   InteractionEnd tells whether anything changed and whether the gesture was cancelled.
// Not implemented: hiding the pointer while scrubbing (the Cursor vocabulary has no hidden shape; the
//   host receives onRestorePointer with the press point to warp the pointer back, rule 33), tabular
//   numerals, non-linear scrub response (rule 25).
// Units: logical pixels. Programmatic changes (setValue, setRange, ...) never call the callbacks.
#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "r1ui/widgets/numberfield/NumberParse.h"
#include "r1ui/widgets/runtime/WidgetObject.h"
#include "r1ui/widgets/textinput/LineEditor.h"

namespace r1ui::widgets {

struct InteractionEnd {
  bool cancelled = false;  // the gesture was abandoned (Escape, capture loss); the value was restored
  bool changed = false;    // the value differs from the one at the start of the gesture
};

class NumberField : public WidgetObject {
 public:
  NumberField();

  static std::span<const theme::StyleRuleEntry> styleRows();
  const char* typeName() const override { return "NumberField"; }

  // ---- value and ranges (programmatic: no callbacks) ----
  double value() const { return value_; }
  // Non-finite input is ignored; the value is clamped to the hard range and rounded in integer mode.
  void setValue(double value);
  // Hard range: no value can leave it. NaN or min > max resets to unbounded.
  void setRange(double min, double max);
  // Soft range: what scrubbing and the slider-like response cover. Invalid input clears it.
  void setSoftRange(double min, double max);
  void clearSoftRange();
  // Fixed increment for arrows, wheel and scrubbing (0 = none). Non-finite or negative clears it.
  void setIncrement(double increment);
  void setInteger(bool integer);
  void setFractionDigits(int minDigits, int maxDigits);
  double minimum() const { return min_; }
  double maximum() const { return max_; }

  // ---- chrome ----
  void setLabel(std::string label);               // leading text such as "X"
  void setLeadingIcon(std::string iconName);      // leading glyph (Lucide or custom name)
  void setSuffix(std::string suffix);             // trailing muted text such as "%"
  void setUnits(std::vector<NumberUnit> units);   // suffixes accepted when typing, with factors
  // Shows the variable pill instead of the value and tints the field as bound; empty clears it.
  void setBoundVariable(std::string name);
  void setVariableButton(bool visible, std::string tooltip = "Apply variable");
  void setDropdownButton(bool visible);
  const std::string& boundVariable() const { return boundName_; }

  // ---- observation ----
  bool editing() const { return editing_; }
  bool scrubbing() const { return scrubbing_; }
  // The text shown for the current value without suffix (what edit mode starts with).
  std::string displayText() const;

  // ---- callbacks ----
  void setOnBeginInteraction(std::function<void()> callback) { onBegin_ = std::move(callback); }
  // `interactive` is true for the intermediate values of a scrub.
  void setOnValueChanged(std::function<void(double value, bool interactive)> callback) { onChanged_ = std::move(callback); }
  void setOnEndInteraction(std::function<void(const InteractionEnd&)> callback) { onEnd_ = std::move(callback); }
  // A mixed field was committed with an expression over "Mixed"; the host evaluates it per object.
  void setOnMixedExpression(std::function<void(const NumberExpression&)> callback) { onMixedExpression_ = std::move(callback); }
  void setOnVariableButton(std::function<void()> callback) { onVariable_ = std::move(callback); }
  void setOnDropdownButton(std::function<void()> callback) { onDropdown_ = std::move(callback); }
  // After a scrub the pointer should return to the press point (window logical coordinates).
  void setOnRestorePointer(std::function<void(double x, double y)> callback) { onRestorePointer_ = std::move(callback); }

  // ---- WidgetObject ----
  void onAttached() override;
  float paintOpacity() const override;
  bool wantsContinuousFrames() const override { return editing_ && focused(); }
  void paint(PaintContext& ctx) override;
  Cursor cursor() const override;
  std::string_view tooltipText() const override;
  void onStateChanged(uint16_t previous) override;
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onPointerWheel(Event& e) override;
  void onClick(Event& e) override;
  void onDragStart(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;
  void onTextInput(Event& e) override;
  void onFocusIn(Event& e) override;
  void onFocusOut(Event& e) override;

 private:
  enum class Part : uint8_t { None, Variable, Dropdown };
  struct Parts {
    double labelX = 0.0;
    double labelWidth = 0.0;
    double iconX = 0.0;
    double valueLeft = 0.0;
    double valueRight = 0.0;
    double suffixX = 0.0;
    double suffixWidth = 0.0;
    double lineTop = 0.0;
    core::layout::Rect variable;  // empty when the button is absent
    core::layout::Rect dropdown;
  };

  Parts parts() const;
  Part partAt(double x, double y) const;
  const theme::ResolvedStyle& fieldStyle() const;
  double baseStep() const;
  static double stepMultiplier(uint8_t modifiers);
  double normalise(double value) const;
  double scrubPerPixel(uint8_t modifiers) const;
  bool mixed() const { return hasState(StateFlag::kMixed); }

  // ---- change protocol ----
  bool fireBegin();
  bool fireChanged(double value, bool interactive);
  bool fireEnd(bool cancelled, bool changed);
  // One complete bracket for a discrete user change; false when the widget was destroyed.
  bool commitValue(double value);
  void stepBy(double notches, uint8_t modifiers);

  // ---- edit mode ----
  void enterEdit(bool selectAll);
  void leaveEdit();
  void commitEdit();
  void revertEdit();
  void applyEditEdit(const LineEdit& edit);
  void scrollEditIntoView();
  void refreshEditText();

  // ---- scrub ----
  void beginScrub(const Event& e);
  void scrubTo(double x, uint8_t modifiers);
  void endScrub();
  void cancelScrub();

  double value_ = 0.0;
  double min_ = -1.0e15;
  double max_ = 1.0e15;
  std::optional<std::pair<double, double>> soft_;
  double increment_ = 0.0;
  bool integer_ = false;
  int minDigits_ = 0;
  int maxDigits_ = 3;
  std::string label_;
  std::string icon_;
  std::string suffix_;
  std::vector<NumberUnit> units_;
  std::string boundName_;
  bool variableButton_ = false;
  std::string variableTooltip_;
  bool dropdownButton_ = false;

  LineEditor lineEditor_;
  bool editing_ = false;
  std::string editStart_;
  bool pressed_ = false;
  bool textDragging_ = false;
  bool scrubbing_ = false;
  Part pressedPart_ = Part::None;
  Part hoverPart_ = Part::None;
  double pressX_ = 0.0;
  double pressY_ = 0.0;
  double lastX_ = 0.0;
  double startValue_ = 0.0;
  double scrubRaw_ = 0.0;
  uint8_t lastModifiers_ = 0;

  std::function<void()> onBegin_;
  std::function<void(double, bool)> onChanged_;
  std::function<void(const InteractionEnd&)> onEnd_;
  std::function<void(const NumberExpression&)> onMixedExpression_;
  std::function<void()> onVariable_;
  std::function<void()> onDropdown_;
  std::function<void(double, double)> onRestorePointer_;
};

}  // namespace r1ui::widgets

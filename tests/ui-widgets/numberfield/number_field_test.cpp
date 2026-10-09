// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for NumberField against docs/spec/interaction/09-property-binding-undo.md: the
//   acceptance scenarios 1-6 and 10 (scrub 20 px = 20, Shift 200, Ctrl 2, typed 150 clamps to 100,
//   typed text reverted by Escape, mixed field with an expression, Escape during a scrub), the edit
//   mode rules (click, Tab, Enter, blur, Escape, unparsable text, equal value, clamp, integer rounding,
//   units, the untouched-text rule), arrow and wheel stepping, fixed increments and snapping, Alt
//   widening, undo-grouping callbacks (one begin / end per gesture), the variable and dropdown buttons,
//   and hostile cases: destroy inside every callback, disabled while scrubbing or editing, zero or tiny
//   width, NaN / infinite configuration, huge values, thousands of rapid moves.
// Callers: CTest (numberfield fast, no GPU; paint is checked on a recording Painter).
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "../textinput/FieldRig.h"
#include "r1ui/widgets/numberfield/NumberField.h"

namespace {

using namespace r1ui::widgets;
namespace events = r1ui::core::events;
namespace layout = r1ui::core::layout;
using events::Key;
namespace Mod = events::Mod;

constexpr double kX = 10.0;
constexpr double kY = 10.0;
constexpr double kMidY = kY + 13.0;

// Records the callbacks of a field in order.
struct Log {
  int begins = 0;
  int ends = 0;
  int changes = 0;
  int interactive = 0;
  double last = 0.0;
  InteractionEnd lastEnd;
  int mixedExpressions = 0;
  double restoreX = -1.0, restoreY = -1.0;
  std::vector<std::string> order;

  void attach(NumberField& f) {
    f.setOnBeginInteraction([this] { ++begins; order.push_back("begin"); });
    f.setOnValueChanged([this](double v, bool interactiveChange) {
      ++changes;
      if (interactiveChange) ++interactive;
      last = v;
      order.push_back(interactiveChange ? "live" : "value");
    });
    f.setOnEndInteraction([this](const InteractionEnd& e) { ++ends; lastEnd = e; order.push_back("end"); });
    f.setOnRestorePointer([this](double x, double y) { restoreX = x; restoreY = y; });
  }
};

NumberField& makeField(r1test::FieldRig& rig, double width = 100.0) {
  NumberField& f = rig.ui.create<NumberField>(rig.ui.root());
  f.style().width = layout::Length::px(width);
  rig.layout();
  return f;
}

// Press at the field's centre, move by `dx` in a few steps, release.
void drag(r1test::FieldRig& rig, double dx, uint8_t mods = 0, double startX = kX + 50.0) {
  rig.ui.pointerMove(startX, kMidY, mods);
  rig.ui.pointerDown(startX, kMidY, events::Button::Left, mods);
  const int steps = 4;
  for (int i = 1; i <= steps; ++i) rig.ui.pointerMove(startX + dx * i / steps, kMidY, mods);
  rig.ui.pointerUp(startX + dx, kMidY, events::Button::Left, mods);
}

void testScrubScenarios() {
  struct Case {
    uint8_t mods;
    double expected;
  };
  for (const Case& c : {Case{0, 20.0}, Case{Mod::kShift, 200.0}, Case{Mod::kCtrl, 2.0}}) {
    const uint8_t mods = c.mods;
    const double expected = c.expected;
    r1test::FieldRig rig;
    NumberField& f = makeField(rig);
    f.setRange(0, 1000);
    f.setSoftRange(0, 100);
    f.setValue(0);
    Log log;
    log.attach(f);
    drag(rig, 20.0, mods);
    R1_EXPECT_NEAR(f.value(), expected, 1e-9);
    R1_EXPECT(log.begins == 1 && log.ends == 1 && !log.lastEnd.cancelled && log.lastEnd.changed);
    R1_EXPECT(log.interactive >= 1);
    R1_EXPECT(log.order.front() == "begin" && log.order.back() == "end");
    R1_EXPECT(!f.editing() && !f.scrubbing());
    R1_EXPECT(std::fabs(log.restoreX - (kX + 50.0 + 20.0 * 0)) < 1e-9 && std::fabs(log.restoreY - kMidY) < 1e-9);  // press point
  }
}

void testScrubClampsToSoftRangeButNotWithModifiers() {
  r1test::FieldRig rig;
  NumberField& f = makeField(rig);
  f.setRange(-50, 500);
  f.setSoftRange(0, 100);
  f.setValue(90);
  drag(rig, 40.0);
  R1_EXPECT_NEAR(f.value(), 100.0, 1e-9);  // stops at the soft end
  drag(rig, 40.0, Mod::kCtrl | Mod::kShift);  // Ctrl wins: fine steps (x0.1), only the hard range applies
  R1_EXPECT_NEAR(f.value(), 104.0, 1e-9);
  f.setValue(0);
  drag(rig, -30.0);
  R1_EXPECT_NEAR(f.value(), 0.0, 1e-9);  // soft minimum
  drag(rig, -30.0, Mod::kShift);          // x10: -300, clamped to the hard minimum
  R1_EXPECT_NEAR(f.value(), -50.0, 1e-9);
  // A scrub that ends where it started is still one bracket but reports no change.
  f.setValue(40);
  Log log;
  log.attach(f);
  rig.ui.pointerMove(kX + 50, kMidY);
  rig.ui.pointerDown(kX + 50, kMidY);
  rig.ui.pointerMove(kX + 70, kMidY);
  rig.ui.pointerMove(kX + 50, kMidY);
  rig.ui.pointerUp(kX + 50, kMidY);
  R1_EXPECT(log.begins == 1 && log.ends == 1 && !log.lastEnd.changed && f.value() == 40.0);
}

void testDragThresholdIsStrict() {
  r1test::FieldRig rig;
  NumberField& f = makeField(rig);
  f.setRange(0, 1000);
  f.setSoftRange(0, 100);
  rig.ui.pointerMove(kX + 50, kMidY);
  rig.ui.pointerDown(kX + 50, kMidY);
  rig.ui.pointerMove(kX + 55, kMidY);  // exactly the threshold: still a press
  R1_EXPECT(!f.scrubbing());
  rig.ui.pointerUp(kX + 55, kMidY);
  R1_EXPECT(f.editing() && f.value() == 0.0);  // so the release is a click
  rig.key(Key::Escape);
  rig.ui.pointerMove(kX + 50, kMidY);
  rig.ui.pointerDown(kX + 50, kMidY);
  rig.ui.pointerMove(kX + 55.01, kMidY);  // strictly more: a scrub
  R1_EXPECT(f.scrubbing());
  rig.ui.pointerUp(kX + 55.01, kMidY);
  R1_EXPECT(!f.editing());
}

void testSmallRangeStep() {
  r1test::FieldRig rig;
  NumberField& f = makeField(rig);
  f.setRange(0, 10);
  f.setSoftRange(0, 10);  // range of 10 or less: the base step is 0.1
  f.setValue(0);
  drag(rig, 50.0);
  R1_EXPECT_NEAR(f.value(), 0.1 * 10.0 * 50.0 / 100.0, 1e-9);  // step x span per 100 px
}

void testFieldsNarrowerThan100BehaveAs100() {
  r1test::FieldRig rig;
  NumberField& f = makeField(rig, 40.0);
  f.setRange(0, 1000);
  f.setSoftRange(0, 100);
  f.setValue(0);
  drag(rig, 20.0, 0, kX + 10.0);
  R1_EXPECT_NEAR(f.value(), 20.0, 1e-9);
}

void testNoSoftRangeResponseGrowsWithValue() {
  r1test::FieldRig rig;
  NumberField& f = makeField(rig);
  f.setRange(-1e9, 1e9);
  f.setValue(0);
  drag(rig, 20.0);
  const double small = f.value();
  R1_EXPECT_NEAR(small, 20.0, 1e-9);  // magnitude below 100: one unit per pixel
  f.setValue(10000);
  drag(rig, 20.0);
  R1_EXPECT(f.value() - 10000 > 100.0 * 20.0 * 0.9);  // 100 units per pixel at 10000
}

void testFixedIncrementSnaps() {
  r1test::FieldRig rig;
  NumberField& f = makeField(rig);
  f.setRange(0, 1000);
  f.setIncrement(5.0);
  f.setValue(0);
  drag(rig, 13.0);  // 13 px / 5 px per increment x 5 = 13 -> snaps to 15
  R1_EXPECT_NEAR(std::fmod(f.value(), 5.0), 0.0, 1e-9);
  R1_EXPECT(f.value() >= 10.0 && f.value() <= 15.0);
  f.setValue(0);
  drag(rig, 13.0, Mod::kCtrl);  // unsnapped with a modifier
  R1_EXPECT(f.value() > 0.0 && std::fmod(f.value(), 5.0) != 0.0);
}

void testAltWidensSoftRange() {
  r1test::FieldRig rig;
  NumberField& f = makeField(rig);
  f.setRange(0, 1000);
  f.setSoftRange(0, 100);
  f.setValue(95);
  drag(rig, 50.0, Mod::kAlt);
  R1_EXPECT(f.value() > 100.0);
  drag(rig, -10.0);  // the widened range is the new soft range
  R1_EXPECT(f.value() <= 145.0 && f.value() > 95.0);
}

void testClickEntersEditAndTypedCommit() {
  r1test::FieldRig rig;
  NumberField& f = makeField(rig);
  f.setRange(0, 100);
  f.setValue(30);
  Log log;
  log.attach(f);
  rig.click(kX + 50, kMidY);  // press and release without moving: text edit mode (rule 10)
  R1_EXPECT(f.editing() && f.focused() && log.begins == 0);
  rig.type("150");
  R1_EXPECT(f.value() == 30.0 && log.changes == 0);  // typing alone changes nothing
  R1_EXPECT(rig.key(Key::Enter));
  R1_EXPECT(!f.editing() && f.value() == 100.0);  // clamped to the hard range (rule 17)
  R1_EXPECT(log.begins == 1 && log.changes == 1 && log.ends == 1 && log.lastEnd.changed && log.interactive == 0);
  // Typing the same value again changes nothing and creates no bracket (rule 16).
  rig.click(kX + 50, kMidY);
  rig.type("100");
  rig.key(Key::Enter);
  R1_EXPECT(log.begins == 1);
  // Unparsable text keeps the old value (rule 15).
  rig.click(kX + 50, kMidY);
  rig.ctrl('A');
  rig.type("abc");
  rig.key(Key::Enter);
  R1_EXPECT(f.value() == 100.0 && log.begins == 1 && !f.editing());
  // Escape reverts (rule 14, scenario 5).
  f.setValue(30);
  rig.click(kX + 50, kMidY);
  rig.type("7");
  R1_EXPECT(rig.key(Key::Escape));
  R1_EXPECT(!f.editing() && f.value() == 30.0 && log.begins == 1);
  // Blur commits (rule 13).
  rig.click(kX + 50, kMidY);
  rig.type("55");
  NumberField& other = makeField(rig);
  rig.ui.router().focus(other.id(), events::FocusReason::Pointer);
  R1_EXPECT(f.value() == 55.0 && log.begins == 2);
}

void testTabEntersEditAndNeverRoundsUntouchedText() {
  r1test::FieldRig rig;
  NumberField& f = makeField(rig);
  f.setValue(0.12345678);
  f.setFractionDigits(0, 2);
  R1_EXPECT(f.displayText() == "0.12");
  Log log;
  log.attach(f);
  rig.tab();  // keyboard focus enters edit mode with the text selected (rule 11)
  R1_EXPECT(f.focused() && f.editing());
  rig.key(Key::Enter);  // commits unchanged text: the stored value must not be rounded to the display digits
  R1_EXPECT(f.value() == 0.12345678 && log.begins == 0);
  R1_EXPECT(f.focused() && !f.editing());
  rig.key(Key::Enter);  // Enter on a focused, idle field enters edit mode (rule 20)
  R1_EXPECT(f.editing());
  rig.key(Key::Escape);
  R1_EXPECT(!f.editing() && f.focused());
}

void testIntegerUnitsAndSuffix() {
  r1test::FieldRig rig;
  NumberField& f = makeField(rig);
  f.setInteger(true);
  f.setSuffix("%");
  f.setUnits({{"cm", 0.01}});
  f.setRange(0, 1000);
  f.setValue(10.6);
  R1_EXPECT(f.value() == 11.0);  // integer fields round
  rig.click(kX + 50, kMidY);
  rig.type("12.7");
  rig.key(Key::Enter);
  R1_EXPECT(f.value() == 13.0);  // rule 18
  rig.click(kX + 50, kMidY);
  rig.ctrl('A');
  rig.type("50%");
  rig.key(Key::Enter);
  R1_EXPECT(f.value() == 50.0);  // the display suffix is accepted when typed
  f.setInteger(false);
  rig.click(kX + 50, kMidY);
  rig.ctrl('A');
  rig.type("250cm");
  rig.key(Key::Enter);
  R1_EXPECT_NEAR(f.value(), 2.5, 1e-9);  // converted to the stored unit (rule 19)
  rig.click(kX + 50, kMidY);
  rig.ctrl('A');
  rig.type("2+3*4");
  rig.key(Key::Enter);
  R1_EXPECT_NEAR(f.value(), 14.0, 1e-9);
}

void testMixedField() {
  r1test::FieldRig rig;
  NumberField& f = makeField(rig);
  f.setRange(0, 1000);
  f.setSoftRange(0, 100);
  f.setValue(5);
  f.setMixed(true);
  Log log;
  log.attach(f);
  std::vector<double> evaluated;
  f.setOnMixedExpression([&](const NumberExpression& e) {
    for (const double v : {1.0, 2.0, 3.0}) evaluated.push_back(e.evaluate(v));
  });
  drag(rig, 30.0);  // scrubbing is disabled: the release is a click, which edits (rule 39)
  R1_EXPECT(f.value() == 5.0 && log.begins == 0);
  R1_EXPECT(f.editing());
  rig.ctrl('A');
  rig.type("Mixed+10");  // the placeholder followed by an expression (rule 41)
  rig.key(Key::Enter);
  R1_EXPECT(evaluated.size() == 3 && evaluated[0] == 11.0 && evaluated[1] == 12.0 && evaluated[2] == 13.0);
  R1_EXPECT(log.begins == 1 && log.ends == 1 && f.hasState(StateFlag::kMixed));
  // Arrows and the wheel do nothing on a mixed field.
  rig.key(Key::Up);
  R1_EXPECT(log.changes == 0);
  // A plain number applies to everything and clears the mixed state.
  rig.key(Key::Enter);
  rig.type("42");
  rig.key(Key::Enter);
  R1_EXPECT(log.changes == 1 && log.last == 42.0 && !f.hasState(StateFlag::kMixed));
}

void testEscapeCancelsScrub() {
  r1test::FieldRig rig;
  NumberField& f = makeField(rig);
  f.setRange(0, 1000);
  f.setSoftRange(0, 100);
  f.setValue(10);
  Log log;
  log.attach(f);
  rig.ui.pointerMove(kX + 50, kMidY);
  rig.ui.pointerDown(kX + 50, kMidY);
  rig.ui.pointerMove(kX + 80, kMidY);
  R1_EXPECT(f.scrubbing() && f.value() == 40.0);
  R1_EXPECT(rig.key(Key::Escape));
  R1_EXPECT(!f.scrubbing() && f.value() == 10.0 && log.last == 10.0);
  R1_EXPECT(log.ends == 1 && log.lastEnd.cancelled && !log.lastEnd.changed);
  rig.ui.pointerMove(kX + 90, kMidY);
  rig.ui.pointerUp(kX + 90, kMidY);  // the late release does nothing (no click, no second bracket)
  R1_EXPECT(f.value() == 10.0 && log.ends == 1 && !f.editing());
}

void testArrowsAndWheel() {
  r1test::FieldRig rig;
  NumberField& f = makeField(rig);
  f.setRange(0, 100);
  f.setSoftRange(0, 100);
  f.setValue(50);
  Log log;
  log.attach(f);
  rig.ui.router().focus(f.id(), events::FocusReason::Pointer);  // focused, not editing
  R1_EXPECT(!f.editing());
  rig.key(Key::Up);
  R1_EXPECT(f.value() == 51.0);
  rig.key(Key::Right);
  R1_EXPECT(f.value() == 52.0);
  rig.key(Key::Down, Mod::kShift);
  R1_EXPECT(f.value() == 42.0);
  rig.key(Key::Up, Mod::kCtrl);
  R1_EXPECT_NEAR(f.value(), 42.1, 1e-9);
  rig.key(Key::Left, Mod::kCtrl | Mod::kShift);  // Ctrl wins
  R1_EXPECT_NEAR(f.value(), 42.0, 1e-9);
  R1_EXPECT(log.begins == 5 && log.ends == 5 && log.changes == 5);  // every step is its own bracket
  // Wheel: one step per notch, up increases; only with keyboard focus.
  R1_EXPECT(rig.ui.wheel(kX + 20, kMidY, 0, 2.0));
  R1_EXPECT_NEAR(f.value(), 44.0, 1e-9);
  R1_EXPECT(rig.ui.wheel(kX + 20, kMidY, 0, -1.0, Mod::kShift));
  R1_EXPECT_NEAR(f.value(), 34.0, 1e-9);
  // Clamped at the end: no callback, no bracket.
  f.setValue(100);
  const int before = log.begins;
  rig.key(Key::Up);
  R1_EXPECT(f.value() == 100.0 && log.begins == before);
  rig.ui.router().clearFocus();
  R1_EXPECT(!rig.ui.wheel(kX + 20, kMidY, 0, 1.0));  // without focus the wheel scrolls the panel instead
  R1_EXPECT(f.value() == 100.0);
  // A small range steps by 0.1.
  f.setSoftRange(0, 5);
  f.setRange(0, 5);
  f.setValue(1);
  rig.ui.router().focus(f.id(), events::FocusReason::Pointer);
  rig.key(Key::Up);
  R1_EXPECT_NEAR(f.value(), 1.1, 1e-9);
  // A fixed increment replaces the base step.
  f.setIncrement(0.25);
  rig.key(Key::Up);
  R1_EXPECT_NEAR(f.value(), 1.35, 1e-9);
}

void testTypingStartsEditing() {
  r1test::FieldRig rig;
  NumberField& f = makeField(rig);
  f.setValue(7);
  rig.ui.router().focus(f.id(), events::FocusReason::Pointer);
  R1_EXPECT(rig.key(static_cast<Key>('5')));  // the digit key must not reach shortcuts
  rig.type("5");
  R1_EXPECT(f.editing());
  rig.type("0");
  rig.key(Key::Enter);
  R1_EXPECT(f.value() == 50.0);
}

void testEditModeDragSelectsText() {
  r1test::FieldRig rig;
  NumberField& f = makeField(rig, 200.0);
  f.setLabel("W");
  f.setValue(12345);
  Log log;
  log.attach(f);
  rig.click(kX + 100, kMidY);
  R1_EXPECT(f.editing());
  rig.ui.pointerMove(kX + 60, kMidY);
  rig.ui.pointerDown(kX + 60, kMidY);
  rig.ui.pointerMove(kX + 100, kMidY);
  rig.ui.pointerUp(kX + 100, kMidY);
  R1_EXPECT(f.editing() && !f.scrubbing() && f.value() == 12345.0 && log.begins == 0);
}

void testButtons() {
  r1test::FieldRig rig;
  NumberField& f = makeField(rig, 114.0);
  f.setValue(1);
  f.setVariableButton(true, "Apply variable");
  f.setDropdownButton(true);
  int variable = 0, dropdown = 0;
  f.setOnVariableButton([&] { ++variable; });
  f.setOnDropdownButton([&] { ++dropdown; });
  const layout::Rect box = rig.ui.absRect(f.id());
  const double dropdownX = box.right() - 5.0 - 8.0;
  const double variableX = box.right() - 5.0 - 16.0 - 4.0 - 10.0;
  rig.ui.pointerMove(variableX, kMidY);
  R1_EXPECT(f.cursor() == Cursor::Pointer && f.tooltipText() == "Apply variable");
  rig.click(variableX, kMidY);
  R1_EXPECT(variable == 1 && !f.editing() && !f.focused());  // a button press never edits or focuses
  rig.click(dropdownX, kMidY);
  R1_EXPECT(dropdown == 1);
  // Pressing a button and releasing elsewhere does not activate it.
  rig.ui.pointerMove(variableX, kMidY);
  rig.ui.pointerDown(variableX, kMidY);
  rig.ui.pointerMove(box.x + 20.0, kMidY);
  rig.ui.pointerUp(box.x + 20.0, kMidY);
  R1_EXPECT(variable == 1);
  rig.ui.pointerMove(box.x + 20.0, kMidY);
  R1_EXPECT(f.cursor() == Cursor::ResizeHorizontal);
  // Bound state: the pill replaces the value and the field reports bound.
  f.setBoundVariable("Spacing/Large");
  R1_EXPECT(f.hasState(StateFlag::kBound) && f.boundVariable() == "Spacing/Large");
  R1_EXPECT(rig.paint() > 0);
  f.setBoundVariable("");
  R1_EXPECT(!f.hasState(StateFlag::kBound));
}

void testDestroyInsideCallbacks() {
  for (int which = 0; which < 3; ++which) {
    r1test::FieldRig rig;
    NumberField& f = makeField(rig);
    f.setRange(0, 1000);
    f.setSoftRange(0, 100);
    const auto id = f.id();
    if (which == 0) f.setOnBeginInteraction([&] { rig.ui.destroy(id); });
    if (which == 1) f.setOnValueChanged([&](double, bool) { rig.ui.destroy(id); });
    if (which == 2) f.setOnEndInteraction([&](const InteractionEnd&) { rig.ui.destroy(id); });
    drag(rig, 30.0);
    R1_EXPECT(!rig.ui.alive(id));
    rig.key(Key::Enter);
    rig.ui.pointerMove(kX + 20, kMidY);
    (void)rig.paint();
  }
  // Destroyed from the typed commit and from the arrow step.
  r1test::FieldRig rig;
  NumberField& f = makeField(rig);
  const auto id = f.id();
  f.setOnValueChanged([&](double, bool) { rig.ui.destroy(id); });
  rig.click(kX + 50, kMidY);
  rig.type("9");
  rig.key(Key::Enter);
  R1_EXPECT(!rig.ui.alive(id));
}

void testDisabledMidGesture() {
  {
    r1test::FieldRig rig;
    NumberField& f = makeField(rig);
    f.setRange(0, 1000);
    f.setSoftRange(0, 100);
    f.setValue(10);
    Log log;
    log.attach(f);
    rig.ui.pointerMove(kX + 50, kMidY);
    rig.ui.pointerDown(kX + 50, kMidY);
    rig.ui.pointerMove(kX + 80, kMidY);
    R1_EXPECT(f.scrubbing());
    f.setEnabled(false);  // the router releases capture and focus: the scrub is cancelled and restored
    R1_EXPECT(!f.scrubbing() && f.value() == 10.0 && log.begins == 1 && log.ends == 1 && log.lastEnd.cancelled);
    rig.ui.pointerUp(kX + 80, kMidY);
    R1_EXPECT(f.value() == 10.0);
  }
  {
    r1test::FieldRig rig;
    NumberField& f = makeField(rig);
    f.setValue(10);
    Log log;
    log.attach(f);
    rig.tab();
    rig.type("77");
    f.setEnabled(false);  // blur commits the typed text
    R1_EXPECT(!f.editing() && f.value() == 77.0 && log.begins == 1 && log.ends == 1);
    rig.type("1");
    rig.key(Key::Up);
    R1_EXPECT(f.value() == 77.0);
    R1_EXPECT(rig.paint() > 0);
  }
}

void testHostileConfiguration() {
  r1test::FieldRig rig;
  NumberField& f = makeField(rig, 0.0);
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  f.setValue(5);
  f.setValue(nan);
  f.setValue(inf);
  f.setValue(-inf);
  R1_EXPECT(f.value() == 5.0);
  f.setRange(nan, 10);
  R1_EXPECT(f.minimum() < -1e14 && f.maximum() > 1e14);
  f.setRange(10, 1);
  R1_EXPECT(f.minimum() < -1e14);
  f.setSoftRange(nan, 1);
  f.setSoftRange(5, 5);
  f.setSoftRange(7, 3);
  f.setIncrement(nan);
  f.setIncrement(-3);
  f.setIncrement(inf);
  f.setFractionDigits(-5, 99);
  f.setValue(1e300);
  R1_EXPECT(std::isfinite(f.value()) && f.value() <= f.maximum());
  f.setValue(-1e300);
  R1_EXPECT(std::isfinite(f.value()) && f.value() >= f.minimum());
  R1_EXPECT(!f.displayText().empty());
  // Zero and tiny widths: scrubbing and painting stay finite.
  for (const double width : {0.0, 1.0, 10.0, 30.0}) {
    NumberField& g = makeField(rig, width);
    g.setLabel("Label wider than the field");
    g.setSuffix("percent");
    g.setVariableButton(true);
    g.setDropdownButton(true);
    g.setSoftRange(0, 100);
    g.setValue(50);
    g.setBoundVariable("a very long variable name");
    g.setBoundVariable("");
    drag(rig, 25.0);
    rig.tab();
    rig.type("12");
    rig.key(Key::Enter);
    R1_EXPECT(std::isfinite(g.value()));
    (void)rig.paint();
  }
}

void testRapidInput() {
  r1test::FieldRig rig;
  NumberField& f = makeField(rig);
  f.setRange(-1e12, 1e12);
  f.setValue(0);
  Log log;
  log.attach(f);
  rig.ui.pointerMove(kX + 50, kMidY);
  rig.ui.pointerDown(kX + 50, kMidY);
  for (int i = 0; i < 20000; ++i) rig.ui.pointerMove(kX + 50 + (i % 400) - 200 + (i % 7), kMidY, i % 3 == 0 ? Mod::kShift : 0);
  rig.ui.pointerUp(kX + 50, kMidY);
  R1_EXPECT(std::isfinite(f.value()) && log.begins == 1 && log.ends == 1);
  for (int i = 0; i < 2000; ++i) {
    rig.ui.router().focus(f.id(), events::FocusReason::Pointer);
    rig.key(i % 2 == 0 ? Key::Up : Key::Down);
    rig.ui.wheel(kX + 5, kMidY, 0, i % 5 - 2.0);
  }
  R1_EXPECT(std::isfinite(f.value()));
  (void)rig.paint();
}

void testPaintStatesAndScales() {
  for (const float scale : {1.0f, 1.5f, 2.0f}) {
    r1test::FieldRig rig(400, 300, scale);
    NumberField& f = makeField(rig, 114.0);
    f.setLabel("W");
    f.setSuffix("%");
    f.setValue(300);
    f.setVariableButton(true);
    f.setDropdownButton(true);
    R1_EXPECT(rig.paint() > 0);
    f.setMixed(true);
    R1_EXPECT(rig.paint() > 0);
    f.setMixed(false);
    f.setLeadingIcon("blend");
    rig.ui.pointerMove(kX + 20, kMidY);
    R1_EXPECT(rig.paint() > 0);
    rig.tab();
    R1_EXPECT(f.editing() && rig.paint() > 0);
    f.setEnabled(false);
    R1_EXPECT(rig.paint() > 0);
  }
}

}  // namespace

int main() {
  testScrubScenarios();
  testScrubClampsToSoftRangeButNotWithModifiers();
  testDragThresholdIsStrict();
  testSmallRangeStep();
  testFieldsNarrowerThan100BehaveAs100();
  testNoSoftRangeResponseGrowsWithValue();
  testFixedIncrementSnaps();
  testAltWidensSoftRange();
  testClickEntersEditAndTypedCommit();
  testTabEntersEditAndNeverRoundsUntouchedText();
  testIntegerUnitsAndSuffix();
  testMixedField();
  testEscapeCancelsScrub();
  testArrowsAndWheel();
  testTypingStartsEditing();
  testEditModeDragSelectsText();
  testButtons();
  testDestroyInsideCallbacks();
  testDisabledMidGesture();
  testHostileConfiguration();
  testRapidInput();
  testPaintStatesAndScales();
  return r1test::finish();
}

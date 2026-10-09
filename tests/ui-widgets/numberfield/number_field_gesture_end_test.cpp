// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: regression tests for NumberField gestures that must always close their change bracket
//   (phase 4 review M8 and M14): a field destroyed in the middle of a scrub emits onEnd, and Up / Down
//   in edit mode commit the typed value through begin / changed / end instead of assigning it silently.
// Callers: CTest (fast tier). Calls: NumberField through the field test rig.
#include "../textinput/FieldRig.h"
#include "r1ui/widgets/numberfield/NumberField.h"

using namespace r1ui::widgets;
namespace events = r1ui::core::events;
namespace layout = r1ui::core::layout;
using events::Key;

namespace {

constexpr double kX = 10.0;
constexpr double kMidY = 10.0 + 13.0;

struct Counts {
  int begins = 0, ends = 0, changes = 0;
  double last = 0.0;
  InteractionEnd lastEnd;
  void attach(NumberField& f) {
    f.setOnBeginInteraction([this] { ++begins; });
    f.setOnValueChanged([this](double v, bool) {
      ++changes;
      last = v;
    });
    f.setOnEndInteraction([this](const InteractionEnd& e) {
      ++ends;
      lastEnd = e;
    });
  }
};

NumberField& makeField(r1test::FieldRig& rig) {
  NumberField& f = rig.ui.create<NumberField>(rig.ui.root());
  f.style().width = layout::Length::px(100);
  rig.layout();
  return f;
}

}  // namespace

int main() {
  {  // M8: the field is destroyed while the user scrubs it
    r1test::FieldRig rig;
    NumberField& f = makeField(rig);
    f.setRange(0, 1000);
    f.setSoftRange(0, 100);
    f.setValue(10);
    Counts c;
    c.attach(f);
    const auto id = f.id();
    rig.ui.pointerMove(kX + 50, kMidY);
    rig.ui.pointerDown(kX + 50, kMidY);
    rig.ui.pointerMove(kX + 80, kMidY);
    R1_EXPECT(f.scrubbing() && c.begins == 1 && c.ends == 0);
    rig.ui.destroy(id);
    R1_EXPECT(c.begins == 1 && c.ends == 1 && c.lastEnd.cancelled);
    rig.ui.pointerUp(kX + 80, kMidY);
    R1_EXPECT(c.ends == 1);
  }
  {  // M14: a typed value followed by Up commits the stepped value through the protocol
    r1test::FieldRig rig;
    NumberField& f = makeField(rig);
    f.setRange(3, 100);
    f.setValue(10);
    Counts c;
    c.attach(f);
    rig.ui.router().focus(f.id(), events::FocusReason::Keyboard);  // edit mode, everything selected
    R1_EXPECT(f.editing());
    rig.type("50");
    rig.key(Key::Up);
    R1_EXPECT(f.value() == 51.0);
    R1_EXPECT(c.begins == 1 && c.ends == 1 && c.changes == 1 && c.last == 51.0);
    R1_EXPECT(f.editing());  // the edit goes on with the new text
  }
  {  // M14: clamping at the range end still reports the value that was typed
    r1test::FieldRig rig;
    NumberField& f = makeField(rig);
    f.setRange(3, 100);
    f.setValue(10);
    Counts c;
    c.attach(f);
    rig.ui.router().focus(f.id(), events::FocusReason::Keyboard);
    rig.type("3");
    rig.key(Key::Down);  // typed 3, one step down clamps to 3: different from the stored 10
    R1_EXPECT(f.value() == 3.0);
    R1_EXPECT(c.begins == 1 && c.ends == 1 && c.last == 3.0);
  }
  return r1test::finish();
}

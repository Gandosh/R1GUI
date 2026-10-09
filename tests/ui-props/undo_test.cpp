// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of UndoStack through PropertyContext: transactions, grouping of repeated edits inside a
//   time window with a fake clock, redundant-step removal, redo truncation, labelled steps, the memory
//   budget with oldest-first eviction, listeners, the text-undo hook, rollback when a host setter
//   refuses or throws (during an edit and during undo), and forgetting destroyed objects.
// Callers: CTest (fast tier, no GPU).
#include <string>
#include <vector>

#include "Samples.h"
#include "TestSupport.h"
#include "r1ui/props/PropertyContext.h"

namespace {

using namespace r1ui::props;
using namespace samples;

size_t row(const PropertyContext& c, const char* name) { return c.findRow(name).value_or(static_cast<size_t>(-1)); }

struct Rig {
  ManualClock clock;
  Light light;
  PropertyContext context;
  size_t intensity = 0;
  explicit Rig(UndoOptions undo = {}) : context(makeOptions(undo, clock)) {
    const Target t[] = {targetOf(light, lightSet())};
    context.setSelection(t);
    intensity = row(context, "intensity");
  }
  static ContextOptions makeOptions(UndoOptions undo, const Clock& clock) {
    undo.clock = &clock;
    ContextOptions o;
    o.ownedUndo = undo;
    return o;
  }
  UndoStack& undo() { return context.undo(); }
};

void grouping() {
  Rig rig;
  const EditOptions step{.mergeable = true};
  // Repeated wheel-style steps on one property inside the window are one step.
  rig.context.setValue(rig.intensity, Value(101.0), step);
  rig.clock.advance(100);
  rig.context.setValue(rig.intensity, Value(102.0), step);
  rig.clock.advance(100);
  rig.context.setValue(rig.intensity, Value(103.0), step);
  R1_EXPECT(rig.undo().undoCount() == 1 && rig.light.intensity() == 103.0);
  R1_EXPECT(rig.undo().undo().done && rig.light.intensity() == 100.0);  // back to the value before the first notch
  R1_EXPECT(rig.undo().redo().done && rig.light.intensity() == 103.0);

  // Outside the window a new step starts.
  rig.clock.advance(rig.undo().groupWindowMs() + 1);
  rig.context.setValue(rig.intensity, Value(110.0), step);
  R1_EXPECT(rig.undo().undoCount() == 2);

  // A different property, a non-mergeable edit, or a different key set breaks the group.
  rig.clock.advance(10);
  rig.context.setValue(row(rig.context, "radius"), Value(2.0), step);
  R1_EXPECT(rig.undo().undoCount() == 3);
  rig.clock.advance(10);
  rig.context.setValue(row(rig.context, "radius"), Value(3.0));  // typed: never merges
  R1_EXPECT(rig.undo().undoCount() == 4);
  rig.clock.advance(10);
  rig.context.setValue(row(rig.context, "radius"), Value(4.0), step);  // previous step is not mergeable
  R1_EXPECT(rig.undo().undoCount() == 5);
}

void gestureGrouping() {
  // A one-notch gesture ended with mergeable = true groups like a mergeable edit; a scrub-style one does not.
  Rig rig;
  const auto notch = [&](double value, bool mergeable) {
    rig.context.beginInteraction(rig.intensity);
    rig.context.setValue(rig.intensity, Value(value));
    rig.context.endInteraction(mergeable);
  };
  notch(101.0, true);
  rig.clock.advance(100);
  notch(102.0, true);
  R1_EXPECT(rig.undo().undoCount() == 1);
  rig.clock.advance(100);
  notch(103.0, false);  // not mergeable: a step of its own
  R1_EXPECT(rig.undo().undoCount() == 2);
  rig.clock.advance(100);
  notch(104.0, true);  // the previous step is not mergeable
  R1_EXPECT(rig.undo().undoCount() == 3);
  rig.undo().undo();
  R1_EXPECT(rig.light.intensity() == 103.0);
}

void groupingWindowTuning() {
  UndoOptions o;
  o.groupWindowMs = 0;
  Rig off(o);
  const EditOptions step{.mergeable = true};
  off.context.setValue(off.intensity, Value(101.0), step);
  off.context.setValue(off.intensity, Value(102.0), step);
  R1_EXPECT(off.undo().undoCount() == 2);  // window 0: grouping is off

  Rig wide;
  wide.undo().setGroupWindowMs(5000);
  wide.context.setValue(wide.intensity, Value(101.0), step);
  wide.clock.advance(4000);
  wide.context.setValue(wide.intensity, Value(102.0), step);
  R1_EXPECT(wide.undo().undoCount() == 1);
  wide.undo().setGroupWindowMs(-5);
  R1_EXPECT(wide.undo().groupWindowMs() == 0);
}

void redundantMerge() {
  Rig rig;
  const EditOptions step{.mergeable = true};
  rig.context.setValue(rig.intensity, Value(150.0), step);
  rig.clock.advance(10);
  rig.context.setValue(rig.intensity, Value(100.0), step);
  // 100 -> 150 -> 100 inside the window: the merged step has no net effect and is removed.
  R1_EXPECT(rig.undo().undoCount() == 0 && rig.light.intensity() == 100.0);
}

void redoTruncation() {
  Rig rig;
  rig.context.setValue(rig.intensity, Value(1.0));
  rig.context.setValue(rig.intensity, Value(2.0));
  rig.context.setValue(rig.intensity, Value(3.0));
  rig.undo().undo();
  rig.undo().undo();
  R1_EXPECT(rig.undo().undoCount() == 1 && rig.undo().redoCount() == 2 && rig.undo().redoLabel() == "Set Intensity");
  rig.context.setValue(rig.intensity, Value(2.0));  // the current value is 1.0, so this is a real edit
  R1_EXPECT(rig.undo().redoCount() == 0);           // a new edit discards the redo history (rule 79)
  R1_EXPECT(!rig.undo().redo().done);

  Rig noop;
  noop.context.setValue(noop.intensity, Value(5.0));
  noop.undo().undo();
  noop.context.setValue(noop.intensity, Value(100.0));  // equals the current value: nothing recorded
  R1_EXPECT(noop.undo().redoCount() == 1);
  R1_EXPECT(noop.undo().undoLabels().empty());
}

void labels() {
  Rig rig;
  rig.context.setValue(rig.intensity, Value(1.0));
  rig.context.setValue(row(rig.context, "castShadows"), Value(false));
  rig.context.reset(rig.intensity);
  const auto names = rig.undo().undoLabels();
  R1_EXPECT(names.size() == 3 && names[0] == "Reset Intensity" && names[1] == "Set Cast shadows" && names[2] == "Set Intensity");
  R1_EXPECT(rig.undo().undoLabels(2).size() == 2);
  ContextStrings custom;
  custom.set = "Edit ";
  ContextOptions options;
  options.strings = custom;
  PropertyContext c(options);
  Light light;
  const Target t[] = {targetOf(light, lightSet())};
  c.setSelection(t);
  c.setValue(row(c, "intensity"), Value(2.0));
  R1_EXPECT(c.undo().undoLabel() == "Edit Intensity");  // the host owns the wording
}

void budget() {
  UndoOptions o;
  o.budgetBytes = 20000;
  Rig rig(o);
  int evicted = 0;
  rig.undo().addListener([&](const UndoEvent& e) { evicted += e.kind == UndoEventKind::Evicted; });
  const size_t name = row(rig.context, "name");
  for (int i = 0; i < 100; ++i) rig.context.setValue(name, Value(std::string(500, static_cast<char>('a' + i % 26)) + std::to_string(i)));
  R1_EXPECT(rig.undo().usedBytes() <= rig.undo().budgetBytes());
  R1_EXPECT(rig.undo().undoCount() < 100 && rig.undo().undoCount() > 5 && evicted > 0);
  // The oldest steps went first: undoing everything that is left lands on a recent value, not the original.
  while (rig.undo().undo().done) {
  }
  R1_EXPECT(rig.light.name != "Light" && !rig.undo().canUndo());

  // Shrinking the budget evicts immediately; zero keeps no history.
  Rig small;
  for (int i = 0; i < 10; ++i) small.context.setValue(small.intensity, Value(1.0 + i));
  const size_t used = small.undo().usedBytes();
  small.undo().setBudgetBytes(used / 2);
  R1_EXPECT(small.undo().undoCount() < 10 && small.undo().usedBytes() <= used / 2);
  small.undo().setBudgetBytes(0);
  R1_EXPECT(small.undo().undoCount() == 0 && small.undo().usedBytes() == 0);
  small.context.setValue(small.intensity, Value(99.0));  // a step larger than the budget is dropped, not kept
  R1_EXPECT(small.undo().undoCount() == 0 && small.light.intensity() == 99.0);

  UndoOptions defaults;
  R1_EXPECT(defaults.budgetBytes == size_t{256} << 20);
}

void listeners() {
  Rig rig;
  std::vector<UndoEventKind> seen;
  const auto id = rig.undo().addListener([&](const UndoEvent& e) {
    seen.push_back(e.kind);
    if (e.kind == UndoEventKind::Undone) {
      R1_EXPECT(!rig.undo().undo().done);  // mutating from a listener is refused
      R1_EXPECT(!rig.undo().begin("x"));
    }
  });
  rig.context.setValue(rig.intensity, Value(1.0));
  rig.undo().undo();
  rig.undo().redo();
  rig.undo().clear();
  R1_EXPECT(seen.size() == 4 && seen[0] == UndoEventKind::Committed && seen[1] == UndoEventKind::Undone && seen[2] == UndoEventKind::Redone && seen[3] == UndoEventKind::Cleared);
  rig.undo().removeListener(id);
  rig.context.setValue(rig.intensity, Value(2.0));
  R1_EXPECT(seen.size() == 4);

  // Cancel notifies too.
  rig.undo().addListener([&](const UndoEvent& e) { seen.push_back(e.kind); });
  rig.context.beginInteraction(rig.intensity);
  rig.context.setValue(rig.intensity, Value(50.0));
  rig.context.cancelInteraction();
  R1_EXPECT(seen.back() == UndoEventKind::Cancelled);
}

class FakeText final : public TextUndoHook {
 public:
  int undos = 0, redos = 0;
  bool pending = true;
  bool canUndoText() const override { return pending; }
  bool undoText() override {
    ++undos;
    pending = false;
    return true;
  }
  bool canRedoText() const override { return !pending && redos == 0; }
  bool redoText() override {
    ++redos;
    return true;
  }
};

void textHook() {
  Rig rig;
  rig.context.setValue(rig.intensity, Value(5.0));
  FakeText text;
  rig.undo().setTextHook(&text);
  // Spec 09 rule 83: the focused text field undoes its own typing first; then the document history.
  R1_EXPECT(rig.undo().undoRouted() == UndoRoute::Text && text.undos == 1 && rig.light.intensity() == 5.0);
  R1_EXPECT(rig.undo().undoRouted() == UndoRoute::Document && rig.light.intensity() == 100.0);
  R1_EXPECT(rig.undo().undoRouted() == UndoRoute::None);
  R1_EXPECT(rig.undo().redoRouted() == UndoRoute::Text && text.redos == 1);
  R1_EXPECT(rig.undo().redoRouted() == UndoRoute::Document && rig.light.intensity() == 5.0);
  rig.undo().setTextHook(nullptr);
  R1_EXPECT(rig.undo().undoRouted() == UndoRoute::Document);
}

// A host whose setter can be made to fail from outside, to fail an undo.
struct Flaky {
  double v = 0.0;
  static inline bool refuse = false;
  static inline bool explode = false;
};

void undoFailure() {
  PropertySet<Flaky> s("Flaky");
  s.category("F");
  s.add(
      "v", "V", [](const Flaky& f) { return f.v; },
      [](Flaky& f, double v) {
        if (Flaky::explode) throw 1;
        if (Flaky::refuse) return false;
        f.v = v;
        return true;
      });
  Flaky a;
  PropertyContext c;
  const Target t[] = {targetOf(a, s)};
  c.setSelection(t);
  R1_EXPECT(c.setValue(0, Value(5.0)).ok());
  Flaky::refuse = true;
  UndoResult r = c.undo().undo();
  R1_EXPECT(!r.done && !r.message.empty() && a.v == 5.0 && c.undo().undoCount() == 1 && c.undo().redoCount() == 0);
  Flaky::refuse = false;
  Flaky::explode = true;
  r = c.undo().undo();
  R1_EXPECT(!r.done && a.v == 5.0 && c.undo().undoCount() == 1);
  Flaky::explode = false;
  R1_EXPECT(c.undo().undo().done && a.v == 0.0 && c.undo().redoCount() == 1);
  Flaky::refuse = true;
  R1_EXPECT(!c.undo().redo().done && a.v == 0.0 && c.undo().redoCount() == 1);
  Flaky::refuse = false;
  R1_EXPECT(c.undo().redo().done && a.v == 5.0);
}

void setterFailureRollsBack() {
  // Three objects; the middle one refuses the value: the first is reverted, nothing is recorded.
  Fragile a, b, d;
  b.rejectAbove = 5.0;
  PropertyContext c;
  const Target t[] = {targetOf(a, fragileSet()), targetOf(b, fragileSet()), targetOf(d, fragileSet())};
  c.setSelection(t);
  EditReport r = c.setValue(0, Value(10.0));
  R1_EXPECT(r.code == EditCode::Rejected && r.changed == 0 && r.issues.size() == 1 && r.issues[0].target == 1);
  R1_EXPECT(a.value == 0.0 && b.value == 0.0 && d.value == 0.0);
  R1_EXPECT(c.undo().undoCount() == 0 && !c.undo().inTransaction());
  R1_EXPECT(c.setValue(0, Value(3.0)).ok() && a.value == 3.0 && d.value == 3.0);

  // A setter that throws: same guarantee, reported as SetterThrew; the next edit still works.
  Fragile only;
  PropertyContext c2;
  const Target t2[] = {targetOf(only, fragileSet())};
  c2.setSelection(t2);
  Fragile::throwCount = 0;
  r = c2.setValue(0, Value(150.0));
  R1_EXPECT(r.code == EditCode::SetterThrew && Fragile::throwCount == 1 && only.value == 0.0 && c2.undo().undoCount() == 0);
  R1_EXPECT(c2.setValue(0, Value(4.0)).ok() && only.value == 4.0 && c2.undo().undoCount() == 1);

  // Inside an interaction the failed edit leaves the interaction usable and its earlier changes intact.
  c2.beginInteraction(0);
  c2.setValue(0, Value(7.0));
  R1_EXPECT(c2.setValue(0, Value(150.0)).code == EditCode::SetterThrew && only.value == 7.0);
  R1_EXPECT(c2.endInteraction() && c2.undo().undoCount() == 2);
  c2.undo().undo();
  R1_EXPECT(only.value == 4.0);
}

void forgetting() {
  Rig rig;
  Light other;
  PropertyContext& c = rig.context;
  std::vector<Target> both = {targetOf(rig.light, lightSet()), targetOf(other, lightSet())};
  c.setSelection(both);
  c.setValue(row(c, "intensity"), Value(5.0));
  R1_EXPECT(rig.undo().undoCount() == 1);
  c.forgetObject(&rig.light);
  R1_EXPECT(c.targetCount() == 1 && rig.undo().undoCount() == 1);  // the step still covers the other object
  c.forgetObject(&other);
  R1_EXPECT(rig.undo().undoCount() == 0 && c.targetCount() == 0);  // now empty: the step vanished
  R1_EXPECT(rig.undo().usedBytes() == 0);
}

void transactionApi() {
  Rig rig;
  UndoStack& u = rig.undo();
  R1_EXPECT(!u.commit() && !u.cancel() && !u.inTransaction());
  R1_EXPECT(u.begin("outer") && u.begin("inner") && u.inTransaction());
  R1_EXPECT(!u.undo().done);  // no undo in the middle of an edit
  R1_EXPECT(u.commit() && u.inTransaction());
  R1_EXPECT(u.commit() && !u.inTransaction());
  R1_EXPECT(u.undoCount() == 0);  // an empty transaction records nothing
  R1_EXPECT(!u.recordValue({}));  // nothing open: refused
}

}  // namespace

int main() {
  grouping();
  gestureGrouping();
  groupingWindowTuning();
  redundantMerge();
  redoTruncation();
  labels();
  budget();
  listeners();
  textHook();
  undoFailure();
  setterFailureRollsBack();
  forgetting();
  transactionApi();
  return r1test::finish();
}

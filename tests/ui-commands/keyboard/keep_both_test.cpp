// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of assignKeepingBoth: refusal for a same-context clash (nothing changes), success for a
//   clash with a parent or child context (both commands keep the chord), the other-slot pin, invalid
//   input, and canKeepBoth on its own.
// Callers: CTest (label fast).
#include "TestSupport.h"
#include "r1ui/commands/keyboard/KeepBoth.h"

namespace {

using namespace r1test;

struct Fixture {
  Fixture() {
    reg.addContext("layers", kWindowContext);
    reg.add(makeCommand("global.del", "Delete", kGlobalContext, seq(Key::Delete)));
    reg.add(makeCommand("layers.del", "Delete layer", "layers"));
    reg.add(makeCommand("other.same", "Other", kGlobalContext));
    reg.add(makeCommand("two.slots", "Two slots", kGlobalContext, seq(kT, Mod::kCtrl), seq(kU, Mod::kCtrl)));
  }
  CommandRegistry reg;
  KeybindingOverrides over{reg};
  Keymap map{reg, over};
};

void testRefusedSameContext() {
  Fixture f;
  const AssignResult r = assignKeepingBoth(f.over, f.map, f.reg, "other.same", 0, seq(Key::Delete));
  R1_EXPECT(!r.ok && !r.error.empty() && !r.conflicts.empty());
  R1_EXPECT(f.over.size() == 0);  // nothing changed
  R1_EXPECT(f.map.effective("global.del", 0) == seq(Key::Delete));
}

void testKeptAcrossContexts() {
  Fixture f;
  const AssignResult r = assignKeepingBoth(f.over, f.map, f.reg, "layers.del", 0, seq(Key::Delete));
  R1_EXPECT(r.ok && r.conflicts.size() == 1 && r.conflicts[0].scope == ConflictScope::Ancestor);
  R1_EXPECT(f.map.effective("layers.del", 0) == seq(Key::Delete));
  R1_EXPECT(f.map.effective("global.del", 0) == seq(Key::Delete));  // the other command keeps it
  R1_EXPECT(f.map.lookup("layers", seq(Key::Delete)).commandId == "layers.del");
  R1_EXPECT(f.map.lookup(kGlobalContext, seq(Key::Delete)).commandId == "global.del");
}

void testOtherSlotPin() {
  Fixture f;
  // Moving the alternate chord into the primary slot unbinds the alternate; an ordinary assignment keeps it.
  AssignResult r = assignKeepingBoth(f.over, f.map, f.reg, "two.slots", 0, seq(kU, Mod::kCtrl));
  R1_EXPECT(r.ok && f.map.effective("two.slots", 0) == seq(kU, Mod::kCtrl) && !f.map.effective("two.slots", 1));
  Fixture g;
  r = assignKeepingBoth(g.over, g.map, g.reg, "two.slots", 0, seq(kY, Mod::kCtrl));
  R1_EXPECT(r.ok && g.map.effective("two.slots", 0) == seq(kY, Mod::kCtrl) && g.map.effective("two.slots", 1) == seq(kU, Mod::kCtrl));
}

void testInvalid() {
  Fixture f;
  R1_EXPECT(!assignKeepingBoth(f.over, f.map, f.reg, "missing", 0, seq(kY)).ok);
  R1_EXPECT(!assignKeepingBoth(f.over, f.map, f.reg, "two.slots", 2, seq(kY)).ok);
  R1_EXPECT(!assignKeepingBoth(f.over, f.map, f.reg, "two.slots", -1, seq(kY)).ok);
  R1_EXPECT(!assignKeepingBoth(f.over, f.map, f.reg, "two.slots", 0, ChordSequence{}).ok);
  R1_EXPECT(f.over.size() == 0);
  R1_EXPECT(canKeepBoth({}));
  Conflict same;
  same.scope = ConflictScope::SameContext;
  Conflict up;
  up.scope = ConflictScope::Ancestor;
  R1_EXPECT(!canKeepBoth({same}) && canKeepBoth({up}) && !canKeepBoth({up, same}));
}

}  // namespace

int main() {
  testRefusedSameContext();
  testKeptAcrossContexts();
  testOtherSlotPin();
  testInvalid();
  return r1test::finish();
}

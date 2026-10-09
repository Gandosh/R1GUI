// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of the binding resolution and the conflict rules: default holders (the earlier
//   declaration keeps a clashing chord), overrides replacing defaults (also when empty) and winning
//   over defaults and older overrides, reset (one command, all), prefix conflicts of sequences (D14),
//   the same command holding a chord in both slots, the same chord in unrelated contexts, parent and
//   child conflicts (spec 07 rules 24 and 25), the ordered conflict report, assignChord with and
//   without Override (live version bump, one notification, the slot pin of rule 55), display text
//   and removal of a command whose overrides stay dormant.
// Callers: CTest (label fast).
#include "TestSupport.h"
#include "r1ui/commands/Conflicts.h"
#include "r1ui/commands/Keymap.h"

namespace {

using namespace r1test;

struct Fixture {
  CommandRegistry reg;
  KeybindingOverrides over{reg};
  Keymap map{reg, over};
};

void testDefaults() {
  Fixture f;
  f.reg.add(makeCommand("edit.copy", "Copy", kGlobalContext, seq(kC, Mod::kCtrl)));
  f.reg.add(makeCommand("edit.clone", "Clone", kGlobalContext, seq(kC, Mod::kCtrl), seq(kD, Mod::kCtrl)));  // loses its primary
  R1_EXPECT(f.map.effective("edit.copy", 0) == seq(kC, Mod::kCtrl));
  R1_EXPECT(!f.map.effective("edit.clone", 0));                           // the later-declared command stays unbound (rule 3)
  R1_EXPECT(f.map.effective("edit.clone", 1) == seq(kD, Mod::kCtrl));  // its alternate is unaffected
  R1_EXPECT(f.map.lookup(kGlobalContext, seq(kC, Mod::kCtrl)).commandId == "edit.copy");
  R1_EXPECT(f.map.lookup(kGlobalContext, seq(kC)).kind == LookupKind::None);
  R1_EXPECT(f.map.lookup("nowhere", seq(kC, Mod::kCtrl)).kind == LookupKind::None);
  R1_EXPECT(f.map.displayText("edit.copy") == "Ctrl+C" && f.map.displayText("edit.clone") == "Ctrl+D");
  R1_EXPECT(f.map.displayText("edit.copy", true) == "CTRL+C" && f.map.displayText("missing").empty());
  R1_EXPECT(!f.map.effective("edit.copy", 5) && !f.map.effective("edit.copy", -1));
}

void testBothSlotsSameChord() {
  Fixture f;
  f.reg.add(makeCommand("a", "A", kGlobalContext, seq(kF), seq(kF)));
  R1_EXPECT(f.map.effective("a", 0) == seq(kF) && f.map.effective("a", 1) == seq(kF));
  R1_EXPECT(f.map.bindingCount() == 1);
  R1_EXPECT(findConflicts(f.reg, f.map, "a", seq(kF)).empty());  // no conflict with itself
}

void testOverrides() {
  Fixture f;
  f.reg.add(makeCommand("edit.undo", "Undo", kGlobalContext, seq(kZ, Mod::kCtrl), seq(kZ, Mod::kCtrl | Mod::kShift)));
  f.reg.add(makeCommand("edit.redo", "Redo", kGlobalContext, seq(kY, Mod::kCtrl)));
  const uint64_t version = f.reg.version();
  R1_EXPECT(f.over.set("edit.undo", 0, std::nullopt) == SetError::None);           // deliberately unbound
  R1_EXPECT(f.reg.version() > version);                                              // live: the registry moved
  R1_EXPECT(!f.map.effective("edit.undo", 0) && f.map.effective("edit.undo", 1));  // the default is not used (rule 4)
  R1_EXPECT(f.map.lookup(kGlobalContext, seq(kZ, Mod::kCtrl)).kind == LookupKind::None);

  // An override naming a chord another command holds takes it over (rule 5).
  R1_EXPECT(f.over.set("edit.redo", 0, seq(kZ, Mod::kCtrl | Mod::kShift)) == SetError::None);
  R1_EXPECT(f.map.lookup(kGlobalContext, seq(kZ, Mod::kCtrl | Mod::kShift)).commandId == "edit.redo");
  R1_EXPECT(!f.map.effective("edit.undo", 1));
  // Two overrides on one chord: the later one wins.
  R1_EXPECT(f.over.set("edit.undo", 1, seq(kZ, Mod::kCtrl | Mod::kShift)) == SetError::None);
  R1_EXPECT(f.map.lookup(kGlobalContext, seq(kZ, Mod::kCtrl | Mod::kShift)).commandId == "edit.undo");
  R1_EXPECT(!f.map.effective("edit.redo", 0));

  // Reset one command, then everything.
  R1_EXPECT(f.over.resetCommand("edit.undo"));
  R1_EXPECT(f.map.effective("edit.undo", 0) == seq(kZ, Mod::kCtrl));
  R1_EXPECT(f.map.effective("edit.redo", 0) == seq(kZ, Mod::kCtrl | Mod::kShift));  // its override remains
  f.over.resetAll();
  R1_EXPECT(f.over.size() == 0 && f.map.effective("edit.redo", 0) == seq(kY, Mod::kCtrl));
  R1_EXPECT(!f.over.resetCommand("edit.undo") && !f.over.clear("edit.undo", 0));

  // Boundaries.
  R1_EXPECT(f.over.set("missing", 0, seq(Key::A)) == SetError::UnknownCommand);
  R1_EXPECT(f.over.set("edit.undo", 2, seq(Key::A)) == SetError::BadSlot);
  R1_EXPECT(f.over.set("edit.undo", 0, ChordSequence::single(chord(Key::Unknown))) == SetError::InvalidChord);
  const uint64_t quiet = f.reg.version();
  f.over.set("edit.undo", 0, seq(Key::A));
  const uint64_t after = f.reg.version();
  f.over.set("edit.undo", 0, seq(Key::A));  // the same value again: no change, no notification
  R1_EXPECT(after > quiet && f.reg.version() == after);
}

void testDormantOverrides() {
  Fixture f;
  f.reg.add(makeCommand("m.cmd", "M", kGlobalContext, seq(kM)));
  f.over.set("m.cmd", 0, seq(kN));
  f.reg.remove("m.cmd");
  R1_EXPECT(f.map.lookup(kGlobalContext, seq(kN)).kind == LookupKind::None);  // the override has no effect without its command
  R1_EXPECT(f.over.find("m.cmd", 0) != nullptr);                                  // but it is kept
  f.reg.add(makeCommand("m.cmd", "M", kGlobalContext, seq(kM)));
  R1_EXPECT(f.map.lookup(kGlobalContext, seq(kN)).commandId == "m.cmd");
}

void testPrefixConflicts() {
  Fixture f;
  f.reg.add(makeCommand("single", "Single", kGlobalContext, seq(kK, Mod::kCtrl)));
  f.reg.add(makeCommand("seq", "Sequence", kGlobalContext, seq2(kK, Mod::kCtrl, kC, Mod::kCtrl)));
  R1_EXPECT(f.map.effective("single", 0));
  R1_EXPECT(!f.map.effective("seq", 0));  // its first step is a binding: the earlier declaration keeps it
  Fixture g;
  g.reg.add(makeCommand("seq", "Sequence", kGlobalContext, seq2(kK, Mod::kCtrl, kC, Mod::kCtrl)));
  g.reg.add(makeCommand("single", "Single", kGlobalContext, seq(kK, Mod::kCtrl)));
  R1_EXPECT(g.map.effective("seq", 0) && !g.map.effective("single", 0));
  R1_EXPECT(g.map.lookup(kGlobalContext, seq(kK, Mod::kCtrl)).kind == LookupKind::Prefix);
  R1_EXPECT(g.map.lookup(kGlobalContext, seq2(kK, Mod::kCtrl, kC, Mod::kCtrl)).commandId == "seq");
  R1_EXPECT(g.map.lookup(kGlobalContext, seq2(kK, Mod::kCtrl, kX, Mod::kCtrl)).kind == LookupKind::None);
  // Two sequences sharing a first step coexist.
  g.reg.add(makeCommand("seq2", "Second", kGlobalContext, seq2(kK, Mod::kCtrl, kD, Mod::kCtrl)));
  R1_EXPECT(g.map.effective("seq2", 0).has_value());
  const std::vector<Continuation> next = g.map.continuations(kGlobalContext, chord(kK, Mod::kCtrl));
  R1_EXPECT(next.size() == 2);
  // An override that is a single chord beats a default sequence that starts with it.
  g.over.set("single", 0, seq(kK, Mod::kCtrl));
  R1_EXPECT(g.map.lookup(kGlobalContext, seq(kK, Mod::kCtrl)).commandId == "single");
  R1_EXPECT(!g.map.effective("seq", 0) && !g.map.effective("seq2", 0));
}

void testContextsAndConflicts() {
  Fixture f;
  f.reg.addContext("viewport", kWindowContext);
  f.reg.addContext("layers", kWindowContext);
  f.reg.addContext("viewport.tools", "viewport");
  f.reg.add(makeCommand("global.delete", "Delete selection", kGlobalContext, seq(Key::Delete)));
  f.reg.add(makeCommand("layers.delete", "Remove layer", "layers", seq(Key::Delete)));
  f.reg.add(makeCommand("viewport.frame", "Frame", "viewport", seq(kF)));
  f.reg.add(makeCommand("layers.find", "Find layer", "layers", seq(kF)));  // sibling: shares F freely
  f.reg.add(makeCommand("tools.fill", "Fill", "viewport.tools"));
  R1_EXPECT(f.map.lookup(kGlobalContext, seq(Key::Delete)).commandId == "global.delete");
  R1_EXPECT(f.map.lookup("layers", seq(Key::Delete)).commandId == "layers.delete");
  R1_EXPECT(f.map.effective("layers.delete", 0) && f.map.effective("viewport.frame", 0) && f.map.effective("layers.find", 0));

  // Siblings never conflict; a parent's chord conflicts for the child; the child's for the parent.
  R1_EXPECT(findConflicts(f.reg, f.map, "layers.find", seq(kF)).empty());  // viewport is a sibling of layers
  const std::vector<Conflict> child = findConflicts(f.reg, f.map, "tools.fill", seq(kF));
  R1_EXPECT(child.size() == 1 && child[0].commandId == "viewport.frame" && child[0].scope == ConflictScope::Ancestor);
  const std::vector<Conflict> parent = findConflicts(f.reg, f.map, "viewport.frame", seq(Key::Delete));
  R1_EXPECT(parent.size() == 1 && parent[0].commandId == "global.delete" && parent[0].scope == ConflictScope::Ancestor);
  const std::vector<Conflict> fromGlobal = findConflicts(f.reg, f.map, "global.delete", seq(kF));
  R1_EXPECT(fromGlobal.size() == 2 && fromGlobal[0].scope == ConflictScope::Descendant);  // descendants in creation order
  R1_EXPECT(fromGlobal[0].commandId == "viewport.frame" && fromGlobal[1].commandId == "layers.find");
}

void testConflictOrderAndKinds() {
  Fixture f;
  f.reg.addContext("viewport", kWindowContext);
  f.reg.add(makeCommand("g", "G", kGlobalContext, seq(kX)));
  f.reg.add(makeCommand("w", "W", kWindowContext, seq(kX)));
  f.reg.add(makeCommand("v", "V", "viewport", seq(kX)));
  f.reg.add(makeCommand("target", "Target", "viewport"));
  const std::vector<Conflict> list = findConflicts(f.reg, f.map, "target", seq(kX));
  // Own context first, then each ancestor nearest first.
  R1_EXPECT(list.size() == 3 && list[0].commandId == "v" && list[0].scope == ConflictScope::SameContext);
  R1_EXPECT(list[1].commandId == "w" && list[2].commandId == "g");
  // Prefix kinds.
  f.reg.add(makeCommand("chain", "Chain", kGlobalContext, seq2(kQ, Mod::kCtrl, kW)));
  R1_EXPECT(findConflicts(f.reg, f.map, "target", seq(kQ, Mod::kCtrl))[0].kind == ConflictKind::NewIsPrefix);
  R1_EXPECT(findConflicts(f.reg, f.map, "target", seq2(kX, 0, kY))[0].kind == ConflictKind::ExistingIsPrefix);
  R1_EXPECT(findConflicts(f.reg, f.map, "missing", seq(kX)).empty());
  R1_EXPECT(findConflicts(f.reg, f.map, "target", ChordSequence{}).empty());
}

void testAssign() {
  Fixture f;
  f.reg.add(makeCommand("edit.copy", "Copy", kGlobalContext, seq(kC, Mod::kCtrl)));
  f.reg.add(makeCommand("edit.paste", "Paste", kGlobalContext, seq(kV, Mod::kCtrl)));
  int notifications = 0;
  f.reg.subscribe([&] { ++notifications; });

  // Spec 07 scenario 7: copy takes Ctrl+V; refused first, then overridden.
  AssignResult refused = assignChord(f.over, f.map, f.reg, "edit.copy", 0, seq(kV, Mod::kCtrl), false);
  R1_EXPECT(!refused.ok && refused.conflicts.size() == 1 && refused.conflicts[0].commandId == "edit.paste");
  R1_EXPECT(f.over.size() == 0 && notifications == 0);  // refusing changes nothing
  AssignResult done = assignChord(f.over, f.map, f.reg, "edit.copy", 0, seq(kV, Mod::kCtrl), true);
  R1_EXPECT(done.ok && done.conflicts.size() == 1);
  R1_EXPECT(notifications == 1);  // one batch, one notification
  R1_EXPECT(f.map.effective("edit.copy", 0) == seq(kV, Mod::kCtrl) && !f.map.effective("edit.paste", 0));
  R1_EXPECT(f.map.displayText("edit.paste").empty());  // the paste entry shows no shortcut text
  R1_EXPECT(f.over.find("edit.copy", 1) != nullptr && !f.over.find("edit.copy", 1)->chord);  // rule 55: both slots saved, the other one empty

  // No conflict: committed at once. Moving a chord between slots of the same command is allowed.
  R1_EXPECT(assignChord(f.over, f.map, f.reg, "edit.paste", 1, seq(kP, Mod::kCtrl), false).ok);
  R1_EXPECT(f.map.effective("edit.paste", 1) == seq(kP, Mod::kCtrl));
  R1_EXPECT(assignChord(f.over, f.map, f.reg, "edit.paste", 0, seq(kP, Mod::kCtrl), false).ok);
  R1_EXPECT(f.map.effective("edit.paste", 0) == seq(kP, Mod::kCtrl) && !f.map.effective("edit.paste", 1));

  // Unbinding never conflicts; bad input changes nothing.
  R1_EXPECT(assignChord(f.over, f.map, f.reg, "edit.paste", 0, std::nullopt, false).ok);
  R1_EXPECT(!f.map.effective("edit.paste", 0));
  R1_EXPECT(!assignChord(f.over, f.map, f.reg, "nope", 0, seq(Key::A), false).ok);
  R1_EXPECT(!assignChord(f.over, f.map, f.reg, "edit.copy", 7, seq(Key::A), false).ok);
  R1_EXPECT(!assignChord(f.over, f.map, f.reg, "edit.copy", 0, ChordSequence::single(chord(Key::Unknown)), false).ok);
  R1_EXPECT(assignChord(f.over, f.map, f.reg, "edit.copy", 0, seq(kV, Mod::kCtrl), false).ok);  // already its own: no conflict, no change
}

}  // namespace

int main() {
  testDefaults();
  testBothSlotsSameChord();
  testOverrides();
  testDormantOverrides();
  testPrefixConflicts();
  testContextsAndConflicts();
  testConflictOrderAndKinds();
  testAssign();
  return r1test::finish();
}

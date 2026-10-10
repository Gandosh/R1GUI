// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of collectKeyUsage: exact modifier matching, category and context filters, the "General"
//   category for commands without one, sequences reported on their first key, overrides moving a
//   binding to another key, a displaced default not being listed, deterministic order, an empty
//   registry and unknown filters.
// Callers: CTest (label fast).
#include "TestSupport.h"
#include "r1ui/commands/keyboard/KeyUsage.h"

namespace {

using namespace r1test;

CommandDef withCategory(CommandDef def, std::string category) {
  def.category = std::move(category);
  return def;
}

struct Fixture {
  CommandRegistry reg;
  KeybindingOverrides over{reg};
  Keymap map{reg, over};
};

bool has(const KeyUsageMap& m, Key key, const char* id) {
  const auto it = m.find(static_cast<uint16_t>(key));
  if (it == m.end()) return false;
  for (const KeyUse& u : it->second) {
    if (u.commandId == id) return true;
  }
  return false;
}

void testModifiersAndFilters() {
  Fixture f;
  f.reg.add(withCategory(makeCommand("edit.copy", "Copy", kGlobalContext, seq(kC, Mod::kCtrl)), "Edit"));
  f.reg.add(withCategory(makeCommand("edit.paste", "Paste", kGlobalContext, seq(kV, Mod::kCtrl)), "Edit"));
  f.reg.add(withCategory(makeCommand("view.fit", "Fit", kGlobalContext, seq(kF)), "View"));
  f.reg.add(makeCommand("misc.act", "Act", kGlobalContext, seq(Key::A, Mod::kAlt | Mod::kShift)));
  f.reg.addContext("layers", kWindowContext);
  f.reg.add(withCategory(makeCommand("layers.del", "Delete layer", "layers", seq(Key::Delete)), "Layers"));

  KeyUsageMap ctrl = collectKeyUsage(f.reg, f.map, {}, Mod::kCtrl);
  R1_EXPECT(has(ctrl, kC, "edit.copy") && has(ctrl, kV, "edit.paste"));
  R1_EXPECT(!has(ctrl, kF, "view.fit"));  // plain F is not a Ctrl chord
  KeyUsageMap none = collectKeyUsage(f.reg, f.map, {}, 0);
  R1_EXPECT(has(none, kF, "view.fit") && has(none, Key::Delete, "layers.del") && !has(none, kC, "edit.copy"));
  // Exact: Alt+Shift matches only the Alt+Shift chord, not Alt alone or Shift alone.
  R1_EXPECT(has(collectKeyUsage(f.reg, f.map, {}, Mod::kAlt | Mod::kShift), Key::A, "misc.act"));
  R1_EXPECT(!has(collectKeyUsage(f.reg, f.map, {}, Mod::kAlt), Key::A, "misc.act"));
  R1_EXPECT(!has(collectKeyUsage(f.reg, f.map, {}, Mod::kShift), Key::A, "misc.act"));
  // Category filter, including General for a command without one.
  KeyUsageFilter edit;
  edit.category = "Edit";
  R1_EXPECT(has(collectKeyUsage(f.reg, f.map, edit, Mod::kCtrl), kC, "edit.copy"));
  R1_EXPECT(!has(collectKeyUsage(f.reg, f.map, edit, 0), kF, "view.fit"));
  KeyUsageFilter general;
  general.category = "General";
  R1_EXPECT(has(collectKeyUsage(f.reg, f.map, general, Mod::kAlt | Mod::kShift), Key::A, "misc.act"));
  R1_EXPECT(collectKeyUsage(f.reg, f.map, general, Mod::kCtrl).empty());
  // Context filter: a panel context sees its own and its ancestors' commands; global does not see the panel's.
  KeyUsageFilter layers;
  layers.context = "layers";
  R1_EXPECT(has(collectKeyUsage(f.reg, f.map, layers, 0), Key::Delete, "layers.del") && has(collectKeyUsage(f.reg, f.map, layers, 0), kF, "view.fit"));
  KeyUsageFilter global;
  global.context = "global";
  R1_EXPECT(!has(collectKeyUsage(f.reg, f.map, global, 0), Key::Delete, "layers.del"));
  KeyUsageFilter unknown;
  unknown.context = "nowhere";
  R1_EXPECT(collectKeyUsage(f.reg, f.map, unknown, 0).empty());
  unknown = {};
  unknown.category = "No such category";
  R1_EXPECT(collectKeyUsage(f.reg, f.map, unknown, Mod::kCtrl).empty());
}

void testSequencesAndOverrides() {
  Fixture f;
  f.reg.add(makeCommand("a", "A", kGlobalContext, seq2(kK, Mod::kCtrl, kC, Mod::kCtrl)));
  f.reg.add(makeCommand("b", "B", kGlobalContext, seq(kB, Mod::kCtrl), seq(kX, Mod::kCtrl)));
  f.reg.add(makeCommand("c", "C", kGlobalContext, seq(kB, Mod::kCtrl)));  // loses Ctrl+B to "b" (earlier declaration)
  KeyUsageMap m = collectKeyUsage(f.reg, f.map, {}, Mod::kCtrl);
  R1_EXPECT(has(m, kK, "a") && m[static_cast<uint16_t>(kK)].front().startsSequence);
  R1_EXPECT(has(m, kB, "b") && !has(m, kB, "c"));  // the displaced default is not in force
  R1_EXPECT(!m[static_cast<uint16_t>(kB)].front().startsSequence);
  // An override moves a binding: the old key empties, the new one lists the command.
  f.over.set("b", 0, seq(kZ, Mod::kCtrl));
  m = collectKeyUsage(f.reg, f.map, {}, Mod::kCtrl);
  R1_EXPECT(has(m, kZ, "b") && !has(m, kB, "b") && has(m, kB, "c"));
  // Unbinding removes it.
  f.over.set("b", 1, std::nullopt);
  m = collectKeyUsage(f.reg, f.map, {}, Mod::kCtrl);
  R1_EXPECT(!has(m, kX, "b"));
}

void testOrderAndEmpty() {
  Fixture empty;
  R1_EXPECT(collectKeyUsage(empty.reg, empty.map, {}, 0).empty());
  Fixture f;
  f.reg.addContext("p", kWindowContext);
  f.reg.add(makeCommand("one", "One", kGlobalContext, seq(kQ)));
  f.reg.add(makeCommand("two", "Two", "p", seq(kQ)));  // another context may hold the same chord
  const KeyUsageMap m = collectKeyUsage(f.reg, f.map, {}, 0);
  const auto& list = m.at(static_cast<uint16_t>(kQ));
  R1_EXPECT(list.size() == 2 && list[0].commandId == "one" && list[1].commandId == "two");  // registration order
}

}  // namespace

int main() {
  testModifiersAndFilters();
  testSequencesAndOverrides();
  testOrderAndEmpty();
  return r1test::finish();
}

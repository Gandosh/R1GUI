// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: hostile-input tests of the hotkey editor: an empty registry, thousands of commands (build,
//   refresh, keyboard usage and paint cost), 10 000-character descriptions, invalid UTF-8 in labels,
//   descriptions and categories, many contexts, selection of a command that disappears, assignments
//   with garbage arguments, resolving without a pending assignment, a registry that changes while a
//   dialog is open, and corrupt set data. Every scene is painted.
// Callers: CTest (label fast). The measured times are printed for the evidence record.
#include <chrono>
#include <cstdio>

#include "HotkeyFixture.h"
#include "r1ui/commands/OverrideIo.h"
#include "r1ui/commands/Text.h"

namespace {

using namespace r1test;
using namespace r1ui::widgets;
using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }

void testEmpty() {
  HotkeyScene s(false);
  HotkeyEditor& e = *s.editor;
  R1_EXPECT(e.list().view().actions().empty() && e.keyboard().assignedKeyCount() == 0);
  s.paintOnce();
  e.setTab(HotkeyEditor::Tab::Command);
  s.t.layout();
  s.paintOnce();
  R1_EXPECT(!e.selectAction("x") && e.assign("x", 0, chordOf(letter('Q'))) == AssignOutcome::Refused);
  R1_EXPECT(!e.resolveConflict(ConflictResolution::Replace) && !e.clearSlot("x", 0) && !e.resetAction("x"));
  s.click(e.assignButton());  // disabled: nothing happens
  R1_EXPECT(!e.recorder().recording());
  for (const Key k : {Key::Down, Key::Up, Key::Enter, Key::Escape, Key::Tab}) s.key(k);
  e.setCategory("anything");
  e.setContextFilter("nowhere");
  s.paintOnce();
  R1_EXPECT(e.list().view().rows().empty());
}

void testManyCommands() {
  HotkeyScene s(false);
  // 8000 commands in 40 categories; one in ten holds a chord (letters x modifiers x function keys).
  const auto t0 = Clock::now();
  for (int i = 0; i < 8000; ++i) {
    cmd::ChordSequence chord;
    if (i % 10 == 0) {
      const int n = i / 10;  // 800 distinct chords: 26 letters x 16 modifier sets are plenty
      chord = chordOf(letter(static_cast<char>('A' + n % 26)), static_cast<uint8_t>((n / 26) % 16));
    }
    s.declare("bulk." + std::to_string(i), "Bulk action " + std::to_string(i), "Category " + std::to_string(i % 40), chord);
  }
  const double declare = msSince(t0);
  const auto t1 = Clock::now();
  s.editor->setFilter("");
  s.registry.touch();
  s.t.layout();
  const double refresh = msSince(t1);
  R1_EXPECT(s.editor->list().view().actions().size() == 8000);
  const auto t2 = Clock::now();
  s.editor->setModifiers(Mod::kCtrl);
  const double layer = msSince(t2);
  R1_EXPECT(s.editor->keyboard().assignedKeyCount() > 5);
  const auto t3 = Clock::now();
  s.paintOnce();
  const double paint = msSince(t3);
  std::printf("hotkey editor, 8000 commands: declare %.0f ms, refresh %.1f ms, layer switch %.1f ms, paint %.1f ms\n", declare, refresh, layer, paint);
  R1_EXPECT(refresh < 2000.0 && layer < 500.0 && paint < 500.0);
  // Assigning goes through the same path at this size.
  R1_EXPECT(s.editor->selectAction("bulk.7999"));
  R1_EXPECT(s.editor->assign("bulk.7999", 0, chordOf(Key::Insert, Mod::kAlt | Mod::kCtrl | Mod::kShift | Mod::kMeta)) == AssignOutcome::Assigned);
}

void testTextHostility() {
  HotkeyScene s(false);
  HotkeyEditor& e = *s.editor;
  R1_EXPECT(s.declare("host.long", "Long description", "Edit", {}, {}, cmd::kGlobalContext, std::string(10000, 'w') + " end"));
  R1_EXPECT(s.declare("host.utf", std::string("Bad \xFF utf \xC0\xAF label"), std::string("Cat \xF5\x80"), {}, {}, cmd::kGlobalContext, std::string("desc \xED\xA0\x80 \xFE")));
  R1_EXPECT(s.declare("host.ctl", "Control\x01 chars", "Edit", {}, {}, cmd::kGlobalContext, "line one\nline two\ttabbed\x07"));
  s.t.layout();
  for (const ActionInfo& a : e.list().view().actions()) {
    R1_EXPECT(cmd::isValidUtf8(a.label) && cmd::isValidUtf8(a.description) && cmd::isValidUtf8(a.category));
  }
  for (const char* id : {"host.long", "host.utf", "host.ctl"}) {
    R1_EXPECT(e.selectAction(id));
    e.setTab(HotkeyEditor::Tab::Command);
    s.t.layout();
    s.paintOnce();  // the detail sheet wraps and clips the description
    e.setTab(HotkeyEditor::Tab::Keyboard);
    s.t.layout();
    s.paintOnce();
  }
  e.setFilter(std::string("\xFF\xFE bad query"));
  e.setFilter(std::string(100000, 'q'));
  s.paintOnce();
  R1_EXPECT(cmd::isValidUtf8(e.filter()));
  e.setFilter("");
}

void testVanishingAndOrder() {
  HotkeyScene s;
  HotkeyEditor& e = *s.editor;
  e.selectAction("edit.copy");
  e.assign("edit.copy", 0, chordOf(letter('Q'), Mod::kAlt));
  // The command disappears while selected, while a conflict dialog for it is pending.
  e.assign("edit.copy", 1, chordOf(letter('V'), Mod::kCtrl));  // clashes with Paste
  R1_EXPECT(e.pending() != nullptr && e.conflictDialogOpen());
  s.registry.remove("edit.copy");
  s.t.layout();
  R1_EXPECT(e.selectedAction().empty());
  R1_EXPECT(e.resolveConflict(ConflictResolution::Replace));  // applies to a command that is gone: refused, no crash
  R1_EXPECT(e.pending() == nullptr && !e.conflictDialogOpen());
  s.paintOnce();
  // Keyboard click with nothing selected and a stale layer.
  e.setModifiers(Mod::kCtrl | Mod::kAlt | Mod::kShift | Mod::kMeta);
  s.clickCap("a");
  s.clickCap("space");
  s.clickCap("f12");
  // Many contexts.
  for (int i = 0; i < 200; ++i) s.registry.addContext("ctx" + std::to_string(i), cmd::kWindowContext);
  s.t.layout();
  e.setContextFilter("ctx150");
  R1_EXPECT(e.list().view().matchCount() > 0);
  s.paintOnce();
}

void testSetsHostility() {
  HotkeyScene s;
  HotkeyEditor& e = *s.editor;
  // Syntactically valid files the importer refuses or half accepts leave the bindings alone.
  R1_EXPECT(e.addSet("wrongformat", "{\"format\":\"other\",\"version\":1,\"overrides\":[]}"));
  R1_EXPECT(e.addSet("wrongversion", "{\"format\":\"r1ui-keybindings\",\"version\":99,\"overrides\":[]}"));
  R1_EXPECT(e.addSet("unusable", "{\"format\":\"r1ui-keybindings\",\"version\":1,\"overrides\":[{\"command\":\"nope\",\"slot\":0,\"chord\":\"Ctrl+Q\"}]}"));
  R1_EXPECT(e.addSet("deep", std::string(2000, '[')));
  R1_EXPECT(e.addSet("binary", std::string("\x00\x01\xFF\xFE", 4)));
  e.assign("free.one", 0, chordOf(letter('Q'), Mod::kAlt));
  for (const char* name : {"wrongformat", "wrongversion", "unusable", "deep", "binary"}) {
    R1_EXPECT(!e.loadSet(name) || std::string(name) == "unusable");
    R1_EXPECT(cmd::formatSequence(*s.effective("free.one", 0)) == "Alt+Q");
  }
  R1_EXPECT(!e.addSet("", "{}") && !e.addSet("   ", "{}"));
  R1_EXPECT(!e.addSet("huge", std::string(cmd::kMaxImportBytes + 1, 'x')));
  s.paintOnce();
}

}  // namespace

int main() {
  testEmpty();
  testManyCommands();
  testTextHostility();
  testVanishingAndOrder();
  testSetsHostility();
  return r1test::finish();
}

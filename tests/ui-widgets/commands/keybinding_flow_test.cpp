// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: flow and hostile-case tests of the keybinding editor: the reset-all confirmation (cancel,
//   Escape, confirm), import and export callbacks, two-step sequences and prefix conflicts in both
//   directions, keyboard operation (Tab order without the remove buttons, Enter and Space to start,
//   Up and Down between rows), live updates from outside (no rebuild for a chord change, a rebuild
//   with the capture cancelled when the command set changes), a captured command removed, the editor
//   destroyed with a popup and a dialog open, 500 commands, invalid UTF-8 in labels.
// Callers: CTest (label fast).
#include <chrono>
#include <unordered_set>

#include "EditorFixture.h"
#include "r1ui/commands/Conflicts.h"
#include "r1ui/commands/OverrideIo.h"
#include "r1ui/widgets/dialog/Dialog.h"

namespace {

using namespace r1test;
using namespace r1ui::widgets;
using r1ui::core::events::FocusReason;

void testResetAll() {
  EditorFixture f;
  KeybindingEditor& ed = *f.editor;
  cmd::assignChord(f.overrides, f.keymap, f.registry, "edit.undo", 0, chordOf(letter('U'), Mod::kAlt), false);
  cmd::assignChord(f.overrides, f.keymap, f.registry, "edit.rename", 0, std::nullopt, false);
  R1_EXPECT(f.overrides.size() > 0);
  f.click(ed.resetAllButton());  // spec 07 scenario 11
  R1_EXPECT(ed.resetAllDialogOpen() && f.t.ui.overlays().anyModal());
  R1_EXPECT(closeDialog(f.t.ui, ed.resetAllDialog(), "cancel"));
  f.t.layout();
  R1_EXPECT(!ed.resetAllDialogOpen() && f.box("edit.undo", 0).chordText() == "Alt+U");  // Cancel changes nothing
  ed.requestResetAll();
  f.t.layout();
  R1_EXPECT(ed.resetAllDialogOpen());
  f.key(Key::Escape);  // Escape is Cancel
  R1_EXPECT(!ed.resetAllDialogOpen() && f.box("edit.undo", 0).chordText() == "Alt+U");
  ed.requestResetAll();
  f.t.layout();
  ed.requestResetAll();  // asking twice opens one dialog
  R1_EXPECT(f.t.ui.overlays().count() == 1);
  R1_EXPECT(closeDialog(f.t.ui, ed.resetAllDialog(), "reset"));
  f.t.layout();
  R1_EXPECT(f.overrides.size() == 0 && f.box("edit.undo", 0).chordText() == "Ctrl+Z" && f.box("edit.rename", 0).chordText() == "F2");  // live (D15)
  // Reset all while a capture is open ends the capture.
  f.clickBox("edit.copy", 0);
  ed.requestResetAll();
  f.t.layout();
  closeDialog(f.t.ui, ed.resetAllDialog(), "reset");
  f.t.layout();
  R1_EXPECT(!ed.capturing());
}

void testImportExport() {
  EditorFixture f;
  KeybindingEditor& ed = *f.editor;
  int imports = 0;
  int exports = 0;
  ed.setOnImport([&] { ++imports; });
  ed.setOnExport([&] { ++exports; });
  R1_EXPECT(f.t.ui.object(ed.importButton())->enabled() && f.t.ui.object(ed.exportButton())->enabled());
  f.click(ed.importButton());
  f.click(ed.exportButton());
  f.click(ed.exportButton());
  R1_EXPECT(imports == 1 && exports == 2);
  ed.setOnImport({});
  R1_EXPECT(!f.t.ui.object(ed.importButton())->enabled());
}

void testTwoStep() {
  EditorFixture f;
  KeybindingEditor& ed = *f.editor;
  ed.setTwoStepCapture(true);
  f.clickBox("edit.undo", 1);
  f.key(letter('K'), Mod::kCtrl);
  R1_EXPECT(ed.capturing() && f.box("edit.undo", 1).preview() == "Ctrl+K, ");
  f.key(letter('J'), Mod::kCtrl);  // the second press completes the sequence
  R1_EXPECT(!ed.capturing() && f.box("edit.undo", 1).chordText() == "Ctrl+K, Ctrl+J");
  // Enter after the first press keeps it as a single chord.
  f.clickBox("edit.rename", 1);
  f.key(letter('L'), Mod::kCtrl);
  f.key(Key::Enter);
  R1_EXPECT(f.box("edit.rename", 1).chordText() == "Ctrl+L");
  // Escape after the first press cancels everything.
  f.clickBox("edit.copy", 1);
  f.key(letter('M'), Mod::kCtrl);
  f.key(Key::Escape);
  R1_EXPECT(!ed.capturing() && f.box("edit.copy", 1).chordText().empty());

  // Prefix conflicts (D14), both ways.
  f.clickBox("file.save", 1);
  f.key(letter('C'), Mod::kCtrl);
  f.key(letter('X'), Mod::kCtrl);  // Ctrl+C already runs Copy
  R1_EXPECT(ed.conflictPopupOpen() && ed.conflictMessage() == "Ctrl+C, Ctrl+X cannot be used: Ctrl+C already runs Copy.");
  ed.cancelCapture();
  ed.setTwoStepCapture(false);
  f.clickBox("edit.paste", 1);
  f.key(letter('K'), Mod::kCtrl);  // Ctrl+K starts the sequence of Undo
  R1_EXPECT(ed.conflictPopupOpen() && ed.conflictMessage() == "Ctrl+K already starts the sequence Ctrl+K, Ctrl+J of Undo.");
  f.click(ed.overrideButton());  // Override unbinds the sequence
  R1_EXPECT(f.box("edit.paste", 1).chordText() == "Ctrl+K" && f.box("edit.undo", 1).chordText().empty());
}

void testKeyboard() {
  EditorFixture f;
  KeybindingEditor& ed = *f.editor;
  // Tab visits the boxes and never the remove buttons.
  std::unordered_set<WidgetId> removeButtons;
  for (const char* id : {"edit.copy", "edit.paste", "edit.undo", "edit.rename", "global.delete", "file.save", "layers.delete"}) {
    for (int slot = 0; slot < 2; ++slot) removeButtons.insert(f.box(id, slot).removeButton());
  }
  f.overrides.set("edit.copy", 1, std::nullopt);  // a reset button is focusable once its command has an override
  f.overrides.set("layers.delete", 1, std::nullopt);
  f.t.ui.focusWidget(ed.searchInput());
  std::unordered_set<WidgetId> visited;
  for (int i = 0; i < 80; ++i) {
    f.t.ui.keyDown(Key::Tab);
    visited.insert(f.t.ui.router().focused());
  }
  for (const WidgetId b : removeButtons) R1_EXPECT(visited.find(b) == visited.end());
  for (const char* id : {"edit.copy", "layers.delete"}) {
    R1_EXPECT(visited.count(ed.boxOf(id, 0)) == 1 && visited.count(ed.boxOf(id, 1)) == 1 && visited.count(ed.resetButtonOf(id)) == 1);
  }
  R1_EXPECT(visited.count(ed.resetAllButton()) == 1 && visited.count(ed.twoStepCheckbox()) == 1);

  // Enter and Space start a capture on a focused box; Up and Down move between rows.
  f.t.ui.focusWidget(ed.boxOf("edit.copy", 0), FocusReason::Keyboard);
  f.key(Key::Enter);
  R1_EXPECT(ed.capturing() && ed.capturingCommand() == "edit.copy");
  f.key(Key::Escape);
  f.key(Key::Space);
  R1_EXPECT(ed.capturing());
  f.key(Key::Escape);
  R1_EXPECT(f.t.ui.router().focused() == ed.boxOf("edit.copy", 0));  // focus stays on the box for the next key
  f.key(Key::Down);
  R1_EXPECT(f.t.ui.router().focused() == ed.boxOf("global.delete", 0));  // Copy, then Delete (sorted by label)
  f.key(Key::Up);
  R1_EXPECT(f.t.ui.router().focused() == ed.boxOf("edit.copy", 0));
  f.key(Key::Up);
  R1_EXPECT(f.t.ui.router().focused() == ed.boxOf("edit.copy", 0));  // the first row has no neighbour above
  ed.setFilter("rename");
  f.t.ui.focusWidget(ed.boxOf("edit.rename", 1), FocusReason::Keyboard);
  f.key(Key::Down);
  R1_EXPECT(f.t.ui.router().focused() == ed.boxOf("edit.rename", 1));  // hidden rows are skipped, none below
}

void testLive() {
  EditorFixture f;
  KeybindingEditor& ed = *f.editor;
  const WidgetId copyBox = ed.boxOf("edit.copy", 0);
  f.overrides.set("edit.copy", 0, chordOf(letter('K')));  // from outside the editor
  R1_EXPECT(f.box("edit.copy", 0).chordText() == "K" && ed.boxOf("edit.copy", 0) == copyBox);  // texts refresh, no rebuild
  f.overrides.resetAll();
  R1_EXPECT(f.box("edit.copy", 0).chordText() == "Ctrl+C");
  cmd::importOverrides("{\"format\":\"r1ui-keybindings\",\"version\":1,\"overrides\":[{\"command\":\"edit.copy\",\"slot\":1,\"chord\":\"F9\"}]}", f.registry, f.overrides);
  R1_EXPECT(f.box("edit.copy", 1).chordText() == "F9");

  // A changed command set rebuilds the table and ends a capture.
  f.clickBox("edit.paste", 0);
  R1_EXPECT(ed.capturing());
  f.declare("view.grid", "Show grid", cmd::CommandKind::Toggle, chordOf(letter('G'), Mod::kCtrl), {}, {}, cmd::kGlobalContext, "View");
  f.t.layout();
  R1_EXPECT(!ed.capturing() && ed.rowCount() == 8 && ed.headingCount() == 4 && ed.box("view.grid", 0)->chordText() == "Ctrl+G");

  // A captured command that is removed: no crash, the row is gone.
  f.clickBox("edit.copy", 0);
  f.key(letter('V'), Mod::kCtrl);
  R1_EXPECT(ed.conflictPopupOpen());
  f.registry.remove("edit.copy");
  f.t.layout();
  R1_EXPECT(!ed.capturing() && !ed.conflictPopupOpen() && ed.rowCount() == 7 && f.t.ui.overlays().count() == 0);

  // The editor destroyed with a popup and a dialog open closes both.
  f.clickBox("edit.paste", 0);
  f.key(letter('Z'), Mod::kCtrl);
  R1_EXPECT(ed.conflictPopupOpen());
  ed.requestResetAll();
  f.t.layout();
  R1_EXPECT(f.t.ui.overlays().count() == 2);
  f.t.ui.destroy(ed.id());
  f.t.layout();
  R1_EXPECT(f.t.ui.overlays().count() == 0);
  f.registry.touch();  // the destroyed editor no longer listens
}

void testHostile() {
  CommandFixture f(1000, 800);
  for (int i = 0; i < 500; ++i) {
    f.declare("cmd." + std::to_string(i), "Command " + std::to_string(i), cmd::CommandKind::Action, {}, {}, {}, cmd::kGlobalContext, "Category " + std::to_string(i % 12));
  }
  f.declare("bad.label", std::string("bad \xFF\xC0\x80 label ") + std::string(5000, 'L'));
  const auto start = std::chrono::steady_clock::now();
  KeybindingEditor& ed = f.t.ui.create<KeybindingEditor>(f.t.ui.root(), f.services());
  ed.style().height = r1ui::core::layout::Length::px(600);
  f.t.layout();
  R1_EXPECT(ed.rowCount() == 501 && ed.headingCount() == 13);
  ed.setFilter("cmd.250");
  R1_EXPECT(ed.visibleRowCount() == 1 && ed.visibleHeadingCount() == 1);
  ed.setFilter("");
  f.t.layout();
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  R1_EXPECT(seconds < 20.0);
  R1_EXPECT(ed.box("bad.label", 0) != nullptr);
  // Rebinding in a big table is live.
  R1_EXPECT(cmd::assignChord(f.overrides, f.keymap, f.registry, "cmd.250", 0, chordOf(letter('Q')), false).ok);
  R1_EXPECT(ed.box("cmd.250", 0)->chordText() == "Q");
  // Capturing a command that was filtered out is impossible; filtering away the captured row ends it.
  f.t.ui.focusWidget(ed.boxOf("cmd.250", 0));
  ed.boxClicked(*ed.box("cmd.250", 0));
  R1_EXPECT(ed.capturing());
  ed.setFilter("cmd.1");
  R1_EXPECT(!ed.capturing());
}

}  // namespace

int main() {
  testResetAll();
  testImportExport();
  testTwoStep();
  testKeyboard();
  testLive();
  testHostile();
  return r1test::finish();
}

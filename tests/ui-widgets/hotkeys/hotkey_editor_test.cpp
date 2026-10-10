// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: behaviour tests of HotkeyEditor: structure and captions, selecting from the list and the layer it
//   switches to, assigning by clicking a key (primary and alternate slot), selecting by clicking a bound
//   key, the three conflict outcomes (Replace, Keep both, Cancel) through the model and through the
//   dialog, the Runtime Command Editor tab (details, recorder, Assign, Clear, Reset), the category,
//   context and search filters, the modifier buttons, hotkey sets (save as, load, host hook, limits,
//   corrupt data), import/export enabling and live following of the registry.
// Callers: CTest (label fast).
#include "HotkeyFixture.h"
#include "r1ui/commands/Conflicts.h"
#include "r1ui/widgets/button/Button.h"
#include "r1ui/widgets/select/Select.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace {

using namespace r1test;
using namespace r1ui::widgets;

std::string text(const std::optional<cmd::ChordSequence>& c) { return c ? cmd::formatSequence(*c) : std::string(); }

void testStructure() {
  HotkeyScene s;
  HotkeyEditor& e = *s.editor;
  R1_EXPECT(e.setName() == "Default" && e.setNames() == std::vector<std::string>{"Default"});
  R1_EXPECT(e.tab() == HotkeyEditor::Tab::Keyboard && e.slot() == 0 && e.selectedAction().empty());
  // The list: grouped, sorted headers, the Hotkey column text of both slots.
  ActionListView& view = e.list().view();
  R1_EXPECT((view.categories() == std::vector<std::string>{"Edit", "File", "Layers", "Misc", "View"}));
  R1_EXPECT(view.actions().size() == 13);
  const auto find = [&](const char* id) -> const ActionInfo* {
    for (const ActionInfo& a : view.actions()) {
      if (a.id == id) return &a;
    }
    return nullptr;
  };
  R1_EXPECT(find("global.delete") != nullptr && find("global.delete")->shortcut == "Delete, Backspace");
  R1_EXPECT(find("edit.comment")->shortcut == "Ctrl+K, Ctrl+C" && find("free.one")->shortcut.empty());
  R1_EXPECT(e.caption() == "Currently displaying hotkeys for: all categories  (no modifiers)");
  R1_EXPECT(e.list().view().options().columnHeaders && e.list().view().options().shortcutHeader == "Hotkey");
  // Both tabs exist; only the keyboard page takes space.
  R1_EXPECT(s.t.ui.absRect(e.keyboard().id()).h > 100);
  e.setTab(HotkeyEditor::Tab::Command);
  s.t.layout();
  R1_EXPECT(s.t.ui.object(s.t.ui.tree().parent(e.keyboard().id()))->style().display == r1ui::core::layout::Display::None);  // the hidden page takes no space
  R1_EXPECT(s.t.ui.absRect(e.detailView().id()).h > 50);
  e.setTab(HotkeyEditor::Tab::Keyboard);
  s.t.layout();
  s.paintOnce();
}

void testSelectionAndLayer() {
  HotkeyScene s;
  HotkeyEditor& e = *s.editor;
  R1_EXPECT(e.selectAction("edit.redo"));
  R1_EXPECT(e.selectedAction() == "edit.redo" && e.modifiers() == (Mod::kCtrl | Mod::kShift));  // its own layer
  R1_EXPECT(e.keyboard().holdsSelected(letter('Z')));
  R1_EXPECT(e.caption().find("Ctrl+Shift") != std::string::npos);
  // Selecting by clicking the row does the same.
  const int row = [&] {
    const auto& rows = e.list().view().rows();
    for (size_t i = 0; i < rows.size(); ++i) {
      if (!rows[i].header && e.list().view().actions()[rows[i].index].id == "view.fit") return static_cast<int>(i);
    }
    return -1;
  }();
  const RectD r = e.list().view().rowRect(row);
  R1_EXPECT(r.h > 0.0);
  s.clickAt(r.x + 80, r.y + r.h / 2);
  R1_EXPECT(e.selectedAction() == "view.fit" && e.modifiers() == 0);
  // A command without a chord keeps the layer.
  e.setModifiers(Mod::kAlt);
  R1_EXPECT(e.selectAction("free.one") && e.modifiers() == Mod::kAlt);
  // Unknown and hidden ids are refused.
  R1_EXPECT(!e.selectAction("nope"));
  cmd::CommandDef hidden;
  hidden.id = "hidden.cmd";
  hidden.label = "Hidden";
  hidden.hiddenFromEditor = true;
  s.registry.add(hidden);
  R1_EXPECT(!e.selectAction("hidden.cmd"));
  // The detail sheet follows the selection.
  e.selectAction("edit.undo");
  const CommandDetail& d = e.detailView().detail();
  R1_EXPECT(d.valid && d.id == "edit.undo" && d.label == "Undo" && d.category == "Edit" && d.context == "global" && d.current == "Ctrl+Z" && d.defaults == "Ctrl+Z");
  R1_EXPECT(d.conflicts.empty() && d.enabled && !d.description.empty());
  // Selecting works through a search that hides the row.
  e.setFilter("fit");
  R1_EXPECT(e.selectAction("file.save"));
  R1_EXPECT(e.filter().empty());
}

void testAssignByClick() {
  HotkeyScene s;
  HotkeyEditor& e = *s.editor;
  e.selectAction("edit.copy");  // layer Ctrl
  s.clickCap("j");
  R1_EXPECT(text(s.effective("edit.copy", 0)) == "Ctrl+J");
  R1_EXPECT(e.pending() == nullptr && e.message().find("Assigned Ctrl+J to Copy") == 0);
  R1_EXPECT(e.keyboard().assigned(letter('J')) && !e.keyboard().assigned(letter('C')));  // live on the board
  R1_EXPECT(e.list().view().actions()[0].shortcut.empty() == false);
  // The alternate slot.
  e.setSlot(1);
  s.clickCap("y");
  R1_EXPECT(text(s.effective("edit.copy", 1)) == "Ctrl+Y" && text(s.effective("edit.copy", 0)) == "Ctrl+J");
  // The same chord again changes nothing.
  s.clickCap("y");
  R1_EXPECT(e.message().find("already") != std::string::npos);
  // Moving a chord to the other slot unbinds the first one.
  e.setSlot(0);
  s.clickCap("y");
  R1_EXPECT(text(s.effective("edit.copy", 0)) == "Ctrl+Y" && !s.effective("edit.copy", 1));
  // A plain key with the Shift layer.
  e.setModifiers(Mod::kShift);
  s.clickCap("q");
  R1_EXPECT(text(s.effective("edit.copy", 0)) == "Shift+Q");
  // Unbindable and modifier caps never assign.
  s.clickCap("grave");
  s.clickCap("lshift");
  R1_EXPECT(text(s.effective("edit.copy", 0)) == "Shift+Q");
  // Reset to default through the tab's button path.
  R1_EXPECT(e.resetAction("edit.copy") && text(s.effective("edit.copy", 0)) == "Ctrl+C");
  R1_EXPECT(!e.resetAction("edit.copy"));
}

void testClickWithoutSelection() {
  HotkeyScene s;
  HotkeyEditor& e = *s.editor;
  e.setModifiers(Mod::kCtrl);
  s.clickCap("z");
  R1_EXPECT(e.selectedAction() == "edit.undo" && e.message().find("runs Undo") != std::string::npos);
  R1_EXPECT(text(s.effective("edit.undo", 0)) == "Ctrl+Z");  // nothing was assigned
  // A free key only says so.
  e.selectAction("free.one");
  e.setModifiers(Mod::kAlt);
  R1_EXPECT(e.selectAction("free.one"));
  // With no selection at all (selection removed with the command).
  s.registry.remove("free.one");
  s.t.layout();
  R1_EXPECT(e.selectedAction().empty());
  e.setModifiers(Mod::kAlt | Mod::kCtrl);
  s.clickCap("m");
  R1_EXPECT(e.message().find("unassigned") != std::string::npos && s.overrides.size() == 0);
  // A bound key chosen while a category filter hides its command switches to all categories.
  e.setModifiers(Mod::kCtrl);
  e.setCategory("File");
  e.setCategory("");
  s.clickCap("s");
  R1_EXPECT(e.selectedAction() == "file.save");
}

void testConflictReplace() {
  HotkeyScene s;
  HotkeyEditor& e = *s.editor;
  e.selectAction("free.one");
  e.setModifiers(0);
  s.clickCap("delete");  // global.delete holds Delete in the same context, layers.delete in a child
  R1_EXPECT(e.pending() != nullptr && e.conflictDialogOpen() && s.t.ui.overlays().anyModal());
  R1_EXPECT(e.pending()->commandId == "free.one" && !e.pending()->canKeepBoth && e.pending()->conflicts.size() == 2);
  R1_EXPECT(!s.effective("free.one", 0));  // nothing changed yet
  // Keep both is refused here (same-context clash) and leaves the dialog open.
  R1_EXPECT(!e.resolveConflict(ConflictResolution::KeepBoth) && e.conflictDialogOpen());
  // Cancel through the dialog.
  R1_EXPECT(closeDialog(s.t.ui, e.conflictDialog(), "cancel"));
  s.t.layout();
  R1_EXPECT(e.pending() == nullptr && !e.conflictDialogOpen() && !s.effective("free.one", 0) && s.overrides.size() == 0);
  R1_EXPECT(e.message().find("cancelled") != std::string::npos);
  // Replace through the dialog.
  s.clickCap("delete");
  R1_EXPECT(e.conflictDialogOpen());
  R1_EXPECT(closeDialog(s.t.ui, e.conflictDialog(), "replace"));
  s.t.layout();
  R1_EXPECT(e.pending() == nullptr && text(s.effective("free.one", 0)) == "Delete");
  R1_EXPECT(!s.effective("layers.delete", 0) && text(s.effective("global.delete", 1)) == "Backspace");
  R1_EXPECT(!e.conflictDialogOpen() && !s.t.ui.overlays().any());
  // Escape is Cancel.
  e.selectAction("free.two");
  e.setModifiers(0);
  s.clickCap("delete");
  R1_EXPECT(e.conflictDialogOpen());
  s.key(Key::Escape);
  R1_EXPECT(e.pending() == nullptr && !e.conflictDialogOpen() && !s.effective("free.two", 0));
  // A second assignment while one is pending replaces the pending one (one dialog).
  e.assign("free.two", 0, chordOf(Key::Delete));
  e.assign("free.two", 1, chordOf(Key::Backspace));
  R1_EXPECT(s.t.ui.overlays().count() == 1 && e.pending()->slot == 1);
  R1_EXPECT(e.resolveConflict(ConflictResolution::Replace));
  R1_EXPECT(text(s.effective("free.two", 1)) == "Backspace" && !s.effective("global.delete", 1));
  R1_EXPECT(!e.resolveConflict(ConflictResolution::Cancel));  // nothing pending any more
}

void testConflictKeepBoth() {
  HotkeyScene s;
  HotkeyEditor& e = *s.editor;
  // layers.hide lives in the layers context; Ctrl+C is held by the global Copy (an ancestor): keepable.
  e.selectAction("layers.hide");
  e.setModifiers(Mod::kCtrl);
  s.clickCap("c");
  R1_EXPECT(e.pending() != nullptr && e.pending()->canKeepBoth && e.pending()->conflicts.size() == 1);
  R1_EXPECT(e.conflictDialogOpen());
  R1_EXPECT(closeDialog(s.t.ui, e.conflictDialog(), "keepboth"));
  s.t.layout();
  R1_EXPECT(text(s.effective("layers.hide", 0)) == "Ctrl+C" && text(s.effective("edit.copy", 0)) == "Ctrl+C");
  R1_EXPECT(s.keymap.lookup("layers", chordOf(letter('C'), Mod::kCtrl)).commandId == "layers.hide");
  R1_EXPECT(s.keymap.lookup(cmd::kGlobalContext, chordOf(letter('C'), Mod::kCtrl)).commandId == "edit.copy");
  // The detail sheet lists the shared chord as a conflict.
  R1_EXPECT(e.detailView().detail().conflicts.size() == 1 && e.detailView().detail().conflicts[0].find("also used by Copy") != std::string::npos);
  // Prefix conflicts: Ctrl+K alone would hide the sequence Ctrl+K, Ctrl+C.
  e.selectAction("free.one");
  R1_EXPECT(e.assign("free.one", 0, chordOf(letter('K'), Mod::kCtrl)) == AssignOutcome::NeedsResolution);
  R1_EXPECT(e.pending()->conflicts[0].kind == cmd::ConflictKind::NewIsPrefix);
  R1_EXPECT(e.resolveConflict(ConflictResolution::Replace));
  R1_EXPECT(text(s.effective("free.one", 0)) == "Ctrl+K" && !s.effective("edit.comment", 0));
}

void testAssignApi() {
  HotkeyScene s;
  HotkeyEditor& e = *s.editor;
  R1_EXPECT(e.assign("nope", 0, chordOf(letter('Q'))) == AssignOutcome::Refused);
  R1_EXPECT(e.assign("free.one", 2, chordOf(letter('Q'))) == AssignOutcome::Refused);
  R1_EXPECT(e.assign("free.one", -1, chordOf(letter('Q'))) == AssignOutcome::Refused);
  R1_EXPECT(e.assign("free.one", 0, cmd::ChordSequence{}) == AssignOutcome::Refused);
  R1_EXPECT(e.assign("free.one", 0, chordOf(Key::Unknown)) == AssignOutcome::Refused);
  R1_EXPECT(s.overrides.size() == 0 && e.pending() == nullptr);
  R1_EXPECT(e.assign("free.one", 0, chordOf(letter('Q'), Mod::kAlt)) == AssignOutcome::Assigned);
  R1_EXPECT(e.assign("free.one", 0, chordOf(letter('Q'), Mod::kAlt)) == AssignOutcome::Unchanged);
  R1_EXPECT(e.clearSlot("free.one", 0) && !s.effective("free.one", 0));
  R1_EXPECT(!e.clearSlot("free.one", 0) && !e.clearSlot("nope", 0) && !e.clearSlot("free.one", 5));
  // A two-step sequence can be assigned through the API.
  R1_EXPECT(e.assign("free.two", 0, cmd::ChordSequence::pair({letter('X'), Mod::kCtrl, false}, {letter('Y'), 0, false})) == AssignOutcome::Assigned);
  R1_EXPECT(text(s.effective("free.two", 0)) == "Ctrl+X, Y");
}

void testRuntimeCommandTab() {
  HotkeyScene s;
  HotkeyEditor& e = *s.editor;
  e.setTab(HotkeyEditor::Tab::Command);
  s.t.layout();
  R1_EXPECT(!e.detailView().detail().valid);
  R1_EXPECT(!s.t.ui.object(e.assignButton())->enabled() && !s.t.ui.object(e.clearButton())->enabled() && !s.t.ui.object(e.resetButton())->enabled());
  e.selectAction("edit.undo");
  s.t.layout();
  R1_EXPECT(s.t.ui.object(e.assignButton())->enabled() && s.t.ui.object(e.clearButton())->enabled() && !s.t.ui.object(e.resetButton())->enabled());
  R1_EXPECT(e.recorder().preview().empty() && !e.recorder().recording());
  // Assign records a chord: modifiers show live, a real key completes.
  s.click(e.assignButton());
  R1_EXPECT(e.recorder().recording() && s.t.ui.router().focused() == e.recorder().id());
  s.key(Key::Unknown, Mod::kCtrl | Mod::kAlt);
  R1_EXPECT(e.recorder().preview() == "Ctrl+Alt+");
  s.key(letter('U'), Mod::kCtrl | Mod::kAlt);
  R1_EXPECT(!e.recorder().recording() && text(s.effective("edit.undo", 0)) == "Ctrl+Alt+U");
  R1_EXPECT(s.t.ui.object(e.resetButton())->enabled());
  // Escape cancels; Escape with a modifier is a chord.
  s.click(e.assignButton());
  s.key(Key::Escape);
  R1_EXPECT(!e.recorder().recording() && text(s.effective("edit.undo", 0)) == "Ctrl+Alt+U");
  s.click(e.assignButton());
  s.key(Key::Escape, Mod::kShift);
  R1_EXPECT(text(s.effective("edit.undo", 0)) == "Shift+Escape");
  // Losing the focus cancels a recording.
  s.click(e.assignButton());
  s.t.ui.focusWidget(e.list().searchField());
  R1_EXPECT(!e.recorder().recording());
  // Typed characters do not reach the chord; releases are swallowed.
  s.click(e.assignButton());
  s.t.ui.textInput(U'x');
  s.t.ui.keyUp(letter('X'));
  R1_EXPECT(e.recorder().recording());
  s.key(letter('Q'));
  R1_EXPECT(text(s.effective("edit.undo", 0)) == "Q");
  // A conflict while recording opens the dialog.
  s.click(e.assignButton());
  s.key(letter('S'), Mod::kCtrl);
  R1_EXPECT(e.pending() != nullptr && e.conflictDialogOpen());
  e.resolveConflict(ConflictResolution::Cancel);
  // Slot selection applies to the recorder, Clear and the idle text.
  e.setSlot(1);
  s.click(e.assignButton());
  s.key(letter('W'), Mod::kAlt);
  R1_EXPECT(text(s.effective("edit.undo", 1)) == "Alt+W" && text(s.effective("edit.undo", 0)) == "Q");
  s.click(e.clearButton());
  R1_EXPECT(!s.effective("edit.undo", 1));
  e.setSlot(0);
  s.click(e.clearButton());
  R1_EXPECT(!s.effective("edit.undo", 0) && !s.t.ui.object(e.clearButton())->enabled());
  s.click(e.resetButton());
  R1_EXPECT(text(s.effective("edit.undo", 0)) == "Ctrl+Z" && s.overrides.size() == 0);
  // Switching back to the keyboard tab ends a recording.
  s.click(e.assignButton());
  e.setTab(HotkeyEditor::Tab::Keyboard);
  R1_EXPECT(!e.recorder().recording());
}

void testFilters() {
  HotkeyScene s;
  HotkeyEditor& e = *s.editor;
  e.setModifiers(Mod::kCtrl);
  e.setCategory("File");
  R1_EXPECT((e.list().view().categories().size() == 5) && e.list().view().matchCount() == 2);
  R1_EXPECT(e.keyboard().assigned(letter('S')) && !e.keyboard().assigned(letter('C')));
  R1_EXPECT(e.caption().find("hotkeys for: File") != std::string::npos);
  Select* category = s.t.ui.objectAs<Select>(e.categorySelect());
  R1_EXPECT(category->selectedValue() == "File");
  category->setSelectedValue("Edit");
  e.setCategory("Edit");
  R1_EXPECT(e.keyboard().assigned(letter('C')) && !e.keyboard().assigned(letter('S')));
  e.setCategory("");
  R1_EXPECT(category->selectedValue() == "*" && e.list().view().matchCount() == 13);
  // Context filter: a panel context lists its own and the global commands; global lists no panel commands.
  e.setContextFilter("global");
  R1_EXPECT(e.list().view().matchCount() == 11 && e.keyboard().usageFilter().context == "global");
  e.setContextFilter("layers");
  R1_EXPECT(e.list().view().matchCount() == 13);
  e.setContextFilter("");
  R1_EXPECT(e.selectAction("layers.hide"));
  e.setContextFilter("global");
  R1_EXPECT(!e.selectAction("layers.hide"));  // outside the filter
  e.setContextFilter("");
  // Search: label, description, id, category; clearing restores.
  e.setFilter("delete");
  R1_EXPECT(e.list().view().matchCount() == 2);
  e.setFilter("layers hide");
  R1_EXPECT(e.list().view().matchCount() == 1);
  e.setFilter("ctrl+z");  // the hotkey text is searched too (Undo only: Redo is Ctrl+Shift+Z)
  R1_EXPECT(e.list().view().matchCount() == 1 && e.list().view().selectedAction()->id == "edit.undo");
  e.setFilter("does what it says");
  R1_EXPECT(e.list().view().matchCount() == 13);
  e.setFilter("");
  // The select lists follow the registry.
  s.declare("new.one", "Brand new", "Extras");
  s.t.layout();
  R1_EXPECT(category->entries().size() == 7);  // all + 6 categories
  e.setCategory("Extras");
  R1_EXPECT(e.list().view().matchCount() == 1);
  s.registry.remove("new.one");
  s.t.layout();
}

void testModifierButtons() {
  HotkeyScene s;
  HotkeyEditor& e = *s.editor;
  s.click(e.modifierButton(Mod::kCtrl));
  R1_EXPECT(e.modifiers() == Mod::kCtrl && e.keyboard().toggledModifiers() == Mod::kCtrl);
  R1_EXPECT(s.t.ui.objectAs<Button>(e.modifierButton(Mod::kCtrl))->tone() == ButtonTone::Accent);
  s.click(e.modifierButton(Mod::kShift));
  R1_EXPECT(e.modifiers() == (Mod::kCtrl | Mod::kShift) && e.keyboard().assigned(letter('Z')) && !e.keyboard().assigned(letter('C')));
  s.click(e.modifierButton(Mod::kCtrl));
  s.click(e.modifierButton(Mod::kShift));
  R1_EXPECT(e.modifiers() == 0 && s.t.ui.objectAs<Button>(e.modifierButton(Mod::kShift))->tone() == ButtonTone::Neutral);
  // The on-screen modifier caps and the buttons are the same state.
  s.clickCap("lalt");
  R1_EXPECT(e.modifiers() == Mod::kAlt && s.t.ui.objectAs<Button>(e.modifierButton(Mod::kAlt))->tone() == ButtonTone::Accent);
  // Physical modifiers combine with the toggles and come back off.
  e.setPhysicalModifiers(Mod::kCtrl);
  R1_EXPECT(e.modifiers() == (Mod::kCtrl | Mod::kAlt));
  e.setPhysicalModifiers(0);
  R1_EXPECT(e.modifiers() == Mod::kAlt);
  R1_EXPECT(e.modifierButton(0).valid() == false);
}

void testSets() {
  HotkeyScene s;
  HotkeyEditor& e = *s.editor;
  std::vector<std::pair<std::string, std::string>> saved;
  e.setOnSetsChanged([&](const std::string& n, const std::string& j) { saved.push_back({n, j}); });
  e.assign("free.one", 0, chordOf(letter('Q'), Mod::kAlt));
  R1_EXPECT(e.saveSet("  Mine  ") && e.setName() == "Mine");
  R1_EXPECT((e.setNames() == std::vector<std::string>{"Default", "Mine"}));
  R1_EXPECT(saved.size() == 1 && saved[0].first == "Mine" && saved[0].second.find("free.one") != std::string::npos);
  Select* select = s.t.ui.objectAs<Select>(e.setSelect());
  R1_EXPECT(select->selectedValue() == "Mine" && select->entries().size() == 2);
  // Changing the bindings and choosing Default restores the original bindings (no overrides at start).
  e.assign("free.two", 0, chordOf(letter('W'), Mod::kAlt));
  R1_EXPECT(e.loadSet("Default") && e.setName() == "Default");
  R1_EXPECT(s.overrides.size() == 0 && !s.effective("free.one", 0) && !s.effective("free.two", 0));
  R1_EXPECT(e.loadSet("Mine") && text(s.effective("free.one", 0)) == "Alt+Q" && !s.effective("free.two", 0));
  // The drop-down loads a set.
  select->setSelectedValue("Default");
  e.loadSet("Default");
  R1_EXPECT(select->selectedValue() == "Default");
  // Refused: empty names, unknown set, corrupt data (the state stays).
  R1_EXPECT(!e.saveSet("") && !e.saveSet("   ") && !e.loadSet("nope"));
  R1_EXPECT(e.addSet("Broken", "this is not json"));
  e.assign("free.one", 0, chordOf(letter('Q'), Mod::kAlt));
  R1_EXPECT(!e.loadSet("Broken") && text(s.effective("free.one", 0)) == "Alt+Q" && e.setName() == "Default");
  R1_EXPECT(e.addSet("Fromhost", "{\"format\":\"r1ui-keybindings\",\"version\":1,\"overrides\":[{\"command\":\"free.two\",\"slot\":0,\"chord\":\"Alt+W\"}]}"));
  R1_EXPECT(e.loadSet("Fromhost") && text(s.effective("free.two", 0)) == "Alt+W" && !s.effective("free.one", 0));
  // Names are sanitised and bounded; the table has a limit.
  R1_EXPECT(e.saveSet(std::string(500, 'n')) && e.setName().size() == kMaxHotkeySetNameBytes);
  for (size_t i = 0; i < kMaxHotkeySets + 10; ++i) e.saveSet("set " + std::to_string(i));
  R1_EXPECT(e.setNames().size() == kMaxHotkeySets);
  R1_EXPECT(!e.saveSet("one more") && e.setJson("one more") == nullptr);
  R1_EXPECT(e.saveSet("set 3") && e.setNames().size() == kMaxHotkeySets);  // replacing an existing name is allowed
  // The Save as dialog.
  e.openSaveSetDialog();
  s.t.layout();
  R1_EXPECT(e.saveSetDialogOpen());
  e.openSaveSetDialog();
  R1_EXPECT(s.t.ui.overlays().count() == 1);
  R1_EXPECT(closeDialog(s.t.ui, e.saveSetDialog(), "cancel"));
  s.t.layout();
  R1_EXPECT(!e.saveSetDialogOpen());
}

void testResetAllAndHooks() {
  HotkeyScene s;
  HotkeyEditor& e = *s.editor;
  e.assign("free.one", 0, chordOf(letter('Q'), Mod::kAlt));
  e.requestResetAll();
  s.t.layout();
  R1_EXPECT(e.resetAllDialogOpen());
  e.requestResetAll();
  R1_EXPECT(s.t.ui.overlays().count() == 1);
  R1_EXPECT(closeDialog(s.t.ui, e.resetAllDialog(), "cancel"));
  s.t.layout();
  R1_EXPECT(s.overrides.size() > 0);
  e.requestResetAll();
  s.t.layout();
  R1_EXPECT(closeDialog(s.t.ui, e.resetAllDialog(), "reset"));
  s.t.layout();
  R1_EXPECT(s.overrides.size() == 0 && !s.effective("free.one", 0));
}

void testLiveAndLayout() {
  HotkeyScene s;
  HotkeyEditor& e = *s.editor;
  int imports = 0;
  e.setOnImport([&] { ++imports; });
  R1_EXPECT(s.t.ui.object(e.list().id()) != nullptr);
  // A command added or removed shows in the list at once.
  s.declare("late.one", "Late arrival", "Late");
  s.t.layout();
  R1_EXPECT(e.list().view().actions().size() == 14);
  e.selectAction("late.one");
  s.registry.remove("late.one");
  s.t.layout();
  R1_EXPECT(e.selectedAction().empty() && !e.detailView().detail().valid && e.list().view().actions().size() == 13);
  // Narrow editors stack the columns: the keyboard moves below the list.
  const double listY = s.t.ui.absRect(e.list().id()).y;
  const double wideKeyboardY = s.t.ui.absRect(e.keyboard().id()).y;
  R1_EXPECT(wideKeyboardY < listY + 200);
  e.style().width = r1ui::core::layout::Length::px(700);
  e.requestLayout();
  s.t.layout();
  s.t.layout();
  R1_EXPECT(s.t.ui.absRect(e.keyboard().id()).y > s.t.ui.absRect(e.list().id()).y + 50);
  s.paintOnce();
  e.style().width = r1ui::core::layout::Length::px(1250);
  e.requestLayout();
  s.t.layout();
  s.t.layout();
  R1_EXPECT(s.t.ui.absRect(e.keyboard().id()).y < s.t.ui.absRect(e.list().id()).y + 200);
  // Destroying the editor unsubscribes: registry changes afterwards are safe.
  s.t.ui.destroy(e.id());
  s.declare("after.one", "After", "Late");
  s.t.layout();
}

}  // namespace

int main() {
  testStructure();
  testSelectionAndLayer();
  testAssignByClick();
  testClickWithoutSelection();
  testConflictReplace();
  testConflictKeepBoth();
  testAssignApi();
  testRuntimeCommandTab();
  testFilters();
  testModifierButtons();
  testSets();
  testResetAllAndHooks();
  testLiveAndLayout();
  return r1test::finish();
}

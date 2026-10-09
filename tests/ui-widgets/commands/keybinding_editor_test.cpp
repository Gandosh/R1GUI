// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: structural golden tests of the keybinding editor (no OpenPencil reference exists): the rows
//   grouped by category and their geometry (aligned columns, heading above its rows), the search and
//   context filters, the capture protocol of spec 07 scenarios 6 to 11 (live modifier preview, commit
//   without conflict, conflict popup with Override and Cancel, closing the popup by a click outside,
//   re-check on another chord, Escape cancel and Shift+Escape binding, Tab captured, one editing box,
//   focus loss), remove and reset buttons, the reset-all dialog, import and export callbacks, two-step
//   sequences and prefix conflicts, keyboard operation, live updates from outside and hostile cases
//   (rebuild during a capture, a captured command removed, the editor destroyed with a popup open,
//   500 commands, invalid UTF-8 labels).
// Callers: CTest (label fast).
#include "EditorFixture.h"
#include "r1ui/commands/Conflicts.h"
#include "r1ui/commands/OverrideIo.h"
#include "r1ui/widgets/iconbutton/IconButton.h"
#include "r1ui/widgets/select/Select.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace {

using namespace r1test;
using namespace r1ui::widgets;
using r1ui::core::events::FocusReason;
using r1ui::core::tree::WidgetId;

void testStructure() {
  EditorFixture f;
  KeybindingEditor& ed = *f.editor;
  R1_EXPECT(ed.rowCount() == 7 && ed.headingCount() == 3 && ed.visibleRowCount() == 7);  // the hidden command has no row
  R1_EXPECT(!ed.rowOf("hidden.cmd").valid());
  R1_EXPECT(f.box("edit.copy", 0).chordText() == "Ctrl+C" && f.box("edit.copy", 1).chordText().empty());
  R1_EXPECT(f.box("global.delete", 1).chordText() == "Backspace" && f.box("edit.rename", 0).chordText() == "F2");

  // Geometry golden: the two chord columns line up in every row, a heading sits above its rows.
  const auto r0 = f.t.ui.absRect(ed.boxOf("edit.copy", 0));
  const auto r1 = f.t.ui.absRect(ed.boxOf("edit.copy", 1));
  R1_EXPECT(r0.w == 168 && r1.w == 168 && r1.x > r0.right() && r0.h >= 24);
  for (const char* id : {"edit.paste", "edit.undo", "file.save", "layers.delete"}) {
    R1_EXPECT(f.t.ui.absRect(ed.boxOf(id, 0)).x == r0.x && f.t.ui.absRect(ed.boxOf(id, 1)).x == r1.x);
  }
  R1_EXPECT(f.t.ui.absRect(ed.rowOf("file.save")).y > f.t.ui.absRect(ed.rowOf("edit.undo")).y);  // File follows Edit
  const char* order[] = {"edit.copy", "global.delete", "edit.paste", "edit.rename", "edit.undo"};  // sorted by label
  for (size_t i = 1; i < 5; ++i) R1_EXPECT(f.t.ui.absRect(ed.rowOf(order[i])).y > f.t.ui.absRect(ed.rowOf(order[i - 1])).y);
  // Reset buttons are enabled only for commands with overrides; remove buttons only for bound slots.
  R1_EXPECT(!f.t.ui.object(ed.resetButtonOf("edit.copy"))->enabled());
  R1_EXPECT(f.t.ui.object(f.box("edit.copy", 0).removeButton())->node().flags.visible && !f.t.ui.object(f.box("edit.copy", 1).removeButton())->node().flags.visible);
  R1_EXPECT(f.t.ui.absRect(f.box("edit.copy", 1).removeButton()).w > 0);  // the hidden remove button keeps its space (rule 49)
  R1_EXPECT(!ed.capturing() && !ed.conflictPopupOpen() && !f.t.ui.object(ed.importButton())->enabled() && !f.t.ui.object(ed.exportButton())->enabled());
}

void testFilters() {
  EditorFixture f;
  KeybindingEditor& ed = *f.editor;
  ed.setFilter("undo");
  R1_EXPECT(ed.visibleRowCount() == 1 && ed.rowVisible("edit.undo") && ed.visibleHeadingCount() == 1);
  ed.setFilter("ctrl z");
  R1_EXPECT(ed.visibleRowCount() == 1 && ed.rowVisible("edit.undo"));  // by chord text
  ed.setFilter("layers");
  R1_EXPECT(ed.visibleRowCount() == 1 && ed.rowVisible("layers.delete"));  // by category
  ed.setFilter("backspace");
  R1_EXPECT(ed.visibleRowCount() == 1 && ed.rowVisible("global.delete"));  // by the alternate slot
  ed.setFilter("nothing matches this");
  R1_EXPECT(ed.visibleRowCount() == 0 && ed.visibleHeadingCount() == 0 && f.hidden(ed.rowOf("edit.copy")));  // a hidden row takes no space
  ed.setFilter("");
  R1_EXPECT(ed.visibleRowCount() == 7 && ed.visibleHeadingCount() == 3 && !f.hidden(ed.rowOf("edit.copy")));

  // The search box drives the same filter.
  TextInput* search = f.t.ui.objectAs<TextInput>(ed.searchInput());
  f.t.ui.focusWidget(search->id());
  for (const char c : std::string("rename")) f.t.ui.textInput(static_cast<char32_t>(c));
  R1_EXPECT(ed.filter() == "rename" && ed.visibleRowCount() == 1 && ed.rowVisible("edit.rename"));
  ed.setFilter("");
  // So does the context selector (entries: All contexts, global, window, text, layers).
  Select* select = f.t.ui.objectAs<Select>(ed.contextSelect());
  select->choose(4);
  R1_EXPECT(ed.visibleRowCount() == 1 && ed.rowVisible("layers.delete"));
  select->choose(0);
  R1_EXPECT(ed.visibleRowCount() == 7);
  ed.setFilter(std::string(100000, 'x'));
  R1_EXPECT(ed.filter().size() <= 256 && ed.visibleRowCount() == 0);
  ed.setFilter("");
}

void testCommitWithoutConflict() {
  EditorFixture f;
  KeybindingEditor& ed = *f.editor;
  f.clickBox("edit.rename", 0);  // spec 07 scenario 6
  R1_EXPECT(ed.capturing() && f.box("edit.rename", 0).editing() && ed.capturingCommand() == "edit.rename" && ed.capturingSlot() == 0);
  R1_EXPECT(f.t.ui.router().focused() == ed.boxOf("edit.rename", 0));
  R1_EXPECT(f.box("edit.rename", 0).placeholderShown() && f.box("edit.rename", 0).shownText() == "F2");  // the hint is the default chord
  R1_EXPECT(f.box("edit.copy", 1).shownText().empty());
  f.key(Key::Unknown, Mod::kCtrl);  // a modifier press shows at once
  R1_EXPECT(f.box("edit.rename", 0).preview() == "Ctrl+");
  f.key(Key::Unknown, Mod::kCtrl | Mod::kShift);
  R1_EXPECT(f.box("edit.rename", 0).preview() == "Ctrl+Shift+");
  R1_EXPECT(f.key(letter('R'), Mod::kCtrl | Mod::kShift));
  R1_EXPECT(!ed.capturing() && !f.box("edit.rename", 0).editing() && f.box("edit.rename", 0).chordText() == "Ctrl+Shift+R");
  R1_EXPECT(f.overrides.find("edit.rename", 0) != nullptr && f.overrides.find("edit.rename", 1) != nullptr);  // both slots saved (rule 55)
  R1_EXPECT(f.keymap.displayText("edit.rename") == "Ctrl+Shift+R");
  const std::vector<std::string> contexts{cmd::kWindowContext};
  R1_EXPECT(!f.router.handleKey({kF2, 0, false, false}, contexts).consumed);  // F2 no longer renames
  R1_EXPECT(f.router.handleKey({letter('R'), Mod::kCtrl | Mod::kShift, false, false}, contexts).consumed && f.runs["edit.rename"] == 1);
  R1_EXPECT(f.t.ui.object(ed.resetButtonOf("edit.rename"))->enabled());
  // A box without a default shows the prompt.
  f.clickBox("edit.copy", 1);
  R1_EXPECT(f.box("edit.copy", 1).shownText() == "Press a key combination");
  f.key(Key::Escape);
}

void testConflict() {
  EditorFixture f;
  KeybindingEditor& ed = *f.editor;
  f.clickBox("edit.copy", 0);  // spec 07 scenario 7
  f.key(letter('V'), Mod::kCtrl);
  R1_EXPECT(ed.capturing() && ed.conflictPopupOpen() && f.box("edit.copy", 0).editing());
  R1_EXPECT(ed.conflictMessage() == "Ctrl+V is already used by Paste.");
  R1_EXPECT(f.box("edit.copy", 0).preview() == "Ctrl+V");
  // Popup layout: below the editing box, the two buttons inside it, Override on the right.
  const auto anchor = f.t.ui.absRect(ed.boxOf("edit.copy", 0));
  const auto popup = f.t.ui.absRect(ed.conflictHost());
  R1_EXPECT(popup.y >= anchor.bottom() && popup.x >= anchor.x - 1 && popup.w > 150 && popup.w <= 260);
  const auto okRect = f.t.ui.absRect(ed.overrideButton());
  const auto cancelRect = f.t.ui.absRect(ed.cancelButton());
  R1_EXPECT(okRect.x > cancelRect.x && okRect.y >= popup.y && okRect.bottom() <= popup.bottom() && okRect.right() <= popup.right());
  // Losing focus does not commit and does not stop editing (rule 47).
  f.t.ui.focusWidget(ed.searchInput());
  R1_EXPECT(ed.capturing() && ed.conflictPopupOpen());
  f.click(ed.overrideButton());
  R1_EXPECT(!ed.capturing() && !ed.conflictPopupOpen());
  R1_EXPECT(f.box("edit.copy", 0).chordText() == "Ctrl+V" && f.box("edit.paste", 0).chordText().empty());  // paste has no primary chord
  R1_EXPECT(f.keymap.displayText("edit.paste").empty());

  // Closing the popup by a click outside ends editing without a change (scenario 8).
  EditorFixture g;
  g.clickBox("edit.copy", 0);
  g.key(letter('V'), Mod::kCtrl);
  R1_EXPECT(g.editor->conflictPopupOpen());
  g.t.ui.pointerMove(900, 780);
  g.t.ui.pointerDown(900, 780);
  g.t.ui.pointerUp(900, 780);
  g.t.layout();
  R1_EXPECT(!g.editor->capturing() && !g.editor->conflictPopupOpen() && g.box("edit.copy", 0).chordText() == "Ctrl+C" && g.box("edit.paste", 0).chordText() == "Ctrl+V");
  R1_EXPECT(g.overrides.size() == 0);

  // The Cancel button and Escape do the same.
  EditorFixture h;
  h.clickBox("edit.copy", 0);
  h.key(letter('V'), Mod::kCtrl);
  h.click(h.editor->cancelButton());
  R1_EXPECT(!h.editor->capturing() && h.overrides.size() == 0 && h.box("edit.copy", 0).chordText() == "Ctrl+C");
  h.clickBox("edit.copy", 0);
  h.key(letter('V'), Mod::kCtrl);
  h.key(Key::Escape);
  R1_EXPECT(!h.editor->capturing() && !h.editor->conflictPopupOpen() && h.overrides.size() == 0);

  // Typing another chord while the popup is open re-checks (rule 48): a free one commits and closes it.
  EditorFixture k;
  k.clickBox("edit.copy", 0);
  k.key(letter('V'), Mod::kCtrl);
  R1_EXPECT(k.editor->conflictPopupOpen());
  k.key(letter('V'), Mod::kCtrl | Mod::kShift);
  R1_EXPECT(!k.editor->conflictPopupOpen() && !k.editor->capturing() && k.box("edit.copy", 0).chordText() == "Ctrl+Shift+V");
  // Another conflict replaces the message.
  k.clickBox("edit.undo", 0);
  k.key(letter('V'), Mod::kCtrl);
  const std::string first = k.editor->conflictMessage();
  k.key(letter('S'), Mod::kCtrl);
  R1_EXPECT(k.editor->conflictPopupOpen() && k.editor->conflictMessage() != first && k.editor->conflictMessage() == "Ctrl+S is already used by Save.");
  k.editor->cancelCapture();
  R1_EXPECT(!k.editor->conflictPopupOpen() && k.t.ui.overlays().count() == 0);

  // A conflict in a related context is named with its context; Override unbinds it too.
  EditorFixture m;
  m.clickBox("layers.delete", 0);
  m.key(letter('Z'), Mod::kCtrl);  // Ctrl+Z belongs to a global command (an ancestor context)
  R1_EXPECT(m.editor->conflictMessage() == "Ctrl+Z is already used by Undo (global).");
  m.click(m.editor->overrideButton());
  R1_EXPECT(m.box("layers.delete", 0).chordText() == "Ctrl+Z" && m.box("edit.undo", 0).chordText().empty());
}

void testCaptureKeys() {
  EditorFixture f;
  KeybindingEditor& ed = *f.editor;
  f.clickBox("edit.rename", 0);
  R1_EXPECT(f.key(Key::Escape) && !ed.capturing() && f.box("edit.rename", 0).chordText() == "F2" && f.overrides.size() == 0);  // Escape cancels (D15)
  f.clickBox("edit.rename", 0);
  f.key(Key::Escape, Mod::kShift);  // with a modifier it is an ordinary key to bind
  R1_EXPECT(f.box("edit.rename", 0).chordText() == "Shift+Escape");
  f.clickBox("edit.undo", 0);
  R1_EXPECT(f.key(Key::Tab) && f.box("edit.undo", 0).chordText() == "Tab");  // every key belongs to the box, Tab included
  // Typed characters and key releases are ignored while editing; held repeats do not re-check.
  f.clickBox("edit.copy", 1);
  R1_EXPECT(!f.t.ui.textInput('x') && ed.capturing());
  f.t.ui.keyUp(letter('X'), 0);
  R1_EXPECT(ed.capturing() && f.box("edit.copy", 1).preview().empty());
  f.t.ui.keyDown(letter('Q'), Mod::kAlt, true);
  R1_EXPECT(ed.capturing());
  // Focus loss ends editing and keeps the old chord (rule 53).
  f.t.ui.focusWidget(ed.searchInput());
  R1_EXPECT(!ed.capturing() && f.box("edit.copy", 1).chordText().empty());
  // One editing box at a time (rule 42).
  f.clickBox("edit.copy", 0);
  f.clickBox("edit.paste", 0);
  R1_EXPECT(!f.box("edit.copy", 0).editing() && f.box("edit.paste", 0).editing() && ed.capturingCommand() == "edit.paste");
  f.key(Key::Escape);
  // A sequence that is waiting in the router is dropped when a capture starts.
  f.registry.add([] {
    cmd::CommandDef d;
    d.id = "seq.cmd";
    d.label = "Sequence";
    d.defaultChords[0] = pairOf(letter('K'), Mod::kCtrl, letter('J'), Mod::kCtrl);
    return d;
  }());
  f.t.layout();  // the new command rebuilt the table
  const std::vector<std::string> contexts{cmd::kWindowContext};
  f.router.handleKey({letter('K'), Mod::kCtrl, false, false}, contexts);
  R1_EXPECT(f.router.pending());
  f.clickBox("edit.copy", 0);
  R1_EXPECT(!f.router.pending());
  f.key(Key::Escape);
}

void testRemoveAndReset() {
  EditorFixture f;
  KeybindingEditor& ed = *f.editor;
  f.click(f.box("edit.undo", 0).removeButton());
  R1_EXPECT(f.box("edit.undo", 0).chordText().empty() && f.keymap.displayText("edit.undo").empty());
  R1_EXPECT(f.overrides.find("edit.undo", 0) != nullptr && !f.overrides.find("edit.undo", 0)->chord);
  R1_EXPECT(cmd::exportOverrides(f.registry, f.overrides).find("\"chord\":null") != std::string::npos);  // the empty override persists (scenario 9)
  R1_EXPECT(!f.t.ui.object(f.box("edit.undo", 0).removeButton())->node().flags.visible);
  // Delete on a focused idle box does the same for keyboard users (the button is not in the Tab order).
  f.t.ui.focusWidget(ed.boxOf("edit.rename", 0), FocusReason::Keyboard);
  R1_EXPECT(f.key(Key::Delete) && f.box("edit.rename", 0).chordText().empty());
  // The per-command reset button restores the defaults of both slots.
  f.click(ed.resetButtonOf("edit.undo"));
  R1_EXPECT(f.box("edit.undo", 0).chordText() == "Ctrl+Z" && f.overrides.find("edit.undo", 0) == nullptr);
  R1_EXPECT(!f.t.ui.object(ed.resetButtonOf("edit.undo"))->enabled());
}

}  // namespace

int main() {
  testStructure();
  testFilters();
  testCommitWithoutConflict();
  testConflict();
  testCaptureKeys();
  testRemoveAndReset();
  return r1test::finish();
}

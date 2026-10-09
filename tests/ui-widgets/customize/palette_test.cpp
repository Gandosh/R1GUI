// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of the command palette and the command-picker dialog: the golden rows (categories in order,
//   label, icon, shortcut text), the search over label, id, description, category and shortcut, selection
//   by click and keys (headers are skipped), scrolling, Enter and double-click choosing, live following of
//   the registry (a command added or removed, a chord rebound), hiddenFromEditor, the drag start (ghost
//   overlay, Escape cancels), and the picker: search focus, arrow keys, Enter, double-click, Add, Cancel
//   and Escape.
// Callers: CTest (label fast).
#include <algorithm>

#include "CustomizeFixture.h"
#include "r1ui/commands/Conflicts.h"
#include "r1ui/widgets/customize/CommandPalette.h"
#include "r1ui/widgets/customize/CommandPicker.h"
#include "r1ui/widgets/dialog/DialogParts.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace {

using namespace r1test;
using namespace r1ui::widgets;
using r1ui::core::layout::RectD;

struct PaletteScene : CustomizeFixture {
  PaletteScene() {
    palette = &t.ui.create<CommandPalette>(t.ui.root(), controller);
    palette->style().width = r1ui::core::layout::Length::px(300);
    palette->style().height = r1ui::core::layout::Length::px(260);
    palette->style().flexShrink = 0.0;
    t.layout();
  }
  CommandPaletteList& list() { return palette->list(); }
  RectD rowRect(const std::string& commandId) {
    const auto& rows = palette->rows();
    for (size_t i = 0; i < rows.size(); ++i) {
      if (!rows[i].header && rows[i].commandId == commandId) return list().rowRect(static_cast<int>(i));
    }
    return {};
  }
  std::vector<std::string> ids() {
    std::vector<std::string> out;
    for (const PaletteRow& r : palette->rows()) {
      if (!r.header) out.push_back(r.commandId);
    }
    return out;
  }
  std::vector<std::string> headers() {
    std::vector<std::string> out;
    for (const PaletteRow& r : palette->rows()) {
      if (r.header) out.push_back(r.text);
    }
    return out;
  }
  CommandPalette* palette = nullptr;
};

bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

void testRows() {
  PaletteScene s;
  R1_EXPECT((s.headers() == std::vector<std::string>{"Customize", "Edit", "File", "Help", "Tools", "View"}));
  R1_EXPECT(s.ids().size() == 15 + 4);  // the fixture's commands and the four customize commands
  // Inside a category the registration order stays.
  std::vector<std::string> edit;
  const auto& rows = s.palette->rows();
  bool in = false;
  for (const PaletteRow& r : rows) {
    if (r.header) in = r.text == "Edit";
    else if (in) edit.push_back(r.commandId);
  }
  R1_EXPECT((edit == std::vector<std::string>{"edit.undo", "edit.redo", "edit.cut", "edit.copy", "edit.paste"}));
  const PaletteRow* undo = nullptr;
  for (const PaletteRow& r : rows) {
    if (r.commandId == "edit.undo") undo = &r;
  }
  R1_EXPECT(undo != nullptr && undo->text == "Undo" && undo->icon == "undo2" && undo->shortcut == "Ctrl+Z" && undo->description == "Undo description");
  // A command without icon gets a neutral one; hidden-from-editor commands are not listed.
  cmd::CommandDef plain;
  plain.id = "misc.plain";
  plain.label = "Plain";
  R1_EXPECT(s.registry.add(plain).ok);
  cmd::CommandDef hidden;
  hidden.id = "misc.hidden";
  hidden.label = "Hidden";
  hidden.hiddenFromEditor = true;
  R1_EXPECT(s.registry.add(hidden).ok);
  R1_EXPECT(contains(s.ids(), "misc.plain") && !contains(s.ids(), "misc.hidden"));
  bool neutral = false;
  for (const PaletteRow& r : s.palette->rows()) neutral = neutral || (r.commandId == "misc.plain" && r.icon == "circle");
  R1_EXPECT(neutral);
  R1_EXPECT(contains(s.headers(), "General"));
  s.registry.remove("misc.plain");
  R1_EXPECT(!contains(s.ids(), "misc.plain") && !contains(s.headers(), "General"));
  // A rebound chord shows at once.
  R1_EXPECT(cmd::assignChord(s.overrides, s.keymap, s.registry, "edit.undo", 0, chordOf(letter('U'), Mod::kAlt), false).ok);
  bool updated = false;
  for (const PaletteRow& r : s.palette->rows()) updated = updated || (r.commandId == "edit.undo" && r.shortcut == "Alt+U");
  R1_EXPECT(updated);
}

void testSearch() {
  PaletteScene s;
  s.palette->setFilter("zoom");
  R1_EXPECT(s.ids() == std::vector<std::string>{"view.zoomIn"} && s.headers() == std::vector<std::string>{"View"});
  R1_EXPECT(s.list().selectedIndex() >= 0 && s.palette->selectedCommand() == "view.zoomIn");  // a search selects its first hit
  s.palette->setFilter("ctrl+s");                                                              // the shortcut text matches
  R1_EXPECT(s.ids() == std::vector<std::string>{"file.save"});
  s.palette->setFilter("EDIT DESCRIPTION");                                                   // case, several words, category and description
  R1_EXPECT(s.ids().size() == 5 && s.headers() == std::vector<std::string>{"Edit"});
  s.palette->setFilter("undo description");
  R1_EXPECT(s.ids() == std::vector<std::string>{"edit.undo"});
  s.palette->setFilter("tools");                                                              // the category matches
  R1_EXPECT(s.ids().size() == 5);
  s.palette->setFilter("tool.p");                                                             // the id matches
  R1_EXPECT(s.ids() == std::vector<std::string>{"tool.pen"});
  s.palette->setFilter("nothing like this");
  R1_EXPECT(s.ids().empty() && s.palette->rows().empty() && s.palette->selectedCommand().empty());
  s.palette->setFilter("");
  R1_EXPECT(s.ids().size() == 19);
  // Typing in the search field filters; the field shows the text.
  TextInput* field = s.t.ui.objectAs<TextInput>(s.palette->searchField());
  s.palette->focusSearch();
  s.type("gr");
  s.t.layout();
  R1_EXPECT(field->text() == "gr" && s.ids() == std::vector<std::string>{"view.grid"});
  // The selection survives a refilter that still shows it.
  s.palette->setFilter("");
  s.palette->select("edit.copy");
  s.palette->setFilter("e");
  R1_EXPECT(s.palette->selectedCommand() == "edit.copy");
}

void testSelectionAndKeys() {
  PaletteScene s;
  s.t.ui.focusWidget(s.palette->listWidget());
  R1_EXPECT(s.palette->selectedCommand().empty());
  s.t.ui.keyDown(Key::Down);
  R1_EXPECT(s.palette->selectedCommand() == "customize.toggle");  // the header above is skipped
  s.t.ui.keyDown(Key::Down);
  s.t.ui.keyDown(Key::Down);
  s.t.ui.keyDown(Key::Down);
  s.t.ui.keyDown(Key::Down);  // crosses into the next category: Edit
  R1_EXPECT(s.palette->selectedCommand() == "edit.undo");
  s.t.ui.keyDown(Key::Up);
  R1_EXPECT(s.palette->selectedCommand() == "customize.newMenu");
  s.t.ui.keyDown(Key::End);
  R1_EXPECT(s.palette->selectedCommand() == "view.zoomIn");
  s.t.ui.keyDown(Key::Home);
  R1_EXPECT(s.palette->selectedCommand() == "customize.toggle");
  s.t.ui.keyDown(Key::PageDown);
  R1_EXPECT(s.list().selectedIndex() > 5 && s.list().scrollOffset() >= 0.0);
  s.t.ui.keyDown(Key::End);
  R1_EXPECT(s.list().scrollOffset() > 0.0 && s.list().scrollOffset() <= s.list().contentHeight());  // the selection scrolled into view
  const auto r = s.rowRect("view.zoomIn");
  const auto area = s.t.ui.absRect(s.palette->listWidget());
  R1_EXPECT(r.h > 0 && r.y >= area.y && r.y + r.h <= area.y + area.h + 0.5);
  s.t.ui.keyDown(Key::Home);
  R1_EXPECT(s.list().scrollOffset() == 0.0);
  // A click selects a command row and ignores a header.
  const auto copy = s.rowRect("edit.copy");
  R1_EXPECT(copy.h == 0.0);  // scrolled out of view at the top: not hit-testable yet
  s.palette->select("edit.copy");
  const auto visible = s.rowRect("edit.copy");
  s.click(visible.x + 100, visible.y + visible.h / 2);
  R1_EXPECT(s.palette->selectedCommand() == "edit.copy");
  // The wheel scrolls and clamps.
  const auto at = s.t.ui.absRect(s.palette->listWidget());
  s.t.ui.wheel(at.x + 50, at.y + 50, 0, -3);
  R1_EXPECT(s.list().scrollOffset() > 0.0);
  s.t.ui.wheel(at.x + 50, at.y + 50, 0, -1000);
  R1_EXPECT(s.list().scrollOffset() == s.list().contentHeight() - at.h);
  s.t.ui.wheel(at.x + 50, at.y + 50, 0, 1000);
  R1_EXPECT(s.list().scrollOffset() == 0.0);
  // Up and Down also work while the search field has focus.
  s.palette->focusSearch();
  s.palette->select("edit.cut");
  s.t.ui.keyDown(Key::Down);
  R1_EXPECT(s.palette->selectedCommand() == "edit.copy");
}

void testChoose() {
  PaletteScene s;
  std::vector<std::string> chosen;
  s.palette->setOnChoose([&](const std::string& id) { chosen.push_back(id); });
  s.palette->select("tool.pen");
  s.t.ui.focusWidget(s.palette->listWidget());
  s.t.ui.keyDown(Key::Enter);
  R1_EXPECT(chosen == std::vector<std::string>{"tool.pen"});
  s.palette->setFilter("hand");
  const auto row = s.rowRect("tool.hand");
  s.doubleClick(row.x + 100, row.y + row.h / 2);
  R1_EXPECT(chosen.size() == 2 && chosen[1] == "tool.hand");
  // Enter in the search field chooses the selected hit.
  s.palette->setFilter("");
  s.palette->focusSearch();
  s.type("save");
  s.t.layout();
  s.t.ui.keyDown(Key::Enter);
  R1_EXPECT(chosen.size() == 3 && chosen[2] == "file.save");
  // Nothing selected: Enter chooses nothing.
  s.palette->setFilter("no such thing");
  s.t.ui.focusWidget(s.palette->listWidget());
  s.t.ui.keyDown(Key::Enter);
  R1_EXPECT(chosen.size() == 3);
}

void testDragStart() {
  PaletteScene s;
  s.palette->select("tool.pen");
  s.t.layout();
  const auto row = s.rowRect("tool.pen");
  s.t.ui.pointerMove(row.x + 60, row.y + row.h / 2);
  s.t.ui.pointerDown(row.x + 60, row.y + row.h / 2);
  s.t.ui.pointerMove(row.x + 62, row.y + row.h / 2);  // under the threshold
  R1_EXPECT(!s.controller.drag().active());
  s.t.ui.pointerMove(row.x + 90, row.y + row.h / 2 + 30);
  R1_EXPECT(s.controller.drag().active() && s.controller.drag().payload().commandId == "tool.pen" && s.controller.drag().payload().text == "Pen");
  R1_EXPECT(s.t.ui.overlays().any());  // the ghost
  s.t.ui.keyDown(Key::Escape);
  R1_EXPECT(!s.controller.drag().active() && !s.t.ui.overlays().any());
  s.t.ui.pointerUp(row.x + 90, row.y + row.h / 2 + 30);
  // A drag released over nothing drops nothing; the hub is free for the next one.
  s.drag(row.x + 60, row.y + row.h / 2, 700, 500);
  R1_EXPECT(!s.controller.drag().active() && !s.t.ui.overlays().any() && s.model.userDelta().empty());
  // A header row cannot be dragged.
  const auto header = s.list().rowRect(0);
  s.drag(header.x + 20, header.y + header.h / 2, 500, 400);
  R1_EXPECT(!s.controller.drag().active());
}

DialogButton* findButton(PaletteScene& s, WidgetId root, const std::string& label) {
  DialogButton* found = nullptr;
  s.t.ui.tree().forEachDescendant(root, [&](WidgetId id) {
    DialogButton* b = s.t.ui.objectAs<DialogButton>(id);
    if (b != nullptr && b->action().label == label) found = b;
  });
  return found;
}

void testPicker() {
  PaletteScene s;
  std::vector<std::string> chosen;
  const auto open = [&] {
    CommandPickerOptions options;
    options.onChosen = [&](const std::string& id) { chosen.push_back(id); };
    const DialogHandle handle = openCommandPicker(s.controller, std::move(options));
    s.t.layout();
    return handle;
  };
  DialogHandle dialog = open();
  R1_EXPECT(dialog.valid() && s.t.ui.overlays().anyModal());
  // The search field has the keyboard: type, then Enter takes the first hit.
  R1_EXPECT(s.t.ui.router().focused().valid() && s.t.ui.objectAs<TextInput>(s.t.ui.router().focused()) != nullptr);
  s.type("zoom");
  s.t.layout();
  s.t.ui.keyDown(Key::Enter);
  s.t.layout();
  R1_EXPECT(chosen == std::vector<std::string>{"view.zoomIn"} && !s.t.ui.overlays().any());
  // Arrow keys choose from the list; Add confirms.
  dialog = open();
  s.t.ui.keyDown(Key::Down);
  s.t.ui.keyDown(Key::Down);
  DialogButton* add = findButton(s, s.t.ui.overlays().hostOf(s.t.ui.overlays().topmost()), "Add");
  R1_EXPECT(add != nullptr);
  s.clickWidget(add->id());
  s.t.layout();
  R1_EXPECT(chosen.size() == 2 && chosen[1] == "customize.revert" && !s.t.ui.overlays().any());
  // Cancel, Escape and the close button choose nothing, even with a selection.
  dialog = open();
  s.t.ui.keyDown(Key::Down);
  s.clickWidget(findButton(s, s.t.ui.overlays().hostOf(s.t.ui.overlays().topmost()), "Cancel")->id());
  s.t.layout();
  R1_EXPECT(chosen.size() == 2 && !s.t.ui.overlays().any());
  dialog = open();
  s.t.ui.keyDown(Key::Down);
  s.t.ui.keyDown(Key::Escape);
  s.t.layout();
  R1_EXPECT(chosen.size() == 2 && !s.t.ui.overlays().any());
  // Add without a selection does nothing.
  dialog = open();
  s.clickWidget(findButton(s, s.t.ui.overlays().hostOf(s.t.ui.overlays().topmost()), "Add")->id());
  s.t.layout();
  R1_EXPECT(chosen.size() == 2);
  // Double-click on a row.
  dialog = open();
  CommandPalette* inside = nullptr;
  s.t.ui.tree().forEachDescendant(s.t.ui.overlays().hostOf(s.t.ui.overlays().topmost()), [&](WidgetId id) {
    if (inside == nullptr) inside = s.t.ui.objectAs<CommandPalette>(id);
  });
  R1_EXPECT(inside != nullptr);
  inside->setFilter("hand");
  s.t.layout();
  const auto row = inside->list().rowRect(inside->list().selectedIndex());
  s.doubleClick(row.x + 100, row.y + row.h / 2);
  s.t.layout();
  R1_EXPECT(chosen.size() == 3 && chosen[2] == "tool.hand" && !s.t.ui.overlays().any());
}

}  // namespace

int main() {
  testRows();
  testSearch();
  testSelectionAndKeys();
  testChoose();
  testDragStart();
  testPicker();
  return r1test::finish();
}

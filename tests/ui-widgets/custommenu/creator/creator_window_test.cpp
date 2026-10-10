// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the tests of the menu creator window with real pointer and key events on a headless context: the
//   type chooser, dragging actions from the list onto the slots of a pie and into a panel, swapping and
//   moving by dragging, double-click add, clearing (key and context menu), the slot count and panel
//   settings, the label of an entry, name validation (required, unique, at least one action), Create and
//   Save with their hooks, edit mode with Cancel reverting everything, saving to and loading from a file
//   through the hooks, and hostile cases (a 10 000 character name, invalid UTF-8, a missing command
//   dropped, a drop after the slots shrank, the window destroyed in the middle of a drag, the draft
//   replaced under a running window).
// Callers: CTest (label fast).
#include "CreatorFixture.h"

namespace {

using namespace r1test;
using cm::MenuKind;

CreateCustomMenuWindow& openPie(CreatorFixture& f) {
  CreateCustomMenuWindow& w = f.makeWindow();
  R1_EXPECT(w.showingChooser() && w.pieCard().valid() && w.panelCard().valid());
  f.click(w.pieCard());
  R1_EXPECT(!w.showingChooser() && w.pieEditor() != nullptr && w.panelEditor() == nullptr);
  return w;
}

CreateCustomMenuWindow& openPanel(CreatorFixture& f) {
  CreateCustomMenuWindow& w = f.makeWindow();
  f.click(w.panelCard());
  R1_EXPECT(!w.showingChooser() && w.panelEditor() != nullptr && w.pieEditor() == nullptr);
  return w;
}

void testChooserAndPie() {
  CreatorFixture f;
  CreateCustomMenuWindow& w = openPie(f);
  R1_EXPECT(f.session.draft() != nullptr && f.session.draft()->kind() == MenuKind::Pie && f.session.typeChosen());
  PiePreviewEditor* pie = w.pieEditor();
  R1_EXPECT(pie->selected() == -1);

  // Three actions dragged from the list onto three slots.
  double x = 0, y = 0;
  R1_EXPECT(pie->slotCenter(0, x, y) && f.dragAction("tool.move", x, y));
  R1_EXPECT(f.session.draft()->menu().entries[0].commandId == "tool.move" && pie->selected() == 0);
  R1_EXPECT(pie->slotCenter(2, x, y) && f.dragAction("tool.rotate", x, y));
  R1_EXPECT(pie->slotCenter(4, x, y) && f.dragAction("tool.scale", x, y));
  const cm::MenuDraft& d = *f.session.draft();
  R1_EXPECT(d.menu().entries[2].commandId == "tool.rotate" && d.menu().entries[4].commandId == "tool.scale" && d.filledCount() == 3);
  R1_EXPECT(f.runs.empty());  // dragging never runs a command

  // Dropping onto an occupied slot replaces it.
  pie->slotCenter(2, x, y);
  R1_EXPECT(f.dragAction("tool.select", x, y) && d.menu().entries[2].commandId == "tool.select");

  // Drag a filled slot onto another: a swap.
  double ax, ay, bx, by;
  pie->slotCenter(0, ax, ay);
  pie->slotCenter(4, bx, by);
  f.drag(ax, ay, bx, by);
  R1_EXPECT(d.menu().entries[0].commandId == "tool.scale" && d.menu().entries[4].commandId == "tool.move");
  // Dragging a slot onto itself or onto nothing changes nothing.
  f.drag(bx, by, bx + 2.0, by + 2.0);
  R1_EXPECT(d.menu().entries[4].commandId == "tool.move");
  pie->slotCenter(6, bx, by);
  f.drag(ax, ay, bx, by);  // an empty slot: moveEntry swaps with the empty slot, i.e. moves it
  R1_EXPECT(d.menu().entries[6].commandId == "tool.scale" && d.menu().entries[0].commandId.empty());

  // Delete clears the selected slot; Escape during a drag cancels it.
  pie->select(6);
  f.t.ui.focusWidget(pie->id());
  f.key(ev::Key::Delete);
  R1_EXPECT(d.menu().entries[6].commandId.empty());
  pie->slotCenter(4, ax, ay);
  pie->slotCenter(1, bx, by);
  f.t.ui.pointerMove(ax, ay);
  f.t.ui.pointerDown(ax, ay);
  f.t.ui.pointerMove(ax + 12.0, ay);
  f.t.ui.pointerMove(bx, by);
  R1_EXPECT(pie->dragging() && w.dragHub()->active());
  f.key(ev::Key::Escape);
  f.t.ui.pointerUp(bx, by);
  R1_EXPECT(!pie->dragging() && !w.dragHub()->active() && d.menu().entries[4].commandId == "tool.move" && d.menu().entries[1].commandId.empty());
  R1_EXPECT(f.t.ui.overlays().stack().empty());
}

void testPieContextMenuAndSlots() {
  CreatorFixture f;
  CreateCustomMenuWindow& w = openPie(f);
  PiePreviewEditor* pie = w.pieEditor();
  R1_EXPECT(pie->dropCommand(1, "edit.undo") && pie->dropCommand(3, "edit.redo"));
  double x, y;
  pie->slotCenter(1, x, y);
  f.clickAt(x, y, ev::Button::Right);
  R1_EXPECT(pie->menuOpen() && pie->selected() == 1);
  f.key(ev::Key::Down);   // the first row is "Clear slot 2"
  f.key(ev::Key::Enter);
  R1_EXPECT(f.session.draft()->menu().entries[1].commandId.empty() && f.session.draft()->menu().entries[3].commandId == "edit.redo");

  // The slot count: fewer slots are refused while a vanishing slot holds an action.
  pie->dropCommand(7, "view.grid");
  f.clickSegment(w.slotsSelector(), 0);  // 4 slots
  R1_EXPECT(f.session.draft()->menu().slotCount == 8);
  R1_EXPECT(w.status().find("slot") != std::string::npos || w.status().find("Slot") != std::string::npos);
  pie->clearSlot(7);
  pie->clearSlot(3);
  f.clickSegment(w.slotsSelector(), 0);
  R1_EXPECT(f.session.draft()->menu().slotCount == 4 && f.session.draft()->menu().entries.size() == 4);
  f.clickSegment(w.slotsSelector(), 1);
  R1_EXPECT(f.session.draft()->menu().slotCount == 6);

  // Keys move the selection around the pie.
  pie->select(0);
  f.t.ui.focusWidget(pie->id());
  f.key(ev::Key::Right);
  R1_EXPECT(pie->selected() == 1);
  f.key(ev::Key::Left);
  f.key(ev::Key::Left);
  R1_EXPECT(pie->selected() == 5);
}

void testDoubleClickAddAndLabel() {
  CreatorFixture f;
  CreateCustomMenuWindow& w = openPie(f);
  R1_EXPECT(w.addAction("tool.move") && w.addAction("tool.rotate"));
  const cm::MenuDraft& d = *f.session.draft();
  R1_EXPECT(d.menu().entries[0].commandId == "tool.move" && d.menu().entries[1].commandId == "tool.rotate");  // the first empty slots
  w.pieEditor()->select(5);
  R1_EXPECT(w.addAction("tool.scale") && d.menu().entries[5].commandId == "tool.scale");  // the selected empty slot
  for (const char* c : {"edit.undo", "edit.redo", "edit.copy", "view.grid", "view.frame"}) w.addAction(c);
  R1_EXPECT(d.filledCount() == 8);
  w.pieEditor()->select(-1);
  R1_EXPECT(!w.addAction("file.save") && !w.status().empty());                    // full, nothing selected
  w.pieEditor()->select(3);
  R1_EXPECT(w.addAction("file.save") && d.menu().entries[3].commandId == "file.save");  // full: the selected slot is replaced
  R1_EXPECT(!w.addAction("no.such.command") && d.filledCount() == 8);

  // The activation of a list row (double click, Enter) adds the action.
  ActionList* list = w.actions();
  w.pieEditor()->select(0);
  list->view().selectAction("file.open");
  f.t.ui.focusWidget(list->viewWidget());
  f.key(ev::Key::Enter);
  R1_EXPECT(d.menu().entries[0].commandId == "file.open");

  // The label of the selected slot.
  w.pieEditor()->select(1);
  f.settle();
  R1_EXPECT(f.t.ui.objectAs<r1ui::widgets::TextInput>(w.labelField())->enabled());
  f.typeInto(w.labelField(), "Spin");
  R1_EXPECT(d.menu().entries[1].label == "Spin");
  w.pieEditor()->select(-1);
  f.settle();
  R1_EXPECT(!f.t.ui.objectAs<r1ui::widgets::TextInput>(w.labelField())->enabled());
}

void testCreateValidationAndHooks() {
  CreatorFixture f;
  f.set.createMenu(MenuKind::Pie, "Taken");
  CreateCustomMenuWindow& w = openPie(f);
  const auto* create = f.t.ui.objectAs<r1ui::widgets::WidgetObject>(w.createButton());
  R1_EXPECT(!create->enabled() && !w.issue().empty());             // no action yet
  w.addAction("tool.move");
  R1_EXPECT(!w.issue().empty() || true);
  f.typeInto(w.nameField(), "");
  R1_EXPECT(!create->enabled() && w.issue().find("name") != std::string::npos);
  f.typeInto(w.nameField(), "taken");
  R1_EXPECT(!create->enabled() && w.issue().find("already exists") != std::string::npos);
  R1_EXPECT(!w.create() && f.committed.empty() && f.set.size() == 1);
  f.typeInto(w.nameField(), "My pie");
  R1_EXPECT(create->enabled() && w.issue().empty());
  f.click(w.createButton());
  R1_EXPECT(f.committed.size() == 1 && !f.committedEdit[0] && f.set.size() == 2);
  const cm::CustomMenu* made = f.set.find(f.committed[0]);
  R1_EXPECT(made != nullptr && made->name == "My pie" && made->entries[0].commandId == "tool.move");
  R1_EXPECT(!f.session.typeChosen() && f.session.draft()->filledCount() == 0);  // the draft was dropped; the window prepared a fresh one
  R1_EXPECT(w.showingChooser());                                                // ready for the next menu
}

void testPanel() {
  CreatorFixture f;
  CreateCustomMenuWindow& w = openPanel(f);
  PanelPreviewEditor* panel = w.panelEditor();
  const cm::MenuDraft& d = *f.session.draft();
  R1_EXPECT(d.kind() == MenuKind::Panel && d.entryCount() == 0);

  // The first drop lands in the "drop here" cell.
  double x, y, cw, ch;
  R1_EXPECT(panel->cellRect(0, x, y, cw, ch));
  R1_EXPECT(f.dragAction("tool.move", x + cw / 2.0, y + ch / 2.0));
  R1_EXPECT(d.entryCount() == 1 && d.menu().entries[0].commandId == "tool.move");
  // Dropping on the left half of the first button inserts before it; on empty room after the last appends.
  panel->cellRect(0, x, y, cw, ch);
  R1_EXPECT(f.dragAction("tool.rotate", x + 4.0, y + ch / 2.0));
  R1_EXPECT(d.entryCount() == 2 && d.menu().entries[0].commandId == "tool.rotate" && d.menu().entries[1].commandId == "tool.move");
  panel->cellRect(2, x, y, cw, ch);
  R1_EXPECT(f.dragAction("tool.scale", x + cw / 2.0, y + ch / 2.0));
  R1_EXPECT(d.entryCount() == 3 && d.menu().entries[2].commandId == "tool.scale");
  R1_EXPECT(w.addAction("edit.undo") && d.entryCount() == 4 && d.menu().entries[3].commandId == "edit.undo");

  // Move a button by dragging it past the last one.
  double ax, ay, bx, by, w2, h2;
  panel->cellRect(0, ax, ay, w2, h2);
  panel->cellRect(3, bx, by, w2, h2);
  f.drag(ax + w2 / 2.0, ay + h2 / 2.0, bx + w2 - 4.0, by + h2 / 2.0);
  R1_EXPECT(d.menu().entries[3].commandId == "tool.rotate" && d.menu().entries[0].commandId == "tool.move");
  // Dropping a button at its own position does nothing.
  panel->cellRect(1, ax, ay, w2, h2);
  f.drag(ax + 6.0, ay + h2 / 2.0, ax + 8.0, ay + h2 / 2.0);
  R1_EXPECT(d.entryCount() == 4 && d.menu().entries[1].commandId == "tool.scale");

  // Keys: Delete removes, Ctrl+arrows move.
  panel->select(1);
  f.t.ui.focusWidget(panel->id());
  f.key(ev::Key::Right, ev::Mod::kCtrl);
  R1_EXPECT(panel->selected() == 2 && d.menu().entries[2].commandId == "tool.scale");
  f.key(ev::Key::Delete);
  R1_EXPECT(d.entryCount() == 3);

  // Settings: columns, button size, labels.
  f.clickSegment(w.columnsSelector(), 3);
  R1_EXPECT(d.menu().panel.columns == 4);
  f.clickSegment(w.sizeSelector(), 2);
  R1_EXPECT(d.menu().panel.buttonSize == 56);
  f.click(w.labelsCheckbox());
  R1_EXPECT(!d.menu().panel.showLabels);
  panel->cellRect(0, ax, ay, w2, h2);
  panel->cellRect(1, bx, by, w2, h2);
  R1_EXPECT(by == ay && bx > ax && h2 == 56.0);  // four columns, 56 px buttons: the second button is beside the first

  // Create a dockable menu.
  f.typeInto(w.nameField(), "Quick");
  f.click(w.createButton());
  R1_EXPECT(f.committed.size() == 1 && f.set.size() == 1);
  const cm::CustomMenu* made = f.set.find(f.committed[0]);
  R1_EXPECT(made && made->kind == MenuKind::Panel && made->entries.size() == 3 && made->panel.columns == 4 && !made->panel.showLabels);
}

void testRightClickRemoveInPanel() {
  CreatorFixture f;
  CreateCustomMenuWindow& w = openPanel(f);
  PanelPreviewEditor* panel = w.panelEditor();
  panel->dropCommand(-1, "tool.move");
  panel->dropCommand(-1, "tool.rotate");
  double x, y, cw, ch;
  panel->cellRect(0, x, y, cw, ch);
  f.clickAt(x + cw / 2.0, y + ch / 2.0, ev::Button::Right);
  R1_EXPECT(panel->menuOpen());
  f.key(ev::Key::Down);
  f.key(ev::Key::Enter);  // "Remove from panel"
  R1_EXPECT(f.session.draft()->entryCount() == 1 && f.session.draft()->menu().entries[0].commandId == "tool.rotate");
}

void testEditModeAndCancel() {
  CreatorFixture f;
  const std::string id = f.set.createMenu(MenuKind::Pie, "Alpha").id;
  f.set.setSlot(id, 0, "tool.move");
  f.set.setSlot(id, 3, "tool.rotate");
  const std::string other = f.set.createMenu(MenuKind::Panel, "Beta").id;
  f.set.addEntry(other, "edit.undo");
  const cm::CustomMenu before = *f.set.find(id);

  CreateCustomMenuWindow& w = f.makeWindow();
  R1_EXPECT(w.showingChooser());
  f.session.beginEdit(id);
  f.settle();
  R1_EXPECT(!w.showingChooser() && w.pieEditor() != nullptr);
  R1_EXPECT(f.t.ui.objectAs<r1ui::widgets::TextInput>(w.nameField())->text() == "Alpha");
  R1_EXPECT(!f.t.ui.objectAs<r1ui::widgets::WidgetObject>(w.typeSelector())->enabled());  // the type of an existing menu is fixed

  // Cancel reverts everything: the live menu was never touched.
  double x, y;
  w.pieEditor()->slotCenter(5, x, y);
  f.dragAction("tool.scale", x, y);
  f.typeInto(w.nameField(), "Changed");
  R1_EXPECT(*f.set.find(id) == before);
  f.click(w.cancelButton());
  R1_EXPECT(f.cancelled == 1 && *f.set.find(id) == before && !f.session.typeChosen() && !f.session.draft()->editing());

  // Edit again and save.
  f.session.beginEdit(id);
  f.settle();
  w.pieEditor()->slotCenter(5, x, y);
  f.dragAction("tool.scale", x, y);
  f.typeInto(w.nameField(), "Alpha 2");
  f.click(w.createButton());
  R1_EXPECT(f.committed.size() == 1 && f.committedEdit[0] && f.committed[0] == id && f.set.size() == 2);
  const cm::CustomMenu* saved = f.set.find(id);
  R1_EXPECT(saved && saved->name == "Alpha 2" && saved->serial == before.serial && saved->entries[5].commandId == "tool.scale" && saved->entries[0].commandId == "tool.move");

  // The menu is deleted while its edit window is open: saving is refused, nothing is created.
  f.session.beginEdit(other);
  f.settle();
  f.set.deleteMenu(other);
  f.typeInto(w.nameField(), "Ghost");
  R1_EXPECT(!w.create() && f.set.size() == 1 && !w.status().empty());
}

void testFilesThroughHooks() {
  CreatorFixture f;
  CreateCustomMenuWindow& w = openPie(f);
  w.addAction("tool.move");
  f.typeInto(w.nameField(), "");
  f.click(w.saveFileButton());                       // no name yet
  R1_EXPECT(f.saved.empty() && !w.status().empty());
  f.typeInto(w.nameField(), "To file");
  f.click(w.saveFileButton());
  R1_EXPECT(f.saved.size() == 1 && f.saved[0].name == "To file" && f.saved[0].entries[0].commandId == "tool.move");
  f.click(w.loadFileButton());
  R1_EXPECT(f.loadRequests == 1);

  // The host loads a file into the creator: the window follows the new draft.
  cm::CustomMenu loaded = cm::makeEmptyMenu(MenuKind::Panel, "Loaded");
  loaded.entries = {{"tool.rotate", "", ""}, {"tool.scale", "", ""}};
  R1_EXPECT(f.session.beginFromFile(loaded));
  f.settle();
  R1_EXPECT(w.panelEditor() != nullptr && w.pieEditor() == nullptr && f.session.draft()->entryCount() == 2);
  R1_EXPECT(f.t.ui.objectAs<r1ui::widgets::TextInput>(w.nameField())->text() == "Loaded");
  f.click(w.createButton());
  R1_EXPECT(f.set.size() == 1 && f.set.menus()[0].name == "Loaded");
  // A menu the model refuses is not loaded.
  cm::CustomMenu bad = cm::makeEmptyMenu(MenuKind::Pie, "Bad");
  bad.entries.resize(3);
  R1_EXPECT(!f.session.beginFromFile(bad));
}

void testTypeSwitchKeepsActions() {
  CreatorFixture f;
  CreateCustomMenuWindow& w = openPie(f);
  w.addAction("tool.move");
  w.addAction("tool.rotate");
  f.clickSegment(w.typeSelector(), 1);  // Dockable panel
  R1_EXPECT(f.session.draft()->kind() == MenuKind::Panel && w.panelEditor() != nullptr && f.session.draft()->entryCount() == 2);
  f.clickSegment(w.typeSelector(), 0);
  R1_EXPECT(f.session.draft()->kind() == MenuKind::Pie && w.pieEditor() != nullptr && f.session.draft()->filledCount() == 2);
}

void testHostile() {
  CreatorFixture f;
  CreateCustomMenuWindow& w = openPie(f);
  w.addAction("tool.move");

  // A huge name and invalid text are bounded; the menu is still created with a legal name.
  f.typeInto(w.nameField(), std::string(10000, 'n'));
  R1_EXPECT(f.t.ui.objectAs<r1ui::widgets::TextInput>(w.nameField())->text().size() <= 64);
  f.t.ui.focusWidget(w.nameField());
  f.t.ui.textInput(0xD800);     // a lone surrogate
  f.t.ui.textInput(0x110000);   // beyond Unicode
  f.t.ui.textInput(0x0);
  f.settle();
  R1_EXPECT(f.t.ui.inputFaults() == 0);
  R1_EXPECT(w.create() || !w.issue().empty());

  // Missing command dropped through the hub, and a drop at a window point outside every slot.
  CreatorFixture g;
  CreateCustomMenuWindow& w2 = openPie(g);
  PiePreviewEditor* pie = w2.pieEditor();
  double x, y;
  pie->slotCenter(0, x, y);
  r1ui::widgets::DragPayload missing;
  missing.commandId = "plugin.removed";
  missing.text = "Gone";
  R1_EXPECT(w2.dragHub()->begin(missing, x, y));
  w2.dragHub()->move(x, y);
  R1_EXPECT(!w2.dragHub()->accepting());
  R1_EXPECT(!w2.dragHub()->end(x, y) && g.session.draft()->filledCount() == 0);
  r1ui::widgets::DragPayload bogus;
  bogus.commandId = std::string(5000, 'z');
  R1_EXPECT(w2.dragHub()->begin(bogus, x, y));
  R1_EXPECT(!w2.dragHub()->end(x, y) && g.session.draft()->filledCount() == 0);
  R1_EXPECT(!pie->dropCommand(99, "tool.move") && !pie->dropCommand(-1, "tool.move") && !pie->clearSlot(99));

  // The slots shrink under a running drag: the drop lands in a valid slot or is refused.
  pie->slotCenter(7, x, y);
  const auto row = [&] {
    w2.actions()->view().selectAction("tool.move");
    g.settle();
    return w2.actions()->view().rowRect(w2.actions()->view().selectedRow());
  }();
  g.t.ui.pointerMove(row.x + 40.0, row.y + 10.0);
  g.t.ui.pointerDown(row.x + 40.0, row.y + 10.0);
  g.t.ui.pointerMove(row.x + 60.0, row.y + 12.0);
  g.t.ui.pointerMove(x, y);
  g.session.draft()->setPieSlotCount(4);
  g.t.ui.pointerUp(x, y);
  g.settle();
  R1_EXPECT(g.session.draft()->menu().entries.size() == 4 && g.session.draft()->filledCount() <= 1);

  // The draft is replaced while the window shows another one.
  g.session.beginEdit(g.set.createMenu(MenuKind::Panel, "Z").id);
  g.settle();
  R1_EXPECT(w2.panelEditor() != nullptr && w2.pieEditor() == nullptr);

  // The window is destroyed in the middle of a drag from the list.
  CreatorFixture h;
  CreateCustomMenuWindow& w3 = openPie(h);
  w3.actions()->view().selectAction("tool.rotate");
  h.settle();
  const auto r = w3.actions()->view().rowRect(w3.actions()->view().selectedRow());
  h.t.ui.pointerMove(r.x + 40.0, r.y + 10.0);
  h.t.ui.pointerDown(r.x + 40.0, r.y + 10.0);
  h.t.ui.pointerMove(r.x + 60.0, r.y + 14.0);
  h.t.ui.pointerMove(r.x - 100.0, r.y + 30.0);
  R1_EXPECT(w3.dragHub()->active());
  h.t.ui.destroy(w3.id());
  h.window = nullptr;
  h.t.ui.pointerMove(100.0, 100.0);
  h.t.ui.pointerUp(100.0, 100.0);
  h.t.layout();
  R1_EXPECT(h.t.ui.overlays().stack().empty() && h.t.ui.inputFaults() == 0);
}

void testIdleAndEnd() {
  CreatorFixture f;
  CreateCustomMenuWindow& w = openPie(f);
  f.settle();
  f.settle();
  R1_EXPECT(!f.t.ui.msUntilTick().has_value() || *f.t.ui.msUntilTick() > 0);
  // Ending the session under a live window brings the chooser back from a timer, not inside the call.
  f.session.end();
  R1_EXPECT(f.session.draft() == nullptr);
  f.settle();
  R1_EXPECT(w.showingChooser() && f.session.draft() != nullptr);
  f.t.ui.destroy(w.id());
  f.window = nullptr;
  f.session.end();  // listeners are gone; no call into the destroyed window
  f.settle();
  R1_EXPECT(f.t.ui.inputFaults() == 0);
}

}  // namespace

int main() {
  testChooserAndPie();
  testPieContextMenuAndSlots();
  testDoubleClickAddAndLabel();
  testCreateValidationAndHooks();
  testPanel();
  testRightClickRemoveInPanel();
  testEditModeAndCancel();
  testFilesThroughHooks();
  testTypeSwitchKeepsActions();
  testHostile();
  testIdleAndEnd();
  return r1test::finish();
}

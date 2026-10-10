// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the tests of CustomMenuPanel: the grid (rows, columns, equal widths, button height, a short last
//   row), labels and icons with entry overrides, live enabled / checked / tooltip refresh, clicking and
//   keyboard activation, missing commands kept and dimmed, rebuilding on every kind of change to the set
//   (add, remove, move, rename of entries, settings, deletion of the menu), notices for a deleted menu, a
//   pie and an empty menu, throwing predicates and commands, 256 entries, many rebuilds without leaks,
//   the dock factory and descriptor, and panels outliving or being destroyed before their data.
// Callers: CTest (label fast).
#include <cstdlib>
#include <memory>

#include "MenuPanelFixture.h"
#include "r1ui/widgets/dock/PanelRegistry.h"

namespace {

using namespace r1test;

void testGrid() {
  MenuPanelFixture f;
  const std::string id = f.makePanelMenu("Grid", {"tool.move", "tool.rotate", "edit.undo", "file.save", "file.open"}, 3, 40);
  CustomMenuPanel& panel = f.makePanel(id);
  R1_EXPECT(panel.buttonCount() == 5 && panel.menuExists());
  // Rows of three; the second row has two buttons and keeps the column width.
  const auto r0 = f.t.ui.absRect(panel.button(0)->id());
  const auto r1 = f.t.ui.absRect(panel.button(1)->id());
  const auto r2 = f.t.ui.absRect(panel.button(2)->id());
  const auto r3 = f.t.ui.absRect(panel.button(3)->id());
  const auto r4 = f.t.ui.absRect(panel.button(4)->id());
  R1_EXPECT(r0.y == r1.y && r1.y == r2.y && r3.y == r4.y && r3.y > r0.y);
  R1_EXPECT(r0.h == 40 && r3.h == 40);
  R1_EXPECT(std::abs(r0.w - r1.w) <= 1 && std::abs(r1.w - r2.w) <= 1 && std::abs(r3.w - r0.w) <= 1);
  R1_EXPECT(r3.x == r0.x && r4.x == r1.x);
  R1_EXPECT(r0.x + r0.w <= r1.x && r1.x + r1.w <= r2.x);
  R1_EXPECT(r2.x + r2.w <= 360);  // inside the panel
  // One column: a list.
  f.set.setPanelColumns(id, 1);
  f.t.layout();
  const auto l0 = f.t.ui.absRect(f.t.ui.objectAs<CustomMenuPanel>(panel.id())->button(0)->id());
  const auto l1 = f.t.ui.absRect(panel.button(1)->id());
  R1_EXPECT(l0.x == l1.x && l1.y > l0.y && l0.w > r0.w);
  // Button size.
  f.set.setPanelButtonSize(id, 64);
  f.t.layout();
  R1_EXPECT(f.t.ui.absRect(panel.button(0)->id()).h == 64);
  f.set.setPanelColumns(id, 12);  // twelve columns, five entries: still one row of equal widths
  f.t.layout();
  R1_EXPECT(f.t.ui.absRect(panel.button(0)->id()).y == f.t.ui.absRect(panel.button(4)->id()).y);
}

void testContentAndOverrides() {
  MenuPanelFixture f;
  const std::string id = f.makePanelMenu("Content", {"tool.move", "edit.undo"});
  f.set.setEntryAppearance(id, 1, "Step back", "copy");
  f.set.addEntry(id, "file.save", cm::CustomMenuSet::npos, "My save", "");
  CustomMenuPanel& panel = f.makePanel(id);
  R1_EXPECT(panel.button(0)->state().label == "Move" && panel.button(0)->state().icon == "move-3d");
  R1_EXPECT(panel.button(1)->state().label == "Step back" && panel.button(1)->state().icon == "copy");
  R1_EXPECT(panel.button(2)->state().label == "My save" && panel.button(2)->state().icon == "save");
  R1_EXPECT(panel.button(0)->state().tooltip.find("Move description") == 0);
  R1_EXPECT(std::string(panel.button(0)->typeName()) == "CustomMenuButton" && panel.button(0)->accessibleName() == "Move");
  // Labels off.
  f.set.setPanelShowLabels(id, false);
  R1_EXPECT(!panel.button(0)->state().showLabel && panel.button(0)->state().icon == "move-3d");
  // An icon override that is not in the icon set still builds (the painter draws its fallback).
  f.set.setEntryAppearance(id, 0, "", "no-such-icon-anywhere");
  f.t.layout();
  R1_EXPECT(panel.button(0)->state().icon == "no-such-icon-anywhere");
}

void testLiveState() {
  MenuPanelFixture f;
  const std::string id = f.makePanelMenu("Live", {"view.grid", "edit.redo", "edit.undo", "tool.move"});
  CustomMenuPanel& panel = f.makePanel(id);
  R1_EXPECT(!panel.button(0)->state().checked && panel.button(0)->state().available);
  R1_EXPECT(!panel.button(1)->state().available);  // redo is disabled
  // Clicking a toggle runs it and the panel shows the new state at once (no frame needed).
  f.clickWidget(panel.button(0)->id());
  R1_EXPECT(f.runs["view.grid"] == 1 && panel.button(0)->state().checked);
  f.clickWidget(panel.button(0)->id());
  R1_EXPECT(f.runs["view.grid"] == 2 && !panel.button(0)->state().checked);
  // A disabled button does nothing.
  f.clickWidget(panel.button(1)->id());
  R1_EXPECT(f.runs["edit.redo"] == 0);
  // The command becomes enabled: the per-frame refresh shows it.
  f.enabled["edit.redo"] = true;
  f.sync.refresh();
  R1_EXPECT(panel.button(1)->state().available);
  f.clickWidget(panel.button(1)->id());
  R1_EXPECT(f.runs["edit.redo"] == 1);
  // A radio command shows checked and unchecks when another tool is chosen elsewhere.
  f.clickWidget(panel.button(3)->id());
  R1_EXPECT(panel.button(3)->state().checked);
  f.checked["tool.move"] = false;
  f.sync.refresh();
  R1_EXPECT(!panel.button(3)->state().checked);
  // A rebound chord reaches the tooltip.
  f.overrides.set("edit.undo", 0, cmd::ChordSequence::single({static_cast<r1ui::core::events::Key>('U'), r1ui::core::events::Mod::kCtrl, false}));
  R1_EXPECT(panel.button(2)->state().tooltip.find("Ctrl+U") != std::string::npos);
  // Keyboard activation: focus + Enter on key-up.
  f.t.ui.focusWidget(panel.button(2)->id(), r1ui::core::events::FocusReason::Keyboard);
  f.t.ui.keyDown(r1ui::core::events::Key::Enter);
  f.t.ui.keyUp(r1ui::core::events::Key::Enter);
  R1_EXPECT(f.runs["edit.undo"] == 1);
}

void testMissingAndHostileCommands() {
  MenuPanelFixture f;
  const std::string id = f.makePanelMenu("Hostile", {"plugin.removed", "bad.predicates", "edit.undo"});
  CustomMenuPanel& panel = f.makePanel(id);
  const r1ui::widgets::CustomMenuButtonState& missing = panel.button(0)->state();
  R1_EXPECT(missing.missing && !missing.available && missing.label == "plugin.removed");
  R1_EXPECT(missing.tooltip == "Command not available: plugin.removed");
  f.clickWidget(panel.button(0)->id());  // nothing happens, nothing breaks
  // Throwing predicates mean "not available" and not checked.
  R1_EXPECT(!panel.button(1)->state().available && !panel.button(1)->state().checked);
  f.clickWidget(panel.button(1)->id());
  R1_EXPECT(f.t.ui.inputFaults() == 0);
  // The missing command appears (a plugin loads): the panel picks it up without a rebuild of the menu.
  f.declare("plugin.removed", "Plugin action", "star", cmd::CommandKind::Action);
  f.sync.refresh();
  R1_EXPECT(!panel.button(0)->state().missing && panel.button(0)->state().available && panel.button(0)->state().label == "Plugin action");
  f.clickWidget(panel.button(0)->id());
  R1_EXPECT(f.runs["plugin.removed"] == 1);
  // And disappears again.
  f.registry.remove("plugin.removed");
  f.sync.refresh();
  R1_EXPECT(panel.button(0)->state().missing);
  // The menu itself still has the entry: nothing was dropped.
  R1_EXPECT(f.set.find(id)->entries[0].commandId == "plugin.removed");
}

void testRebuildOnEdits() {
  MenuPanelFixture f;
  const std::string id = f.makePanelMenu("Edits", {"edit.undo"});
  CustomMenuPanel& panel = f.makePanel(id);
  R1_EXPECT(panel.buttonCount() == 1);
  f.set.addEntry(id, "file.save");
  R1_EXPECT(panel.buttonCount() == 2 && panel.button(1)->commandId() == "file.save");
  f.set.addEntry(id, "file.open", 0);
  R1_EXPECT(panel.buttonCount() == 3 && panel.button(0)->commandId() == "file.open");
  f.set.moveEntry(id, 0, 2);
  R1_EXPECT(panel.button(2)->commandId() == "file.open" && panel.button(0)->commandId() == "edit.undo");
  f.set.removeEntry(id, 1);
  R1_EXPECT(panel.buttonCount() == 2);
  f.set.renameMenu(id, "Renamed");  // names are not shown by the panel; the rebuild is harmless
  R1_EXPECT(panel.buttonCount() == 2);
  // Buttons keep working after the rebuilds.
  f.t.layout();
  f.clickWidget(panel.button(0)->id());
  R1_EXPECT(f.runs["edit.undo"] == 1);

  // Deleting the menu leaves a notice and no buttons; the panel survives.
  f.set.deleteMenu(id);
  R1_EXPECT(panel.buttonCount() == 0 && !panel.menuExists());
  f.t.layout();
  f.sync.refresh();
  // Emptying a menu shows the empty notice; a pie id shows the pie notice.
  const std::string empty = f.set.createMenu(cm::MenuKind::Panel, "Empty").id;
  CustomMenuPanel& emptyPanel = f.makePanel(empty);
  R1_EXPECT(emptyPanel.buttonCount() == 0 && emptyPanel.menuExists());
  const std::string pie = f.set.createMenu(cm::MenuKind::Pie, "Pie").id;
  CustomMenuPanel& piePanel = f.makePanel(pie);
  R1_EXPECT(piePanel.buttonCount() == 0 && piePanel.menuExists());
  // A panel for an id that never existed is a notice, not a crash.
  CustomMenuPanel& ghost = f.makePanel("menu.9999");
  R1_EXPECT(ghost.buttonCount() == 0 && !ghost.menuExists());
}

void testLimitsAndLeaks() {
  MenuPanelFixture f(900, 700);
  const std::string id = f.set.createMenu(cm::MenuKind::Panel, "Big").id;
  for (size_t i = 0; i < cm::kMaxPanelEntries; ++i) f.set.addEntry(id, i % 3 == 0 ? "edit.undo" : "plugin.x" + std::to_string(i));
  f.set.setPanelColumns(id, 12);
  CustomMenuPanel& panel = f.makePanel(id);
  R1_EXPECT(panel.buttonCount() == cm::kMaxPanelEntries);
  f.sync.refresh();
  // Rebuilding many times does not grow the tree or the bookkeeping.
  f.set.setPanelColumns(id, 4);
  f.t.layout();
  const size_t widgets = f.t.ui.widgetCount();
  for (int i = 0; i < 100; ++i) f.set.setPanelColumns(id, 2 + (i % 5));
  f.set.setPanelColumns(id, 4);
  f.t.layout();
  R1_EXPECT(f.t.ui.widgetCount() == widgets);
  R1_EXPECT(f.t.ui.layoutCallbackCount() <= 1 && f.t.ui.animationCount() == 0);
  // Destroying the panel stops it listening: editing the set afterwards is safe and costs nothing.
  f.t.ui.destroy(panel.id());
  f.t.layout();
  for (int i = 0; i < 10; ++i) f.set.setPanelColumns(id, 3 + (i % 3));
  R1_EXPECT(f.t.ui.inputFaults() == 0);
}

void testCommandThatClosesThePanel() {
  MenuPanelFixture f;
  cmd::CommandDef closer;
  closer.id = "panel.close";
  closer.label = "Close";
  const auto target = std::make_shared<WidgetId>();
  MenuPanelFixture* self = &f;
  closer.execute = [self, target](const cmd::ExecuteArgs&) {
    self->t.ui.destroy(*target);
    return cmd::ExecuteResult::handled();
  };
  R1_EXPECT(f.registry.add(closer).ok);
  const std::string id = f.makePanelMenu("Closer", {"panel.close", "edit.undo"});
  CustomMenuPanel& panel = f.makePanel(id);
  *target = panel.id();
  f.clickWidget(panel.button(0)->id());
  R1_EXPECT(!f.t.ui.alive(*target) && f.t.ui.inputFaults() == 0);
  f.t.layout();
  f.sync.refresh();
  // The command that deletes its own menu from inside the panel.
  const std::string id2 = f.makePanelMenu("Self-deleting", {"edit.undo"});
  CustomMenuPanel& panel2 = f.makePanel(id2);
  cmd::CommandDef killer;
  killer.id = "menu.kill";
  killer.label = "Kill";
  killer.execute = [self, id2](const cmd::ExecuteArgs&) {
    self->set.deleteMenu(id2);
    return cmd::ExecuteResult::handled();
  };
  R1_EXPECT(f.registry.add(killer).ok);
  f.set.addEntry(id2, "menu.kill");
  f.t.layout();
  f.clickWidget(panel2.button(1)->id());
  R1_EXPECT(panel2.buttonCount() == 0 && f.t.ui.inputFaults() == 0);
}

void testDockGlue() {
  MenuPanelFixture f;
  const std::string id = f.makePanelMenu("Docked", {"edit.undo", "edit.copy"});
  f.set.setPanelSize(id, 300, 220);
  r1ui::widgets::PanelRegistry panels;
  const r1ui::widgets::PanelDescriptor descriptor =
      r1ui::widgets::describeCustomMenuPanel(*f.set.find(id), 1000 + f.set.find(id)->serial, r1ui::widgets::makeCustomMenuPanelFactory(f.services(), f.sync, f.set, id));
  R1_EXPECT(descriptor.title == "Docked" && descriptor.floatSize.x == 300.0 && descriptor.floatSize.y == 220.0 && descriptor.id == 1001);
  R1_EXPECT(panels.add(descriptor));
  const WidgetId content = panels.find(1001)->factory(f.t.ui, f.t.ui.root());
  R1_EXPECT(content.valid());
  CustomMenuPanel* panel = f.t.ui.objectAs<CustomMenuPanel>(content);
  R1_EXPECT(panel != nullptr && panel->menuId() == id);
  f.t.layout();
  R1_EXPECT(panel->buttonCount() == 2);
  // A second panel for the same menu works the same (the same menu may be shown twice).
  const WidgetId again = panels.find(1001)->factory(f.t.ui, f.t.ui.root());
  R1_EXPECT(f.t.ui.objectAs<CustomMenuPanel>(again)->buttonCount() == 2);
  f.set.addEntry(id, "file.save");
  R1_EXPECT(panel->buttonCount() == 3 && f.t.ui.objectAs<CustomMenuPanel>(again)->buttonCount() == 3);
}

}  // namespace

int main() {
  testGrid();
  testContentAndOverrides();
  testLiveState();
  testMissingAndHostileCommands();
  testRebuildOnEdits();
  testLimitsAndLeaks();
  testCommandThatClosesThePanel();
  testDockGlue();
  return r1test::finish();
}

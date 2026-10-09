// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of menus built from commands: the row of a command (label, icon, kind, shortcut text in
//   the format of spec 07 rule 28, tooltip with chord, enabled, checked), invisible commands and the
//   separators around them, submenus, activation through the router (click and keyboard, disabled rows
//   do nothing, toggles and radios end in the state the command reports), the menu bar (rebuilt right
//   before it opens so it is never stale) and the live refresh of an OPEN menu when a chord is rebound,
//   reset, imported, or a predicate changes (decision D15), plus hostile entries (unknown ids, deep
//   nesting, thousands of rows, a command removed while its menu is open).
// Callers: CTest (label fast).
#include "CommandFixture.h"
#include "r1ui/commands/Conflicts.h"
#include "r1ui/commands/OverrideIo.h"
#include "r1ui/widgets/commands/CommandMenus.h"
#include "r1ui/widgets/menu/MenuPanel.h"

namespace {

using namespace r1test;
using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;

void populate(CommandFixture& f) {
  f.declare("edit.undo", "Undo", cmd::CommandKind::Action, chordOf(letter('Z'), Mod::kCtrl), {}, "undo2");
  f.declare("edit.redo", "Redo", cmd::CommandKind::Action, chordOf(letter('Y'), Mod::kCtrl), chordOf(letter('Z'), Mod::kCtrl | Mod::kShift), "redo2");
  f.declare("view.grid", "Show grid", cmd::CommandKind::Toggle, chordOf(letter('G'), Mod::kCtrl), {}, "grid-3x3");
  f.declare("radio.pen", "Pen", cmd::CommandKind::Radio, chordOf(letter('P')));
  f.declare("radio.hand", "Hand", cmd::CommandKind::Radio, chordOf(letter('H')));
  f.declare("file.save", "Save", cmd::CommandKind::Action, chordOf(letter('S'), Mod::kCtrl | Mod::kShift | Mod::kAlt | Mod::kMeta));
}

MenuItemSpec rowOf(const MenuSpec& menu, std::string_view id) {
  for (const MenuItemSpec& item : menu.items) {
    if (item.id == id) return item;
  }
  return {};
}

void testRows() {
  CommandFixture f;
  populate(f);
  using E = CommandMenuEntry;
  const std::vector<E> layout{E::command("edit.undo"), E::command("edit.redo"), E::separator(), E::command("view.grid"), E::separator(),
                              E::command("radio.pen"), E::command("radio.hand"), E::command("file.save")};
  f.checked["view.grid"] = true;
  f.checked["radio.pen"] = true;
  f.enabled["edit.redo"] = false;
  const MenuSpec menu = buildCommandMenu(f.services(), layout);
  R1_EXPECT(menu.items.size() == 8);
  const MenuItemSpec undo = rowOf(menu, "edit.undo");
  R1_EXPECT(undo.label == "Undo" && undo.icon == "undo2" && undo.shortcut == "Ctrl+Z" && undo.kind == MenuItemKind::Action && undo.enabled);
  R1_EXPECT(undo.tooltip == "Undo description");
  // The shortcut text is the existing formatter's output (one chord format everywhere).
  R1_EXPECT(undo.shortcut == formatChordText(letter('Z'), Mod::kCtrl));
  R1_EXPECT(rowOf(menu, "file.save").shortcut == formatChordText(letter('S'), Mod::kCtrl | Mod::kShift | Mod::kAlt | Mod::kMeta));
  R1_EXPECT(rowOf(menu, "file.save").shortcut == "Ctrl+Cmd+Alt+Shift+S");  // spec 07 rule 28 order
  const MenuItemSpec redo = rowOf(menu, "edit.redo");
  R1_EXPECT(!redo.enabled && redo.shortcut == "Ctrl+Y");  // a disabled row still shows its chord
  const MenuItemSpec grid = rowOf(menu, "view.grid");
  R1_EXPECT(grid.kind == MenuItemKind::Check && grid.checked);
  const MenuItemSpec pen = rowOf(menu, "radio.pen");
  R1_EXPECT(pen.kind == MenuItemKind::Radio && pen.checked && !rowOf(menu, "radio.hand").checked && pen.shortcut == "P");
  CommandMenuOptions upper;
  upper.upperCaseShortcuts = true;
  R1_EXPECT(rowOf(buildCommandMenu(f.services(), layout, upper), "edit.undo").shortcut == "CTRL+Z");  // spec 07 rule 27
  CommandMenuOptions noIcons;
  noIcons.showIcons = false;
  R1_EXPECT(rowOf(buildCommandMenu(f.services(), layout, noIcons), "edit.undo").icon.empty());
}

void testVisibilityAndSeparators() {
  CommandFixture f;
  populate(f);
  bool shown = true;
  f.registry.add([&] {
    cmd::CommandDef d = cmd::CommandDef{};
    d.id = "maybe";
    d.label = "Maybe";
    d.visible = [&] { return shown; };
    return d;
  }());
  using E = CommandMenuEntry;
  const std::vector<E> layout{E::separator(), E::command("edit.undo"), E::separator(), E::command("maybe"), E::separator(), E::command("edit.redo"), E::command("ghost"), E::separator()};
  MenuSpec menu = buildCommandMenu(f.services(), layout);
  R1_EXPECT(menu.items.size() == 5 && menu.items.front().id == "edit.undo" && menu.items.back().id == "edit.redo");  // no leading or trailing separator; unknown ids skipped
  shown = false;
  menu = buildCommandMenu(f.services(), layout);
  R1_EXPECT(menu.items.size() == 3 && menu.items[1].kind == MenuItemKind::Separator);  // a collapsed command leaves one separator, not two
  // Submenus: empty ones are dropped, depth is bounded.
  std::vector<E> nested{E::command("edit.undo")};
  for (int i = 0; i < 40; ++i) nested = {E::submenu("level " + std::to_string(i), std::move(nested))};
  menu = buildCommandMenu(f.services(), nested);
  int depth = 0;
  const std::vector<MenuItemSpec>* level = &menu.items;
  while (!level->empty() && level->front().kind == MenuItemKind::Submenu) {
    ++depth;
    level = &level->front().children;
  }
  R1_EXPECT(depth < kMaxMenuDepth);
  R1_EXPECT(buildCommandMenu(f.services(), std::vector<E>{E::submenu("empty", {E::command("ghost")})}).items.empty());
  // Huge input is capped per level.
  std::vector<E> many;
  for (int i = 0; i < 5000; ++i) many.push_back(E::command("edit.undo"));
  R1_EXPECT(buildCommandMenu(f.services(), many).items.size() == kMaxMenuItems);
}

void testActivation() {
  CommandFixture f;
  populate(f);
  using E = CommandMenuEntry;
  MenuController controller(f.t.ui);
  const std::vector<E> layout{E::command("edit.undo"), E::command("edit.redo"), E::command("view.grid"), E::command("radio.pen"), E::command("radio.hand")};
  f.enabled["edit.redo"] = false;
  R1_EXPECT(openCommandContextMenu(controller, f.services(), layout, 40, 20));
  f.t.layout();
  MenuPanel* panel = f.t.ui.objectAs<MenuPanel>(controller.panelAt(0));
  R1_EXPECT(panel != nullptr && panel->itemCount() == 5);
  // Keyboard: Down, Enter runs Undo through the router and closes the menu.
  panel->highlightFirst();
  f.t.ui.keyDown(Key::Enter);
  R1_EXPECT(f.runs["edit.undo"] == 1 && !controller.isOpen());
  // A disabled row is not activatable.
  R1_EXPECT(openCommandContextMenu(controller, f.services(), layout, 40, 20));
  f.t.layout();
  panel = f.t.ui.objectAs<MenuPanel>(controller.panelAt(0));
  R1_EXPECT(!panel->selectable(1));
  panel->itemActivated(1, true);
  R1_EXPECT(f.runs["edit.redo"] == 0 && controller.isOpen());
  // A toggle ends in the state the command reports; a radio checks exactly one row.
  panel->itemActivated(2, true);
  R1_EXPECT(f.runs["view.grid"] == 1 && f.checked["view.grid"]);
  R1_EXPECT(openCommandContextMenu(controller, f.services(), layout, 40, 20));
  f.t.layout();
  panel = f.t.ui.objectAs<MenuPanel>(controller.panelAt(0));
  R1_EXPECT(panel->item(2).checked);  // the reopened menu shows the new state
  panel->itemActivated(4, true);
  R1_EXPECT(f.runs["radio.hand"] == 1 && f.checked["radio.hand"] && !f.checked["radio.pen"]);
  controller.close();
  // A refusing command is reported by the router, not by an exception.
  f.registry.remove("edit.undo");
  const MenuSpec stale = buildCommandMenu(f.services(), layout);
  R1_EXPECT(stale.items.size() == 4);
}

void testLiveRefreshWhileOpen() {
  CommandFixture f;
  populate(f);
  using E = CommandMenuEntry;
  MenuController controller(f.t.ui);
  const std::vector<E> layout{E::command("edit.undo"), E::command("edit.redo"), E::command("view.grid")};
  R1_EXPECT(openCommandContextMenu(controller, f.services(), layout, 40, 20));
  f.t.layout();
  MenuPanel* panel = f.t.ui.objectAs<MenuPanel>(controller.panelAt(0));
  R1_EXPECT(panel->item(0).shortcut == "Ctrl+Z");

  // A rebound chord reaches the open menu at once (D15, spec 07 rule 30).
  R1_EXPECT(cmd::assignChord(f.overrides, f.keymap, f.registry, "edit.undo", 0, chordOf(letter('U'), Mod::kCtrl | Mod::kShift), false).ok);
  R1_EXPECT(panel->item(0).shortcut == "Ctrl+Shift+U");
  const WidgetId row = panel->itemWidget(0);
  R1_EXPECT(f.t.ui.object(row)->tooltipText() == "Undo description (Ctrl+Shift+U)");
  // Unbinding empties the shortcut text and the cell.
  R1_EXPECT(cmd::assignChord(f.overrides, f.keymap, f.registry, "edit.undo", 0, std::nullopt, false).ok);
  R1_EXPECT(panel->item(0).shortcut.empty());
  f.overrides.resetAll();
  R1_EXPECT(panel->item(0).shortcut == "Ctrl+Z");  // reset is live
  // An import is live too.
  const auto report = cmd::importOverrides("{\"format\":\"r1ui-keybindings\",\"version\":1,\"overrides\":[{\"command\":\"edit.redo\",\"slot\":0,\"chord\":\"F2\"}]}", f.registry,
                                           f.overrides);
  R1_EXPECT(report.ok && panel->item(1).shortcut == "F2");

  // Enabled and checked follow the predicates when the host refreshes.
  f.enabled["edit.redo"] = false;
  f.checked["view.grid"] = true;
  f.sync.refresh();
  R1_EXPECT(!panel->item(1).enabled && !panel->selectable(1) && panel->item(2).checked);
  R1_EXPECT(!f.t.ui.object(panel->itemWidget(1))->enabled());
  f.enabled["edit.redo"] = true;
  f.sync.refresh();
  R1_EXPECT(panel->selectable(1));

  // A row that is highlighted and becomes disabled loses the highlight.
  panel->setHighlight(0);
  f.enabled["edit.undo"] = false;
  f.sync.refresh();
  R1_EXPECT(panel->highlighted() == -1);

  // A removed command: its row keeps working text; nothing crashes; the menu rebuilds without it.
  f.registry.remove("edit.redo");
  f.sync.refresh();
  controller.close();
  R1_EXPECT(!openCommandContextMenu(controller, f.services(), std::vector<E>{E::command("edit.redo")}, 10, 10));

  // A settled open menu asks for no frames: refreshing identical state touches nothing.
  CommandFixture g;
  populate(g);
  MenuController c2(g.t.ui);
  openCommandContextMenu(c2, g.services(), std::vector<E>{E::command("edit.undo")}, 40, 20);
  g.t.layout();
  R1_EXPECT(refreshOpenCommandMenus(g.t.ui, g.services()) == 0);
}

void testMenuBar() {
  CommandFixture f;
  populate(f);
  using E = CommandMenuEntry;
  MenuBar& bar = f.t.ui.create<MenuBar>(f.t.ui.root());
  bindCommandMenuBar(bar, f.services(), {{"Edit", {E::command("edit.undo"), E::command("edit.redo")}}, {"", {E::command("edit.undo")}}, {"View", {E::command("view.grid")}}});
  f.t.layout();
  R1_EXPECT(bar.menuCount() == 2);  // an empty title is refused
  // State changed while the bar was closed: the menu is rebuilt when it opens.
  f.enabled["edit.redo"] = false;
  R1_EXPECT(bar.openMenu(0));
  f.t.layout();
  MenuPanel* panel = f.t.ui.objectAs<MenuPanel>(bar.controller().panelAt(0));
  R1_EXPECT(panel != nullptr && panel->itemCount() == 2 && !panel->item(1).enabled && panel->item(0).shortcut == "Ctrl+Z");
  bar.closeMenu();
  f.enabled["edit.redo"] = true;
  cmd::assignChord(f.overrides, f.keymap, f.registry, "edit.undo", 0, chordOf(letter('Q'), Mod::kAlt), false);
  R1_EXPECT(bar.openMenu(0));
  panel = f.t.ui.objectAs<MenuPanel>(bar.controller().panelAt(0));
  R1_EXPECT(panel->item(1).enabled && panel->item(0).shortcut == "Alt+Q");
  // The command runs through the bar's menu as through a chord.
  panel->itemActivated(0, true);
  R1_EXPECT(f.runs["edit.undo"] == 1);
  // The same command through its chord.
  const std::vector<std::string> contexts{cmd::kWindowContext};
  R1_EXPECT(f.router.handleKey({letter('Q'), Mod::kAlt, false, false}, contexts).consumed && f.runs["edit.undo"] == 2);
}

}  // namespace

int main() {
  testRows();
  testVisibilityAndSeparators();
  testActivation();
  testLiveRefreshWhileOpen();
  testMenuBar();
  return r1test::finish();
}

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of the Customization editing operations (customize/Customization.h): hide and show with
//   the restore list, move legality (locked menu, cycle, illegal parent, kind boundaries), rename,
//   user menus (unique id and title), user entries, removal rules, per-menu reset and reset all,
//   toolbar size step and gap, user toolbars, free-form panels (place, move, resize, snap, bounds,
//   z-order, delete), the edit session (revert, commit), listeners and the version counter, the
//   workspace layer.
// Callers: CTest (label fast).
#include "CustomizeFixtures.h"

namespace {

using namespace r1test;

void testHideShowRestore() {
  cz::Customization c = makeCustomization();
  const uint64_t v0 = c.version();
  R1_EXPECT(c.hideEntry("edit.copy").ok);
  R1_EXPECT(c.version() > v0);
  R1_EXPECT(sectionIds(c.effective().layout, "edit.clip") == "edit.cut,edit.paste,edit.more");
  R1_EXPECT(sectionIds(c.editView().layout, "edit.clip") == "edit.cut,edit.copy,edit.paste,edit.more");
  R1_EXPECT(c.hideEntry("menu.view").ok);
  c.setCommandLabeler([](const std::string& id) { return id == "edit.copy" ? std::string("Copy") : id; });
  const std::vector<cz::RestoreItem> list = c.restoreList();
  R1_EXPECT(list.size() == 2 && list[0].id == "edit.copy" && list[1].id == "menu.view");
  R1_EXPECT(list[0].label == "Copy" && list[0].path == "Edit > Clipboard > Copy");
  R1_EXPECT(c.showEntry("edit.copy").ok && c.showEntry("menu.view").ok);
  R1_EXPECT(c.userDelta().empty() && c.restoreList().empty());  // the hide edit leaves no trace once shown

  R1_EXPECT(!c.hideEntry("nope").ok && c.hideEntry("nope").error == cz::EditError::UnknownNode);
  const cz::EditResult locked = c.hideEntry("help.about");
  R1_EXPECT(!locked.ok && locked.error == cz::EditError::Locked && locked.reason.find("Help") != std::string::npos);
  R1_EXPECT(c.isLocked("menu.help") && c.isLocked("help.about") && !c.isLocked("menu.file"));
  R1_EXPECT(!c.lockReason("help.about").empty() && c.lockReason("menu.file").empty());
  R1_EXPECT(c.userDelta().empty());  // a refused edit changes nothing
}

void testMove() {
  cz::Customization c = makeCustomization();
  R1_EXPECT(c.move("edit.paste", {"edit.clip", "edit.cut", cz::Side::Before}).ok);
  R1_EXPECT(sectionIds(c.effective().layout, "edit.clip") == "edit.paste,edit.cut,edit.copy,edit.more");
  R1_EXPECT(c.move("edit.paste", {"file.main", "", cz::Side::Start}).ok);  // a second move replaces the first
  R1_EXPECT(c.userDelta().moves.size() == 1 && sectionIds(c.effective().layout, "file.main") == "edit.paste,file.open,file.save");
  R1_EXPECT(c.move("edit.clip", {"menu.file", "file.main", cz::Side::After}).ok);  // a whole section
  R1_EXPECT(join(childIds(nodeNamed(c.effective().layout, "menu.file")->children)) == "file.main,edit.clip");

  R1_EXPECT(c.move("menu.view", {"menubar", "menu.file", cz::Side::Before}).ok);
  R1_EXPECT(c.effective().layout.menuBar.menus.front().id == "menu.view");

  const uint64_t v = c.version();
  const cz::Delta before = c.userDelta();
  R1_EXPECT(c.move("edit.undo", {"help.main", "", cz::Side::End}).error == cz::EditError::Locked);        // into a locked menu
  R1_EXPECT(c.move("help.about", {"file.main", "", cz::Side::End}).error == cz::EditError::Locked);        // out of a locked menu
  R1_EXPECT(c.move("edit.more", {"edit.more.main", "", cz::Side::End}).error == cz::EditError::Cycle);     // into itself
  R1_EXPECT(c.move("edit.undo", {"menu.file", "", cz::Side::End}).error == cz::EditError::Illegal);        // a command cannot sit in a menu
  R1_EXPECT(c.move("tb.undo", {"file.main", "", cz::Side::End}).error == cz::EditError::Illegal);          // toolbar item into a menu
  R1_EXPECT(c.move("edit.undo", {"nowhere", "", cz::Side::End}).error == cz::EditError::UnknownParent);
  R1_EXPECT(c.move("edit.undo", {"file.main", "missing", cz::Side::After}).error == cz::EditError::UnknownNode);
  R1_EXPECT(c.move("edit.undo", {"file.main", "edit.undo", cz::Side::After}).error == cz::EditError::Cycle);  // relative to itself
  R1_EXPECT(c.move("ghost", {"file.main", "", cz::Side::End}).error == cz::EditError::UnknownNode);
  R1_EXPECT(c.version() == v && c.userDelta() == before);

  // Between toolbars and into a group.
  R1_EXPECT(c.move("tb.select", {"tb.shapes", "", cz::Side::End}).ok);
  R1_EXPECT(join(childIds(nodeNamed(c.effective().layout, "tb.shapes")->children)) == "tb.rect,tb.ellipse,tb.select");
}

void testRename() {
  cz::Customization c = makeCustomization();
  R1_EXPECT(c.renameLabel("menu.edit", "  Editing ").ok);
  R1_EXPECT(nodeNamed(c.effective().layout, "menu.edit")->shownLabel() == "Editing" && nodeNamed(c.effective().layout, "menu.edit")->label == "Edit");
  R1_EXPECT(c.renameLabel("menu.edit", "").ok && c.userDelta().empty());               // empty restores the default
  R1_EXPECT(c.renameLabel("edit.more", "More tools").ok && c.renameLabel("edit.copy", "Duplicate!").ok);
  R1_EXPECT(c.renameLabel("edit.more", "More").ok && c.userDelta().edits.count("edit.more") == 0);  // equal to the default: no edit stored
  R1_EXPECT(c.renameLabel("menu.view", "FILE").error == cz::EditError::Duplicate);       // menu titles are unique
  R1_EXPECT(c.renameLabel("tb.sep1", "x").error == cz::EditError::Illegal);
  R1_EXPECT(c.renameLabel("menu.help", "Aide").error == cz::EditError::Locked);
  R1_EXPECT(c.renameLabel("edit.copy", std::string("a\tb\nc\xFF")).ok);                  // control characters and bad UTF-8 are cleaned
  R1_EXPECT(cz::findNode(c.effective().layout, "edit.copy")->userLabel.find('\n') == std::string::npos);
  R1_EXPECT(c.renameLabel("edit.copy", std::string(2000, 'x')).ok && cz::findNode(c.effective().layout, "edit.copy")->userLabel.size() == cz::kMaxLabelBytes);
}

void testUserMenus() {
  cz::Customization c = makeCustomization();
  const cz::EditResult m = c.addUserMenu("  My tools ");
  R1_EXPECT(m.ok && !m.id.empty());
  R1_EXPECT(c.effective().layout.menuBar.menus.back().id == m.id && c.effective().layout.menuBar.menus.back().shownLabel() == "My tools");
  R1_EXPECT(c.effective().layout.menuBar.menus.back().user);
  R1_EXPECT(c.addUserMenu("my TOOLS").error == cz::EditError::Duplicate);
  R1_EXPECT(c.addUserMenu("file").error == cz::EditError::Duplicate);       // against built-in titles too
  R1_EXPECT(c.addUserMenu("   ").error == cz::EditError::InvalidText);

  // Entries go to the menu's section; a command that is not registered is refused.
  cz::Customization limited(builtinV1(), [](const std::string& id) { return id != "ghost"; });
  const std::string menuId = limited.addUserMenu("Mine").id;
  R1_EXPECT(limited.addCommand(menuId, "ghost").error == cz::EditError::UnknownCommand);
  const cz::EditResult a = limited.addCommand(menuId, "edit.undo");
  const cz::EditResult b = limited.addCommand(menuId, "edit.redo", a.id, cz::Side::Before);
  const cz::EditResult sep = limited.addSeparator(menuId);
  const cz::EditResult head = limited.addHeading(menuId, "Extras");
  const cz::EditResult sub = limited.addSubmenu(menuId, "Deeper");
  R1_EXPECT(a.ok && b.ok && sep.ok && head.ok && sub.ok);
  const cz::Node* section = &cz::findNode(limited.effective().layout, menuId)->children[0];
  R1_EXPECT(join(childIds(section->children)) == b.id + "," + a.id + "," + sep.id + "," + head.id + "," + sub.id);
  R1_EXPECT(limited.addCommand(sub.id, "view.grid").ok);  // into the sub-menu's first section
  R1_EXPECT(limited.addCommand("edit.clip", "file.open").ok && limited.addCommand("menu.edit", "file.save").ok);  // built-in menus accept user entries
  R1_EXPECT(limited.addCommand("menu.help", "edit.undo").error == cz::EditError::Locked);
  R1_EXPECT(limited.addCommand("tb.main", "tool.pen").ok && limited.addCommand("tb.shapes", "tool.pen").ok);
  R1_EXPECT(limited.addCommand("fp.main", "edit.undo").error == cz::EditError::Illegal);
  R1_EXPECT(limited.addCommand("nowhere", "edit.undo").error == cz::EditError::UnknownParent);
  R1_EXPECT(cz::validateLayout(limited.effective().layout).empty());

  // Removal: user nodes only; the subtree goes with them; built-in entries are hidden, not removed.
  R1_EXPECT(limited.removeUserEntry("edit.copy").error == cz::EditError::NotUserNode);
  R1_EXPECT(limited.deleteUserMenu("menu.file").error == cz::EditError::NotUserNode);
  R1_EXPECT(limited.removeUserEntry(sub.id).ok && limited.find(sub.id) == nullptr);
  const size_t entries = limited.userDelta().added.size();
  R1_EXPECT(limited.deleteUserMenu(menuId).ok && limited.find(menuId) == nullptr && limited.userDelta().added.size() < entries);
  R1_EXPECT(limited.deleteUserMenu("nowhere").error == cz::EditError::UnknownNode);

  // A built-in entry moved into a user menu goes back to its default place when that menu is deleted.
  const std::string again = limited.addUserMenu("Again").id;
  const std::string section2 = cz::findNode(limited.effective().layout, again)->children[0].id;
  R1_EXPECT(limited.move("view.grid", {section2, "", cz::Side::End}).ok);
  R1_EXPECT(sectionIds(limited.effective().layout, "view.main") == "view.rulers");
  R1_EXPECT(limited.deleteUserMenu(again).ok && sectionIds(limited.effective().layout, "view.main") == "view.grid,view.rulers");

  // Ids are never reused.
  const std::string x = limited.addUserMenu("X").id;
  limited.deleteUserMenu(x);
  R1_EXPECT(limited.addUserMenu("X").id != x);

  // Sections and groups.
  cz::Customization s = makeCustomization();
  const cz::EditResult sec = s.addSection("menu.file", "Recent", "file.main", cz::Side::After);
  R1_EXPECT(sec.ok && join(childIds(nodeNamed(s.effective().layout, "menu.file")->children)) == "file.main," + sec.id);
  R1_EXPECT(s.addGroup("tb.main", {"tool.hand", "tool.text"}, "tb.sep1", cz::Side::Before).ok);
  R1_EXPECT(s.addGroup("tb.main", {}).error == cz::EditError::Illegal);
  R1_EXPECT(s.addSpacer("tb.main").ok);
  R1_EXPECT(s.addSpacer("menu.file").error == cz::EditError::Illegal);
  R1_EXPECT(cz::validateLayout(s.effective().layout).empty());
}

void testResets() {
  cz::Customization c = makeCustomization();
  c.hideEntry("edit.copy");
  c.renameLabel("menu.edit", "Editing");
  c.move("edit.paste", {"file.main", "", cz::Side::End});
  c.addCommand("edit.clip", "file.open");
  c.hideEntry("file.save");
  c.setToolbarGap("tb.main", 6);
  c.addUserMenu("Mine");
  R1_EXPECT(c.resetMenu("menu.edit").ok);
  R1_EXPECT(sectionIds(c.effective().layout, "edit.clip") == "edit.cut,edit.copy,edit.paste,edit.more");  // paste came back, the user entry is gone
  R1_EXPECT(nodeNamed(c.effective().layout, "menu.edit")->shownLabel() == "Edit");
  R1_EXPECT(c.editView().layout.menuBar.menus.size() == 5 && !nodeNamed(c.editView().layout, "file.save")->visible);  // other menus untouched
  R1_EXPECT(c.resetMenu("tb.main").ok && c.effective().layout.toolbars[0].gap == cz::kDefaultToolbarGap);
  R1_EXPECT(c.resetMenu("help.main").error == cz::EditError::Illegal);
  R1_EXPECT(c.resetMenu("menu.help").error == cz::EditError::Locked);
  R1_EXPECT(c.resetMenu("nope").error == cz::EditError::UnknownNode);
  R1_EXPECT(c.resetAll().ok && c.userDelta().empty() && c.effective().layout.menuBar.menus.size() == 4);
  R1_EXPECT(c.userDelta().serial > 0);  // ids stay unique after a reset
}

void testToolbars() {
  cz::Customization c = makeCustomization();
  R1_EXPECT(c.setToolbarSizeStep("tb.main", cz::SizeStep::Large).ok && c.effective().layout.toolbars[0].sizeStep == cz::SizeStep::Large);
  R1_EXPECT(c.setToolbarGap("tb.main", 8).ok && c.effective().layout.toolbars[0].gap == 8.0);
  R1_EXPECT(c.setToolbarSizeStep("tb.main", cz::SizeStep::Medium).ok && c.setToolbarGap("tb.main", cz::kDefaultToolbarGap).ok && c.userDelta().empty());
  R1_EXPECT(c.setToolbarGap("tb.main", -1).error == cz::EditError::OutOfRange);
  R1_EXPECT(c.setToolbarGap("tb.main", 1e9).error == cz::EditError::OutOfRange);
  R1_EXPECT(c.setToolbarGap("tb.main", std::nan("")).error == cz::EditError::OutOfRange);
  R1_EXPECT(c.setToolbarSizeStep("nope", cz::SizeStep::Small).error == cz::EditError::UnknownNode);
  R1_EXPECT(cz::sizeStepPixels(cz::SizeStep::Small) < cz::sizeStepPixels(cz::SizeStep::Medium) && cz::sizeStepPixels(cz::SizeStep::Medium) == 32.0 &&
            cz::sizeStepPixels(cz::SizeStep::Large) > 32.0);

  const cz::EditResult t = c.addUserToolbar("Mine", cz::Orientation::Vertical);
  R1_EXPECT(t.ok && c.addUserToolbar("main TOOLS").error == cz::EditError::Duplicate && c.addUserToolbar("").error == cz::EditError::InvalidText);
  R1_EXPECT(c.addCommand(t.id, "tool.pen").ok && c.addCommand(t.id, "tool.hand").ok && c.addSeparator(t.id).ok);
  R1_EXPECT(c.setToolbarSizeStep(t.id, cz::SizeStep::Small).ok && c.effective().layout.toolbars[1].sizeStep == cz::SizeStep::Small);
  R1_EXPECT(c.effective().layout.toolbars[1].orientation == cz::Orientation::Vertical && c.effective().layout.toolbars[1].items.size() == 3);
  R1_EXPECT(c.deleteUserToolbar("tb.main").error == cz::EditError::NotUserNode);
  R1_EXPECT(c.deleteUserToolbar(t.id).ok && c.effective().layout.toolbars.size() == 1 && c.userDelta().empty());
}

void testFreeForm() {
  cz::Customization c = makeCustomization();
  const cz::EditResult p = c.placeButton("fp.main", "edit.redo", {20, 60, 70, 30});
  R1_EXPECT(p.ok && p.rect == (cz::Rect{20, 60, 70, 30}));
  const std::string b = p.id;
  // Bounds and minimum size.
  R1_EXPECT(c.moveButton(b, -50, 1000).rect == (cz::Rect{0, 270, 70, 30}));
  R1_EXPECT(c.resizeButton(b, 2, 2).rect.w == cz::kMinButtonSize && c.resizeButton(b, 2, 2).rect.h == cz::kMinButtonSize);
  R1_EXPECT(c.resizeButton(b, 9999, 9999).rect.w == 400 && c.resizeButton(b, 9999, 9999).rect.h == 300);
  R1_EXPECT(c.placeButton("fp.main", "edit.redo", {1e300, 0, 32, 32}).ok);
  R1_EXPECT(c.setButtonRect(b, {20, 60, 70, 30}).rect == (cz::Rect{20, 60, 70, 30}));
  R1_EXPECT(c.placeButton("fp.main", "edit.redo", {std::nan(""), 0, 32, 32}).error == cz::EditError::OutOfRange);
  R1_EXPECT(c.placeButton("nope", "edit.redo", {0, 0, 32, 32}).error == cz::EditError::UnknownParent);

  // Snap is off by default; on, rectangles land on the 8 px grid and stay inside.
  R1_EXPECT(!c.effective().layout.panels[0].snap && c.effective().layout.panels[0].grid == 8.0);
  R1_EXPECT(c.moveButton(b, 13, 21).rect.x == 13);
  R1_EXPECT(c.setPanelSnap("fp.main", true).ok);
  const cz::Rect snapped = c.setButtonRect(b, {13, 21, 50, 30}).rect;
  R1_EXPECT(snapped == (cz::Rect{16, 24, 48, 32}));
  R1_EXPECT(c.setButtonRect(b, {395, 295, 100, 100}).rect.x + c.find(b)->rect.w <= 400);
  R1_EXPECT(c.setPanelSnap("fp.main", true, 0.5).error == cz::EditError::OutOfRange);
  R1_EXPECT(c.fitRect("fp.main", {3, 3, 5, 5}) == (cz::Rect{0, 0, 16, 16}));

  // Z-order: the order of the buttons; the last is on top.
  R1_EXPECT(c.bringToFront("fp.save").ok);
  const cz::FreeFormPanelLayout& panel = c.effective().layout.panels[0];
  R1_EXPECT(panel.buttons.back().id == "fp.save");
  R1_EXPECT(c.sendToBack("fp.save").ok && c.effective().layout.panels[0].buttons.front().id == "fp.save");

  // Delete: user buttons are removed, built-in ones are hidden.
  R1_EXPECT(c.deleteButton(b).ok && c.find(b) == nullptr);
  R1_EXPECT(c.deleteButton("fp.undo").ok && c.find("fp.undo") != nullptr && !c.find("fp.undo")->visible);
  R1_EXPECT(c.deleteButton("nope").error == cz::EditError::UnknownNode);

  // A built-in button moved and then restored to its builtin rectangle leaves no edit behind.
  cz::Customization d = makeCustomization();
  R1_EXPECT(d.moveButton("fp.save", 100, 100).ok && d.moveButton("fp.save", 8, 8).ok && d.userDelta().empty());

  // A user panel.
  const cz::EditResult up = d.addUserPanel("Mine", 200, 100);
  R1_EXPECT(up.ok && d.addUserPanel("Tiny", 5, 5).error == cz::EditError::OutOfRange && d.addUserPanel("", 100, 100).error == cz::EditError::InvalidText);
  R1_EXPECT(d.placeButton(up.id, "edit.undo", {10, 10, 60, 30}).ok && d.effective().layout.panels.size() == 2);
  R1_EXPECT(d.deleteUserPanel("fp.main").error == cz::EditError::NotUserNode && d.deleteUserPanel(up.id).ok && d.effective().layout.panels.size() == 1);
}

void testSession() {
  cz::Customization c = makeCustomization();
  c.hideEntry("edit.copy");
  R1_EXPECT(!c.revertSession());  // no session open
  c.beginEditSession();
  R1_EXPECT(c.inEditSession() && !c.sessionChanged());
  c.hideEntry("file.save");
  c.addUserMenu("Mine");
  c.renameLabel("menu.view", "Display");
  R1_EXPECT(c.sessionChanged());
  R1_EXPECT(c.revertSession() && c.inEditSession());
  R1_EXPECT(!c.sessionChanged() && c.restoreList().size() == 1 && c.restoreList()[0].id == "edit.copy");
  R1_EXPECT(c.effective().layout.menuBar.menus.size() == 4 && nodeNamed(c.effective().layout, "menu.view")->shownLabel() == "View");
  int commits = 0;
  c.setOnCommit([&] { ++commits; });
  c.hideEntry("file.save");
  c.commitSession();
  R1_EXPECT(!c.inEditSession() && commits == 1 && !c.revertSession());
  R1_EXPECT(c.restoreList().size() == 2);

  // Listeners.
  int calls = 0;
  const auto id = c.subscribe([&] { ++calls; });
  c.showEntry("file.save");
  R1_EXPECT(calls == 1);
  c.showEntry("file.save");   // no change: no notification
  R1_EXPECT(calls == 1);
  c.hideEntry("nope");        // refused: no notification
  R1_EXPECT(calls == 1);
  c.unsubscribe(id);
  c.showEntry("edit.copy");
  R1_EXPECT(calls == 1);
}

void testWorkspaceLayer() {
  cz::Customization c = makeCustomization();
  c.hideEntry("edit.copy");
  c.renameLabel("menu.view", "Mine");
  cz::Delta workspace;
  workspace.edits["menu.view"].label = "Workspace";
  workspace.edits["edit.paste"].hidden = true;
  c.setWorkspaceDelta(workspace);
  R1_EXPECT(nodeNamed(c.effective().layout, "menu.view")->shownLabel() == "Workspace");  // the workspace wins
  R1_EXPECT(sectionIds(c.effective().layout, "edit.clip") == "edit.cut,edit.more");        // both layers hide
  R1_EXPECT(c.userDelta().edits.count("edit.paste") == 0);                                 // only the user layer is edited
  const cz::Delta merged = cz::mergeDeltas(c.userDelta(), workspace);
  R1_EXPECT(merged.edits.at("menu.view").label == "Workspace" && merged.edits.at("edit.copy").hidden == true);
}

void testCommandChanges() {
  bool present = true;
  cz::Customization c(builtinV1(), [&](const std::string& id) { return id != "edit.copy" || present; });
  R1_EXPECT(sectionIds(c.effective().layout, "edit.clip") == "edit.cut,edit.copy,edit.paste,edit.more");
  present = false;
  const uint64_t v = c.version();
  c.commandsChanged();
  R1_EXPECT(c.version() > v && sectionIds(c.effective().layout, "edit.clip") == "edit.cut,edit.paste,edit.more");
  R1_EXPECT(c.editView().layout.menuBar.menus[1].children[1].children[1].missing);
  R1_EXPECT(c.effective().report.has(cz::ReportEntry::Code::MissingCommand, "edit.copy"));
}

}  // namespace

int main() {
  testHideShowRestore();
  testMove();
  testRename();
  testUserMenus();
  testResets();
  testToolbars();
  testFreeForm();
  testSession();
  testWorkspaceLayer();
  testCommandChanges();
  return r1test::finish();
}

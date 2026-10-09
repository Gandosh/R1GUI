// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of the menu edit mode (MenuEditor inside CustomizableMenuBar), all through synthetic pointer
//   and key input on a headless UiContext: the structure of the edit display (golden rows: handles, eyes,
//   lock glyphs, indentation of sub-menus), hide and restore, moving an entry with the insertion
//   indicator, Escape cancelling a drag, dragging a section, dropping a command from the palette,
//   refusal on the locked menu (with the tooltip reason), in-place rename (Enter, Escape, empty), the
//   New menu flow, the keyboard path (cursor, Space, Alt+arrows, Delete, Insert and the command picker),
//   the context menu, dropping on a menu title, session revert, and save and reload.
// Callers: CTest (label fast).
#include <algorithm>

#include "CustomizeFixture.h"
#include "r1ui/widgets/customize/CommandPalette.h"
#include "r1ui/widgets/customize/CommandPicker.h"
#include "r1ui/widgets/customize/CustomizableBars.h"
#include "r1ui/widgets/customize/CustomizeToolStrip.h"
#include "r1ui/widgets/menu/MenuPanel.h"

namespace {

using namespace r1test;
using namespace r1ui::widgets;
using r1ui::core::layout::RectD;

struct MenuScene : CustomizeFixture {
  MenuScene() {
    bar = &t.ui.create<CustomizableMenuBar>(t.ui.root(), controller);
    palette = &t.ui.create<CommandPalette>(t.ui.root(), controller);
    palette->style().width = r1ui::core::layout::Length::px(300);
    palette->style().height = r1ui::core::layout::Length::px(260);
    palette->style().flexShrink = 0.0;
    enterEditMode();
    ed = bar->editor();
  }
  static double cx(const RectD& r) { return r.x + r.w / 2.0; }
  static double cy(const RectD& r) { return r.y + r.h / 2.0; }
  MenuEditor::RowView row(const std::string& id) {
    const int index = ed->rowIndex(id);
    R1_EXPECT(index >= 0);
    return ed->row(static_cast<size_t>(index < 0 ? 0 : index));
  }
  std::string order(const std::string& sectionId) { return join(childIds(cz::findNode(model.effective().layout, sectionId)->children)); }
  static std::vector<std::string> childIds(const std::vector<cz::Node>& nodes) {
    std::vector<std::string> out;
    for (const cz::Node& n : nodes) out.push_back(n.id);
    return out;
  }
  static std::string join(const std::vector<std::string>& v) {
    std::string out;
    for (const std::string& s : v) out += (out.empty() ? "" : ",") + s;
    return out;
  }
  // Drags a palette row onto a window point and runs `during` before the release.
  template <class Fn>
  void dragFromPalette(const std::string& commandId, double x, double y, Fn during, bool release = true) {
    palette->select(commandId);
    t.layout();
    const RectD rect = palette->list().rowRect(palette->list().selectedIndex());
    drag(rect.x + 90, cy(rect), x, y, during, release);
  }

  CustomizableMenuBar* bar = nullptr;
  CommandPalette* palette = nullptr;
  MenuEditor* ed = nullptr;
};

void testStructure() {
  MenuScene s;
  R1_EXPECT(s.ed != nullptr && s.bar->menuBar() == nullptr);
  R1_EXPECT(s.ed->titleCount() == 4);
  R1_EXPECT(s.ed->title(0).label == "File" && s.ed->title(0).selected && !s.ed->title(0).locked);
  R1_EXPECT(s.ed->title(3).label == "Help" && s.ed->title(3).locked && !s.ed->title(2).locked);
  R1_EXPECT(s.ed->setCurrentMenu("menu.edit") && !s.ed->setCurrentMenu("nope"));
  s.t.layout();
  // Golden rows of the Edit menu: a divider section, two entries, a divider section, three entries.
  R1_EXPECT(s.ed->rowCount() == 7);
  using K = cz::Kind;
  const K kinds[] = {K::Section, K::Command, K::Command, K::Section, K::Command, K::Command, K::Command};
  const char* labels[] = {"", "Undo", "Redo", "", "Cut", "Copy", "Paste"};
  for (size_t i = 0; i < 7; ++i) {
    const auto r = s.ed->row(i);
    R1_EXPECT(r.kind == kinds[i] && r.label == labels[i]);
    R1_EXPECT(r.depth == (kinds[i] == K::Section ? 0 : 1));
    R1_EXPECT(r.handle.x < r.text.x && r.text.x + r.text.w <= r.eye.x + 1 && r.eye.x + r.eye.w <= r.rect.x + r.rect.w);  // handle | label | eye, left to right
    R1_EXPECT(!r.locked && r.visible && !r.missing);
    if (i > 0) R1_EXPECT(r.rect.y >= s.ed->row(i - 1).rect.y + s.ed->row(i - 1).rect.h - 0.5);
  }
  R1_EXPECT(s.ed->row(1).shortcut == "Ctrl+Z" && s.ed->row(4).shortcut == "Ctrl+X");
  // A sub-menu's own section and entries are indented below it.
  R1_EXPECT(s.ed->setCurrentMenu("menu.view"));
  s.t.layout();
  R1_EXPECT(s.ed->rowCount() == 5);
  R1_EXPECT(s.ed->row(2).kind == K::Submenu && s.ed->row(2).depth == 1 && s.ed->row(3).kind == K::Section && s.ed->row(3).depth == 2 && s.ed->row(4).depth == 3);
  // Locked menu: rows carry the lock and no handle is hit-testable.
  R1_EXPECT(s.ed->setCurrentMenu("menu.help"));
  s.t.layout();
  R1_EXPECT(s.ed->row(1).locked && s.ed->row(1).label == "About");
}

void testHideAndRestore() {
  MenuScene s;
  s.ed->setCurrentMenu("menu.edit");
  s.t.layout();
  const auto eye = s.row("menu.edit.edit.copy").eye;
  s.click(s.cx(eye), s.cy(eye));
  R1_EXPECT(!s.row("menu.edit.edit.copy").visible && s.ed->rowCount() == 7);  // still shown, faded
  R1_EXPECT(s.model.restoreList().size() == 1 && s.model.restoreList()[0].path == "Edit > Copy");
  R1_EXPECT(s.order("menu.edit.s-2") == "menu.edit.edit.cut,menu.edit.edit.paste");  // the effective menu lost it
  // The restore list popup of the tool strip brings it back.
  CustomizeToolStrip& strip = s.t.ui.create<CustomizeToolStrip>(s.t.ui.root(), s.controller);
  s.t.layout();
  R1_EXPECT(strip.openRestoreList(50, 50));
  s.t.layout();
  const MenuPanel* panel = s.t.ui.objectAs<MenuPanel>(strip.restoreMenu().panelAt(0));
  R1_EXPECT(panel != nullptr && panel->itemCount() == 1 && panel->item(0).label == "Show Edit > Copy");
  if (panel != nullptr) {
    const MenuItemSpec item = panel->item(0);
    item.onActivate(item);
  }
  R1_EXPECT(s.model.restoreList().empty() && s.row("menu.edit.edit.copy").visible);
  R1_EXPECT(strip.openRestoreList(50, 50));  // nothing hidden: one disabled row
  const MenuPanel* none = s.t.ui.objectAs<MenuPanel>(strip.restoreMenu().panelAt(0));
  R1_EXPECT(none != nullptr && none->itemCount() == 1 && !none->item(0).enabled);
  strip.restoreMenu().close();
  // The whole menu title has an eye too.
  const auto titleEye = s.ed->title(2).eye;
  s.click(s.cx(titleEye), s.cy(titleEye));
  R1_EXPECT(!s.ed->title(2).visible && s.model.restoreList().size() == 1 && s.model.restoreList()[0].kind == cz::Kind::Menu);
}

void testMoveWithIndicator() {
  MenuScene s;
  s.ed->setCurrentMenu("menu.edit");
  s.t.layout();
  const auto paste = s.row("menu.edit.edit.paste");
  const auto undo = s.row("menu.edit.edit.undo");
  // Upper half of Undo: the indicator sits above it, the drop puts Paste before it.
  s.drag(s.cx(paste.handle), s.cy(paste.handle), s.cx(undo.text), undo.rect.y + 4, [&] {
    R1_EXPECT(s.controller.drag().active() && s.controller.drag().accepting());
    const auto& ind = s.ed->indicator();
    R1_EXPECT(ind.active && ind.placement.anchor == "menu.edit.edit.undo" && ind.placement.side == cz::Side::Before);
    R1_EXPECT(ind.line.h == 2.0 && ind.line.y < undo.rect.y + 1 && ind.line.y > undo.rect.y - 3);
  });
  R1_EXPECT(!s.ed->indicator().active && !s.controller.drag().active());
  R1_EXPECT(s.order("menu.edit.s") == "menu.edit.edit.paste,menu.edit.edit.undo,menu.edit.edit.redo");
  // Lower half of Redo (the last entry of that section): after it.
  const auto redo = s.row("menu.edit.edit.redo");
  const auto cut = s.row("menu.edit.edit.cut");
  s.drag(s.cx(cut.handle), s.cy(cut.handle), s.cx(redo.text), redo.rect.y + redo.rect.h - 3, [&] {
    R1_EXPECT(s.ed->indicator().placement.side == cz::Side::After && s.ed->indicator().placement.anchor == "menu.edit.edit.redo");
  });
  R1_EXPECT(s.order("menu.edit.s") == "menu.edit.edit.paste,menu.edit.edit.undo,menu.edit.edit.redo,menu.edit.edit.cut");
  // Over a section row: the start of that section.
  const auto section = s.ed->row(static_cast<size_t>(s.ed->rowIndex("menu.edit.s-2")));
  const auto undoNow = s.row("menu.edit.edit.undo");
  s.drag(s.cx(undoNow.handle), s.cy(undoNow.handle), s.cx(section.text), s.cy(section.rect), [&] {
    R1_EXPECT(s.ed->indicator().placement.parent == "menu.edit.s-2" && s.ed->indicator().placement.side == cz::Side::Start);
  });
  R1_EXPECT(s.order("menu.edit.s-2") == "menu.edit.edit.undo,menu.edit.edit.copy,menu.edit.edit.paste");
  // The model's version moved with each drop (live apply) and nothing else was touched.
  R1_EXPECT(s.model.userDelta().moves.size() == 3);
}

void testEscapeCancelsDrag() {
  MenuScene s;
  s.ed->setCurrentMenu("menu.edit");
  s.t.layout();
  const auto cut = s.row("menu.edit.edit.cut");
  const auto undo = s.row("menu.edit.edit.undo");
  const uint64_t version = s.model.version();
  s.drag(s.cx(cut.handle), s.cy(cut.handle), s.cx(undo.text), undo.rect.y + 4, [&] { R1_EXPECT(s.ed->indicator().active); }, false);
  s.t.ui.keyDown(Key::Escape);
  R1_EXPECT(!s.controller.drag().active() && !s.ed->indicator().active);
  s.t.ui.pointerUp(s.cx(undo.text), undo.rect.y + 4);
  R1_EXPECT(s.model.version() == version && s.model.userDelta().empty());
  // Dropping outside every target does nothing either.
  s.drag(s.cx(cut.handle), s.cy(cut.handle), 850, 650);
  R1_EXPECT(s.model.userDelta().empty());
}

void testSectionDrag() {
  MenuScene s;
  s.ed->setCurrentMenu("menu.edit");
  s.t.layout();
  const auto second = s.row("menu.edit.s-2");
  const auto first = s.row("menu.edit.s");
  s.drag(s.cx(second.handle), s.cy(second.handle), s.cx(first.text), first.rect.y + 2, [&] {
    R1_EXPECT(s.ed->indicator().active && s.ed->indicator().placement.parent == "menu.edit" && s.ed->indicator().placement.anchor == "menu.edit.s" &&
              s.ed->indicator().placement.side == cz::Side::Before);
  });
  R1_EXPECT(s.order("menu.edit") == "menu.edit.s-2,menu.edit.s");
  R1_EXPECT(s.ed->row(0).id == "menu.edit.s-2" && s.ed->row(0).kind == cz::Kind::Section);  // the display followed
  // Past the last row: the end of the menu.
  const auto top = s.row("menu.edit.s-2");
  const double below = s.ed->row(s.ed->rowCount() - 1).rect.y + s.ed->row(s.ed->rowCount() - 1).rect.h - 2;
  s.drag(s.cx(top.handle), s.cy(top.handle), s.cx(top.text), below, [&] { R1_EXPECT(s.ed->indicator().placement.side == cz::Side::After); });
  R1_EXPECT(s.order("menu.edit") == "menu.edit.s,menu.edit.s-2");
}

void testPaletteDrop() {
  MenuScene s;
  s.ed->setCurrentMenu("menu.edit");
  s.t.layout();
  const auto redo = s.row("menu.edit.edit.redo");
  s.dragFromPalette("tool.pen", s.cx(redo.text), redo.rect.y + redo.rect.h - 3, [&] {
    R1_EXPECT(s.controller.drag().active() && s.controller.drag().accepting() && s.ed->indicator().active);
    R1_EXPECT(s.ed->indicator().placement.anchor == "menu.edit.edit.redo" && s.ed->indicator().placement.side == cz::Side::After);
  });
  const cz::Node* section = cz::findNode(s.model.effective().layout, "menu.edit.s");
  R1_EXPECT(section != nullptr && section->children.size() == 3 && section->children[2].commandId == "tool.pen" && section->children[2].user);
  // Start of a section by hovering its row.
  const auto sec2 = s.ed->row(static_cast<size_t>(s.ed->rowIndex("menu.edit.s-2")));
  s.dragFromPalette("tool.hand", s.cx(sec2.text), s.cy(sec2.rect), [&] { R1_EXPECT(s.ed->indicator().placement.side == cz::Side::Start); });
  R1_EXPECT(cz::findNode(s.model.effective().layout, "menu.edit.s-2")->children.front().commandId == "tool.hand");
  // Onto another menu's title: the end of its last section.
  const auto viewTitle = s.ed->title(2).rect;
  s.dragFromPalette("tool.select", s.cx(viewTitle), s.cy(viewTitle), [&] { R1_EXPECT(s.ed->indicator().title == 2); });
  R1_EXPECT(cz::findNode(s.model.effective().layout, "menu.view.s")->children.back().commandId == "tool.select");
  // The locked Help menu refuses, with a reason.
  const auto helpTitle = s.ed->title(3).rect;
  s.dragFromPalette("tool.select", s.cx(helpTitle), s.cy(helpTitle), [&] { R1_EXPECT(!s.controller.drag().accepting() && !s.ed->indicator().active); });
  R1_EXPECT(s.controller.lastRefusal().find("locked") != std::string::npos);
  R1_EXPECT(cz::findNode(s.model.effective().layout, "menu.help.s")->children.size() == 1);
  // A drop on empty space beside the editor, or a command the registry no longer has, changes nothing.
  const size_t added = s.model.userDelta().added.size();
  s.dragFromPalette("tool.select", 880, 120, [&] { R1_EXPECT(!s.controller.drag().accepting()); });
  R1_EXPECT(s.model.userDelta().added.size() == added);
}

void testRename() {
  MenuScene s;
  s.ed->setCurrentMenu("menu.edit");
  s.t.layout();
  const auto cut = s.row("menu.edit.edit.cut");
  s.doubleClick(cut.text.x + 40, s.cy(cut.text));
  R1_EXPECT(s.ed->renaming() && s.ed->renamingId() == "menu.edit.edit.cut");
  s.type("Cut it");
  s.t.ui.keyDown(Key::Enter);
  s.t.layout();
  R1_EXPECT(!s.ed->renaming() && s.model.find("menu.edit.edit.cut")->userLabel == "Cut it");
  R1_EXPECT(s.row("menu.edit.edit.cut").label == "Cut it");
  // Escape cancels and keeps the old text.
  R1_EXPECT(s.ed->beginRename("menu.edit.edit.cut"));
  s.type("Nope");
  s.t.ui.keyDown(Key::Escape);
  s.t.layout();
  R1_EXPECT(!s.ed->renaming() && s.row("menu.edit.edit.cut").label == "Cut it");
  // Empty restores the default.
  R1_EXPECT(s.ed->beginRename("menu.edit.edit.cut"));
  s.t.ui.keyDown(Key::Backspace);
  s.t.ui.keyDown(Key::Enter);
  s.t.layout();
  R1_EXPECT(s.row("menu.edit.edit.cut").label == "Cut" && s.model.find("menu.edit.edit.cut")->userLabel.empty());
  // A menu title renames by double-click too; a locked one does not.
  const auto title = s.ed->title(2).rect;
  s.doubleClick(title.x + 14, s.cy(title));
  R1_EXPECT(s.ed->renaming() && s.ed->renamingId() == "menu.view");
  s.type("Display");
  s.t.ui.keyDown(Key::Enter);
  s.t.layout();
  R1_EXPECT(s.ed->title(2).label == "Display");
  R1_EXPECT(!s.ed->beginRename("menu.help") && s.controller.lastRefusal().find("locked") != std::string::npos);
  // Losing focus commits a changed text.
  s.ed->setCurrentMenu("menu.edit");
  s.t.layout();
  R1_EXPECT(s.ed->beginRename("menu.edit.edit.paste"));
  s.type("Insert");
  s.t.ui.clearFocus();
  s.t.layout();
  R1_EXPECT(!s.ed->renaming() && s.row("menu.edit.edit.paste").label == "Insert");
}

void testNewMenuAndDelete() {
  MenuScene s;
  const auto button = s.ed->newMenuRect();
  s.click(s.cx(button), s.cy(button));
  R1_EXPECT(s.ed->titleCount() == 5 && s.ed->title(4).label == "New menu" && s.ed->title(4).user);
  R1_EXPECT(s.ed->renaming() && s.ed->currentMenu() == s.ed->title(4).id);  // the title is in place edit
  s.type("My tools");
  s.t.ui.keyDown(Key::Enter);
  s.t.layout();
  R1_EXPECT(s.ed->title(4).label == "My tools");
  // The user menu comes after the built-in ones and takes entries by drag.
  s.dragFromPalette("tool.pen", s.cx(s.ed->title(4).rect), s.cy(s.ed->title(4).rect), [] {});
  s.t.layout();
  R1_EXPECT(s.ed->rowCount() == 2 && s.ed->row(1).label == "Pen" && s.ed->row(1).user);
  // A second one gets a unique name.
  s.click(s.cx(s.ed->newMenuRect()), s.cy(s.ed->newMenuRect()));
  R1_EXPECT(s.ed->title(5).label == "New menu");
  s.t.ui.keyDown(Key::Escape);
  // Delete through the context menu of its title.
  const auto title = s.ed->title(4).rect;
  s.rightClick(s.cx(title), s.cy(title));
  R1_EXPECT(s.ed->contextMenu().isOpen());
  const MenuPanel* panel = s.t.ui.objectAs<MenuPanel>(s.ed->contextMenu().panelAt(0));
  bool found = false;
  for (int i = 0; panel != nullptr && i < panel->itemCount(); ++i) {
    if (panel->item(i).label == "Delete menu") {
      const MenuItemSpec item = panel->item(i);
      item.onActivate(item);
      found = true;
    }
  }
  s.t.layout();
  R1_EXPECT(found && s.ed->titleCount() == 5 && s.ed->title(4).label == "New menu");
  // A built-in title offers no delete.
  s.rightClick(s.cx(s.ed->title(0).rect), s.cy(s.ed->title(0).rect));
  const MenuPanel* builtin = s.t.ui.objectAs<MenuPanel>(s.ed->contextMenu().panelAt(0));
  bool deletable = false;
  for (int i = 0; builtin != nullptr && i < builtin->itemCount(); ++i) deletable = deletable || builtin->item(i).label == "Delete menu";
  R1_EXPECT(!deletable);
  s.ed->contextMenu().close();
}

void testLockedMenu() {
  MenuScene s;
  s.ed->setCurrentMenu("menu.help");
  s.t.layout();
  const auto about = s.row("menu.help.help.about");
  R1_EXPECT(about.locked);
  s.click(s.cx(about.eye), s.cy(about.eye));
  R1_EXPECT(s.model.userDelta().empty() && s.controller.lastRefusal().find("locked") != std::string::npos);
  // Hovering a locked row gives the reason as its tooltip; the handle zone does not start a drag.
  s.t.ui.pointerMove(s.cx(about.text), s.cy(about.text));
  R1_EXPECT(std::string(s.ed->tooltipText()).find("locked") != std::string::npos);
  const double version = static_cast<double>(s.model.version());
  s.drag(s.cx(about.handle), s.cy(about.handle), s.cx(about.handle), about.rect.y - 60);
  R1_EXPECT(!s.controller.drag().active() && static_cast<double>(s.model.version()) == version);
  s.t.ui.pointerMove(s.cx(s.ed->title(3).rect), s.cy(s.ed->title(3).rect));
  R1_EXPECT(std::string(s.ed->tooltipText()).find("locked") != std::string::npos);
  // Keyboard edits refuse too.
  s.ed->setCursor("menu.help.help.about");
  s.t.ui.focusWidget(s.ed->id());
  s.t.ui.keyDown(Key::Delete);
  R1_EXPECT(s.model.userDelta().empty());
}

void testKeyboard() {
  MenuScene s;
  s.ed->setCurrentMenu("menu.edit");
  s.t.layout();
  s.t.ui.focusWidget(s.ed->id());
  s.t.ui.keyDown(Key::Down);
  R1_EXPECT(s.ed->cursorId() == s.ed->row(0).id);
  s.t.ui.keyDown(Key::Down);
  s.t.ui.keyDown(Key::Down);
  R1_EXPECT(s.ed->cursorId() == "menu.edit.edit.redo");
  s.t.ui.keyDown(Key::Space);  // toggles visibility
  R1_EXPECT(!s.row("menu.edit.edit.redo").visible);
  s.t.ui.keyDown(Key::Space);
  R1_EXPECT(s.row("menu.edit.edit.redo").visible);
  // Alt+Up moves the entry above Undo; Alt+Down at the edge crosses into the next section.
  s.t.ui.keyDown(Key::Up, Mod::kAlt);
  R1_EXPECT(s.order("menu.edit.s") == "menu.edit.edit.redo,menu.edit.edit.undo");
  s.t.ui.keyDown(Key::Down, Mod::kAlt);
  s.t.ui.keyDown(Key::Down, Mod::kAlt);
  R1_EXPECT(s.order("menu.edit.s-2").find("menu.edit.edit.redo") == 0);
  // Left and Right switch menus; Home and End jump.
  s.t.ui.keyDown(Key::Right);
  R1_EXPECT(s.ed->currentMenu() == "menu.view");
  s.t.ui.keyDown(Key::Left);
  s.t.ui.keyDown(Key::End);
  R1_EXPECT(s.ed->cursorId() == s.ed->row(s.ed->rowCount() - 1).id);
  // Delete hides a built-in entry.
  s.t.ui.keyDown(Key::Delete);
  R1_EXPECT(!s.row(s.ed->cursorId()).visible);

  // Insert opens the command picker: type to search, Enter adds the command after the cursor row.
  s.ed->setCursor("menu.edit.edit.cut");
  s.t.ui.keyDown(Key::Insert);
  s.t.layout();
  R1_EXPECT(s.t.ui.overlays().anyModal());
  s.type("hand");
  s.t.layout();
  s.t.ui.keyDown(Key::Enter);
  s.t.layout();
  R1_EXPECT(!s.t.ui.overlays().any());
  const cz::Node* section = cz::findNode(s.model.effective().layout, "menu.edit.s-2");
  size_t at = 0;
  for (size_t i = 0; i < section->children.size(); ++i) at = section->children[i].id == "menu.edit.edit.cut" ? i : at;
  R1_EXPECT(section->children[at + 1].commandId == "tool.hand" && section->children[at + 1].user);
  // The arrow keys choose in the picker and Escape closes it without a change.
  const size_t entries = s.model.userDelta().added.size();
  s.t.ui.keyDown(Key::Insert);
  s.t.layout();
  s.t.ui.keyDown(Key::Down);
  s.t.ui.keyDown(Key::Escape);
  s.t.layout();
  R1_EXPECT(!s.t.ui.overlays().any() && s.model.userDelta().added.size() == entries);
}

void testContextMenu() {
  MenuScene s;
  s.ed->setCurrentMenu("menu.edit");
  s.t.layout();
  const auto cut = s.row("menu.edit.edit.cut");
  s.rightClick(s.cx(cut.text), s.cy(cut.text));
  R1_EXPECT(s.ed->contextMenu().isOpen() && s.ed->cursorId() == "menu.edit.edit.cut");
  const MenuPanel* panel = s.t.ui.objectAs<MenuPanel>(s.ed->contextMenu().panelAt(0));
  std::vector<std::string> labels;
  for (int i = 0; panel != nullptr && i < panel->itemCount(); ++i) labels.push_back(panel->item(i).label);
  const auto has = [&](const char* label) { return std::find(labels.begin(), labels.end(), label) != labels.end(); };
  R1_EXPECT(has("Hide") && has("Rename...") && has("Add command...") && has("Add separator") && has("Add heading") && has("Add sub-menu") && has("Reset this menu"));
  const auto activate = [&](const char* label) {
    const MenuPanel* p = s.t.ui.objectAs<MenuPanel>(s.ed->contextMenu().panelAt(0));
    for (int i = 0; p != nullptr && i < p->itemCount(); ++i) {
      if (p->item(i).label == label) {
        const MenuItemSpec item = p->item(i);
        item.onActivate(item);
      }
    }
    s.ed->contextMenu().close();
    s.t.layout();
  };
  activate("Add separator");
  R1_EXPECT(s.ed->rowCount() == 8 && s.row(s.ed->row(5).id).kind == cz::Kind::Separator);  // after Cut
  s.rightClick(s.cx(cut.text), s.cy(cut.text));
  activate("Add sub-menu");
  R1_EXPECT(s.ed->renaming());  // the new sub-menu's name is in place edit
  s.type("Extras");
  s.t.ui.keyDown(Key::Enter);
  s.t.layout();
  bool extras = false;
  for (size_t i = 0; i < s.ed->rowCount(); ++i) extras = extras || (s.ed->row(i).kind == cz::Kind::Submenu && s.ed->row(i).label == "Extras");
  R1_EXPECT(extras);
  // Reset this menu removes everything added to it.
  s.rightClick(s.cx(cut.text), s.cy(cut.text));
  activate("Reset this menu");
  R1_EXPECT(s.ed->rowCount() == 7 && s.model.userDelta().empty());
}

void testSessionAndStorage() {
  MenuScene s;
  R1_EXPECT(s.controller.editMode() && s.model.inEditSession());
  s.ed->setCurrentMenu("menu.edit");
  s.t.layout();
  const auto eye = s.row("menu.edit.edit.copy").eye;
  s.click(s.cx(eye), s.cy(eye));
  s.model.renameLabel("menu.view", "Display");
  R1_EXPECT(s.model.sessionChanged());
  R1_EXPECT(s.controller.revertSession());
  R1_EXPECT(s.model.userDelta().empty() && s.ed->title(2).label == "View" && s.row("menu.edit.edit.copy").visible);
  R1_EXPECT(!s.store.contents());
  // Changes are saved when edit mode is left.
  s.click(s.cx(eye), s.cy(eye));
  s.controller.setEditMode(false);
  R1_EXPECT(s.store.contents().has_value() && s.store.contents()->find("menu.edit.edit.copy") != std::string::npos);
  // Another window family loads the file and shows the same menus.
  MenuScene other;
  other.controller.setEditMode(false);
  cz::MemoryTextStore copy;
  copy.setContents(*s.store.contents());
  cz::CustomizationStorage storage(other.model, copy);
  R1_EXPECT(storage.load().ok);
  other.t.layout();
  MenuBar* realBar = other.bar->menuBar();
  R1_EXPECT(realBar != nullptr && realBar->openMenu(1));
  other.t.layout();
  const MenuPanel* panel = other.t.ui.objectAs<MenuPanel>(realBar->controller().panelAt(0));
  bool hasCopy = false, hasCut = false;
  for (int i = 0; panel != nullptr && i < panel->itemCount(); ++i) {
    hasCopy = hasCopy || panel->item(i).id == "edit.copy";
    hasCut = hasCut || panel->item(i).id == "edit.cut";
  }
  R1_EXPECT(!hasCopy && hasCut);
  realBar->closeMenu();
}

}  // namespace

int main() {
  testStructure();
  testHideAndRestore();
  testMoveWithIndicator();
  testEscapeCancelsDrag();
  testSectionDrag();
  testPaletteDrop();
  testRename();
  testNewMenuAndDelete();
  testLockedMenu();
  testKeyboard();
  testContextMenu();
  testSessionAndStorage();
  return r1test::finish();
}

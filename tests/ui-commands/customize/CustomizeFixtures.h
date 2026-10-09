// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the built-in layouts the customization tests share: a menu bar with File, Edit (two sections
//   and a sub-menu), View and a locked Help menu, one toolbar with a flyout group, one free-form
//   panel, plus a "v2" version of the same layouts that simulates a product update (an entry added,
//   one removed, one reordered), and small helpers to read the results.
// Why: effectiveLayout, the editing operations and the persistence tests all need the same realistic
//   built-in set; ids follow the host convention "menu.<name>", "<menu>.<entry>".
// Callers: tests/ui-commands/customize/*_test.cpp.
#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/commands/customize/Customization.h"
#include "r1ui/commands/customize/CustomizationIo.h"

namespace r1test {

namespace cz = r1ui::commands::customize;

inline cz::Node cmdNode(const std::string& id, const std::string& command) { return cz::Node::command(id, command); }

inline cz::LayoutSet builtinV1() {
  cz::LayoutSet set;
  set.menuBar.id = "menubar";
  set.menuBar.menus.push_back(cz::Node::menu("menu.file", "File", {cz::Node::section("file.main", "", {cmdNode("file.open", "file.open"), cmdNode("file.save", "file.save")})}));
  set.menuBar.menus.push_back(cz::Node::menu(
      "menu.edit", "Edit",
      {cz::Node::section("edit.history", "", {cmdNode("edit.undo", "edit.undo"), cmdNode("edit.redo", "edit.redo")}),
       cz::Node::section("edit.clip", "Clipboard",
                         {cmdNode("edit.cut", "edit.cut"), cmdNode("edit.copy", "edit.copy"), cmdNode("edit.paste", "edit.paste"),
                          cz::Node::submenu("edit.more", "More", {cz::Node::section("edit.more.main", "", {cmdNode("edit.dup", "edit.duplicate")})})})}));
  set.menuBar.menus.push_back(cz::Node::menu("menu.view", "View", {cz::Node::section("view.main", "", {cmdNode("view.grid", "view.grid"), cmdNode("view.rulers", "view.rulers")})}));
  cz::Node help = cz::Node::menu("menu.help", "Help", {cz::Node::section("help.main", "", {cmdNode("help.about", "help.about")})});
  help.locked = true;
  set.menuBar.menus.push_back(std::move(help));

  cz::ToolbarLayout tb;
  tb.id = "tb.main";
  tb.title = "Main tools";
  tb.items = {cmdNode("tb.select", "tool.select"), cmdNode("tb.pen", "tool.pen"), cz::Node::separator("tb.sep1"), cmdNode("tb.undo", "edit.undo"),
              cz::Node::group("tb.shapes", {cmdNode("tb.rect", "tool.rect"), cmdNode("tb.ellipse", "tool.ellipse")})};
  set.toolbars.push_back(std::move(tb));

  cz::FreeFormPanelLayout panel;
  panel.id = "fp.main";
  panel.title = "Quick actions";
  panel.width = 400;
  panel.height = 300;
  panel.buttons = {cz::Node::freeButton("fp.save", "file.save", {8, 8, 64, 32}), cz::Node::freeButton("fp.undo", "edit.undo", {80, 8, 64, 32})};
  set.panels.push_back(std::move(panel));
  return set;
}

// The update: edit.copy moved before edit.cut, edit.redo removed, a new "Select all" after paste, a
// new view entry, a new toolbar button.
inline cz::LayoutSet builtinV2() {
  cz::LayoutSet set = builtinV1();
  cz::Node& edit = set.menuBar.menus[1];
  auto& history = edit.children[0].children;
  history.erase(history.begin() + 1);
  auto& clip = edit.children[1].children;
  std::swap(clip[0], clip[1]);
  clip.insert(clip.begin() + 3, cmdNode("edit.selectAll", "edit.selectAll"));
  set.menuBar.menus[2].children[0].children.push_back(cmdNode("view.zoom", "view.zoom"));
  set.toolbars[0].items.insert(set.toolbars[0].items.begin() + 1, cmdNode("tb.hand", "tool.hand"));
  return set;
}

inline std::vector<std::string> childIds(const std::vector<cz::Node>& nodes) {
  std::vector<std::string> out;
  for (const cz::Node& n : nodes) out.push_back(n.id);
  return out;
}

inline std::string join(const std::vector<std::string>& v) {
  std::string out;
  for (const std::string& s : v) out += (out.empty() ? "" : ",") + s;
  return out;
}

inline const cz::Node* nodeNamed(const cz::LayoutSet& set, const std::string& id) { return cz::findNode(set, id); }

// Ids of the entries of a section, in order.
inline std::string sectionIds(const cz::LayoutSet& set, const std::string& section) {
  const cz::Node* n = nodeNamed(set, section);
  return n == nullptr ? "<none>" : join(childIds(n->children));
}

// Every command id the fixtures use exists, except those a test removes.
inline cz::CommandExists everything() {
  return [](const std::string&) { return true; };
}

inline cz::Customization makeCustomization() {
  return cz::Customization(builtinV1(), everything());
}

}  // namespace r1test

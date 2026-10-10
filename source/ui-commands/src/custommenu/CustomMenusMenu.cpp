// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: commandIdFor, parseMenuCommandId and describeCustomMenusMenu (CustomMenusMenu.h).
// Invariants: parseMenuCommandId(commandIdFor(a, id)) round-trips for every valid menu id; the
//   description is a pure function of the set (menus in creation order within each group).
// Callers: widget layer, tests.
#include "r1ui/commands/custommenu/CustomMenusMenu.h"

namespace r1ui::commands::custommenu {

namespace {

constexpr std::string_view kPrefix = "custommenu.";

const char* actionWord(MenuAction action) {
  switch (action) {
    case MenuAction::Open: return "open";
    case MenuAction::Edit: return "edit";
    case MenuAction::Save: return "save";
    case MenuAction::Delete: return "delete";
  }
  return "open";
}

MenuDescEntry command(MenuAction action, const std::string& menuId) {
  MenuDescEntry e;
  e.kind = MenuDescEntry::Kind::Command;
  e.commandId = commandIdFor(action, menuId);
  return e;
}

MenuDescEntry separator() {
  MenuDescEntry e;
  e.kind = MenuDescEntry::Kind::Separator;
  return e;
}

MenuDescEntry heading(std::string text) {
  MenuDescEntry e;
  e.kind = MenuDescEntry::Kind::Heading;
  e.text = std::move(text);
  return e;
}

MenuDescEntry submenu(std::string text, std::vector<MenuDescEntry> children) {
  MenuDescEntry e;
  e.kind = MenuDescEntry::Kind::Submenu;
  e.text = std::move(text);
  e.children = std::move(children);
  return e;
}

MenuDescEntry perMenu(const CustomMenu& menu) {
  return submenu(menu.name, {command(MenuAction::Edit, menu.id), command(MenuAction::Save, menu.id), separator(), command(MenuAction::Delete, menu.id)});
}

MenuDescEntry globalCommand(const char* id) {
  MenuDescEntry e;
  e.kind = MenuDescEntry::Kind::Command;
  e.commandId = id;
  return e;
}

}  // namespace

std::string commandIdFor(MenuAction action, const std::string& menuId) { return std::string(kPrefix) + actionWord(action) + "." + menuId; }

std::optional<MenuCommandRef> parseMenuCommandId(std::string_view commandId) {
  if (commandId.substr(0, kPrefix.size()) != kPrefix) return std::nullopt;
  const std::string_view rest = commandId.substr(kPrefix.size());
  const size_t dot = rest.find('.');
  if (dot == std::string_view::npos || dot + 1 >= rest.size()) return std::nullopt;
  const std::string_view word = rest.substr(0, dot);
  MenuCommandRef ref;
  bool known = false;
  for (const MenuAction action : {MenuAction::Open, MenuAction::Edit, MenuAction::Save, MenuAction::Delete}) {
    if (word == actionWord(action)) {
      ref.action = action;
      known = true;
    }
  }
  if (!known) return std::nullopt;
  ref.menuId = std::string(rest.substr(dot + 1));
  return ref;
}

std::vector<MenuDescEntry> describeCustomMenusMenu(const CustomMenuSet& set) {
  std::vector<MenuDescEntry> out;
  std::vector<const CustomMenu*> panels;
  std::vector<const CustomMenu*> pies;
  for (const CustomMenu& m : set.menus()) (m.kind == MenuKind::Panel ? panels : pies).push_back(&m);
  if (!panels.empty()) {
    out.push_back(heading("Dockable menus"));
    for (const CustomMenu* m : panels) out.push_back(command(MenuAction::Open, m->id));
    out.push_back(separator());
  }
  if (!pies.empty()) {
    out.push_back(heading("Pie menus"));
    for (const CustomMenu* m : pies) out.push_back(perMenu(*m));
    out.push_back(separator());
  }
  if (!panels.empty()) {
    std::vector<MenuDescEntry> edits;
    for (const CustomMenu* m : panels) edits.push_back(perMenu(*m));
    out.push_back(submenu("Edit Dockable Menu", std::move(edits)));
    out.push_back(separator());
  }
  out.push_back(globalCommand(kCommandCreate));
  out.push_back(globalCommand(kCommandLoad));
  return out;
}

}  // namespace r1ui::commands::custommenu

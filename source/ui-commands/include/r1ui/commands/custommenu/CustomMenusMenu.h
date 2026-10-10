// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the description of the top-level "Custom Menus" main menu (owner requirement 2026-10-10) and the
//   naming scheme of the commands behind it, as plain data that the widget layer turns into a
//   CommandMenuEntry list and a set of registered commands.
// Why: a user must be able to reopen a closed dockable menu, and to edit, save or delete any custom menu,
//   from the main menu bar. The model owns what the menu contains and which command id means what, so
//   the registry binder, the menu builder and the tests agree on it without a widget in sight.
// Callers: the widget layer (CustomMenuCommands, CustomMenusMenu), the host, tests.
// Shape of the menu (empty parts and redundant separators disappear):
//   Dockable menus (heading) / one command per dockable menu (opens or focuses its panel)
//   ---
//   Pie menus (heading) / one submenu per pie menu: Edit..., Save As..., ---, Delete
//   ---
//   Edit Dockable Menu (submenu) / one submenu per dockable menu: Edit..., Save As..., ---, Delete
//   ---
//   Create Custom Menu... / Load Custom Menu...
// Command ids: "custommenu.<action>.<menu id>" for per-menu actions ("custommenu.open.menu.3"), and
//   "custommenu.create" / "custommenu.load" for the two global ones. All fit the 128-byte id limit.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/commands/custommenu/CustomMenuSet.h"

namespace r1ui::commands::custommenu {

inline constexpr const char* kCustomMenusTitle = "Custom Menus";
inline constexpr const char* kCommandCreate = "custommenu.create";
inline constexpr const char* kCommandLoad = "custommenu.load";

enum class MenuAction : uint8_t { Open, Edit, Save, Delete };

// "custommenu.open.menu.3" and friends.
std::string commandIdFor(MenuAction action, const std::string& menuId);

struct MenuCommandRef {
  MenuAction action = MenuAction::Open;
  std::string menuId;
};
// The inverse; nullopt for any other id (including the two global commands).
std::optional<MenuCommandRef> parseMenuCommandId(std::string_view commandId);

// One row of the described menu; the same four kinds as widgets::CommandMenuEntry.
struct MenuDescEntry {
  enum class Kind : uint8_t { Command, Separator, Heading, Submenu };
  Kind kind = Kind::Command;
  std::string commandId;
  std::string text;  // Heading and Submenu title
  std::vector<MenuDescEntry> children;
};

std::vector<MenuDescEntry> describeCustomMenusMenu(const CustomMenuSet& set);

}  // namespace r1ui::commands::custommenu

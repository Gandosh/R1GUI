// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the widget-side form of the "Custom Menus" main menu: the CommandMenuEntry list built from the
//   description in ui-commands/custommenu/CustomMenusMenu.h, the title for bindCommandMenuBar and the
//   customization-layout node for hosts that use the customizable menu bar.
// Why: owner requirement 2026-10-10: every created dockable menu appears as an entry in a top-level main
//   menu "Custom Menus", placed after the existing menus, so a closed panel can be reopened; pie menus
//   are listed with Edit and Delete. The shape lives in the headless description; this file only converts
//   it, so a host adds the menu with one call.
// Callers: the host (menu bar construction and rebuild when the set changes), tests.
// Use with bindCommandMenuBar: customMenusMenuTitle(set) is a snapshot of the entries; the bar keeps those
//   entries, so a host that binds it rebuilds the bar's titles when set.version() changes.
// Use with the customizable bar: put customMenusMenuNode(set) last in the built-in LayoutSet's menus and
//   call Customization::setBuiltin when set.version() changes.
// The commands behind the entries are registered by CustomMenuCommands; entries whose command is not
//   registered are skipped by the menu builders.
#pragma once

#include <string>
#include <vector>

#include "r1ui/commands/custommenu/CustomMenusMenu.h"
#include "r1ui/commands/customize/Layout.h"
#include "r1ui/widgets/commands/CommandMenus.h"

namespace r1ui::widgets {

std::vector<CommandMenuEntry> customMenusMenuEntries(const commands::custommenu::CustomMenuSet& set);
CommandMenuTitle customMenusMenuTitle(const commands::custommenu::CustomMenuSet& set);
// The menu as a customization-layout node with the given stable id.
commands::customize::Node customMenusMenuNode(const commands::custommenu::CustomMenuSet& set, const std::string& id = "menu.custom");

}  // namespace r1ui::widgets

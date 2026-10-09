// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: building menus from command ids: a layout of entries (command, separator, heading, submenu)
//   becomes a MenuSpec whose rows take label, icon, kind, enabled and checked state, shortcut text and
//   tooltip from the command and run the command through the router; plus the live refresh of menus
//   that are open while a chord, an enabled answer or a checked answer changes, and the binders for a
//   MenuBar and a context menu.
// Why: spec 07 rules 26 to 36: the menu entry created from a command shows the command's label,
//   icon and behaviour; the shortcut text on its right is the first valid chord in force; the tooltip
//   is the description followed by the chord in brackets; enabled and checked are re-evaluated; an
//   entry whose command is not visible is collapsed (separators stay consistent: none leading,
//   trailing or doubled); clicking runs the same guarded execution as the chord. Decision D15:
//   shortcut text refreshes in a menu that stays open.
// Callers: hosts and the gallery. Calls: MenuModel / MenuBar / MenuController / MenuPanel (existing menu
//   widgets), ui-commands through CommandServices.
// Mapping: Action and Momentary -> Action row; Toggle -> Check row; Radio -> Radio row (consecutive
//   Radio rows form one group in the menu widgets). Rows keep the command id in MenuItemSpec::id; that
//   is how the live refresh finds the command of an open row. Icons must exist in the icon set (the
//   menu widgets throw at paint for a well-formed name without an SVG).
// Live refresh: refreshOpenCommandMenus walks the open overlays, finds MenuPanel rows whose id is a
//   registered command and applies label, shortcut, tooltip, enabled and checked in place
//   (MenuPanel::refreshItem). It cannot add or remove rows: a command that becomes invisible while its
//   menu is open disappears at the next open. Closed menus of a bound MenuBar are rebuilt just
//   before they open (MenuBar::setBeforeOpen), so they are never stale.
// Lifetime: the MenuSpec's callbacks capture the router by pointer; the services must outlive every
//   menu built from them.
#pragma once

#include <span>
#include <string>
#include <vector>

#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/menu/MenuBar.h"
#include "r1ui/widgets/menu/MenuController.h"
#include "r1ui/widgets/menu/MenuModel.h"

namespace r1ui::widgets {

struct CommandMenuEntry {
  enum class Kind : uint8_t { Command, Separator, Heading, Submenu };
  Kind kind = Kind::Command;
  std::string commandId;                  // Command
  std::string text;                       // Heading and Submenu title
  std::vector<CommandMenuEntry> children; // Submenu

  static CommandMenuEntry command(std::string id);
  static CommandMenuEntry separator();
  static CommandMenuEntry heading(std::string text);
  static CommandMenuEntry submenu(std::string text, std::vector<CommandMenuEntry> children);
};

struct CommandMenuOptions {
  bool upperCaseShortcuts = false;  // spec 07 rule 27 shows capitals; the reference menus use mixed case
  bool showIcons = true;
};

// The row for one command, as it looks right now. Visibility is the caller's concern:
// buildCommandMenu skips invisible commands, this function does not look at it.
MenuItemSpec commandMenuItem(const CommandServices& services, const commands::CommandDef& command, const CommandMenuOptions& options = {});

// A menu for `entries`; unknown command ids and invisible commands are skipped, redundant separators
// removed, at most kMaxMenuItems rows per level and kMaxMenuDepth levels are built.
MenuSpec buildCommandMenu(const CommandServices& services, std::span<const CommandMenuEntry> entries, const CommandMenuOptions& options = {});

struct CommandMenuTitle {
  std::string title;
  std::vector<CommandMenuEntry> entries;
};

// Adds one menu per title to `bar` and keeps them current (rebuilt right before each opens).
void bindCommandMenuBar(MenuBar& bar, const CommandServices& services, std::vector<CommandMenuTitle> titles, const CommandMenuOptions& options = {});

// Opens a context menu of `entries` at (x, y) through `controller`; false when it has no rows.
bool openCommandContextMenu(MenuController& controller, const CommandServices& services, std::span<const CommandMenuEntry> entries, double x, double y,
                            const CommandMenuOptions& options = {});

// Re-reads every command row of every open menu; returns the number of rows updated.
size_t refreshOpenCommandMenus(UiContext& ui, const CommandServices& services, const CommandMenuOptions& options = {});

}  // namespace r1ui::widgets

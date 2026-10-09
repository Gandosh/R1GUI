// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the conversions between the customization model (ui-commands/customize) and the existing
//   command-driven widgets: an effective MenuLayout becomes the CommandMenuTitle lists that
//   bindCommandMenuBar consumes (plus the user labels of command entries), an effective ToolbarLayout
//   becomes the CommandToolbarItem runs that bindCommandToolbar consumes (a spacer splits the runs),
//   and menuLayoutFromEntries turns the flat CommandMenuEntry layouts hosts already have into the
//   built-in MenuLayout the customization starts from.
// Why: non-edit-mode menus and toolbars must stay exactly what the command binders built before
//   customization existed (the pixel-equal visual tests of the command group still pass untouched), so
//   the customization layer feeds the same binders with the effective layout instead of drawing
//   anything itself.
// Callers: CustomizableMenuBar, CustomizableToolbar, hosts and the gallery (builtin layouts), tests.
// Flattening rules: sections are separated by a separator row; a section's heading text becomes a
//   Heading row at its start; separators and headings inside a section stay entries; a sub-menu holds the
//   flattened sections of its own. The inverse (menuLayoutFromEntries) splits a flat list at separators
//   (the separators become the section boundaries) and turns a Heading that directly follows a
//   boundary into the section heading, so converting there and back yields the same rows.
// Label overrides: CommandMenuEntry has no label field, so user labels of command entries travel in a
//   parallel tree of LabelOverride; applyLabelOverrides patches the built MenuSpec (the row keeps its
//   behaviour; its id gets the prefix "customize:" so the live refresh of open menus, which resets labels
//   to the command's own, leaves it alone: such rows refresh when the menu is opened again).
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "r1ui/commands/customize/Customization.h"
#include "r1ui/widgets/commands/CommandMenus.h"
#include "r1ui/widgets/commands/CommandToolbar.h"

namespace r1ui::widgets {

struct LabelOverride {
  commands::customize::Kind kind = commands::customize::Kind::Command;
  std::string commandId;
  std::string label;  // user label of a command entry; empty = none
  std::vector<LabelOverride> children;
};

struct MenuConversion {
  std::vector<CommandMenuTitle> titles;
  std::vector<std::vector<LabelOverride>> overrides;  // parallel to titles[i].entries
};

// `effective` should come from Customization::effective() (hidden entries already dropped).
MenuConversion convertMenuBar(const commands::customize::MenuLayout& effective, const commands::customize::Customization& model);

// Entries of one menu or sub-menu: the flattened sections.
std::vector<CommandMenuEntry> flattenSections(const std::vector<commands::customize::Node>& sections, const commands::customize::Customization& model,
                                              std::vector<LabelOverride>* overrides);

// A toolbar as runs of items between spacers: a spacer flag follows every run but the last.
struct ToolbarRun {
  std::vector<CommandToolbarItem> items;
  bool spacerAfter = false;
};
std::vector<ToolbarRun> convertToolbar(const commands::customize::ToolbarLayout& effective);

// The built-in menu (a Menu node with sections) for a flat entry list; ids are "<menuId>.sN" for sections
// and "<menuId>.<commandId>" (made unique) for entries.
commands::customize::Node menuNodeFromEntries(const std::string& menuId, const std::string& title, const std::vector<CommandMenuEntry>& entries);
// The built-in toolbar layout for command toolbar items; ids are "<toolbarId>.<commandId>" (made unique).
commands::customize::ToolbarLayout toolbarFromItems(const std::string& toolbarId, const std::string& title, const std::vector<CommandToolbarItem>& items);

// Patches labels into a built menu (see the header comment).
void applyLabelOverrides(std::vector<MenuItemSpec>& items, const std::vector<CommandMenuEntry>& entries, const std::vector<LabelOverride>& overrides,
                         const CommandServices& services);

}  // namespace r1ui::widgets

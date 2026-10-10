// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the conversion of the "Custom Menus" description into CommandMenuEntry lists and a layout node
//   (CustomMenusMenu.h).
// Invariants: the entries mirror the description one to one (kinds, texts, command ids, nesting); depth is
//   bounded by the description itself (two levels).
// Callers: the host, tests.
#include "r1ui/widgets/custommenu/CustomMenusMenu.h"

#include "r1ui/widgets/customize/LayoutConvert.h"

namespace r1ui::widgets {

namespace {

namespace cm = commands::custommenu;

CommandMenuEntry convert(const cm::MenuDescEntry& desc) {
  switch (desc.kind) {
    case cm::MenuDescEntry::Kind::Separator: return CommandMenuEntry::separator();
    case cm::MenuDescEntry::Kind::Heading: return CommandMenuEntry::heading(desc.text);
    case cm::MenuDescEntry::Kind::Submenu: {
      std::vector<CommandMenuEntry> children;
      children.reserve(desc.children.size());
      for (const cm::MenuDescEntry& child : desc.children) children.push_back(convert(child));
      return CommandMenuEntry::submenu(desc.text, std::move(children));
    }
    case cm::MenuDescEntry::Kind::Command: break;
  }
  return CommandMenuEntry::command(desc.commandId);
}

}  // namespace

std::vector<CommandMenuEntry> customMenusMenuEntries(const cm::CustomMenuSet& set) {
  const std::vector<cm::MenuDescEntry> description = cm::describeCustomMenusMenu(set);
  std::vector<CommandMenuEntry> entries;
  entries.reserve(description.size());
  for (const cm::MenuDescEntry& desc : description) entries.push_back(convert(desc));
  return entries;
}

CommandMenuTitle customMenusMenuTitle(const cm::CustomMenuSet& set) { return {cm::kCustomMenusTitle, customMenusMenuEntries(set)}; }

commands::customize::Node customMenusMenuNode(const cm::CustomMenuSet& set, const std::string& id) {
  return menuNodeFromEntries(id, cm::kCustomMenusTitle, customMenusMenuEntries(set));
}

}  // namespace r1ui::widgets

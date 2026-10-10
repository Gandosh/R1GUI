// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CustomMenuCommands, which keeps the CommandRegistry in step with the CustomMenuSet: one command per
//   action behind the "Custom Menus" main menu (open a dockable menu, edit, save, delete a menu, create a
//   menu, load a menu file), registered when a menu appears, relabelled when it is renamed and removed
//   when it is deleted.
// Why: the menu builders (buildCommandMenu, the customizable menu bar) work on command ids; for the
//   "Custom Menus" menu to show up, work with chords and tooltips and appear in the command palette, its
//   entries must be real commands. The binder owns that registration so no host has to.
// Callers: the host (constructs one next to the registry), tests. Calls: CommandRegistry, CustomMenuSet.
//   The ids and the menu shape come from ui-commands/custommenu/CustomMenusMenu.h.
// What happens on execution is the host's business, given as hooks: opening a panel needs the dock,
//   editing needs the creator window, saving and loading need file dialogs, deleting usually asks first.
//   A missing hook makes its command refuse with a reason, except `remove`, which defaults to deleting
//   the menu at once. Hooks run synchronously from the command; they may edit the set.
// Registered commands: category "Custom Menus"; "open" commands carry the menu's name as label and a
//   description; the edit, save and delete commands are hidden from the keybinding editor (they would
//   be three rows per menu). A registration the registry refuses (limit) is counted in
//   registrationFailures() and the menu simply lacks that entry.
// Lifetime: the registry and the set must outlive the binder; the destructor removes every command it
//   registered. UI thread only.
#pragma once

#include <functional>
#include <map>
#include <string>

#include "r1ui/commands/CommandRegistry.h"
#include "r1ui/commands/custommenu/CustomMenusMenu.h"

namespace r1ui::widgets {

struct CustomMenuHooks {
  std::function<void(const std::string& menuId)> openPanel;  // show or focus the dockable menu's panel
  std::function<void(const std::string& menuId)> edit;       // open the creator window on the menu
  std::function<void(const std::string& menuId)> save;       // ask for a path and write a .r1mn file
  std::function<void(const std::string& menuId)> remove;     // confirm and delete; default: delete at once
  std::function<void()> create;                              // open the creator window on a new menu
  std::function<void()> load;                                // ask for a .r1mn file and import it
};

class CustomMenuCommands {
 public:
  CustomMenuCommands(commands::CommandRegistry& registry, commands::custommenu::CustomMenuSet& set, CustomMenuHooks hooks);
  ~CustomMenuCommands();
  CustomMenuCommands(const CustomMenuCommands&) = delete;
  CustomMenuCommands& operator=(const CustomMenuCommands&) = delete;

  void setHooks(CustomMenuHooks hooks) { hooks_ = std::move(hooks); }
  // Brings the registry in line with the set (called by the set's listener; safe to call any time).
  void sync();
  size_t registeredCount() const { return registered_.size(); }
  size_t registrationFailures() const { return failures_; }

 private:
  struct Shown {
    std::string name;
    commands::custommenu::MenuKind kind = commands::custommenu::MenuKind::Pie;
  };
  bool addCommand(commands::CommandDef def);
  void removeAll(const std::string& menuId);
  void registerMenu(const commands::custommenu::CustomMenu& menu);
  void registerGlobals();
  commands::ExecuteResult run(const std::function<void()>& hook, const char* what);

  commands::CommandRegistry& registry_;
  commands::custommenu::CustomMenuSet& set_;
  CustomMenuHooks hooks_;
  commands::custommenu::CustomMenuSet::ListenerId listener_ = 0;
  std::map<std::string, Shown> shown_;          // menu id -> what its commands were registered for
  std::map<std::string, bool> registered_;      // command id -> true
  size_t failures_ = 0;
};

}  // namespace r1ui::widgets

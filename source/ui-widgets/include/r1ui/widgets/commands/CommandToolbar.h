// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: bindCommandToolbar: fills a Toolbar with buttons created from command ids and keeps them
//   live: enabled state, active (checked) state, visibility, and the tooltip with the current chord.
// Why: spec 07 rules 29, 31, 32, 34 and 35: the toolbar button of a command shows the command's
//   icon, is dimmed when the command cannot run, shows checked when the command says so, collapses
//   when the command is not visible, runs the same guarded execution as the chord on click and shows
//   "<label> (<chord>)" as tooltip, updated the moment a chord is rebound (the reference's "Pen (P)").
// Callers: hosts and the gallery. Calls: Toolbar / ToolbarButton, CommandUiSync (refresh), the router.
// Mapping: Action and Momentary -> action button; Toggle -> toggle button (its own flip is corrected
//   by the next refresh, so a refused command never leaves a wrong state); Radio -> tool button (the
//   Toolbar keeps exactly one tool active; the refresh makes the checked command the active one); a
//   group item -> a tool group with a flyout whose entries are the group's commands. The tooltip text
//   is the command's tooltip when set, otherwise its label. A command without an icon gets "circle".
// Limits: a flyout entry's shortcut text is fixed when the toolbar is built (the Toolbar widget owns
//   the entry strings); the group's main button tooltip and everything else is live. Disabled or
//   invisible state of flyout entries is not shown.
// Lifetime: the binding object is a handle; destroying it detaches the refresher and silences the
//   button callbacks, the Toolbar widget and its buttons stay. The binding never outlives the sync's
//   services; the Toolbar may be destroyed first (the binding checks liveness).
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/widgets/commands/CommandUiSync.h"
#include "r1ui/widgets/toolbar/Toolbar.h"

namespace r1ui::widgets {

struct CommandToolbarItem {
  enum class Kind : uint8_t { Command, Separator, Group };
  Kind kind = Kind::Command;
  std::vector<std::string> commandIds;  // Command: one id; Group: the entries, the first is the initial tool

  static CommandToolbarItem command(std::string id);
  static CommandToolbarItem separator();
  static CommandToolbarItem group(std::vector<std::string> ids);
};

class CommandToolbarBinding {
 public:
  ~CommandToolbarBinding();
  CommandToolbarBinding(const CommandToolbarBinding&) = delete;
  CommandToolbarBinding& operator=(const CommandToolbarBinding&) = delete;

  // Re-reads every bound command now (the sync does this for you).
  void refresh();
  // The button of a command (a group's main button for its entries); null when not bound or gone.
  ToolbarButton* button(std::string_view commandId);
  core::tree::WidgetId toolbar() const;

  struct State;

 private:
  friend std::unique_ptr<CommandToolbarBinding> bindCommandToolbar(UiContext&, Toolbar&, CommandUiSync&, std::vector<CommandToolbarItem>);
  CommandToolbarBinding() = default;
  std::shared_ptr<State> state_;
  CommandUiSync::Attachment attachment_;
};

// Adds the items to `toolbar` (existing buttons stay before them), refreshes once and attaches to
// `sync`. Commands that are not registered at build time get no button (and no error): a module that
// registers later is not picked up, build the toolbar after the commands exist.
std::unique_ptr<CommandToolbarBinding> bindCommandToolbar(UiContext& ui, Toolbar& toolbar, CommandUiSync& sync, std::vector<CommandToolbarItem> items);

}  // namespace r1ui::widgets

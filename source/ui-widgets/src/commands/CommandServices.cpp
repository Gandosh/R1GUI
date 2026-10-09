// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CommandServices.h: the UI clock and the tooltip text of a command.
// Invariants: pure functions of the services and the command; the tooltip never has an empty
//   description part (the label is the fallback).
// Callers: CommandMenus, CommandToolbar, the host (UiClock).
#include "r1ui/widgets/commands/CommandServices.h"

#include "r1ui/widgets/menu/MenuModel.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

uint64_t UiClock::nowMs() const { return ui_.now(); }

std::string commandTooltip(const CommandServices& services, const commands::CommandDef& command, std::string_view base) {
  return tooltipWithShortcut(base.empty() ? std::string_view(command.label) : base, services.keymap.displayText(command.id));
}

}  // namespace r1ui::widgets

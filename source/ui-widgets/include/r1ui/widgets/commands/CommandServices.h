// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CommandServices, the bundle of command objects the widget side needs (registry, overrides,
//   keymap, router), UiClock (the router's clock driven by the UiContext time) and the shared
//   helpers that turn a command into display text.
// Why: menus, toolbars, the key handler and the keybinding editor all read the same four objects;
//   passing one bundle keeps call sites short and makes "one source for menu, toolbar and shortcut"
//   (spec 07) visible in the signatures.
// Callers: CommandMenus, CommandToolbar, CommandKeyHandler, KeybindingEditor, the gallery, hosts.
//   Calls: ui-commands.
// Lifetime: the bundle holds references; the host owns the four objects and keeps them alive longer
//   than every widget and binding created from the bundle.
#pragma once

#include <string>
#include <string_view>

#include "r1ui/commands/Clock.h"
#include "r1ui/commands/CommandRegistry.h"
#include "r1ui/commands/CommandRouter.h"
#include "r1ui/commands/Keymap.h"
#include "r1ui/commands/Overrides.h"

namespace r1ui::widgets {

struct CommandServices {
  commands::CommandRegistry& registry;
  commands::KeybindingOverrides& overrides;
  commands::Keymap& keymap;
  commands::CommandRouter& router;
};

class UiContext;

// The router's time source: the UiContext clock (UiContext::setTime), so sequence timeouts run on the
// same clock as timers and tooltips.
class UiClock final : public commands::Clock {
 public:
  explicit UiClock(const UiContext& ui) : ui_(ui) {}
  uint64_t nowMs() const override;

 private:
  const UiContext& ui_;
};

// The text under a button or menu row: label plus chord in brackets ("Pen (P)", spec 07 rule 29);
// the chord is the first valid slot in force. `base` empty falls back to the label.
std::string commandTooltip(const CommandServices& services, const commands::CommandDef& command, std::string_view base);

}  // namespace r1ui::widgets

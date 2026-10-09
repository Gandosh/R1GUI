// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CommandKeyHandler, the adapter that offers keys nobody used to the command router. It is the
//   GlobalKeyHandler of a UiContext: the Router has already offered the key to the focused widget and
//   its ancestors and to Tab navigation (spec 01 rule 16 steps 4 and 5), so what arrives here is
//   step 6, "application-wide shortcuts", with the binding contexts of the focus path.
// Why: spec 01 rules 16 and 52 to 56 and spec 07 rule 17: the focused place decides which commands
//   a chord can mean (the same chord runs a layers command with the layers panel focused and the
//   global one elsewhere), text fields swallow plain printable keys but let Ctrl and Alt chords
//   through, a drag in progress suppresses chords, and a half-typed sequence shows a pending
//   indicator and times out after 1.5 s without another key.
// Callers: the host installs it with UiContext::setGlobalKeyHandler; the preview and tests feed keys
//   through UiContext::keyDown. Calls: CommandRouter, UiContext (focus path, timers).
// Contexts: the default resolver gives {"text"} when the focused widget takes typed text, otherwise
//   {"window"}; both chains end at "global". Panels with their own commands install a resolver that
//   walks the focused widget's ancestors and returns their context names, innermost first.
// Pending timeout: when a sequence starts the handler arms a UiContext timer for the remaining time
//   so the indicator clears and the state resets without another key; the timer is the only scheduled
//   work and exists only while a sequence is pending.
// Key release: the Router offers no key-up to global handlers (spec 01 rule 21); a host that wants
//   release-triggered or momentary commands forwards its key-up events to onKeyUp().
// Lifetime: the handler must be destroyed (or replaced) before the UiContext; its destructor cancels
//   the timer.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "r1ui/core/events/EventHandler.h"
#include "r1ui/widgets/commands/CommandServices.h"

namespace r1ui::widgets {

class CommandKeyHandler final : public core::events::GlobalKeyHandler {
 public:
  // Returns the leaf contexts of the focus path, innermost first.
  using ContextResolver = std::function<std::vector<std::string>(UiContext&, core::tree::WidgetId focused)>;

  CommandKeyHandler(UiContext& ui, CommandServices services);
  ~CommandKeyHandler() override;
  CommandKeyHandler(const CommandKeyHandler&) = delete;
  CommandKeyHandler& operator=(const CommandKeyHandler&) = delete;

  void setContextResolver(ContextResolver resolver);

  bool onGlobalKey(const core::events::Event& event, core::events::Router& router) override;
  // Key release from the host; true when it ended a momentary command or ran a release chord.
  bool onKeyUp(core::events::Key key, uint8_t modifiers);

 private:
  std::vector<std::string> contexts() const;
  void armPendingTimer();

  UiContext& ui_;
  CommandServices services_;
  ContextResolver resolver_;
  uint32_t timer_ = 0;
};

}  // namespace r1ui::widgets

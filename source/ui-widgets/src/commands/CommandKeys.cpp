// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CommandKeys.h.
// Invariants: at most one pending timer is armed; the timer callback only calls router.tick() on the
//   services captured at construction (alive for the handler's life) and is cancelled in the destructor.
// Callers: hosts, tests.
#include "r1ui/widgets/commands/CommandKeys.h"

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace cmd = commands;

CommandKeyHandler::CommandKeyHandler(UiContext& ui, CommandServices services) : ui_(ui), services_(services) {}

CommandKeyHandler::~CommandKeyHandler() {
  if (timer_ != 0) ui_.cancelTimer(timer_);
}

void CommandKeyHandler::setContextResolver(ContextResolver resolver) { resolver_ = std::move(resolver); }

std::vector<std::string> CommandKeyHandler::contexts() const {
  const core::tree::WidgetId focused = ui_.router().focused();
  if (resolver_) return resolver_(ui_, focused);
  const WidgetObject* object = ui_.object(focused);
  return {object != nullptr && object->wantsTextInput() ? cmd::kTextContext : cmd::kWindowContext};
}

bool CommandKeyHandler::onGlobalKey(const core::events::Event& event, core::events::Router&) {
  const std::vector<std::string> chain = contexts();
  const cmd::RouteResult result = services_.router.handleKey({event.key, event.modifiers, event.repeat, false}, chain);
  if (result.pendingStarted) armPendingTimer();
  return result.consumed;
}

bool CommandKeyHandler::onKeyUp(core::events::Key key, uint8_t modifiers) {
  const std::vector<std::string> chain = contexts();
  return services_.router.handleKey({key, modifiers, false, true}, chain).consumed;
}

// The sequence times out on the UI clock; the timer makes the router notice it while the user does
// nothing (the indicator clears, the next key starts fresh).
void CommandKeyHandler::armPendingTimer() {
  if (timer_ != 0) ui_.cancelTimer(timer_);
  cmd::CommandRouter* router = &services_.router;
  timer_ = ui_.setTimer(router->pendingRemainingMs(), [router]() { router->tick(); });
}

}  // namespace r1ui::widgets

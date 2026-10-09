// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CommandRouter.h: context expansion, key-down / key-up routing, the sequence
//   state machine and guarded execution.
// Invariants: pending_ implies pendingChain_ is non-empty; held_ maps a key to a momentary command
//   whose Press ran and whose Release has not; executing_ is empty whenever control is outside a
//   command callback (the guard pops on every exit path including exceptions).
// Callers: widget layer key fallback, menu and toolbar binders, tests.
#include "r1ui/commands/CommandRouter.h"

#include <algorithm>
#include <exception>

namespace r1ui::commands {

namespace {

bool plainPrintable(const KeyInput& input) {
  const auto code = static_cast<unsigned>(input.key);
  const bool printable = code == 32 || (code >= 48 && code <= 57) || (code >= 65 && code <= 90);
  return printable && (input.modifiers & (Mod::kCtrl | Mod::kAlt | Mod::kMeta)) == 0;
}

// Pops the executing stack on every exit path.
class ExecutionScope {
 public:
  ExecutionScope(std::vector<std::string>& stack, const std::string& id) : stack_(stack) { stack_.push_back(id); }
  ~ExecutionScope() { stack_.pop_back(); }
  ExecutionScope(const ExecutionScope&) = delete;
  ExecutionScope& operator=(const ExecutionScope&) = delete;

 private:
  std::vector<std::string>& stack_;
};

}  // namespace

// ---- contexts -----------------------------------------------------------------------------------

// Each leaf followed by its parents, duplicates dropped; "global" is always consulted last.
std::vector<std::string> CommandRouter::expand(std::span<const std::string> contexts) const {
  std::vector<std::string> chain;
  const auto append = [&](const std::string& name) {
    if (std::find(chain.begin(), chain.end(), name) == chain.end()) chain.push_back(name);
  };
  for (const std::string& leaf : contexts) {
    for (const std::string& name : registry_.contextChain(leaf)) append(name);
  }
  append(kGlobalContext);
  return chain;
}

// ---- execution ----------------------------------------------------------------------------------

ExecuteResult CommandRouter::run(const CommandDef& command, const ExecuteArgs& args) {
  if (std::find(executing_.begin(), executing_.end(), command.id) != executing_.end()) {
    return ExecuteResult::refused("re-entrant execution of " + command.id);
  }
  if (executing_.size() >= static_cast<size_t>(kMaxExecutionDepth)) return ExecuteResult::refused("commands nested too deeply");
  // The Release of a momentary command balances its Press and is never blocked by the enabled state.
  const auto execute = command.execute;  // a copy: the command may unregister itself while it runs
  const ExecutionScope scope(executing_, command.id);
  try {
    if (args.phase != ExecutePhase::Release && !command.isEnabled()) return ExecuteResult::refused("disabled");
    if (!execute) return ExecuteResult::notHandled();
    return execute(args);
  } catch (const std::exception& e) {
    return ExecuteResult::refused(std::string("exception: ") + e.what());
  } catch (...) {
    return ExecuteResult::refused("unknown exception");
  }
}

ExecuteResult CommandRouter::execute(std::string_view commandId, ExecuteSource source, ExecutePhase phase) {
  const CommandDef* command = registry_.find(commandId);
  if (command == nullptr) return ExecuteResult::refused("unknown command");
  ExecuteArgs args;
  args.phase = phase;
  args.source = source;
  return run(*command, args);
}

bool CommandRouter::tryRun(const CommandDef& command, const ExecuteArgs& args, RouteResult& out) {
  out.commandId = command.id;
  out.result = run(command, args);
  if (!out.result.isHandled()) return false;
  out.consumed = true;
  return true;
}

// ---- key routing --------------------------------------------------------------------------------

RouteResult CommandRouter::handleKey(const KeyInput& input, std::span<const std::string> contexts) {
  RouteResult out;
  if (!executing_.empty()) {
    out.reentrant = true;
    return out;
  }
  if (input.key == Key::Unknown || suppressed_) return out;  // modifier-only presses never change state
  tick();
  const std::vector<std::string> chain = expand(contexts);
  return input.up ? handleUp(input, chain) : handleDown(input, chain);
}

RouteResult CommandRouter::handleDown(const KeyInput& input, const std::vector<std::string>& chain) {
  if (pending_) return completeSequence(input);
  RouteResult out;
  const bool textEntry = std::any_of(chain.begin(), chain.end(), [&](const std::string& name) {
    const ContextInfo* info = registry_.context(name);
    return info != nullptr && info->textEntry;
  });
  if (textEntry && plainPrintable(input)) {
    out.swallowedByText = true;
    return out;
  }
  const auto heldKey = held_.find(static_cast<uint16_t>(input.key));
  if (input.repeat && heldKey != held_.end()) {  // holding a momentary command: its repeats stop here
    out.consumed = true;
    out.commandId = heldKey->second;
    return out;
  }

  const KeyChord chord{input.key, static_cast<uint8_t>(input.modifiers & kAllModifiers), false};
  for (const std::string& context : chain) {
    const LookupResult found = keymap_.lookup(context, ChordSequence::single(chord));
    if (found.kind == LookupKind::Exact) {
      const CommandDef* command = registry_.find(found.commandId);
      if (command == nullptr || (input.repeat && !command->repeatable)) continue;
      ExecuteArgs args;
      args.phase = command->kind == CommandKind::Momentary ? ExecutePhase::Press : ExecutePhase::Invoke;
      args.source = ExecuteSource::Key;
      args.repeat = input.repeat;
      if (tryRun(*command, args, out)) {
        if (command->kind == CommandKind::Momentary) held_[static_cast<uint16_t>(input.key)] = command->id;
        return out;
      }
    } else if (found.kind == LookupKind::Prefix && !input.repeat) {
      beginPending(chord, chain);
      out.consumed = true;
      out.pendingStarted = true;
      return out;
    }
  }
  return out;
}

RouteResult CommandRouter::handleUp(const KeyInput& input, const std::vector<std::string>& chain) {
  RouteResult out;
  const auto held = held_.find(static_cast<uint16_t>(input.key));
  if (held != held_.end()) {
    const std::string id = held->second;
    held_.erase(held);
    out.consumed = true;
    out.commandId = id;
    if (const CommandDef* command = registry_.find(id)) {
      ExecuteArgs args;
      args.phase = ExecutePhase::Release;
      args.source = ExecuteSource::Key;
      out.result = run(*command, args);
    }
    return out;
  }
  if (pending_) return out;
  const KeyChord chord{input.key, static_cast<uint8_t>(input.modifiers & kAllModifiers), true};
  for (const std::string& context : chain) {
    const LookupResult found = keymap_.lookup(context, ChordSequence::single(chord));
    if (found.kind != LookupKind::Exact) continue;
    const CommandDef* command = registry_.find(found.commandId);
    if (command == nullptr) continue;
    ExecuteArgs args;
    args.source = ExecuteSource::Key;
    if (tryRun(*command, args, out)) return out;
  }
  return out;
}

// ---- sequences ----------------------------------------------------------------------------------

RouteResult CommandRouter::completeSequence(const KeyInput& input) {
  RouteResult out;
  out.consumed = true;  // while a sequence is pending every key belongs to it
  if (input.repeat) return out;
  if (input.key == Key::Escape && (input.modifiers & kAllModifiers) == 0) {
    endPending();
    out.sequenceAborted = true;
    return out;
  }
  const KeyChord second{input.key, static_cast<uint8_t>(input.modifiers & kAllModifiers), false};
  const ChordSequence typed = ChordSequence::pair(pendingFirst_, second);
  const std::vector<std::string> chain = pendingChain_;
  endPending();
  for (const std::string& context : chain) {
    const LookupResult found = keymap_.lookup(context, typed);
    if (found.kind != LookupKind::Exact) continue;
    const CommandDef* command = registry_.find(found.commandId);
    if (command == nullptr) continue;
    ExecuteArgs args;
    args.source = ExecuteSource::Key;
    tryRun(*command, args, out);  // consumed stays true: the sequence is over either way
    return out;
  }
  out.sequenceAborted = true;
  return out;
}

void CommandRouter::beginPending(const KeyChord& first, const std::vector<std::string>& chain) {
  pending_ = true;
  pendingFirst_ = first;
  pendingSince_ = clock_.nowMs();
  pendingChain_ = chain;
  PendingState state;
  state.active = true;
  state.text = formatChord(first);
  for (const std::string& context : chain) {
    state.continuations = keymap_.continuations(context, first);
    if (!state.continuations.empty()) break;
  }
  for (const Continuation& c : state.continuations) {
    const CommandDef* command = registry_.find(c.commandId);
    state.commandLabels.push_back(command != nullptr ? command->label : c.commandId);
  }
  publish(state);
}

void CommandRouter::endPending() {
  if (!pending_) return;
  pending_ = false;
  pendingChain_.clear();
  publish(PendingState{});
}

void CommandRouter::cancelPending() { endPending(); }

uint64_t CommandRouter::pendingRemainingMs() const {
  if (!pending_) return 0;
  const uint64_t elapsed = clock_.nowMs() - pendingSince_;
  return elapsed >= timeoutMs_ ? 0 : timeoutMs_ - elapsed;
}

void CommandRouter::tick() {
  if (pending_ && clock_.nowMs() - pendingSince_ >= timeoutMs_) endPending();
}

void CommandRouter::setSequenceTimeoutMs(uint64_t ms) { timeoutMs_ = std::clamp<uint64_t>(ms, 100, 60000); }

void CommandRouter::setSuppressed(bool suppressed) {
  suppressed_ = suppressed;
  if (suppressed) endPending();
}

void CommandRouter::releaseMomentary() {
  const auto held = std::move(held_);
  held_.clear();
  for (const auto& [key, id] : held) {
    const CommandDef* command = registry_.find(id);
    if (command == nullptr) continue;
    ExecuteArgs args;
    args.phase = ExecutePhase::Release;
    args.source = ExecuteSource::Key;
    run(*command, args);
  }
}

void CommandRouter::publish(const PendingState& state) {
  if (observer_) observer_(state);
}

}  // namespace r1ui::commands

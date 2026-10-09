// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CommandRouter: turns key events into command executions. It resolves a key against the
//   binding contexts of the focused place (innermost first, then parents), runs the matching command
//   behind the enabled and repeat checks, drives the two-step sequence state machine and executes
//   commands requested by menus and toolbars through the same guarded path.
// Why: spec 07 rules 13, 16 to 24 and spec 01 rules 16 to 24, 52 to 56, plus decision D14. Keeping the
//   whole decision in one headless object means a chord, a menu click and a toolbar click behave
//   identically (spec 07 rule 34) and the rules are testable without any widget.
// Callers: the widget layer (CommandKeyHandler from the global key fallback, or panels that bind their
//   own contexts), menu and toolbar binders (execute()), tests. Calls: Keymap, CommandRegistry, Clock.
// Where the router sits: the UI layer offers a key to open menus, overlays and the focused widget
//   chain first (spec 01 rule 16 steps 1 to 5); only an unused key reaches handleKey. A panel that
//   owns commands may call handleKey earlier with its own context stack.
// Contexts: handleKey takes the leaf contexts of the focus path, innermost first (for example
//   {"layers", "viewport"}); each is expanded through its parents, duplicates dropped, so the order
//   tried is own scope, then outer scopes. A context flagged text-entry anywhere in the stack makes
//   plain printable keys (letters, digits, Space, optionally with Shift) swallowed: they return
//   not-consumed with `swallowedByText` set and nothing is looked up; keys with Ctrl, Alt or Meta pass.
// Matching: in each context the exact binding wins; a command that is disabled, refuses, reports
//   NotHandled, or whose key is a repeat while the command is not repeatable does not use the key:
//   the next context is tried and finally the key is reported unused (spec 07 rules 19, 20; spec 01
//   scenario 7). Hidden or invisible commands still match (spec 07 edge cases).
// Sequences (D14): the first chord of a sequence enters the pending state (consumed), reports it
//   through the pending observer (text such as "Ctrl+K" and the possible continuations) and arms a
//   1.5 s timeout checked by tick() and by the next handleKey. The next key completes the sequence
//   (executes), an unmatched key aborts it and is consumed, Escape cancels it. Modifier-only presses
//   (Key::Unknown) never change the state. Repeats while pending are consumed and ignored.
// Key release: handleKey with `up` runs chords bound with a release trigger, and the Release phase of
//   momentary commands whose press was accepted (even if the modifiers changed meanwhile).
//   releaseMomentary() ends all held momentary commands (window deactivated).
// Guards: execution is not re-entrant for the same command, nested execution is limited to
//   kMaxExecutionDepth, key events arriving from inside a command are ignored, exceptions thrown by a
//   command become Refused results, and setSuppressed(true) (drag in progress, blocking operation)
//   makes every key unused and cancels a pending sequence.
// Threading: UI thread only.
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "r1ui/commands/Clock.h"
#include "r1ui/commands/CommandRegistry.h"
#include "r1ui/commands/Keymap.h"

namespace r1ui::commands {

inline constexpr uint64_t kDefaultSequenceTimeoutMs = 1500;
inline constexpr int kMaxExecutionDepth = 4;

struct KeyInput {
  Key key = Key::Unknown;
  uint8_t modifiers = Mod::kNone;
  bool repeat = false;
  bool up = false;
};

struct RouteResult {
  bool consumed = false;          // the key was used: do not offer it further
  std::string commandId;          // the command that ran (or was refused last), if any
  ExecuteResult result;           // its result when one was executed
  bool pendingStarted = false;    // the key opened a sequence
  bool sequenceAborted = false;   // the key ended a pending sequence without a match (or Escape)
  bool swallowedByText = false;   // plain printable key in a text-entry context
  bool reentrant = false;         // arrived while a command was executing
};

struct PendingState {
  bool active = false;
  std::string text;                         // the chords typed so far, e.g. "Ctrl+K"
  std::vector<Continuation> continuations;  // what can follow (first context that has the prefix)
  std::vector<std::string> commandLabels;   // label per continuation, same order
};

class CommandRouter {
 public:
  using PendingObserver = std::function<void(const PendingState&)>;

  CommandRouter(const CommandRegistry& registry, const Keymap& keymap, const Clock& clock) : registry_(registry), keymap_(keymap), clock_(clock) {}

  // Routes one key event through `contexts` (innermost first; unknown names are skipped).
  RouteResult handleKey(const KeyInput& input, std::span<const std::string> contexts);

  // Runs a command requested by a menu, toolbar or the API: checks existence and the enabled
  // predicate, applies the re-entrancy guard and returns the command's result. Unknown id: Refused.
  // A momentary command asked to Invoke gets Press and Release back to back (a click has no duration).
  ExecuteResult execute(std::string_view commandId, ExecuteSource source = ExecuteSource::Api, ExecutePhase phase = ExecutePhase::Invoke);

  // Expires a pending sequence whose timeout elapsed; call once per frame / tick.
  void tick();
  void cancelPending();
  bool pending() const { return pending_; }
  // Milliseconds until the pending sequence times out (0 when none): hosts use it to arm a timer.
  uint64_t pendingRemainingMs() const;
  void setPendingObserver(PendingObserver observer) { observer_ = std::move(observer); }
  // Timeout of a pending sequence, clamped to 100..60000 ms.
  void setSequenceTimeoutMs(uint64_t ms);
  uint64_t sequenceTimeoutMs() const { return timeoutMs_; }

  void setSuppressed(bool suppressed);
  bool suppressed() const { return suppressed_; }
  // Ends every held momentary command with its Release phase.
  void releaseMomentary();
  size_t heldMomentaryCount() const { return held_.size(); }
  bool executing() const { return !executing_.empty(); }

 private:
  std::vector<std::string> expand(std::span<const std::string> contexts) const;
  RouteResult handleDown(const KeyInput& input, const std::vector<std::string>& chain);
  RouteResult handleUp(const KeyInput& input, const std::vector<std::string>& chain);
  RouteResult completeSequence(const KeyInput& input);
  // Runs `command` if it may run now; true when it used the key.
  bool tryRun(const CommandDef& command, const ExecuteArgs& args, RouteResult& out);
  ExecuteResult run(const CommandDef& command, const ExecuteArgs& args);
  void beginPending(const KeyChord& first, const std::vector<std::string>& chain);
  void endPending();
  void publish(const PendingState& state);

  const CommandRegistry& registry_;
  const Keymap& keymap_;
  const Clock& clock_;
  PendingObserver observer_;
  uint64_t timeoutMs_ = kDefaultSequenceTimeoutMs;
  bool suppressed_ = false;

  bool pending_ = false;
  KeyChord pendingFirst_;
  uint64_t pendingSince_ = 0;
  std::vector<std::string> pendingChain_;

  std::vector<std::string> executing_;                  // ids of the commands on the stack
  std::unordered_map<uint16_t, std::string> held_;      // key -> momentary command awaiting Release
};

}  // namespace r1ui::commands

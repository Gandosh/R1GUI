// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the declaration of one command (CommandDef) and the small value types around its execution:
//   kind, execute arguments, execute result and the limits that protect registry and widgets.
// Why: spec 07: a command is declared once (id, label, description, icon, behaviour, default chords)
//   and that single declaration drives the menu entry, the toolbar button and the shortcut, which
//   must agree in label, icon, tooltip, enabled and checked state and displayed chord.
// Callers: modules (declare commands), CommandRegistry (stores a sanitised copy), CommandRouter
//   (executes), widgets (read label, state and chords). Calls: Chord.h.
// Predicates: enabled, checked and visible are evaluated on demand and must be cheap and must not
//   throw; a missing predicate means enabled, unchecked, visible (spec 07 rules 9 to 11). The host
//   re-evaluates them every frame or on a change notification (CommandRegistry::touch).
// Execute: returns Handled (the command did its work), NotHandled (this context does not handle it:
//   the key keeps travelling to the next context) or Refused with a reason (cannot run now). A toggle
//   flips its own state inside execute; the next evaluation of `checked` shows it (spec 07 rule 32).
//   A Momentary command is executed twice per key press: Press on key-down, Release on key-up.
// Limits: strings are cut at these byte counts (label 256, description and tooltip 2048); an id is
//   at most 128 bytes of [A-Za-z0-9._:/-].
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "r1ui/commands/Chord.h"

namespace r1ui::commands {

inline constexpr size_t kMaxIdBytes = 128;
inline constexpr size_t kMaxLabelBytes = 256;
inline constexpr size_t kMaxDescriptionBytes = 2048;
inline constexpr size_t kMaxCategoryBytes = 128;
inline constexpr size_t kMaxCommands = 100000;

inline constexpr const char* kGlobalContext = "global";

enum class CommandKind : uint8_t {
  Action,     // runs and is done
  Toggle,     // two-state; `checked` reports the state
  Radio,      // one of a group (`radioGroup`); `checked` is true for the current choice
  Momentary   // Press on key-down, Release on key-up
};

enum class ExecutePhase : uint8_t { Invoke, Press, Release };
enum class ExecuteSource : uint8_t { Key, Menu, Toolbar, Api };

struct ExecuteArgs {
  ExecutePhase phase = ExecutePhase::Invoke;
  ExecuteSource source = ExecuteSource::Api;
  bool repeat = false;  // the key press is an auto-repeat (only commands marked repeatable see it)
};

struct ExecuteResult {
  enum class Status : uint8_t { Handled, NotHandled, Refused };
  Status status = Status::Handled;
  std::string reason;  // Refused: why the command cannot run now

  static ExecuteResult handled() { return {Status::Handled, {}}; }
  static ExecuteResult notHandled() { return {Status::NotHandled, {}}; }
  static ExecuteResult refused(std::string why) { return {Status::Refused, std::move(why)}; }
  bool isHandled() const { return status == Status::Handled; }
};

struct CommandDef {
  std::string id;           // stable, e.g. "edit.undo"
  std::string label;        // must not be empty
  std::string description;  // tooltip text and grey help text in the editor
  std::string tooltip;      // overrides `description` as tooltip when non-empty
  std::string icon;         // icon name (assets/icons) or empty
  std::string category;     // groups rows in the keybinding editor; empty = "General"
  std::string context = kGlobalContext;  // the binding context this command belongs to
  CommandKind kind = CommandKind::Action;
  std::string radioGroup;   // Radio: commands with the same group form one choice
  std::array<ChordSequence, 2> defaultChords{};  // primary, alternate
  bool repeatable = false;
  bool hiddenFromEditor = false;

  std::function<bool()> enabled;
  std::function<bool()> checked;
  std::function<bool()> visible;
  std::function<ExecuteResult(const ExecuteArgs&)> execute;

  // The tooltip text before the chord is appended: `tooltip` when set, otherwise `description`.
  const std::string& tooltipText() const { return tooltip.empty() ? description : tooltip; }
  bool isEnabled() const { return !enabled || enabled(); }
  bool isChecked() const { return checked && checked(); }
  bool isVisible() const { return !visible || visible(); }
};

}  // namespace r1ui::commands

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: conflict detection for a proposed binding and the "Override" resolution the keybinding editor
//   offers: unbind the colliding commands, then assign the chord.
// Why: spec 07 rules 24, 25 and 45 to 48 and decision D14: a chord conflicts with a binding of the
//   same context, of an ancestor context or of a descendant context (siblings never conflict); the
//   editor looks at the command's own context first, then each ancestor in order, then all descendants;
//   a sequence conflicts with every binding that equals it, is its prefix or extends it. The same
//   command's other slot is never a conflict (the chord can move between slots).
// Callers: KeybindingEditor, tests, hosts that rebind programmatically. Calls: Keymap, CommandRegistry,
//   KeybindingOverrides.
// Report: every colliding (command, slot) is listed, ordered by scope (own context, ancestors
//   nearest first, descendants), then registration order, then slot, so the first entry is the one
//   the popup names (rule 25).
// Override: assignChord(..., overrideConflicts = true) unbinds every listed (command, slot) as an
//   override, sets the new chord and, per spec 07 rule 55, pins the command's other slot to its value
//   in force (unbound when it held the same chord, i.e. the chord moved). The changes are one batch:
//   one registry notification.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/commands/CommandRegistry.h"
#include "r1ui/commands/Keymap.h"
#include "r1ui/commands/Overrides.h"

namespace r1ui::commands {

enum class ConflictKind : uint8_t {
  Equal,
  ExistingIsPrefix,  // the existing binding is the first step of the proposed sequence
  NewIsPrefix        // the proposed chord is the first step of an existing sequence
};
enum class ConflictScope : uint8_t { SameContext, Ancestor, Descendant };

struct Conflict {
  std::string commandId;
  std::string context;
  int slot = 0;
  ConflictKind kind = ConflictKind::Equal;
  ConflictScope scope = ConflictScope::SameContext;
  ChordSequence existing;
};

// Conflicts the chord would cause if bound to `commandId` (any slot but its own other slot).
std::vector<Conflict> findConflicts(const CommandRegistry& registry, const Keymap& keymap, std::string_view commandId, const ChordSequence& proposed);

struct AssignResult {
  bool ok = false;
  std::vector<Conflict> conflicts;  // listed when refused for conflicts, and when overridden (what was unbound)
  std::string error;
};

// Binds (or with an empty `chord`, unbinds) one slot through the overrides. Refuses with the conflict
// list unless `overrideConflicts` is true. Invalid input (unknown command, bad slot, invalid chord) is
// refused with an error text and changes nothing.
AssignResult assignChord(KeybindingOverrides& overrides, const Keymap& keymap, const CommandRegistry& registry, std::string_view commandId, int slot,
                         std::optional<ChordSequence> chord, bool overrideConflicts);

}  // namespace r1ui::commands

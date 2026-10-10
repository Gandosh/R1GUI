// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the "keep both" resolution of a shortcut conflict, next to assignChord's "replace".
// Why: the hotkey editor offers Replace (take the shortcut away from the other command: assignChord
//   with overrideConflicts), Keep both and Cancel. Keep both is legal only when no conflicting binding
//   lives in the SAME context: the keymap resolves a clash inside one context by letting only one
//   command hold the chord, but a chord used in a parent or child context coexists (the more specific
//   context wins when the focus is there). So the other command keeps its binding and the new chord is
//   simply set.
// Callers: HotkeyEditor, tests. Calls: Conflicts.h (findConflicts), Overrides.
// Rules: refused (nothing changes) for an unknown command, a bad slot, an invalid chord, or when any
//   conflict is in the same context; the other slot is treated exactly as assignChord treats it (it is
//   pinned to its value in force, unbound when the chord moves from it); one batch, one notification.
#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "r1ui/commands/Conflicts.h"

namespace r1ui::commands {

// True when every conflict is in an ancestor or descendant context (and there is at least none to
// refuse: an empty list is trivially keepable).
bool canKeepBoth(const std::vector<Conflict>& conflicts);

AssignResult assignKeepingBoth(KeybindingOverrides& overrides, const Keymap& keymap, const CommandRegistry& registry, std::string_view commandId, int slot,
                               const ChordSequence& chord);

}  // namespace r1ui::commands

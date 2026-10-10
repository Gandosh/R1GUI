// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the data side of the action list: ActionInfo (one listable action), the conversion from a
//   CommandRegistry, the search rule (case-insensitive, space separated terms that must all occur in
//   the label, id, description or category), and the helper that reports commands without a
//   description.
// Why: the list widget, the hotkey editor and tests need the same rule for "what is an action and
//   which actions does this text find"; as plain functions they are tested without any widget and
//   cost nothing to reuse (a menu creator can list actions from its own source with the same type).
// Callers: ActionList, HotkeyEditor, hosts, tests. Calls: ui-commands (registry, keymap, Text).
// Limits and text safety: every text field is made well-formed UTF-8 (invalid bytes become U+FFFD,
//   control characters become spaces) and cut at a code point boundary at kMax*Bytes; at most
//   kMaxActions actions are taken. Matching is ASCII case-insensitive on bytes, which is exact for
//   valid UTF-8 because a multi-byte sequence never contains ASCII bytes.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/commands/CommandRegistry.h"
#include "r1ui/commands/Keymap.h"

namespace r1ui::widgets {

inline constexpr size_t kMaxActions = 100000;
inline constexpr size_t kMaxActionIdBytes = 128;
inline constexpr size_t kMaxActionLabelBytes = 256;
inline constexpr size_t kMaxActionDescriptionBytes = 4096;
inline constexpr size_t kMaxActionCategoryBytes = 128;
inline constexpr size_t kMaxActionShortcutBytes = 128;
inline constexpr size_t kMaxActionIconBytes = 64;
inline constexpr size_t kMaxActionQueryBytes = 256;
inline constexpr size_t kMaxActionQueryTerms = 16;

struct ActionInfo {
  std::string id;           // the command id when the action comes from a registry
  std::string label;
  std::string description;  // shown grey on the same row, full text in the tooltip
  std::string category;     // empty = "General"
  std::string icon;         // icon name or empty
  std::string shortcut;     // display text, e.g. "Ctrl+Z, Alt+U"; empty = unassigned
  bool enabled = true;
};

// The ActionInfo with every field sanitised and cut to its limit; an empty category becomes "General".
ActionInfo sanitizedAction(const ActionInfo& action);

struct ActionSourceOptions {
  bool includeHidden = false;    // commands marked hiddenFromEditor
  bool sortByLabel = false;      // otherwise registration order
  bool bothShortcuts = true;     // "primary, alternate" instead of the primary only
};

// One ActionInfo per command (at most kMaxActions). Shortcut text comes from the keymap, so it
// follows overrides.
std::vector<ActionInfo> actionsFromRegistry(const commands::CommandRegistry& registry, const commands::Keymap& keymap, const ActionSourceOptions& options = {});

// The ids of the commands whose description is empty or blank: a development check, so every command a
// user can see in a list explains itself (the list shows the description next to the name).
std::vector<std::string> commandsWithoutDescription(const commands::CommandRegistry& registry);

// ---- search ----
// The query split at ASCII whitespace into lower-cased terms (cut at the byte and term limits).
struct ActionQuery {
  std::vector<std::string> terms;
  bool empty() const { return terms.empty(); }
};
ActionQuery parseActionQuery(std::string_view text);

// The lower-cased text a query is matched against: "label id description category", and the shortcut text
// too when `includeShortcut` is set (the hotkey editor searches by hotkey).
std::string actionHaystack(const ActionInfo& action, bool includeShortcut = false);
bool matchesAction(const ActionQuery& query, std::string_view haystack);

// Position of the first occurrence of `needle` (already lower-case) in `text`, ASCII case-insensitive,
// or npos. Used by the list to highlight matches.
size_t findFolded(std::string_view text, std::string_view needle);

// The ASCII lower-cased copy of `text`.
std::string foldActionText(std::string_view text);

}  // namespace r1ui::widgets

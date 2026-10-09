// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: Keymap, the bindings that are in force: command defaults merged with the user overrides,
//   resolved into one table per binding context, rebuilt lazily when the registry or the overrides
//   change.
// Why: spec 07 rules 2 to 5 and 18: a default chord is taken only if no other command of the same
//   context already holds it (the earlier-declared command keeps it), an override replaces the default
//   of its slot even when empty, and when an override names a chord another command holds, the
//   overriding command wins. Decision D14 extends "hold the same chord" to prefixes: a chord that is
//   the first step of a sequence in the same context cannot also be a single binding.
// Resolution order (priority, highest first): overrides, newest stamp first; then defaults in
//   registration order, primary slot before alternate. A candidate is accepted unless an accepted
//   binding of the same context is equal to it (a different command) or is a prefix of it or extends
//   it. The same command holding the same chord in both slots is one binding with two slots (spec 07:
//   no conflict with itself).
// Callers: CommandRouter (lookup), conflict detection, widgets (shortcut text, editor rows).
//   Calls: CommandRegistry, KeybindingOverrides.
// Cost: a rebuild is O(commands) and happens only after a version change; lookup is O(1) per context.
// Threading: UI thread only; query methods rebuild on demand (mutable cache).
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "r1ui/commands/CommandRegistry.h"
#include "r1ui/commands/Overrides.h"

namespace r1ui::commands {

struct Binding {
  std::string commandId;
  uint8_t slotMask = 0;  // bit s set: the command holds this chord in slot s
};

enum class LookupKind : uint8_t { None, Exact, Prefix };

struct LookupResult {
  LookupKind kind = LookupKind::None;
  std::string commandId;  // Exact
};

// A way to finish a sequence that starts with a given chord.
struct Continuation {
  KeyChord second;
  std::string commandId;
};

class Keymap {
 public:
  Keymap(const CommandRegistry& registry, const KeybindingOverrides& overrides) : registry_(registry), overrides_(overrides) {}

  // Looks `typed` (one chord, or a complete two-chord sequence) up in one context only (no parents).
  // For a single chord: Exact when a binding is exactly that chord, Prefix when it starts a sequence.
  LookupResult lookup(std::string_view context, const ChordSequence& typed) const;

  // The chord in force for a command slot (nullopt: unbound, unknown command, bad slot).
  std::optional<ChordSequence> effective(std::string_view commandId, int slot) const;
  // Display text of the first valid chord (primary, else alternate); empty when unbound.
  std::string displayText(std::string_view commandId, bool upperCase = false) const;

  // Visits every binding of every context (order unspecified).
  void forEachBinding(const std::function<void(const std::string& context, const ChordSequence& sequence, const Binding& binding)>& visit) const;
  // Sequences of `context` that start with `first`.
  std::vector<Continuation> continuations(std::string_view context, const KeyChord& first) const;

  // Number of bindings in force (a command holding one chord in two slots counts once).
  size_t bindingCount() const;

 private:
  struct Table {
    std::unordered_map<uint64_t, Binding> exact;  // ChordSequence::code() -> binding
    std::unordered_map<uint32_t, int> firsts;     // first-chord codes of two-step sequences -> count
  };
  void ensureFresh() const;
  void rebuild() const;
  bool accept(Table& table, const ChordSequence& sequence, const std::string& commandId, int slot) const;

  const CommandRegistry& registry_;
  const KeybindingOverrides& overrides_;
  mutable uint64_t builtRegistry_ = 0;
  mutable uint64_t builtOverrides_ = 0;
  mutable std::unordered_map<std::string, Table> tables_;
  mutable std::unordered_map<std::string, std::array<std::optional<ChordSequence>, kSlotCount>> effective_;
};

}  // namespace r1ui::commands

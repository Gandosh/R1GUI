// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: KeybindingOverrides, the per-user table of chord choices: for one command and slot either a
//   chord (sequence) or "deliberately unbound".
// Why: spec 07 rules 4, 5, 55 to 57 and 61: an override replaces the default for its slot even when
//   it is empty (unbound), the later override wins over an earlier one for the same chord, resets
//   take effect at once, and every change reaches menus, tooltips and chord processing immediately
//   (decision D15). The table is plain data; Keymap turns defaults plus overrides into the bindings
//   that are in force.
// Callers: the keybinding editor, the host (load, import, reset), Keymap (reads), OverrideIo (file
//   format). Calls: CommandRegistry (existence checks; touch() after every change).
// Live application: every effective change calls CommandRegistry::touch(), so the registry version
//   moves and menus, toolbars and the editor refresh. Several changes made inside an OverrideBatch
//   produce one notification. Setting a slot to the value it already has changes nothing.
// Stamps: every stored override carries a growing stamp; when two overrides name the same chord in one
//   context the one with the larger stamp wins (spec 07 edge cases: the later loaded wins).
// Dormant entries: overrides of a command that is not registered are kept (the command may come back
//   with its module) but have no effect and are not exported.
// Boundaries: set() accepts only registered commands, slots 0 and 1 and valid sequences; the table is
//   bounded by kMaxOverrides.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "r1ui/commands/CommandRegistry.h"

namespace r1ui::commands {

inline constexpr size_t kMaxOverrides = 2 * kMaxCommands;
inline constexpr int kSlotCount = 2;

struct OverrideEntry {
  std::string commandId;
  int slot = 0;
  std::optional<ChordSequence> chord;  // nullopt = deliberately unbound
  uint64_t stamp = 0;
};

enum class SetError : uint8_t { None, UnknownCommand, BadSlot, InvalidChord, TableFull };

class KeybindingOverrides {
 public:
  explicit KeybindingOverrides(CommandRegistry& registry) : registry_(registry) {}

  // Sets the slot's override. `chord` empty (nullopt) unbinds the slot.
  SetError set(std::string_view commandId, int slot, std::optional<ChordSequence> chord);
  // Removes the override so the default applies again; false when there was none.
  bool clear(std::string_view commandId, int slot);
  // Removes both slots' overrides of one command; false when there was none.
  bool resetCommand(std::string_view commandId);
  // Removes every override (reset to defaults, live).
  void resetAll();

  const OverrideEntry* find(std::string_view commandId, int slot) const;
  size_t size() const { return count_; }
  // All entries ordered by stamp (oldest first).
  std::vector<OverrideEntry> entries() const;
  // Replaces the whole table; entries that are invalid (bad slot, invalid chord) are skipped and the
  // order of the vector gives the stamps. Returns the number kept.
  size_t replaceAll(const std::vector<OverrideEntry>& entries);

  uint64_t version() const { return version_; }

  // Defers the registry notification until the last batch object is destroyed.
  class Batch {
   public:
    explicit Batch(KeybindingOverrides& owner) : owner_(owner) { ++owner_.batchDepth_; }
    ~Batch() { owner_.endBatch(); }
    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;

   private:
    KeybindingOverrides& owner_;
  };

 private:
  using Slots = std::array<std::optional<OverrideEntry>, kSlotCount>;
  void changed();
  void endBatch();

  CommandRegistry& registry_;
  std::unordered_map<std::string, Slots> table_;
  size_t count_ = 0;
  uint64_t nextStamp_ = 1;
  uint64_t version_ = 1;
  int batchDepth_ = 0;
  bool batchDirty_ = false;
};

}  // namespace r1ui::commands

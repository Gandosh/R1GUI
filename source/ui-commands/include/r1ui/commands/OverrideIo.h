// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the file side of keybinding overrides: the versioned JSON format, validated import with a
//   report, export, and a small storage interface with file and in-memory implementations.
// Why: spec 07 rules 56 to 60: user overrides persist per user, import replaces the bindings after
//   confirmation (the confirmation is the host's), export writes them out, and a missing or
//   unreadable file leaves the defaults in force. Files may be hand edited or hostile, so every
//   boundary is bounded and validated and a rejected file never changes the live state.
// Format (version 1): {"format":"r1ui-keybindings","version":1,"overrides":[{"context":"global",
//   "command":"edit.undo","slot":0,"chord":"Ctrl+Z"}, {"context":"global","command":"edit.redo",
//   "slot":1,"chord":null}]}. `chord` null means deliberately unbound; a sequence is "Ctrl+K, Ctrl+C".
//   `context` is optional on import and must match the command's context when present. Unknown extra
//   members are ignored.
// Import rules: the whole file is rejected (nothing changes) when it is larger than kMaxImportBytes,
//   not valid JSON (nesting limit 8), not an object of this format and version, or has more than
//   kMaxImportEntries entries. Single entries are skipped with an issue when the command is unknown
//   (counted separately, spec 06 rule 35), the slot is not 0 or 1, the chord does not parse or the
//   context does not match; the rest apply. A file with no applicable entry changes nothing (spec 07
//   rule 59). Replace swaps the whole table in one step; Merge sets entry by entry in one batch.
// Callers: the host (load at start, save on change or close), the keybinding editor (import and
//   export buttons through host callbacks), tests. Calls: CommandRegistry, KeybindingOverrides,
//   core JSON.
// Stores: load() reports a missing file as `exists = false` (normal on first start) and any read
//   failure or a file over kMaxImportBytes as an error; save() writes a temporary file and renames
//   it over the target so a crash never leaves a half-written file. No exceptions escape.
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/commands/CommandRegistry.h"
#include "r1ui/commands/Overrides.h"

namespace r1ui::commands {

inline constexpr size_t kMaxImportBytes = size_t{1} << 20;
inline constexpr size_t kMaxImportEntries = 20000;

// ---- storage ------------------------------------------------------------------------------------

struct LoadResult {
  bool exists = false;
  std::string text;
  std::string error;  // empty unless reading failed
};

class KeybindingStore {
 public:
  virtual ~KeybindingStore() = default;
  virtual LoadResult load() = 0;
  virtual bool save(std::string_view text, std::string& error) = 0;
};

class MemoryKeybindingStore final : public KeybindingStore {
 public:
  LoadResult load() override;
  bool save(std::string_view text, std::string& error) override;
  const std::optional<std::string>& contents() const { return text_; }
  void setContents(std::string text) { text_ = std::move(text); }

 private:
  std::optional<std::string> text_;
};

class FileKeybindingStore final : public KeybindingStore {
 public:
  explicit FileKeybindingStore(std::filesystem::path path) : path_(std::move(path)) {}
  LoadResult load() override;
  bool save(std::string_view text, std::string& error) override;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

// ---- format -------------------------------------------------------------------------------------

enum class ImportMode : uint8_t { Replace, Merge };

struct ImportIssue {
  size_t index = 0;        // position in the file's overrides array
  std::string commandId;   // as written (sanitised, may be empty)
  std::string message;
  bool unknownCommand = false;
};

struct ImportReport {
  bool ok = false;            // the file was acceptable as a whole
  bool changed = false;       // the live table was changed
  std::string error;          // why the file was rejected as a whole
  size_t applied = 0;
  size_t ignoredUnknown = 0;  // entries naming commands that are not registered
  std::vector<ImportIssue> issues;
};

// The JSON text of every override whose command is registered, oldest first.
std::string exportOverrides(const CommandRegistry& registry, const KeybindingOverrides& overrides);

ImportReport importOverrides(std::string_view json, const CommandRegistry& registry, KeybindingOverrides& overrides, ImportMode mode = ImportMode::Replace);

// Store round trips. saveOverrides returns false with `error` set on failure. loadOverrides: a missing
// file is ok with nothing changed; a read failure is not ok and changes nothing.
bool saveOverrides(const CommandRegistry& registry, const KeybindingOverrides& overrides, KeybindingStore& store, std::string& error);
ImportReport loadOverrides(KeybindingStore& store, const CommandRegistry& registry, KeybindingOverrides& overrides, ImportMode mode = ImportMode::Replace);

}  // namespace r1ui::commands

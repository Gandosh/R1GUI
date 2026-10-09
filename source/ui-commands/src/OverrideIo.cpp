// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of OverrideIo.h: JSON export / validated import, memory and file stores.
// Invariants: import validates everything before touching the live table; the file store never
//   throws and never leaves a partial target file; exported text parses back to the same overrides.
// Callers: the host, the keybinding editor's callbacks, tests.
#include "r1ui/commands/OverrideIo.h"

#include <fstream>
#include <system_error>

#include "r1ui/commands/Text.h"
#include "r1ui/core/Json.h"
#include "r1ui/core/JsonWriter.h"

namespace r1ui::commands {

namespace {

constexpr const char* kFormatName = "r1ui-keybindings";
constexpr int kFormatVersion = 1;

ImportReport reject(std::string error) {
  ImportReport report;
  report.error = std::move(error);
  return report;
}

}  // namespace

// ---- stores -------------------------------------------------------------------------------------

LoadResult MemoryKeybindingStore::load() {
  LoadResult result;
  if (text_) {
    result.exists = true;
    result.text = *text_;
  }
  return result;
}

bool MemoryKeybindingStore::save(std::string_view text, std::string& error) {
  (void)error;
  text_ = std::string(text);
  return true;
}

LoadResult FileKeybindingStore::load() {
  LoadResult result;
  std::error_code ec;
  const bool exists = std::filesystem::exists(path_, ec);
  if (ec) {
    result.error = "cannot access " + path_.string() + ": " + ec.message();
    return result;
  }
  if (!exists) return result;
  const auto size = std::filesystem::file_size(path_, ec);
  if (ec) {
    result.error = "cannot read " + path_.string() + ": " + ec.message();
    return result;
  }
  if (size > kMaxImportBytes) {
    result.error = "keybinding file is larger than the " + std::to_string(kMaxImportBytes) + " byte limit";
    return result;
  }
  std::ifstream in(path_, std::ios::binary);
  if (!in) {
    result.error = "cannot open " + path_.string();
    return result;
  }
  result.text.assign(static_cast<size_t>(size), '\0');
  in.read(result.text.data(), static_cast<std::streamsize>(size));
  if (!in && !in.eof()) {
    result.text.clear();
    result.error = "read failed: " + path_.string();
    return result;
  }
  result.text.resize(static_cast<size_t>(in.gcount()));
  result.exists = true;
  return result;
}

bool FileKeybindingStore::save(std::string_view text, std::string& error) {
  std::error_code ec;
  if (path_.has_parent_path()) {
    std::filesystem::create_directories(path_.parent_path(), ec);
    if (ec) {
      error = "cannot create " + path_.parent_path().string() + ": " + ec.message();
      return false;
    }
  }
  std::filesystem::path temp = path_;
  temp += ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
      error = "cannot write " + temp.string();
      return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out) {
      error = "write failed: " + temp.string();
      std::filesystem::remove(temp, ec);
      return false;
    }
  }
  std::filesystem::rename(temp, path_, ec);
  if (ec) {
    error = "cannot replace " + path_.string() + ": " + ec.message();
    std::filesystem::remove(temp, ec);
    return false;
  }
  return true;
}

// ---- export -------------------------------------------------------------------------------------

std::string exportOverrides(const CommandRegistry& registry, const KeybindingOverrides& overrides) {
  std::string out = "{\"format\":";
  core::appendQuoted(out, kFormatName);
  out += ",\"version\":" + std::to_string(kFormatVersion) + ",\"overrides\":[";
  bool first = true;
  for (const OverrideEntry& entry : overrides.entries()) {
    const CommandDef* command = registry.find(entry.commandId);
    if (command == nullptr) continue;  // dormant: its module is not loaded
    if (!first) out += ',';
    first = false;
    out += "{\"context\":";
    core::appendQuoted(out, command->context);
    out += ",\"command\":";
    core::appendQuoted(out, entry.commandId);
    out += ",\"slot\":" + std::to_string(entry.slot) + ",\"chord\":";
    if (entry.chord) {
      core::appendQuoted(out, formatSequence(*entry.chord));
    } else {
      out += "null";
    }
    out += '}';
  }
  out += "]}\n";
  return out;
}

// ---- import -------------------------------------------------------------------------------------

ImportReport importOverrides(std::string_view json, const CommandRegistry& registry, KeybindingOverrides& overrides, ImportMode mode) {
  if (json.size() > kMaxImportBytes) return reject("keybinding file is larger than the " + std::to_string(kMaxImportBytes) + " byte limit");
  core::JsonLimits limits;
  limits.maxDepth = 8;
  limits.maxInputBytes = kMaxImportBytes;
  limits.maxNodes = 8 * kMaxImportEntries;
  const core::JsonResult parsed = core::parseJson(json, limits);
  if (!parsed.ok()) return reject("not valid JSON: " + parsed.error.message);
  const core::JsonValue& root = *parsed.value;
  if (!root.isObject()) return reject("the file must contain a JSON object");
  const core::JsonValue* format = root.find("format");
  if (format == nullptr || !format->isString() || format->stringValue() != kFormatName) return reject("not a keybinding file");
  const core::JsonValue* version = root.find("version");
  if (version == nullptr || version->type() != core::JsonType::Number || version->numberValue() != kFormatVersion) {
    return reject("unsupported keybinding file version");
  }
  const core::JsonValue* list = root.find("overrides");
  if (list == nullptr || list->type() != core::JsonType::Array) return reject("the file has no overrides array");
  if (list->size() > kMaxImportEntries) return reject("too many entries (limit " + std::to_string(kMaxImportEntries) + ")");

  ImportReport report;
  report.ok = true;
  std::vector<OverrideEntry> accepted;
  accepted.reserve(list->size());
  for (size_t i = 0; i < list->size(); ++i) {
    const core::JsonValue& item = list->child(i);
    ImportIssue issue;
    issue.index = i;
    const auto skip = [&](std::string message) {
      issue.message = std::move(message);
      report.issues.push_back(issue);
    };
    if (!item.isObject()) {
      skip("entry is not an object");
      continue;
    }
    const core::JsonValue* command = item.find("command");
    if (command == nullptr || !command->isString() || !isValidIdentifier(command->stringValue(), kMaxIdBytes)) {
      skip("entry has no valid command id");
      continue;
    }
    issue.commandId = command->stringValue();
    const CommandDef* def = registry.find(issue.commandId);
    if (def == nullptr) {
      ++report.ignoredUnknown;
      issue.unknownCommand = true;
      skip("unknown command");
      continue;
    }
    const core::JsonValue* context = item.find("context");
    if (context != nullptr && (!context->isString() || context->stringValue() != def->context)) {
      skip("context does not match the command's context");
      continue;
    }
    const core::JsonValue* slot = item.find("slot");
    if (slot == nullptr || slot->type() != core::JsonType::Number || (slot->numberValue() != 0.0 && slot->numberValue() != 1.0)) {
      skip("slot must be 0 or 1");
      continue;
    }
    const core::JsonValue* chord = item.find("chord");
    OverrideEntry entry;
    entry.commandId = issue.commandId;
    entry.slot = static_cast<int>(slot->numberValue());
    if (chord == nullptr) {
      skip("entry has no chord member (use null to unbind)");
      continue;
    }
    if (chord->type() != core::JsonType::Null) {
      const std::optional<ChordSequence> sequence = chord->isString() ? parseSequence(chord->stringValue()) : std::nullopt;
      if (!sequence) {
        skip("malformed chord");
        continue;
      }
      entry.chord = sequence;
    }
    accepted.push_back(std::move(entry));
  }

  if (accepted.empty()) return report;  // nothing usable: the live bindings stay as they are
  report.applied = accepted.size();
  if (mode == ImportMode::Replace) {
    overrides.replaceAll(accepted);
  } else {
    const KeybindingOverrides::Batch batch(overrides);
    for (const OverrideEntry& e : accepted) overrides.set(e.commandId, e.slot, e.chord);
  }
  report.changed = true;
  return report;
}

// ---- store round trips --------------------------------------------------------------------------

bool saveOverrides(const CommandRegistry& registry, const KeybindingOverrides& overrides, KeybindingStore& store, std::string& error) {
  return store.save(exportOverrides(registry, overrides), error);
}

ImportReport loadOverrides(KeybindingStore& store, const CommandRegistry& registry, KeybindingOverrides& overrides, ImportMode mode) {
  const LoadResult loaded = store.load();
  if (!loaded.error.empty()) return reject(loaded.error);
  if (!loaded.exists) {
    ImportReport report;
    report.ok = true;
    return report;
  }
  return importOverrides(loaded.text, registry, overrides, mode);
}

}  // namespace r1ui::commands

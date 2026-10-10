// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: BrushState.h: export, strict parse, load/save through a TextStore and BrushStateStorage.
// Invariants: parseBrushState never throws and never returns a state that replaceUserState would have
//   to repair silently (every kept id is clean, unique and within the limits); a rejected or unreadable
//   file changes nothing in the model and is moved aside, never overwritten; loading does not trigger a
//   save of the file just read.
// Callers: hosts, tests.
#include <cmath>
#include <unordered_set>

#include "r1ui/commands/Text.h"
#include "r1ui/commands/brushes/BrushLetters.h"
#include "r1ui/commands/brushes/BrushState.h"
#include "r1ui/core/Json.h"
#include "r1ui/core/JsonWriter.h"

namespace r1ui::commands::brushes {

namespace {

constexpr size_t kMaxIssues = 64;

bool cleanId(const std::string& id) { return !id.empty() && id.size() <= kMaxBrushIdBytes && sanitizeText(id, kMaxBrushIdBytes) == id; }

void appendStringList(std::string& out, const std::vector<std::string>& items) {
  out += "[";
  for (size_t i = 0; i < items.size(); ++i) {
    out += i == 0 ? "\n    " : ",\n    ";
    core::appendQuoted(out, items[i]);
  }
  out += items.empty() ? "]" : "\n  ]";
}

}  // namespace

std::string exportBrushState(const BrushUserState& state) {
  std::string out = "{\n  \"format\":\"";
  out += kBrushStateFormat;
  out += "\",\n  \"version\":" + std::to_string(kBrushStateVersion) + ",\n  \"pickOnUnique\":" + (state.pickOnUnique ? "true" : "false") + ",\n  \"favourites\":";
  appendStringList(out, state.favourites);
  out += ",\n  \"recents\":";
  appendStringList(out, state.recents);
  out += ",\n  \"letters\":[";
  for (size_t i = 0; i < state.letters.size(); ++i) {
    out += i == 0 ? "\n    {\"brush\":" : ",\n    {\"brush\":";
    core::appendQuoted(out, state.letters[i].first);
    out += ",\"letter\":";
    core::appendQuoted(out, state.letters[i].second);
    out += "}";
  }
  out += state.letters.empty() ? "]\n}\n" : "\n  ]\n}\n";
  return out;
}

BrushStateParse parseBrushState(std::string_view json) {
  BrushStateParse result;
  const auto issue = [&](std::string where, std::string message) {
    if (result.issues.size() < kMaxIssues) result.issues.push_back({std::move(where), std::move(message)});
  };
  if (json.size() > kMaxBrushStateBytes) {
    result.error = "the file is larger than the " + std::to_string(kMaxBrushStateBytes) + " byte limit";
    return result;
  }
  core::JsonLimits limits;
  limits.maxDepth = 6;
  limits.maxInputBytes = kMaxBrushStateBytes;
  limits.maxNodes = 200'000;
  const core::JsonResult parsed = core::parseJson(json, limits);
  if (!parsed.ok()) {
    result.error = "not valid JSON: " + parsed.error.message;
    return result;
  }
  const core::JsonValue& root = *parsed.value;
  if (!root.isObject()) {
    result.error = "the file must contain a JSON object";
    return result;
  }
  const core::JsonValue* format = root.find("format");
  if (format == nullptr || !format->isString() || format->stringValue() != kBrushStateFormat) {
    result.error = "not a brush library file";
    return result;
  }
  const core::JsonValue* version = root.find("version");
  const double v = version != nullptr && version->type() == core::JsonType::Number ? version->numberValue() : 0.0;
  if (!(v >= 1.0 && v <= 1.0e6) || std::floor(v) != v) {
    result.error = "the file has no valid version";
    return result;
  }
  if (v > kBrushStateVersion) {
    result.error = "the file was written by a newer version";
    return result;
  }
  if (const core::JsonValue* pick = root.find("pickOnUnique"); pick != nullptr) {
    if (pick->type() != core::JsonType::Bool) {
      result.error = "pickOnUnique must be true or false";
      return result;
    }
    result.state.pickOnUnique = pick->boolValue();
  }
  const auto list = [&](const char* name, const core::JsonValue*& out) {
    out = root.find(name);
    if (out != nullptr && out->type() != core::JsonType::Array) {
      result.error = std::string(name) + " must be a list";
      return false;
    }
    return true;
  };
  const core::JsonValue* favourites = nullptr;
  const core::JsonValue* recents = nullptr;
  const core::JsonValue* letters = nullptr;
  if (!list("favourites", favourites) || !list("recents", recents) || !list("letters", letters)) return result;

  const auto readIds = [&](const core::JsonValue* items, const char* name, size_t limit, std::vector<std::string>& out) {
    if (items == nullptr) return;
    std::unordered_set<std::string> seen;
    for (size_t i = 0; i < items->size(); ++i) {
      const std::string where = std::string(name) + "[" + std::to_string(i) + "]";
      const core::JsonValue& item = items->child(i);
      if (!item.isString() || !cleanId(item.stringValue())) {
        issue(where, "skipped: not a valid brush id");
        continue;
      }
      if (out.size() >= limit) {
        issue(where, "skipped: past the limit of " + std::to_string(limit));
        continue;
      }
      if (!seen.insert(item.stringValue()).second) {
        issue(where, "skipped: duplicate");
        continue;
      }
      out.push_back(item.stringValue());
    }
  };
  readIds(favourites, "favourites", kMaxStateEntries, result.state.favourites);
  readIds(recents, "recents", kMaxRecents, result.state.recents);

  if (letters != nullptr) {
    std::unordered_set<std::string> seen;
    for (size_t i = 0; i < letters->size(); ++i) {
      const std::string where = "letters[" + std::to_string(i) + "]";
      const core::JsonValue& item = letters->child(i);
      const core::JsonValue* brush = item.isObject() ? item.find("brush") : nullptr;
      const core::JsonValue* letter = item.isObject() ? item.find("letter") : nullptr;
      if (brush == nullptr || !brush->isString() || !cleanId(brush->stringValue())) {
        issue(where, "skipped: not a valid brush id");
        continue;
      }
      if (letter == nullptr || !letter->isString() || !singleKeyLetter(letter->stringValue())) {
        issue(where, "skipped: the letter must be one letter or digit");
        continue;
      }
      if (result.state.letters.size() >= kMaxStateEntries || !seen.insert(brush->stringValue()).second) {
        issue(where, "skipped: duplicate brush or past the limit");
        continue;
      }
      result.state.letters.emplace_back(brush->stringValue(), letter->stringValue());
    }
  }
  result.ok = true;
  return result;
}

BrushStateLoadReport loadBrushState(BrushLibraryModel& model, customize::TextStore& store) {
  BrushStateLoadReport report;
  const customize::TextLoad loaded = store.load();
  const auto reject = [&](std::string why, bool moveAside) {
    report.ok = false;
    report.error = std::move(why);
    if (moveAside) {
      std::string moveError;
      report.keptAside = store.setAside(moveError);
    }
    return report;
  };
  if (!loaded.error.empty()) return reject(loaded.error, loaded.exists);
  if (!loaded.exists) return report;
  BrushStateParse parsed = parseBrushState(loaded.text);
  report.issues = std::move(parsed.issues);
  if (!parsed.ok) return reject("brush library file rejected: " + parsed.error, true);
  model.replaceUserState(std::move(parsed.state));
  report.loaded = true;
  return report;
}

bool saveBrushState(const BrushLibraryModel& model, customize::TextStore& store, std::string& error) { return store.save(exportBrushState(model), error); }

BrushStateStorage::BrushStateStorage(BrushLibraryModel& model, customize::TextStore& store) : model_(model), store_(store), savedVersion_(model.stateVersion()) {
  listener_ = model_.subscribe([this] {
    if (autoSave_ && model_.stateVersion() != savedVersion_) save();
  });
}

BrushStateStorage::~BrushStateStorage() { model_.unsubscribe(listener_); }

BrushStateLoadReport BrushStateStorage::load() {
  const bool previous = autoSave_;
  autoSave_ = false;
  BrushStateLoadReport report = loadBrushState(model_, store_);
  autoSave_ = previous;
  savedVersion_ = model_.stateVersion();
  lastError_ = report.error;
  return report;
}

bool BrushStateStorage::save() {
  std::string error;
  savedVersion_ = model_.stateVersion();  // a failed write is reported once, not retried on every change
  const bool ok = saveBrushState(model_, store_, error);
  lastError_ = ok ? std::string() : error;
  if (ok) ++saves_;
  return ok;
}

}  // namespace r1ui::commands::brushes

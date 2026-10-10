// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the set store of CustomMenuIo.h: exportSet, parseSet, loadMenus, saveMenus and CustomMenuStorage.
// Invariants: parseSet never throws; a rejected or unreadable store changes nothing in the live set and a
//   corrupt file is moved aside (never overwritten); ids and names of the loaded menus are unique and
//   nextSerial is above every serial in use; loading does not trigger a save of the file just read.
// Callers: hosts at startup and shutdown, tests.
#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "CustomMenuCodec.h"
#include "r1ui/commands/custommenu/CustomMenuIo.h"
#include "r1ui/core/JsonWriter.h"

namespace r1ui::commands::custommenu {

std::string exportSet(const CustomMenuSet& set) {
  std::string out = "{\n  \"format\":\"";
  out += kSetFileFormat;
  out += "\",\n  \"version\":" + std::to_string(kMenuFileVersion) + ",\n  \"serial\":" + std::to_string(set.nextSerial()) + ",\n  \"menus\":";
  if (set.menus().empty()) {
    out += "[]\n}\n";
    return out;
  }
  out += "[\n";
  for (size_t i = 0; i < set.menus().size(); ++i) {
    out += "    {\n";
    codec::writeMembers(out, set.menus()[i], true, "      ");
    out += i + 1 < set.menus().size() ? "    },\n" : "    }\n";
  }
  out += "  ]\n}\n";
  return out;
}

SetParseResult parseSet(std::string_view text) {
  SetParseResult result;
  if (text.size() > kMaxSetFileBytes) {
    result.error = "the file is larger than the " + std::to_string(kMaxSetFileBytes) + " byte limit";
    return result;
  }
  core::JsonLimits limits;
  limits.maxDepth = 8;
  limits.maxInputBytes = kMaxSetFileBytes;
  limits.maxNodes = 1'000'000;
  const core::JsonResult parsed = core::parseJson(text, limits);
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
  if (format == nullptr || !format->isString() || format->stringValue() != kSetFileFormat) {
    result.error = "not a custom menus file";
    return result;
  }
  const core::JsonValue* version = root.find("version");
  const double v = version != nullptr && version->type() == core::JsonType::Number ? version->numberValue() : 0.0;
  if (!(v >= 1.0 && v <= 1.0e6) || std::floor(v) != v) {
    result.error = "the file has no valid version";
    return result;
  }
  if (v > kMenuFileVersion) {
    result.error = "the file was written by a newer version";
    return result;
  }
  const core::JsonValue* menus = root.find("menus");
  if (menus == nullptr || menus->type() != core::JsonType::Array) {
    result.error = "the file has no menus list";
    return result;
  }
  if (menus->size() > kMaxMenus) {
    result.error = "too many menus (limit " + std::to_string(kMaxMenus) + ")";
    return result;
  }
  uint32_t serial = 1;
  if (const core::JsonValue* s = root.find("serial"); s != nullptr && s->type() == core::JsonType::Number && s->numberValue() >= 1.0 &&
                                                       s->numberValue() < 4.0e9 && std::floor(s->numberValue()) == s->numberValue()) {
    serial = static_cast<uint32_t>(s->numberValue());
  }

  size_t repaired = 0;
  codec::ParseContext ctx{result.issues, repaired};
  std::unordered_set<std::string> ids;
  size_t total = 0;
  std::vector<CustomMenu> kept;
  std::vector<size_t> needsId;
  for (size_t i = 0; i < menus->size(); ++i) {
    const std::string at = "menus[" + std::to_string(i) + "]";
    CustomMenu menu;
    std::string error;
    if (!codec::parseMenu(menus->child(i), at, true, ctx, menu, error)) {
      ++result.skipped;
      ctx.issue(at, error + "; menu skipped", false);
      continue;
    }
    bool duplicate = !menu.id.empty() && ids.count(menu.id) != 0;
    for (const CustomMenu& other : kept) duplicate = duplicate || sameName(other.name, menu.name);
    size_t entries = 0;
    for (const MenuEntry& e : menu.entries) entries += e.commandId.empty() ? 0 : 1;
    if (duplicate) {
      ++result.skipped;
      ctx.issue(at, "repeats the id or name of an earlier menu; skipped", false);
      continue;
    }
    if (total + entries > kMaxTotalEntries) {
      ++result.skipped;
      ctx.issue(at, "too many entries in total; skipped", false);
      continue;
    }
    total += entries;
    if (menu.id.empty()) {
      needsId.push_back(kept.size());
    } else {
      ids.insert(menu.id);
      serial = std::max(serial, menu.serial + 1);
    }
    kept.push_back(std::move(menu));
  }
  for (const size_t index : needsId) {
    if (serial == UINT32_MAX) break;
    kept[index].serial = serial++;
    kept[index].id = menuIdFor(kept[index].serial);
  }
  // A menu that is still without an id (counter exhausted) cannot be kept.
  kept.erase(std::remove_if(kept.begin(), kept.end(), [](const CustomMenu& m) { return m.id.empty(); }), kept.end());
  for (CustomMenu& menu : kept) {
    std::string reason;
    if (!validateMenu(menu, reason)) {
      ++result.skipped;
      ctx.issue("menu " + menu.name, reason + "; menu skipped", false);
      menu.id.clear();
    }
  }
  kept.erase(std::remove_if(kept.begin(), kept.end(), [](const CustomMenu& m) { return m.id.empty(); }), kept.end());
  if (menus->size() > 0 && kept.empty()) {
    result.error = "the file lists menus but none is usable";
    return result;
  }
  result.menus = std::move(kept);
  result.nextSerial = serial;
  result.ok = true;
  return result;
}

// ---- store glue ---------------------------------------------------------------------------------

MenuLoadReport loadMenus(CustomMenuSet& set, customize::TextStore& store) {
  MenuLoadReport report;
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
  SetParseResult parsed = parseSet(loaded.text);
  report.issues = std::move(parsed.issues);
  if (!parsed.ok) return reject("custom menus file rejected: " + parsed.error, true);
  const MenuEditResult applied = set.replaceAll(std::move(parsed.menus), parsed.nextSerial);
  if (!applied.ok) return reject("custom menus file rejected: " + applied.reason, true);
  report.loaded = true;
  return report;
}

bool saveMenus(const CustomMenuSet& set, customize::TextStore& store, std::string& error) { return store.save(exportSet(set), error); }

CustomMenuStorage::CustomMenuStorage(CustomMenuSet& set, customize::TextStore& store) : set_(set), store_(store) {
  listener_ = set_.subscribe([this] {
    if (autoSave_) save();
  });
}

CustomMenuStorage::~CustomMenuStorage() { set_.unsubscribe(listener_); }

MenuLoadReport CustomMenuStorage::load() {
  const bool previous = autoSave_;
  autoSave_ = false;
  MenuLoadReport report = loadMenus(set_, store_);
  autoSave_ = previous;
  lastError_ = report.error;
  return report;
}

bool CustomMenuStorage::save() {
  std::string error;
  const bool ok = saveMenus(set_, store_, error);
  lastError_ = ok ? std::string() : error;
  return ok;
}

}  // namespace r1ui::commands::custommenu

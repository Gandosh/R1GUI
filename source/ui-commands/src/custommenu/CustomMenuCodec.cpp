// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the menu object codec declared in CustomMenuCodec.h.
// Invariants: parseMenu produces a menu that passes validateMenu (apart from id/serial when absent) or
//   returns false; text is cleaned, counts are bounded before anything is allocated for them, numbers
//   are checked finite and integral before they become ints.
// Callers: CustomMenuFile.cpp, CustomMenuStore.cpp.
#include "CustomMenuCodec.h"

#include <cmath>
#include <limits>

#include "r1ui/commands/Text.h"
#include "r1ui/core/JsonWriter.h"

namespace r1ui::commands::custommenu::codec {

using core::JsonType;
using core::JsonValue;

// ---- writing ------------------------------------------------------------------------------------

namespace {

void writeEntry(std::string& out, const MenuEntry& entry) {
  if (entry.commandId.empty()) {
    out += "null";
    return;
  }
  out += "{\"command\":";
  core::appendQuoted(out, entry.commandId);
  if (!entry.label.empty()) {
    out += ",\"label\":";
    core::appendQuoted(out, entry.label);
  }
  if (!entry.icon.empty()) {
    out += ",\"icon\":";
    core::appendQuoted(out, entry.icon);
  }
  out += "}";
}

void writeList(std::string& out, const std::vector<MenuEntry>& entries, const char* indent) {
  if (entries.empty()) {
    out += "[]";
    return;
  }
  out += "[\n";
  for (size_t i = 0; i < entries.size(); ++i) {
    out += indent;
    out += "  ";
    writeEntry(out, entries[i]);
    out += i + 1 < entries.size() ? ",\n" : "\n";
  }
  out += indent;
  out += "]";
}

}  // namespace

void writeMembers(std::string& out, const CustomMenu& menu, bool withId, const char* indent) {
  const auto member = [&](const char* key) {
    out += indent;
    out += '"';
    out += key;
    out += "\":";
  };
  if (withId) {
    member("id");
    core::appendQuoted(out, menu.id);
    out += ",\n";
  }
  member("kind");
  core::appendQuoted(out, kindName(menu.kind));
  out += ",\n";
  member("name");
  core::appendQuoted(out, menu.name);
  out += ",\n";
  if (menu.kind == MenuKind::Pie) {
    member("slotCount");
    core::appendNumber(out, menu.slotCount);
    out += ",\n";
    member("slots");
    writeList(out, menu.entries, indent);
    out += "\n";
    return;
  }
  member("columns");
  core::appendNumber(out, menu.panel.columns);
  out += ",\n";
  member("buttonSize");
  core::appendNumber(out, menu.panel.buttonSize);
  out += ",\n";
  member("showLabels");
  out += menu.panel.showLabels ? "true" : "false";
  out += ",\n";
  member("panelSize");
  out += "[";
  core::appendNumber(out, std::isfinite(menu.panel.width) ? menu.panel.width : 0.0);
  out += ",";
  core::appendNumber(out, std::isfinite(menu.panel.height) ? menu.panel.height : 0.0);
  out += "],\n";
  member("entries");
  writeList(out, menu.entries, indent);
  out += "\n";
}

// ---- parsing ------------------------------------------------------------------------------------

namespace {

bool asInt(const JsonValue* v, int& out) {
  if (v == nullptr || v->type() != JsonType::Number) return false;
  const double d = v->numberValue();
  if (!std::isfinite(d) || std::floor(d) != d || std::fabs(d) > 1.0e9) return false;
  out = static_cast<int>(d);
  return true;
}

// One entry object. Returns false when the entry has no usable command (the caller decides what that
// means for the menu kind).
bool parseEntry(const JsonValue& v, const std::string& at, ParseContext& ctx, MenuEntry& entry) {
  entry = MenuEntry{};
  if (!v.isObject()) {
    ctx.issue(at, "entry is not an object");
    return false;
  }
  const JsonValue* command = v.find("command");
  if (command == nullptr || !command->isString() || command->stringValue().empty()) return false;
  if (!isValidIdentifier(command->stringValue(), kMaxIdBytes)) {
    ctx.issue(at, "command id is not valid; entry dropped");
    return false;
  }
  entry.commandId = command->stringValue();
  if (const JsonValue* label = v.find("label"); label != nullptr && label->isString()) {
    entry.label = cleanLabel(label->stringValue());
    if (entry.label != label->stringValue()) ctx.issue(at, "label was cleaned or shortened");
  }
  if (const JsonValue* icon = v.find("icon"); icon != nullptr && icon->isString() && !icon->stringValue().empty()) {
    if (isValidIconName(icon->stringValue())) {
      entry.icon = icon->stringValue();
    } else {
      ctx.issue(at, "icon name is not valid; ignored");
    }
  }
  return true;
}

// "menu.N" with N a positive integer written without leading zeros.
bool parseMenuId(const std::string& id, uint32_t& serial) {
  if (id.rfind("menu.", 0) != 0 || id.size() < 6 || id.size() > 15) return false;
  uint64_t n = 0;
  for (size_t i = 5; i < id.size(); ++i) {
    if (id[i] < '0' || id[i] > '9') return false;
    n = n * 10 + static_cast<uint64_t>(id[i] - '0');
  }
  if (n == 0 || n >= std::numeric_limits<uint32_t>::max()) return false;
  serial = static_cast<uint32_t>(n);
  return menuIdFor(serial) == id;
}

}  // namespace

bool parseMenu(const JsonValue& obj, const std::string& at, bool wantId, ParseContext& ctx, CustomMenu& menu, std::string& error) {
  menu = CustomMenu{};
  if (!obj.isObject()) {
    error = at + " is not an object";
    return false;
  }
  if (wantId) {
    const JsonValue* id = obj.find("id");
    uint32_t serial = 0;
    if (id != nullptr && id->isString() && parseMenuId(id->stringValue(), serial)) {
      menu.id = id->stringValue();
      menu.serial = serial;
    }
  }
  const JsonValue* kind = obj.find("kind");
  if (kind == nullptr || !kind->isString() || !parseKind(kind->stringValue(), menu.kind)) {
    error = "the menu kind must be \"pie\" or \"panel\"";
    return false;
  }
  const JsonValue* name = obj.find("name");
  if (name == nullptr || !name->isString()) {
    error = "the menu has no name";
    return false;
  }
  menu.name = cleanName(name->stringValue());
  if (menu.name.empty()) {
    error = "the menu name is empty";
    return false;
  }
  if (menu.name != name->stringValue()) ctx.issue("name", "the name was cleaned or shortened");

  if (menu.kind == MenuKind::Pie) {
    const JsonValue* slots = obj.find("slots");
    if (slots == nullptr || slots->type() != JsonType::Array) {
      error = "a pie menu needs a \"slots\" list";
      return false;
    }
    const JsonValue* countValue = obj.find("slotCount");
    int count = 0;
    if (countValue != nullptr) {
      if (!asInt(countValue, count) || !isValidPieSlotCount(count)) {
        error = "slotCount must be 4, 6 or 8";
        return false;
      }
    } else if (slots->size() <= 8) {
      count = static_cast<int>(slots->size());
    }
    if (!isValidPieSlotCount(count) || slots->size() != static_cast<size_t>(count)) {
      error = "a pie menu needs exactly slotCount slots (4, 6 or 8)";
      return false;
    }
    menu.slotCount = count;
    menu.entries.resize(static_cast<size_t>(count));
    for (size_t i = 0; i < slots->size(); ++i) {
      const JsonValue& slot = slots->child(i);
      if (slot.type() == JsonType::Null) continue;
      parseEntry(slot, "slots[" + std::to_string(i) + "]", ctx, menu.entries[i]);
    }
    return true;
  }

  PanelSettings& p = menu.panel;
  const auto setting = [&](const char* key, int low, int high, int& target) {
    const JsonValue* v = obj.find(key);
    if (v == nullptr) return;
    int value = 0;
    if (asInt(v, value) && value >= low && value <= high) {
      target = value;
    } else {
      ctx.issue(key, "value out of range; the default is used");
    }
  };
  setting("columns", kMinColumns, kMaxColumns, p.columns);
  setting("buttonSize", kMinButtonSize, kMaxButtonSize, p.buttonSize);
  if (const JsonValue* v = obj.find("showLabels"); v != nullptr) {
    if (v->type() == JsonType::Bool) {
      p.showLabels = v->boolValue();
    } else {
      ctx.issue("showLabels", "not a boolean; the default is used");
    }
  }
  if (const JsonValue* v = obj.find("panelSize"); v != nullptr) {
    const auto dimension = [](const JsonValue& d, double& out) {
      if (d.type() != JsonType::Number) return false;
      const double value = d.numberValue();
      if (value != 0.0 && !(std::isfinite(value) && value >= kMinPanelDimension && value <= kMaxPanelDimension)) return false;
      out = value == 0.0 ? 0.0 : value;
      return true;
    };
    double w = 0.0;
    double h = 0.0;
    if (v->type() == JsonType::Array && v->size() == 2 && dimension(v->child(0), w) && dimension(v->child(1), h)) {
      p.width = w;
      p.height = h;
    } else {
      ctx.issue("panelSize", "not a valid [width, height]; the dock decides the size");
    }
  }
  const JsonValue* entries = obj.find("entries");
  if (entries == nullptr || entries->type() != JsonType::Array) {
    error = "a panel menu needs an \"entries\" list";
    return false;
  }
  if (entries->size() > kMaxPanelEntries) {
    error = "too many entries (limit " + std::to_string(kMaxPanelEntries) + ")";
    return false;
  }
  for (size_t i = 0; i < entries->size(); ++i) {
    MenuEntry entry;
    if (parseEntry(entries->child(i), "entries[" + std::to_string(i) + "]", ctx, entry)) {
      menu.entries.push_back(std::move(entry));
    } else {
      ctx.issue("entries[" + std::to_string(i) + "]", "entry has no usable command; dropped");
    }
  }
  return true;
}

}  // namespace r1ui::commands::custommenu::codec

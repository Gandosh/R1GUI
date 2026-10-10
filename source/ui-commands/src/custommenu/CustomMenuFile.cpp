// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the .r1mn file of CustomMenuIo.h: exportMenuFile, parseMenuFile, saveMenuFile, loadMenuFile,
//   importMenuFile and importMenuText.
// Invariants: parseMenuFile never throws and either rejects the whole file with an error or returns a
//   menu that passes validateMenu; importing changes the set only when the file was accepted AND the set
//   accepted the menu; the file is written atomically.
// Callers: hosts, the creator window, tests.
#include <cmath>

#include "CustomMenuCodec.h"
#include "r1ui/commands/custommenu/CustomMenuIo.h"
#include "r1ui/commands/custommenu/TextFile.h"

namespace r1ui::commands::custommenu {

std::string exportMenuFile(const CustomMenu& menu) {
  std::string out = "{\n  \"format\":\"";
  out += kMenuFileFormat;
  out += "\",\n  \"version\":" + std::to_string(kMenuFileVersion) + ",\n";
  codec::writeMembers(out, menu, false, "  ");
  out += "}\n";
  return out;
}

MenuParseResult parseMenuFile(std::string_view text) {
  MenuParseResult result;
  if (text.size() > kMaxMenuFileBytes) {
    result.error = "the file is larger than the " + std::to_string(kMaxMenuFileBytes) + " byte limit";
    return result;
  }
  core::JsonLimits limits;
  limits.maxDepth = 8;
  limits.maxInputBytes = kMaxMenuFileBytes;
  limits.maxNodes = 8 * kMaxPanelEntries + 64;
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
  if (format == nullptr || !format->isString() || format->stringValue() != kMenuFileFormat) {
    result.error = "not a custom menu file";
    return result;
  }
  const core::JsonValue* version = root.find("version");
  const double v = version != nullptr && version->type() == core::JsonType::Number ? version->numberValue() : 0.0;
  if (!(v >= 1.0 && v <= 1.0e6) || std::floor(v) != v) {
    result.error = "the file has no valid version";
    return result;
  }
  if (v > kMenuFileVersion) {
    result.error = "the file was written by a newer version (" + std::to_string(static_cast<int>(v)) + ")";
    return result;
  }
  codec::ParseContext ctx{result.issues, result.repaired};
  CustomMenu menu;
  if (!codec::parseMenu(root, "menu", false, ctx, menu, result.error)) return result;
  std::string reason;
  menu.id = "menu.1";  // placeholder so validateMenu can check the rest; the set assigns the real id
  menu.serial = 1;
  if (!validateMenu(menu, reason)) {
    result.error = reason;
    return result;
  }
  menu.id.clear();
  menu.serial = 0;
  result.menu = std::move(menu);
  result.ok = true;
  return result;
}

std::filesystem::path withMenuExtension(const std::filesystem::path& path) {
  if (path.empty() || path.has_extension()) return path;
  std::filesystem::path out = path;
  out += kMenuFileExtension;
  return out;
}

bool saveMenuFile(const CustomMenu& menu, const std::filesystem::path& path, std::string& error) {
  return writeTextFileAtomic(path, exportMenuFile(menu), error);
}

MenuParseResult loadMenuFile(const std::filesystem::path& path) {
  MenuParseResult result;
  std::string text;
  if (!readTextFile(path, kMaxMenuFileBytes, text, result.error)) return result;
  return parseMenuFile(text);
}

ImportMenuResult importMenuText(CustomMenuSet& set, std::string_view text, CollisionPolicy policy) {
  ImportMenuResult result;
  MenuParseResult parsed = parseMenuFile(text);
  result.issues = std::move(parsed.issues);
  if (!parsed.ok) {
    result.error = parsed.error;
    return result;
  }
  result.adopted = set.adopt(std::move(parsed.menu), policy);
  if (!result.adopted.ok) {
    result.error = result.adopted.reason;
    return result;
  }
  result.ok = true;
  return result;
}

ImportMenuResult importMenuFile(CustomMenuSet& set, const std::filesystem::path& path, CollisionPolicy policy) {
  ImportMenuResult result;
  std::string text;
  if (!readTextFile(path, kMaxMenuFileBytes, text, result.error)) return result;
  return importMenuText(set, text, policy);
}

}  // namespace r1ui::commands::custommenu

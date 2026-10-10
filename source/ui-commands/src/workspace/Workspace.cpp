// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the .r1ws container of Workspace.h: part validation, canonical re-serialisation of a JSON value,
//   export, strict parse, and the file save/load wrappers.
// Invariants: a part is accepted only when it parses as a JSON object within the part limits; what goes
//   into the file and what comes back out of parseWorkspace is the same canonical compact text, so a
//   save/load/save cycle is byte-stable; the recursion depth of the serialiser is bounded by the parser's
//   depth limit.
// Callers: hosts, WorkspaceFolder, tests.
#include "r1ui/commands/workspace/Workspace.h"

#include <cmath>

#include "r1ui/commands/custommenu/CustomMenu.h"
#include "r1ui/commands/custommenu/TextFile.h"
#include "r1ui/core/Json.h"
#include "r1ui/core/JsonWriter.h"

namespace r1ui::commands::workspace {

namespace {

using core::JsonType;
using core::JsonValue;

// Compact JSON for a parsed value (keys in document order).
void write(std::string& out, const JsonValue& v) {
  switch (v.type()) {
    case JsonType::Null: out += "null"; return;
    case JsonType::Bool: out += v.boolValue() ? "true" : "false"; return;
    case JsonType::Number: core::appendNumber(out, v.numberValue()); return;
    case JsonType::String: core::appendQuoted(out, v.stringValue()); return;
    case JsonType::Array: {
      out += '[';
      for (size_t i = 0; i < v.size(); ++i) {
        if (i != 0) out += ',';
        write(out, v.child(i));
      }
      out += ']';
      return;
    }
    case JsonType::Object: {
      out += '{';
      for (size_t i = 0; i < v.size(); ++i) {
        if (i != 0) out += ',';
        core::appendQuoted(out, v.keyAt(i));
        out += ':';
        write(out, v.child(i));
      }
      out += '}';
      return;
    }
  }
}

// Validates one part and returns its canonical text.
bool canonicalPart(std::string_view part, const char* what, std::string& canonical, std::string& error) {
  core::JsonLimits limits;
  limits.maxDepth = kMaxPartDepth;
  limits.maxInputBytes = kMaxPartBytes;
  limits.maxNodes = kMaxWorkspaceNodes;
  const core::JsonResult parsed = core::parseJson(part, limits);
  if (!parsed.ok()) {
    error = std::string("the ") + what + " part is not valid JSON: " + parsed.error.message;
    return false;
  }
  if (!parsed.value->isObject()) {
    error = std::string("the ") + what + " part must be a JSON object";
    return false;
  }
  canonical.clear();
  write(canonical, *parsed.value);
  return true;
}

struct PartRef {
  const char* key;
  std::optional<std::string> Workspace::*member;
};
constexpr PartRef kParts[] = {{"layout", &Workspace::layout}, {"menus", &Workspace::menus}, {"customization", &Workspace::customization}, {"keybindings", &Workspace::keybindings}};

}  // namespace

bool exportWorkspace(const Workspace& workspace, std::string& text, std::string& error) {
  const std::string name = custommenu::cleanName(workspace.name);
  if (name.empty()) {
    error = "give the workspace a name";
    return false;
  }
  std::string out = "{\"format\":\"";
  out += kWorkspaceFormat;
  out += "\",\"version\":" + std::to_string(kWorkspaceVersion) + ",\"name\":";
  core::appendQuoted(out, name);
  for (const PartRef& part : kParts) {
    const std::optional<std::string>& value = workspace.*(part.member);
    if (!value) continue;
    std::string canonical;
    if (!canonicalPart(*value, part.key, canonical, error)) return false;
    out += ",\"";
    out += part.key;
    out += "\":";
    out += canonical;
  }
  out += "}\n";
  if (out.size() > kMaxWorkspaceBytes) {
    error = "the workspace is larger than the " + std::to_string(kMaxWorkspaceBytes) + " byte limit";
    return false;
  }
  text = std::move(out);
  return true;
}

WorkspaceParseResult parseWorkspace(std::string_view text) {
  WorkspaceParseResult result;
  if (text.size() > kMaxWorkspaceBytes) {
    result.error = "the file is larger than the " + std::to_string(kMaxWorkspaceBytes) + " byte limit";
    return result;
  }
  core::JsonLimits limits;
  limits.maxDepth = kMaxPartDepth + 2;
  limits.maxInputBytes = kMaxWorkspaceBytes;
  limits.maxNodes = kMaxWorkspaceNodes;
  const core::JsonResult parsed = core::parseJson(text, limits);
  if (!parsed.ok()) {
    result.error = "not valid JSON: " + parsed.error.message;
    return result;
  }
  const JsonValue& root = *parsed.value;
  if (!root.isObject()) {
    result.error = "the file must contain a JSON object";
    return result;
  }
  const JsonValue* format = root.find("format");
  if (format == nullptr || !format->isString() || format->stringValue() != kWorkspaceFormat) {
    result.error = "not a workspace file";
    return result;
  }
  const JsonValue* version = root.find("version");
  const double v = version != nullptr && version->type() == JsonType::Number ? version->numberValue() : 0.0;
  if (!(v >= 1.0 && v <= 1.0e6) || std::floor(v) != v) {
    result.error = "the file has no valid version";
    return result;
  }
  if (v > kWorkspaceVersion) {
    result.error = "the file was written by a newer version";
    return result;
  }
  const JsonValue* name = root.find("name");
  if (name == nullptr || !name->isString() || custommenu::cleanName(name->stringValue()).empty()) {
    result.error = "the workspace has no usable name";
    return result;
  }
  Workspace workspace;
  workspace.name = custommenu::cleanName(name->stringValue());
  for (const PartRef& part : kParts) {
    const JsonValue* value = root.find(part.key);
    if (value == nullptr) continue;
    if (!value->isObject()) {
      result.error = std::string("the ") + part.key + " part must be a JSON object";
      return result;
    }
    std::string canonical;
    write(canonical, *value);
    if (canonical.size() > kMaxPartBytes) {
      result.error = std::string("the ") + part.key + " part is larger than the " + std::to_string(kMaxPartBytes) + " byte limit";
      return result;
    }
    workspace.*(part.member) = std::move(canonical);
  }
  result.workspace = std::move(workspace);
  result.ok = true;
  return result;
}

std::filesystem::path withWorkspaceExtension(const std::filesystem::path& path) {
  if (path.empty() || path.has_extension()) return path;
  std::filesystem::path out = path;
  out += kWorkspaceExtension;
  return out;
}

bool saveWorkspaceFile(const Workspace& workspace, const std::filesystem::path& path, std::string& error) {
  std::string text;
  if (!exportWorkspace(workspace, text, error)) return false;
  return custommenu::writeTextFileAtomic(path, text, error);
}

WorkspaceParseResult loadWorkspaceFile(const std::filesystem::path& path) {
  WorkspaceParseResult result;
  std::string text;
  if (!custommenu::readTextFile(path, kMaxWorkspaceBytes, text, result.error)) return result;
  return parseWorkspace(text);
}

}  // namespace r1ui::commands::workspace

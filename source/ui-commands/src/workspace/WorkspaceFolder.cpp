// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: WorkspaceFolder and the key rules of WorkspaceFolder.h.
// Invariants: every path built here is root / (validated key + ".r1ws"); directory iteration uses the
//   error_code overloads and skips entries it cannot read; save is atomic through TextFile.h.
// Callers: hosts, tests.
#include "r1ui/commands/workspace/WorkspaceFolder.h"

#include <algorithm>
#include <cctype>
#include <system_error>

#include "r1ui/commands/custommenu/CustomMenu.h"
#include "r1ui/commands/custommenu/TextFile.h"

namespace r1ui::commands::workspace {

namespace {

bool allowedChar(char c) {
  const unsigned char u = static_cast<unsigned char>(c);
  return (u < 0x80 && std::isalnum(u)) || c == ' ' || c == '_' || c == '-' || c == '.' || c == '(' || c == ')';
}

std::string upper(std::string text) {
  for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return text;
}

// CON, PRN, AUX, NUL, COM1..9, LPT1..9, with or without an extension.
bool isDeviceName(const std::string& key) {
  const std::string stem = upper(key.substr(0, key.find('.')));
  std::string trimmedStem = stem;
  while (!trimmedStem.empty() && trimmedStem.back() == ' ') trimmedStem.pop_back();
  if (trimmedStem == "CON" || trimmedStem == "PRN" || trimmedStem == "AUX" || trimmedStem == "NUL") return true;
  return trimmedStem.size() == 4 && (trimmedStem.rfind("COM", 0) == 0 || trimmedStem.rfind("LPT", 0) == 0) && trimmedStem[3] >= '1' && trimmedStem[3] <= '9';
}

bool hasExtension(const std::filesystem::path& p) {
  std::string ext = p.extension().string();
  for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext == kWorkspaceExtension;
}

}  // namespace

bool isValidWorkspaceKey(const std::string& key) {
  if (key.empty() || key.size() > kMaxKeyBytes) return false;
  if (!std::all_of(key.begin(), key.end(), allowedChar)) return false;
  if (key.front() == ' ' || key.front() == '.' || key.back() == ' ' || key.back() == '.') return false;
  if (key.find("..") != std::string::npos) return false;
  return !isDeviceName(key);
}

std::string workspaceKeyFor(const std::string& name) {
  std::string key;
  for (const char c : custommenu::cleanName(name)) {
    if (allowedChar(c)) key += c;
  }
  while (key.find("..") != std::string::npos) key.erase(key.find(".."), 1);
  while (!key.empty() && (key.front() == ' ' || key.front() == '.')) key.erase(key.begin());
  if (key.size() > kMaxKeyBytes) key.resize(kMaxKeyBytes);
  while (!key.empty() && (key.back() == ' ' || key.back() == '.')) key.pop_back();
  if (key.empty()) key = "workspace";
  if (isDeviceName(key)) key = "workspace-" + key;
  return key;
}

std::filesystem::path WorkspaceFolder::pathFor(const std::string& key) const { return root_ / (key + kWorkspaceExtension); }

bool WorkspaceFolder::exists(const std::string& key) const {
  if (!isValidWorkspaceKey(key)) return false;
  std::error_code ec;
  return std::filesystem::is_regular_file(pathFor(key), ec);
}

std::vector<WorkspaceInfo> WorkspaceFolder::list() const {
  std::vector<WorkspaceInfo> out;
  std::error_code ec;
  std::filesystem::directory_iterator it(root_, std::filesystem::directory_options::skip_permission_denied, ec);
  if (ec) return out;
  for (const std::filesystem::directory_iterator end; it != end && out.size() < kMaxListed; it.increment(ec)) {
    if (ec) break;
    std::error_code fileError;
    if (!it->is_regular_file(fileError) || fileError || !hasExtension(it->path())) continue;
    WorkspaceInfo info;
    info.key = it->path().stem().string();
    if (!isValidWorkspaceKey(info.key)) continue;
    const auto size = it->file_size(fileError);
    info.sizeBytes = fileError ? 0 : static_cast<uint64_t>(size);
    if (fileError) {
      info.error = "cannot read the file: " + fileError.message();
    } else if (info.sizeBytes > kMaxWorkspaceBytes) {
      info.error = "the file is larger than the workspace limit";
    } else {
      const WorkspaceParseResult parsed = loadWorkspaceFile(it->path());
      info.valid = parsed.ok;
      info.error = parsed.error;
      if (parsed.ok) info.name = parsed.workspace.name;
    }
    out.push_back(std::move(info));
  }
  std::sort(out.begin(), out.end(), [](const WorkspaceInfo& a, const WorkspaceInfo& b) {
    const std::string& left = a.valid ? a.name : a.key;
    const std::string& right = b.valid ? b.name : b.key;
    const std::string l = upper(left);
    const std::string r = upper(right);
    return l != r ? l < r : a.key < b.key;
  });
  return out;
}

SaveResult WorkspaceFolder::save(const Workspace& workspace, SaveMode mode) {
  SaveResult result;
  std::string text;
  if (!exportWorkspace(workspace, text, result.error)) return result;
  std::string key = workspaceKeyFor(workspace.name);
  if (mode == SaveMode::NewOnly && exists(key)) {
    const std::string base = key;
    bool found = false;
    for (int n = 2; n < 1000 && !found; ++n) {
      const std::string suffix = " (" + std::to_string(n) + ")";
      std::string stem = base.substr(0, kMaxKeyBytes - suffix.size());
      while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.')) stem.pop_back();
      const std::string candidate = stem + suffix;
      if (isValidWorkspaceKey(candidate) && !exists(candidate)) {
        key = candidate;
        found = true;
      }
    }
    if (!found) {
      result.error = "there is no free file name for the workspace";
      return result;
    }
  }
  if (!custommenu::writeTextFileAtomic(pathFor(key), text, result.error)) return result;
  result.ok = true;
  result.key = key;
  return result;
}

WorkspaceParseResult WorkspaceFolder::load(const std::string& key) const {
  if (!isValidWorkspaceKey(key)) {
    WorkspaceParseResult bad;
    bad.error = "'" + key + "' is not a valid workspace key";
    return bad;
  }
  return loadWorkspaceFile(pathFor(key));
}

bool WorkspaceFolder::remove(const std::string& key, std::string& error) {
  if (!isValidWorkspaceKey(key)) {
    error = "'" + key + "' is not a valid workspace key";
    return false;
  }
  std::error_code ec;
  if (!std::filesystem::is_regular_file(pathFor(key), ec)) {
    error = "there is no workspace '" + key + "'";
    return false;
  }
  if (!std::filesystem::remove(pathFor(key), ec) || ec) {
    error = "cannot remove the workspace: " + ec.message();
    return false;
  }
  return true;
}

}  // namespace r1ui::commands::workspace

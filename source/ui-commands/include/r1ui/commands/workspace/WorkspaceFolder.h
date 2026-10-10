// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: WorkspaceFolder, the folder of saved workspaces (the host passes <app data>/workspaces): list,
//   save, load, rename-free removal of "<key>.r1ws" files.
// Why: owner requirement 2026-10-10: list and remove saved workspaces from a workspaces folder under the
//   app data folder. The user thinks in names ("Sculpting", "Review"); the file system needs safe file
//   names, so each workspace has a key derived from its name.
// Callers: the host's workspace menu and picker, tests. Calls: Workspace.h, TextFile.h.
// Keys: 1..64 ASCII characters from letters, digits, space and "_-.()", never starting or ending with a
//   space or a dot, never containing "..", never a Windows device name (CON, NUL, COM1, ...). A key is
//   re-validated on every call, so a key received from outside cannot leave the folder.
// Save: the key comes from the name (non-ASCII and other characters dropped; "workspace" when nothing is
//   left). NewOnly never overwrites: when the key is taken it appends " (2)", " (3)" ... Overwrite
//   replaces the file of that key. The returned key names the file written.
// List: every "*.r1ws" whose stem is a valid key, at most kMaxListed, sorted by name (ASCII
//   case-insensitive); a file that cannot be read or parsed is listed with valid = false and the reason,
//   so the user can still remove it. Files larger than the workspace limit are not read.
// Failure behavior: nothing throws; failures come back as an error sentence.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "r1ui/commands/workspace/Workspace.h"

namespace r1ui::commands::workspace {

inline constexpr size_t kMaxListed = 1000;
inline constexpr size_t kMaxKeyBytes = 64;

bool isValidWorkspaceKey(const std::string& key);
// The key a name maps to before collisions are resolved; never empty, always valid.
std::string workspaceKeyFor(const std::string& name);

struct WorkspaceInfo {
  std::string key;
  std::string name;      // empty when !valid
  uint64_t sizeBytes = 0;
  bool valid = false;
  std::string error;     // why a file is not valid
};

enum class SaveMode : uint8_t { NewOnly, Overwrite };

struct SaveResult {
  bool ok = false;
  std::string error;
  std::string key;
};

class WorkspaceFolder {
 public:
  explicit WorkspaceFolder(std::filesystem::path root) : root_(std::move(root)) {}
  const std::filesystem::path& root() const { return root_; }
  std::filesystem::path pathFor(const std::string& key) const;

  std::vector<WorkspaceInfo> list() const;
  SaveResult save(const Workspace& workspace, SaveMode mode = SaveMode::NewOnly);
  WorkspaceParseResult load(const std::string& key) const;
  bool exists(const std::string& key) const;
  // False with `error` when the key is invalid or the file does not exist / cannot be removed.
  bool remove(const std::string& key, std::string& error);

 private:
  std::filesystem::path root_;
};

}  // namespace r1ui::commands::workspace

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the .r1ws custom workspace container (slice 5.18): one file that packs a user's whole setup, so
//   it can be saved, carried to another machine and loaded after a clean installation.
// Why: owner requirement 2026-10-10: "Save custom workspace" packs the panel layout, the custom menus,
//   the customization delta (menu bar and toolbars) and the keybinding overrides into one file;
//   "Load workspace" hands the parts back to the host to apply. The container does not interpret the
//   parts: each is a JSON document produced by the module that owns it (the dock layout text, the
//   exportSet text of CustomMenuIo.h, exportCustomization, exportOverrides) and validated here only as
//   "well-formed JSON object within the size and depth limits".
// Callers: the host (save/load commands, workspace picker), tests. Calls: core JSON, TextFile.h.
//
// File (version 1), one JSON object:
//   {"format":"r1ui-workspace","version":1,"name":TEXT,
//    "layout":OBJECT,"menus":OBJECT,"customization":OBJECT,"keybindings":OBJECT}
//   Every part is optional (a host without keybinding overrides leaves it out); a part present must be a
//   JSON object. The parts are embedded as JSON, not as strings, so the file stays readable; they are
//   re-serialised compactly with their member order preserved and numbers in shortest round-trip form
//   (integers above 2^53 are not preserved, none of the owners' formats uses them). Unknown members are
//   ignored.
// Limits: whole file 32 MiB; each part 16 MiB and 128 levels deep; 1 000 000 JSON values in total; the
//   name is cleaned like a menu name (kMaxNameBytes). A file outside the limits, not JSON (including
//   invalid UTF-8 and duplicate keys), of another format, with a newer version, without a usable name or
//   with a part that is not an object is rejected as a whole; the caller's state is never touched by
//   this module.
// Failure behavior: nothing throws; failures come back as an error sentence. Writes are atomic.
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace r1ui::commands::workspace {

inline constexpr const char* kWorkspaceFormat = "r1ui-workspace";
inline constexpr int kWorkspaceVersion = 1;
inline constexpr const char* kWorkspaceExtension = ".r1ws";
inline constexpr size_t kMaxWorkspaceBytes = size_t{32} << 20;
inline constexpr size_t kMaxPartBytes = size_t{16} << 20;
inline constexpr size_t kMaxPartDepth = 128;
inline constexpr size_t kMaxWorkspaceNodes = 1'000'000;

// The user's setup. Each part is JSON text (an object) or absent.
struct Workspace {
  std::string name;
  std::optional<std::string> layout;         // dock layout text (ui-dock layout JSON)
  std::optional<std::string> menus;          // custom menus set text (exportSet)
  std::optional<std::string> customization;  // menu bar / toolbar delta (exportCustomization)
  std::optional<std::string> keybindings;    // keybinding overrides (exportOverrides)
  friend bool operator==(const Workspace&, const Workspace&) = default;
};

struct WorkspaceParseResult {
  bool ok = false;
  std::string error;
  Workspace workspace;  // parts are compact canonical JSON text
};

// Checks the parts and the name and returns the file text; false with `error` (and `text` untouched)
// when a part is not a JSON object within the limits, the name is unusable or the result is too large.
bool exportWorkspace(const Workspace& workspace, std::string& text, std::string& error);
WorkspaceParseResult parseWorkspace(std::string_view text);

// The path with ".r1ws" appended when it has no extension.
std::filesystem::path withWorkspaceExtension(const std::filesystem::path& path);
bool saveWorkspaceFile(const Workspace& workspace, const std::filesystem::path& path, std::string& error);
WorkspaceParseResult loadWorkspaceFile(const std::filesystem::path& path);

}  // namespace r1ui::commands::workspace

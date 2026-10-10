// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: two bounded file primitives shared by the custom menu files (.r1mn), the menu set store and the
//   workspace files (.r1ws): read a whole file up to a byte limit, and write a whole file atomically.
// Why: both file kinds are written by the user's actions and read after a clean installation; a crash
//   or a full disk must never leave half a file, and a huge or missing file must be reported, not
//   crash the loader.
// Callers: CustomMenuIo, WorkspaceFile, WorkspaceFolder, tests. Calls: std::filesystem (error_code
//   overloads only).
// Atomic write: the text goes to "<path>.tmp" (created next to the target), is flushed and then renamed
//   over the target, so a reader sees the old or the new file, never half of one. A failed write removes
//   the temporary file and leaves the target untouched. Missing parent folders are created.
// Failure behavior: nothing throws; `error` says why in a sentence.
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace r1ui::commands::custommenu {

// False when the file is missing, unreadable or larger than `maxBytes` (it is not read then).
bool readTextFile(const std::filesystem::path& path, size_t maxBytes, std::string& text, std::string& error);

bool writeTextFileAtomic(const std::filesystem::path& path, std::string_view text, std::string& error);

}  // namespace r1ui::commands::custommenu

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the text hygiene shared by the command module: UTF-8 validation and sanitising to a byte
//   limit, plus the identifier syntax of command ids, context names and icon names.
// Why: command labels, ids and chord texts come from modules, user files and import dialogs; every
//   boundary needs the same rule (well-formed UTF-8, bounded length, no control characters) so
//   menus, tooltips and the editor never receive a string that can corrupt layout or a file.
// Callers: CommandRegistry (registration), chord parsing, override import, widgets. Calls: nothing.
// Failure behavior: nothing here throws; invalid input is replaced (sanitizeText) or reported
//   (isValidUtf8, isValidIdentifier).
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace r1ui::commands {

// True when `text` is well-formed UTF-8 (no overlong forms, surrogates or values above U+10FFFF).
bool isValidUtf8(std::string_view text);

// Copy of `text` with invalid sequences replaced by U+FFFD and control characters (below U+0020 and
// U+007F) replaced by a space, cut to at most `maxBytes` on a sequence boundary.
std::string sanitizeText(std::string_view text, size_t maxBytes);

// Ids, context names and categories: 1..maxBytes of [A-Za-z0-9._:/-]. Icon names are the stricter
// [A-Za-z0-9_-] used by the icon cache.
bool isValidIdentifier(std::string_view text, size_t maxBytes);
bool isValidIconName(std::string_view text);

}  // namespace r1ui::commands

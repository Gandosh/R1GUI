// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the JSON encoding of one CustomMenu shared by the .r1mn file and the set store: writing the
//   menu's members in their fixed order, and parsing a menu object with repairs and issues.
// Why: the two file kinds carry the same menu object; keeping one codec means one place defines the
//   member names, the limits and the repair rules.
// Callers: CustomMenuFile.cpp, CustomMenuStore.cpp (internal to the module; not a public header).
#pragma once

#include <string>
#include <vector>

#include "r1ui/commands/custommenu/CustomMenuIo.h"
#include "r1ui/core/Json.h"

namespace r1ui::commands::custommenu::codec {

// Appends the members of `menu` (kind, name, settings, entries), one per line, each line starting with
// `indent`; every member ends with a comma except the last. `withId` also writes "id" first.
void writeMembers(std::string& out, const CustomMenu& menu, bool withId, const char* indent);

struct ParseContext {
  std::vector<MenuIssue>& issues;
  size_t& repaired;
  void issue(const std::string& where, std::string message, bool isRepair = true) {
    if (isRepair) ++repaired;
    if (issues.size() < 256) issues.push_back({where, std::move(message)});
  }
};

// Parses the object into `menu` (id and serial are left empty unless `wantId`, in which case a missing or
// malformed id leaves them empty and the caller assigns one). Returns false and sets `error` when the
// object cannot be a menu at all.
bool parseMenu(const core::JsonValue& obj, const std::string& at, bool wantId, ParseContext& ctx, CustomMenu& menu, std::string& error);

}  // namespace r1ui::commands::custommenu::codec

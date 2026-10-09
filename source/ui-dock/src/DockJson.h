// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the small JSON helpers shared by the layout reader and writer: member lookup with type
//   checks, strict member lists, bounded integer reading, key writing and UTF-8 safe truncation.
// Why: DockSerialize.cpp (reader) and DockSerializeWrite.cpp (writer) must agree on the schema's
//   vocabulary; keeping the primitives in one internal header stops the two from drifting.
// Callers: ui-dock sources only (not a public header).
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

#include "r1ui/core/Json.h"
#include "r1ui/core/JsonWriter.h"

namespace r1ui::dock::detail {

// Rejects members other than `allowed`, so a typo or a field of a newer schema is never silently
// ignored. Returns an error message or an empty string.
std::string checkMembers(const core::JsonValue& object, std::initializer_list<const char*> allowed, const char* what);

// Looks up `name` and checks its type; on failure sets `error` and returns nullptr.
const core::JsonValue* member(const core::JsonValue& object, const char* name, core::JsonType type, std::string& error,
                              const char* what);

// A non-negative integral number not above `limit`.
bool readIndex(const core::JsonValue& value, double limit, double& out);

// `text` cut to at most `maxBytes` on a UTF-8 boundary with control characters replaced by spaces.
std::string cleanText(std::string_view text, size_t maxBytes);

inline void writeKey(std::string& out, const char* key) {
  core::appendQuoted(out, key);
  out.push_back(':');
}

}  // namespace r1ui::dock::detail

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the small helpers the editing files of Customization share: result builders, the mapping from a
//   report entry to an error and a sentence, text cleaning and the delta clean-up helpers.
// Why: the operations (CustomizationEdit.cpp) and the toolbar and panel operations
//   (CustomizationContainers.cpp) validate and report in the same way.
// Callers: CustomizationEdit.cpp, CustomizationContainers.cpp.
#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>

#include "r1ui/commands/Text.h"
#include "r1ui/commands/customize/Customization.h"

namespace r1ui::commands::customize::detail {

using Code = ReportEntry::Code;

inline EditResult failure(EditError error, std::string reason) {
  EditResult r;
  r.error = error;
  r.reason = std::move(reason);
  return r;
}

inline EditResult success(std::string id = {}) {
  EditResult r;
  r.ok = true;
  r.id = std::move(id);
  return r;
}

// Blocking problems are those that would make the requested change not take effect as asked.
inline bool blocking(Code code) { return code != Code::MissingCommand; }

inline EditError errorFor(Code code) {
  switch (code) {
    case Code::Locked: return EditError::Locked;
    case Code::Cycle: return EditError::Cycle;
    case Code::IllegalParent: return EditError::Illegal;
    case Code::MissingParent: return EditError::UnknownParent;
    case Code::MissingNode:
    case Code::MissingAnchor: return EditError::UnknownNode;
    case Code::DuplicateId: return EditError::Duplicate;
    case Code::Invalid: return EditError::OutOfRange;
    case Code::Limit: return EditError::LimitReached;
    default: return EditError::Illegal;
  }
}

inline std::string reasonFor(const ReportEntry& e) {
  switch (e.code) {
    case Code::Locked: return "That part of the interface is locked by the application.";
    case Code::Cycle: return "An item cannot be moved into itself.";
    case Code::IllegalParent: return "That item cannot go there.";
    case Code::MissingParent: return "The target no longer exists.";
    case Code::MissingAnchor: return "The item next to the drop position no longer exists.";
    case Code::Limit: return "The menu is too large or nested too deeply.";
    default: return e.detail;
  }
}

inline std::string trimmed(std::string text) {
  const auto space = [](char c) { return c == ' ' || c == '\t'; };
  while (!text.empty() && space(text.back())) text.pop_back();
  size_t first = 0;
  while (first < text.size() && space(text[first])) ++first;
  return text.substr(first);
}

inline std::string cleanLabel(const std::string& text) { return trimmed(sanitizeText(text, kMaxLabelBytes)); }

inline std::string lowerAscii(std::string text) {
  for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

inline void pruneEdit(Delta& d, const std::string& id) {
  const auto it = d.edits.find(id);
  if (it != d.edits.end() && it->second.empty()) d.edits.erase(it);
}

inline void collectNodeIds(const Node& node, std::unordered_set<std::string>& out) {
  out.insert(node.id);
  for (const Node& c : node.children) collectNodeIds(c, out);
}

}  // namespace r1ui::commands::customize::detail

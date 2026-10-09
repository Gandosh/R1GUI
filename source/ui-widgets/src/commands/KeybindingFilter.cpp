// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of KeybindingFilter.h.
// Invariants: pure; bounded by kMaxQueryBytes and kMaxQueryTerms times the haystack sizes.
// Callers: KeybindingEditor, tests.
#include "r1ui/widgets/commands/KeybindingFilter.h"

namespace r1ui::widgets {

namespace {

char fold(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool containsFolded(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) return true;
  if (needle.size() > haystack.size()) return false;
  for (size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
    size_t i = 0;
    while (i < needle.size() && fold(haystack[start + i]) == fold(needle[i])) ++i;
    if (i == needle.size()) return true;
  }
  return false;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

}  // namespace

bool matchesKeybindingQuery(std::string_view query, const KeybindingRowText& row) {
  if (query.size() > kMaxQueryBytes) query = query.substr(0, kMaxQueryBytes);
  size_t terms = 0;
  size_t i = 0;
  while (i < query.size() && terms < kMaxQueryTerms) {
    while (i < query.size() && isSpace(query[i])) ++i;
    const size_t start = i;
    while (i < query.size() && !isSpace(query[i])) ++i;
    if (i == start) break;
    const std::string_view term = query.substr(start, i - start);
    ++terms;
    const bool found = containsFolded(row.label, term) || containsFolded(row.category, term) || containsFolded(row.id, term) || containsFolded(row.chord0, term) ||
                       containsFolded(row.chord1, term);
    if (!found) return false;
  }
  return true;
}

}  // namespace r1ui::widgets

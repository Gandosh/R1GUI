// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of ActionModel.h.
// Invariants: every function is pure and total; no result holds a view into its arguments; sanitised
//   text is well-formed UTF-8 within its limit.
// Callers: ActionList, ActionListView, HotkeyEditor, tests.
#include "r1ui/widgets/actions/ActionModel.h"

#include <algorithm>

#include "r1ui/commands/Overrides.h"
#include "r1ui/commands/Text.h"

namespace r1ui::widgets {

namespace {

char fold(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

std::string joinShortcuts(const commands::Keymap& keymap, const std::string& id, bool both) {
  std::string text;
  for (int slot = 0; slot < commands::kSlotCount; ++slot) {
    const std::optional<commands::ChordSequence> chord = keymap.effective(id, slot);
    if (!chord || chord->empty()) continue;
    const std::string part = commands::formatSequence(*chord);
    if (part.empty()) continue;
    if (!text.empty()) text += ", ";
    text += part;
    if (!both) break;
  }
  return text;
}

}  // namespace

std::string foldActionText(std::string_view text) {
  std::string out(text);
  for (char& c : out) c = fold(c);
  return out;
}

ActionInfo sanitizedAction(const ActionInfo& action) {
  ActionInfo out;
  out.id = commands::sanitizeText(action.id, kMaxActionIdBytes);
  out.label = commands::sanitizeText(action.label, kMaxActionLabelBytes);
  out.description = commands::sanitizeText(action.description, kMaxActionDescriptionBytes);
  out.category = commands::sanitizeText(action.category, kMaxActionCategoryBytes);
  if (out.category.find_first_not_of(' ') == std::string::npos) out.category = "General";
  out.icon = commands::sanitizeText(action.icon, kMaxActionIconBytes);
  out.shortcut = commands::sanitizeText(action.shortcut, kMaxActionShortcutBytes);
  out.enabled = action.enabled;
  return out;
}

std::vector<ActionInfo> actionsFromRegistry(const commands::CommandRegistry& registry, const commands::Keymap& keymap, const ActionSourceOptions& options) {
  std::vector<ActionInfo> out;
  out.reserve(std::min(registry.size(), kMaxActions));
  for (const commands::CommandDef* def : registry.commands()) {
    if (out.size() >= kMaxActions) break;
    if (def->hiddenFromEditor && !options.includeHidden) continue;
    ActionInfo action;
    action.id = def->id;
    action.label = def->label;
    action.description = def->description;
    action.category = def->category;
    action.icon = def->icon;
    action.shortcut = joinShortcuts(keymap, def->id, options.bothShortcuts);
    action.enabled = def->isEnabled();
    out.push_back(sanitizedAction(action));
  }
  if (options.sortByLabel) {
    std::stable_sort(out.begin(), out.end(), [](const ActionInfo& a, const ActionInfo& b) { return foldActionText(a.label) < foldActionText(b.label); });
  }
  return out;
}

std::vector<std::string> commandsWithoutDescription(const commands::CommandRegistry& registry) {
  std::vector<std::string> ids;
  for (const commands::CommandDef* def : registry.commands()) {
    const bool blank = def->description.find_first_not_of(" \t\r\n") == std::string::npos;
    if (blank) ids.push_back(def->id);
  }
  return ids;
}

ActionQuery parseActionQuery(std::string_view text) {
  ActionQuery query;
  if (text.size() > kMaxActionQueryBytes) text = text.substr(0, kMaxActionQueryBytes);
  size_t i = 0;
  while (i < text.size() && query.terms.size() < kMaxActionQueryTerms) {
    while (i < text.size() && isSpace(text[i])) ++i;
    const size_t start = i;
    while (i < text.size() && !isSpace(text[i])) ++i;
    if (i == start) break;
    query.terms.push_back(foldActionText(text.substr(start, i - start)));
  }
  return query;
}

std::string actionHaystack(const ActionInfo& action, bool includeShortcut) {
  std::string hay;
  hay.reserve(action.label.size() + action.id.size() + action.description.size() + action.category.size() + 3);
  hay += action.label;
  hay += ' ';
  hay += action.id;
  hay += ' ';
  hay += action.description;
  hay += ' ';
  hay += action.category;
  if (includeShortcut) {
    hay += ' ';
    hay += action.shortcut;
  }
  return foldActionText(hay);
}

bool matchesAction(const ActionQuery& query, std::string_view haystack) {
  for (const std::string& term : query.terms) {
    if (haystack.find(term) == std::string_view::npos) return false;
  }
  return true;
}

size_t findFolded(std::string_view text, std::string_view needle) {
  if (needle.empty() || needle.size() > text.size()) return std::string_view::npos;
  for (size_t start = 0; start + needle.size() <= text.size(); ++start) {
    size_t i = 0;
    while (i < needle.size() && fold(text[start + i]) == needle[i]) ++i;
    if (i == needle.size()) return start;
  }
  return std::string_view::npos;
}

}  // namespace r1ui::widgets

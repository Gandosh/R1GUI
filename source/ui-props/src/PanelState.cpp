// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the implementation of PanelState.h: search word handling, collapse bookkeeping with the
//   save/restore around a search, and the filtering that builds a PanelModel.
// Why: see PanelState.h. Everything is total over hostile input (long or invalid search text, thousands
//   of rows); the cost of building a model is one state() read per row.
// Callers: PropertyPanel, tests.
#include "r1ui/props/PanelState.h"

#include <algorithm>
#include <map>

namespace r1ui::props {

// ---- search and collapse ---------------------------------------------------------------------------------

void PanelState::setSearch(std::string_view text) {
  if (text.size() > kMaxSearchBytes) text = text.substr(0, kMaxSearchBytes);
  const bool was = searching();
  std::vector<std::string> words;
  size_t pos = 0;
  while (pos < text.size() && words.size() < kMaxSearchWords) {
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) ++pos;
    size_t end = pos;
    while (end < text.size() && text[end] != ' ' && text[end] != '\t') ++end;
    if (end > pos) words.push_back(foldAscii(text.substr(pos, end - pos)));
    pos = end;
  }
  const bool now = !words.empty();
  if (!was && now) savedCollapsed_ = collapsed_;      // rule 59: remember the user's expansion
  if (was && !now) collapsed_ = savedCollapsed_;      // and bring it back when the search ends
  search_ = std::string(text);
  words_ = std::move(words);
}

bool PanelState::setCategoryCollapsed(const std::string& category, bool collapsed) {
  if (searching()) return false;
  if (collapsed) collapsed_.insert(category);
  else collapsed_.erase(category);
  return true;
}

// ---- model --------------------------------------------------------------------------------------------------

namespace {

bool matches(const PropertyDescriptor& d, const std::vector<std::string>& words) {
  if (words.empty()) return true;
  std::string haystack;
  haystack.reserve(d.label.size() + d.name.size() + d.category.size() + d.group.size() + d.meta.tooltip.size() + 8);
  for (const std::string* part : {&d.label, &d.name, &d.category, &d.group, &d.meta.tooltip}) {
    haystack += *part;
    haystack += '\n';
  }
  return std::all_of(words.begin(), words.end(), [&](const std::string& w) { return containsFolded(haystack, w); });
}

}  // namespace

PanelModel buildPanelModel(const PropertyContext& context, const PanelState& state) {
  PanelModel model;
  model.searchVisible = context.rowCount() > 0 || state.searching();  // rules 3 and 9: no rows and no text: no search box
  std::map<std::string, size_t> byCategory;
  const bool searching = state.searching();
  for (size_t row = 0; row < context.rowCount(); ++row) {
    const PropertyDescriptor& d = context.descriptor(row);
    if (!matches(d, state.searchWords())) continue;
    const PropertyState st = context.state(row);
    if (!st.visible) continue;
    if (state.onlyModified() && !st.canReset) continue;
    const bool hideAdvanced = d.meta.advanced && !state.showAdvanced() && !searching;
    size_t slot;
    const auto found = byCategory.find(d.category);
    if (found == byCategory.end()) {
      slot = model.categories.size();
      byCategory.emplace(d.category, slot);
      PanelCategory category;
      category.name = d.category;
      category.collapsed = state.effectiveCollapsed(d.category);
      model.categories.push_back(std::move(category));
    } else {
      slot = found->second;
    }
    PanelCategory& category = model.categories[slot];
    if (hideAdvanced) {
      ++category.hiddenAdvanced;
      continue;
    }
    category.rows.push_back({row, d.meta.advanced});
    category.anyModified = category.anyModified || st.canReset;
    ++model.totalRows;
  }
  // A category that only has hidden advanced rows is not shown at all.
  std::erase_if(model.categories, [](const PanelCategory& c) { return c.rows.empty(); });
  return model;
}

}  // namespace r1ui::props

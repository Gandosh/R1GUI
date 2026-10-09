// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: PanelState (the per-panel view state: category collapse, search text, advanced toggle and the
//   modified-only filter) and buildPanelModel(), which turns a PropertyContext plus a PanelState into
//   the list of categories and rows a panel should show.
// Why: spec 09 rules 52-59, 63-64 and decision D23: which rows are visible (edit conditions, search,
//   advanced, modified-only) and which categories are open is panel logic independent of any widget.
//   Keeping it headless makes the filtering rules testable and lets another view (a list, a tree)
//   reuse them.
// Rules: categories are open by default; the search is case-insensitive over label, stored name,
//   category, group and tooltip and every space-separated word must match (rule 56); while a search is
//   active every category with a match is shown open and advanced rows are included (rule 58); the
//   collapse state before the search began is restored when the search is cleared (rule 59); rows whose
//   visibility condition is false are removed; advanced rows are listed only with showAdvanced (or while
//   searching); with onlyModified only rows whose reset affordance would show stay (rule 52).
// Callers: PropertyPanel, tests. Search text is bounded (kMaxSearchBytes, kMaxSearchWords); non-ASCII
//   bytes compare exactly (case folding is ASCII only).
#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/props/PropertyContext.h"

namespace r1ui::props {

inline constexpr size_t kMaxSearchBytes = 256;
inline constexpr size_t kMaxSearchWords = 8;

class PanelState {
 public:
  // ---- search ----
  // Sets the search text (cut to kMaxSearchBytes, invalid UTF-8 treated as bytes). The first non-empty
  // text saves the collapse state; clearing restores it.
  void setSearch(std::string_view text);
  const std::string& search() const { return search_; }
  bool searching() const { return !words_.empty(); }
  const std::vector<std::string>& searchWords() const { return words_; }

  // ---- categories ----
  // False (no change) while searching, because categories are forced open then.
  bool setCategoryCollapsed(const std::string& category, bool collapsed);
  // Stored state; use effectiveCollapsed for what a view should draw.
  bool collapsed(const std::string& category) const { return collapsed_.contains(category); }
  bool effectiveCollapsed(const std::string& category) const { return !searching() && collapsed(category); }

  // ---- filters ----
  void setShowAdvanced(bool show) { showAdvanced_ = show; }
  bool showAdvanced() const { return showAdvanced_; }
  void setOnlyModified(bool only) { onlyModified_ = only; }
  bool onlyModified() const { return onlyModified_; }

 private:
  std::string search_;
  std::vector<std::string> words_;  // folded
  std::set<std::string> collapsed_;
  std::set<std::string> savedCollapsed_;
  bool showAdvanced_ = false;
  bool onlyModified_ = false;
};

struct PanelRow {
  size_t row = 0;  // index into the context's rows
  bool advanced = false;
  friend bool operator==(const PanelRow&, const PanelRow&) = default;
};

struct PanelCategory {
  std::string name;
  bool collapsed = false;      // effective (never true while searching)
  bool anyModified = false;    // some row's reset affordance shows
  size_t hiddenAdvanced = 0;   // advanced rows left out because showAdvanced is off
  std::vector<PanelRow> rows;
  friend bool operator==(const PanelCategory&, const PanelCategory&) = default;
};

struct PanelModel {
  std::vector<PanelCategory> categories;
  size_t totalRows = 0;     // rows listed (collapsed categories included)
  bool searchVisible = false;  // false when there is nothing selected to search (rule 3/9)
  friend bool operator==(const PanelModel&, const PanelModel&) = default;
};

PanelModel buildPanelModel(const PropertyContext& context, const PanelState& state);

}  // namespace r1ui::props

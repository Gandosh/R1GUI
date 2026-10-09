// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: SelectModel, the data and navigation logic of a select list with no widget or drawing in
//   it: the entries (items, group labels, separators), the optional text filter, the visible rows,
//   stepping between enabled items, page jumps and type-ahead.
// Why: the keyboard rules of docs/spec/interaction/10-tooltips-popups-menus-modal.md (rules 52-55) and
//   01 (rules 31-39, 41) are pure index arithmetic; keeping them out of the widgets makes every rule a
//   one-line test, including hostile input (huge lists, control characters, filters matching nothing).
// Callers: Select, SelectList, tests.
// Rules: labels are sanitised on entry (invalid UTF-8 repaired, control characters dropped); the list
//   holds at most kMaxEntries entries (setEntries refuses more and keeps the old list); a filter
//   keeps items whose label contains it (ASCII case-insensitive), hides separators, and keeps a group
//   label only while one of its items is visible; stepping never wraps and skips group labels,
//   separators and disabled items; type-ahead matches a label prefix, ASCII case-insensitive.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace r1ui::widgets {

enum class SelectEntryKind : unsigned char { Item, Group, Separator };

struct SelectEntry {
  SelectEntryKind kind = SelectEntryKind::Item;
  std::string label;
  std::string value;  // returned to the application; the label when empty
  bool disabled = false;
};

class SelectModel {
 public:
  static constexpr size_t kMaxEntries = 100000;

  // Replaces the entries and clears the filter. False (nothing changed) when there are too many.
  bool setEntries(std::vector<SelectEntry> entries);
  const std::vector<SelectEntry>& entries() const { return entries_; }

  // ---- filter -------------------------------------------------------------------------------
  // Empty text shows everything. The visible rows keep the order of the entries.
  void setFilter(std::string_view text);
  const std::string& filter() const { return filter_; }
  // Indices into entries() of the rows to show.
  const std::vector<size_t>& visible() const { return visible_; }
  bool anyVisibleItem() const;

  // ---- navigation (indices into entries()) --------------------------------------------------
  static bool selectable(const SelectEntry& entry) { return entry.kind == SelectEntryKind::Item && !entry.disabled; }
  std::optional<size_t> first() const;
  std::optional<size_t> last() const;
  // The enabled visible item `delta` (+1 / -1) steps from `current`; with no current, the first
  // (delta > 0) or last. Stays on `current` at the ends (no wrap); empty when nothing is selectable.
  std::optional<size_t> step(std::optional<size_t> current, int delta) const;
  // Moves `rows` visible rows (negative = up) from `current` and lands on the nearest enabled item,
  // clamped to the first and last.
  std::optional<size_t> page(std::optional<size_t> current, int rows) const;
  // First enabled visible item whose label starts with `prefix`. A single repeated character cycles
  // through the items starting with it, after `from`; longer prefixes search from `from` inclusive.
  std::optional<size_t> typeAhead(std::string_view prefix, std::optional<size_t> from) const;

  // Entry index of the first item whose value equals `value`.
  std::optional<size_t> indexOfValue(std::string_view value) const;
  // Position of an entry index inside visible(), if it is visible.
  std::optional<size_t> rowOf(size_t entryIndex) const;

 private:
  void rebuildVisible();

  std::vector<SelectEntry> entries_;
  std::string filter_;
  std::vector<size_t> visible_;
};

// Lower-cases ASCII letters only (other bytes unchanged); used for filter and type-ahead.
std::string foldAscii(std::string_view text);

}  // namespace r1ui::widgets

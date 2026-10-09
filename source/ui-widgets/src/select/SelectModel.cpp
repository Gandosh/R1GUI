// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of SelectModel.h.
// Invariants: visible_ always holds valid, strictly increasing indices into entries_; all searches are
//   linear in the number of visible rows and never allocate per row (labels are folded one at a time).
// Callers: Select, SelectList, tests.
#include "r1ui/widgets/select/SelectModel.h"

#include <algorithm>

#include "r1ui/widgets/textinput/LineEditor.h"

namespace r1ui::widgets {

std::string foldAscii(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

bool SelectModel::setEntries(std::vector<SelectEntry> entries) {
  if (entries.size() > kMaxEntries) return false;
  for (SelectEntry& e : entries) {
    e.label = LineEditor::sanitizeLine(e.label);
    e.value = e.value.empty() ? e.label : LineEditor::sanitizeLine(e.value);
  }
  entries_ = std::move(entries);
  filter_.clear();
  rebuildVisible();
  return true;
}

void SelectModel::setFilter(std::string_view text) {
  filter_ = foldAscii(LineEditor::sanitizeLine(text));
  rebuildVisible();
}

void SelectModel::rebuildVisible() {
  visible_.clear();
  if (filter_.empty()) {
    visible_.resize(entries_.size());
    for (size_t i = 0; i < entries_.size(); ++i) visible_[i] = i;
    return;
  }
  std::optional<size_t> pendingGroup;
  for (size_t i = 0; i < entries_.size(); ++i) {
    const SelectEntry& e = entries_[i];
    if (e.kind == SelectEntryKind::Group) {
      pendingGroup = i;
    } else if (e.kind == SelectEntryKind::Item && foldAscii(e.label).find(filter_) != std::string::npos) {
      if (pendingGroup) {
        visible_.push_back(*pendingGroup);
        pendingGroup.reset();
      }
      visible_.push_back(i);
    }
  }
}

bool SelectModel::anyVisibleItem() const {
  return std::any_of(visible_.begin(), visible_.end(), [&](size_t i) { return entries_[i].kind == SelectEntryKind::Item; });
}

std::optional<size_t> SelectModel::rowOf(size_t entryIndex) const {
  const auto it = std::lower_bound(visible_.begin(), visible_.end(), entryIndex);
  if (it == visible_.end() || *it != entryIndex) return std::nullopt;
  return static_cast<size_t>(it - visible_.begin());
}

std::optional<size_t> SelectModel::first() const {
  for (const size_t i : visible_) {
    if (selectable(entries_[i])) return i;
  }
  return std::nullopt;
}

std::optional<size_t> SelectModel::last() const {
  for (auto it = visible_.rbegin(); it != visible_.rend(); ++it) {
    if (selectable(entries_[*it])) return *it;
  }
  return std::nullopt;
}

std::optional<size_t> SelectModel::step(std::optional<size_t> current, int delta) const {
  if (delta == 0) return current;
  std::optional<size_t> row;
  if (current) row = rowOf(*current);
  if (!row) return delta > 0 ? first() : last();
  const auto size = static_cast<std::ptrdiff_t>(visible_.size());
  for (auto p = static_cast<std::ptrdiff_t>(*row) + (delta > 0 ? 1 : -1); p >= 0 && p < size; p += delta > 0 ? 1 : -1) {
    const size_t entry = visible_[static_cast<size_t>(p)];
    if (selectable(entries_[entry])) return entry;
  }
  return current;
}

std::optional<size_t> SelectModel::page(std::optional<size_t> current, int rows) const {
  if (visible_.empty() || rows == 0) return current;
  const auto size = static_cast<std::ptrdiff_t>(visible_.size());
  std::ptrdiff_t from = rows > 0 ? -1 : size;
  if (current) {
    if (const std::optional<size_t> row = rowOf(*current)) from = static_cast<std::ptrdiff_t>(*row);
  }
  const std::ptrdiff_t target = std::clamp<std::ptrdiff_t>(from + rows, 0, size - 1);
  const std::ptrdiff_t dir = rows > 0 ? 1 : -1;
  for (std::ptrdiff_t p = target; p >= 0 && p < size; p += dir) {
    if (selectable(entries_[visible_[static_cast<size_t>(p)]])) return visible_[static_cast<size_t>(p)];
  }
  for (std::ptrdiff_t p = target; p >= 0 && p < size; p -= dir) {
    if (selectable(entries_[visible_[static_cast<size_t>(p)]])) return visible_[static_cast<size_t>(p)];
  }
  return current;
}

std::optional<size_t> SelectModel::typeAhead(std::string_view prefix, std::optional<size_t> from) const {
  if (prefix.empty() || visible_.empty()) return std::nullopt;
  const std::string folded = foldAscii(prefix);
  const bool repeated = folded.find_first_not_of(folded.front()) == std::string::npos;
  const std::string needle = repeated ? folded.substr(0, 1) : folded;
  const size_t count = visible_.size();
  size_t start = 0;
  if (from) {
    if (const std::optional<size_t> row = rowOf(*from)) start = repeated ? *row + 1 : *row;
  }
  for (size_t k = 0; k < count; ++k) {
    const size_t entry = visible_[(start + k) % count];
    if (!selectable(entries_[entry])) continue;
    if (foldAscii(entries_[entry].label).compare(0, needle.size(), needle) == 0) return entry;
  }
  return std::nullopt;
}

std::optional<size_t> SelectModel::indexOfValue(std::string_view value) const {
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (entries_[i].kind == SelectEntryKind::Item && entries_[i].value == value) return i;
  }
  return std::nullopt;
}

}  // namespace r1ui::widgets

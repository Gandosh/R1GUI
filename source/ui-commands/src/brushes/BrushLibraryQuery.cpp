// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: BrushLibraryModel::query and conflicts: the quick letter algorithm that turns what the user typed
//   into the ordered tiles with their next-letter hints, and the report of keys that cannot be told apart.
// Invariants: query is a pure function of the model's data and the request (no hidden state, same input
//   same output, ties broken by the host's list position); every tile index refers to a listed brush;
//   the Recent section only appears when nothing is typed and no category is chosen; at most one tile per
//   brush in the main list.
// Callers: the brush popup, tests.
#include <algorithm>
#include <unordered_map>

#include "BrushLibraryInternal.h"

namespace r1ui::commands::brushes {

namespace {

constexpr size_t kMaxQueryTextBytes = 4096;

bool startsWith(const std::u32string& key, const std::u32string& prefix) {
  return key.size() >= prefix.size() && key.compare(0, prefix.size(), prefix) == 0;
}

std::u32string trimSpaces(std::u32string text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && text[begin] == U' ') ++begin;
  while (end > begin && text[end - 1] == U' ') --end;
  return text.substr(begin, end - begin);
}

}  // namespace

QueryResult BrushLibraryModel::query(const QueryRequest& request) const {
  ensureDerived();
  const Derived& d = *derived_;
  const size_t n = infos_.size();
  QueryResult out;

  const std::string_view text = std::string_view(request.text).substr(0, kMaxQueryTextBytes);
  const bool searching = request.mode == QueryMode::SearchAnywhere;
  std::u32string typed;
  std::u32string needle;
  if (searching) needle = trimSpaces(foldText(text, 128));
  else typed = keyOf(text);
  const bool filtering = searching ? !needle.empty() : !typed.empty();
  out.typedLength = static_cast<uint32_t>(typed.size());

  const auto inCategory = [&](uint32_t i) { return request.category.empty() || infos_[i].category == request.category; };
  const auto base = [&](uint32_t i) {
    TileInfo tile;
    tile.brush = i;
    tile.enabled = infos_[i].enabled;
    tile.favourite = d.entries[i].favourite;
    tile.active = !activeId_.empty() && infos_[i].id == activeId_;
    tile.badgeLength = d.entries[i].uniqueLength;
    return tile;
  };
  for (uint32_t i = 0; i < n; ++i) out.totalCount += inCategory(i) ? 1 : 0;

  if (!filtering) {
    // Browsing: Recent first (when unfiltered), then favourites, then everything else, alphabetical.
    if (request.includeRecents && request.category.empty()) {
      for (const std::string& id : recents_) {
        const auto index = indexOfId(id);
        if (!index || !infos_[*index].enabled || out.tiles.size() >= kMaxRecents) continue;
        TileInfo tile = base(*index);
        tile.recent = true;
        out.tiles.push_back(tile);
      }
    }
    out.recentCount = static_cast<uint32_t>(out.tiles.size());
    for (const bool wantFavourites : {true, false}) {
      for (const uint32_t i : d.alpha) {
        if (!inCategory(i) || d.entries[i].favourite != wantFavourites) continue;
        out.tiles.push_back(base(i));
      }
    }
    out.matchCount = static_cast<uint32_t>(out.tiles.size() - out.recentCount);
    out.defaultHighlight = out.recentCount;
    for (size_t t = out.recentCount; t < out.tiles.size(); ++t) {
      if (out.tiles[t].active) {
        out.defaultHighlight = static_cast<uint32_t>(t);
        break;
      }
    }
    if (out.tiles.empty()) out.defaultHighlight = 0;
    return out;
  }

  std::vector<uint32_t> matched;
  std::vector<size_t> position(searching ? n : 0, 0);
  for (uint32_t i = 0; i < n; ++i) {
    if (!inCategory(i)) continue;
    if (searching) {
      const size_t at = d.entries[i].nameFold.find(needle);
      if (at == std::u32string::npos) continue;
      position[i] = at;
      matched.push_back(i);
    } else if (infos_[i].enabled && startsWith(d.entries[i].key, typed)) {
      matched.push_back(i);
    }
  }
  const auto rank = [&](uint32_t a, uint32_t b) {
    const Derived::Entry& ea = d.entries[a];
    const Derived::Entry& eb = d.entries[b];
    if (searching) {
      const bool pa = position[a] == 0;
      const bool pb = position[b] == 0;
      if (pa != pb) return pa;
    } else {
      const bool xa = ea.key.size() == typed.size();
      const bool xb = eb.key.size() == typed.size();
      if (xa != xb) return xa;
      if (ea.hasOverride() != eb.hasOverride()) return ea.hasOverride();
    }
    if (ea.favourite != eb.favourite) return ea.favourite;
    if (ea.alphaPos != eb.alphaPos) return ea.alphaPos < eb.alphaPos;
    return a < b;
  };
  std::sort(matched.begin(), matched.end(), rank);

  std::unordered_map<char32_t, uint32_t> nextCounts;
  if (!searching) {
    for (const uint32_t i : matched) {
      const std::u32string& key = d.entries[i].key;
      if (key.size() > typed.size()) ++nextCounts[key[typed.size()]];
    }
  }
  for (const uint32_t i : matched) {
    TileInfo tile = base(i);
    if (!searching) {
      const std::u32string& key = d.entries[i].key;
      tile.exact = key.size() == typed.size();
      tile.typedLength = static_cast<uint8_t>(std::min<size_t>(typed.size(), 255));
      if (key.size() > typed.size()) {
        tile.nextLetter = key[typed.size()];
        tile.nextUnique = nextCounts[tile.nextLetter] == 1;
      }
    }
    out.tiles.push_back(tile);
  }
  out.matchCount = static_cast<uint32_t>(matched.size());
  if (!searching && matched.size() == 1) out.unique = matched.front();

  if (!request.hideUnmatched) {
    std::vector<uint8_t> isMatch(n, 0);
    for (const uint32_t i : matched) isMatch[i] = 1;
    for (const uint32_t i : d.alpha) {
      if (!inCategory(i) || isMatch[i] != 0) continue;
      TileInfo tile = base(i);
      tile.matched = false;
      out.tiles.push_back(tile);
    }
  }
  out.defaultHighlight = 0;
  return out;
}

std::vector<LetterConflict> BrushLibraryModel::conflicts() const {
  ensureDerived();
  const Derived& d = *derived_;
  std::unordered_map<std::u32string, std::vector<uint32_t>> groups;
  for (uint32_t i = 0; i < infos_.size(); ++i) {
    if (infos_[i].enabled && !d.entries[i].key.empty()) groups[d.entries[i].key].push_back(i);
  }
  std::vector<LetterConflict> result;
  for (auto& [key, members] : groups) {
    if (members.size() < 2) continue;
    std::sort(members.begin(), members.end(), [&](uint32_t a, uint32_t b) {
      const Derived::Entry& ea = d.entries[a];
      const Derived::Entry& eb = d.entries[b];
      if (ea.hasOverride() != eb.hasOverride()) return ea.hasOverride();
      if (ea.favourite != eb.favourite) return ea.favourite;
      if (ea.alphaPos != eb.alphaPos) return ea.alphaPos < eb.alphaPos;
      return a < b;
    });
    result.push_back({displayKey(key, key.size()), std::move(members)});
  }
  std::sort(result.begin(), result.end(), [](const LetterConflict& a, const LetterConflict& b) { return a.key < b.key; });
  return result;
}

}  // namespace r1ui::commands::brushes

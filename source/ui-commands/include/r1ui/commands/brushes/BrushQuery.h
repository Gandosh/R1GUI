// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the value types of a brush library query: what the user typed, and the ordered tiles the popup
//   shows for it (which brush, matched or dimmed, the next letter to type, whether that letter is
//   enough, the badge length), plus the pure rule that decides when a typed prefix picks a brush at once.
// Why: the popup must stay a thin view. Everything that depends on names, letters and ties (the quick
//   letter algorithm, the order, the hints) is computed by the model into these plain values, so it can
//   be tested without a window, a font or a timer.
// Callers: BrushLibraryModel::query (produces), the brush popup and tests (consume).
// Order of tiles (documented in docs/dev/brush-library.md): with nothing typed the Recent section
//   (up to 8 brushes) comes first when no category is chosen, then the main list with favourites first
//   and the rest alphabetical. With a prefix typed (TypeToPick) the matches follow one another: a brush
//   whose key equals the prefix first, then brushes with a letter of their own (user or host override),
//   then favourites, then alphabetical; ties are broken by the position in the host's list, so the order
//   is a pure function of the data.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace r1ui::commands::brushes {

enum class QueryMode : uint8_t {
  TypeToPick,      // the text is a prefix of the key (the letters of the name); letters narrow, hints show the next letter
  SearchAnywhere   // the text is searched anywhere in the name; no hints, never picks by itself
};

struct QueryRequest {
  QueryMode mode = QueryMode::TypeToPick;
  std::string text;          // what the user typed (UTF-8); characters that are no letters or digits are ignored by TypeToPick
  std::string category;      // empty = every category
  bool hideUnmatched = true; // false keeps non-matching brushes at the end of the list with matched = false
  bool includeRecents = true;
};

struct TileInfo {
  uint32_t brush = 0;        // index into BrushLibraryModel::brushes()
  bool matched = true;       // false: dimmed (only produced when hideUnmatched is false)
  bool enabled = true;       // false: the host disabled the brush; it is shown dimmed and cannot be picked
  bool favourite = false;
  bool active = false;       // the host's active brush
  bool recent = false;       // a tile of the Recent section
  bool exact = false;        // its key equals the typed prefix
  char32_t nextLetter = 0;   // the next key character after the typed prefix (TypeToPick), 0 = none
  bool nextUnique = false;   // typing nextLetter leaves exactly this brush
  uint8_t typedLength = 0;   // key characters of the typed prefix (TypeToPick), so the tile can draw them dim before nextLetter
  uint8_t badgeLength = 0;   // key characters of the badge (the shortest prefix that no other brush shares)
};

struct QueryResult {
  std::vector<TileInfo> tiles;      // [0, recentCount) is the Recent section, the rest the main list
  uint32_t recentCount = 0;
  uint32_t matchCount = 0;          // matched tiles of the main list
  uint32_t totalCount = 0;          // brushes in the category (before matching)
  std::optional<uint32_t> unique;   // TypeToPick with a typed prefix: the only matching brush
  uint32_t typedLength = 0;         // key characters the prefix has
  uint32_t defaultHighlight = 0;    // the tile to highlight first (the active brush when nothing is typed, else the best match)
};

// The pure rule behind the "pick on unique match" option: a brush is picked at once only when the option
// is on and the query left exactly one enabled brush.
inline std::optional<uint32_t> pickOnUnique(const QueryResult& result, bool optionOn) {
  return optionOn ? result.unique : std::nullopt;
}

}  // namespace r1ui::commands::brushes

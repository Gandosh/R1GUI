// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of BrushLibraryModel::query: the quick letter algorithm on the owner's example names (narrowing,
//   next-letter hints, unique picks, order and tie-breaks), the Recent section, categories, search mode and
//   the dimming mode; property tests over seeded random libraries (every brush reachable by typing its key,
//   hints truthful, deterministic order, no duplicates); hostile text and a 2000-brush timing.
// Callers: CTest (fast).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>
#include <string>

#include "BrushFixtures.h"
#include "r1ui/commands/brushes/BrushLetters.h"

using namespace r1ui::commands::brushes;
using r1test::brush;

namespace {

std::vector<std::string> ids(const BrushLibraryModel& model, const QueryResult& result, bool matchedOnly = true) {
  std::vector<std::string> out;
  for (const TileInfo& tile : result.tiles) {
    if (matchedOnly && !tile.matched) continue;
    out.push_back(model.brushes()[tile.brush].id);
  }
  return out;
}

QueryResult typed(const BrushLibraryModel& model, const std::string& text, const std::string& category = {}) {
  QueryRequest request;
  request.text = text;
  request.category = category;
  return model.query(request);
}

const TileInfo* tileOf(const BrushLibraryModel& model, const QueryResult& result, const char* id) {
  const auto index = model.indexOfId(id);
  for (const TileInfo& tile : result.tiles) {
    if (index && tile.brush == *index && !tile.recent) return &tile;
  }
  return nullptr;
}

}  // namespace

int main() {
  // ---- the owner's example: B, then a letter, then the second letter ----
  {
    BrushLibraryModel model;
    model.setBrushes(r1test::sampleBrushes());

    // Nothing typed: every brush once, alphabetical; the Recent section is empty.
    QueryResult all = typed(model, "");
    R1_EXPECT(all.tiles.size() == model.size() && all.recentCount == 0 && all.matchCount == model.size());
    R1_EXPECT(ids(model, all).front() == "blob");

    // "s" narrows to the S brushes; each shows its next letter; the unique ones are marked.
    QueryResult s = typed(model, "s");
    R1_EXPECT(ids(model, s) == (std::vector<std::string>{"slash", "smooth", "snake-hook", "standard"}));
    R1_EXPECT(s.matchCount == 4 && s.totalCount == model.size() && !s.unique.has_value());
    const TileInfo* smooth = tileOf(model, s, "smooth");
    R1_EXPECT(smooth != nullptr && smooth->nextLetter == U'm' && smooth->nextUnique && smooth->typedLength == 1);
    R1_EXPECT(tileOf(model, s, "slash")->nextLetter == U'l' && tileOf(model, s, "snake-hook")->nextLetter == U'n' && tileOf(model, s, "standard")->nextLetter == U't');
    R1_EXPECT(!pickOnUnique(s, true).has_value());

    // Upper case and a leading space or symbol are the same prefix.
    R1_EXPECT(ids(model, typed(model, "S")) == ids(model, s));
    R1_EXPECT(ids(model, typed(model, " -s ")) == ids(model, s));

    // The second letter leaves one brush: picked at once only when the option says so.
    QueryResult sm = typed(model, "sm");
    R1_EXPECT(sm.unique == model.indexOfId("smooth") && sm.matchCount == 1);
    R1_EXPECT(pickOnUnique(sm, true) == sm.unique);
    R1_EXPECT(!pickOnUnique(sm, false).has_value());

    // "clay": Clay itself first (exact), then Clay Buildup with the hint "b"; two matches, so no pick.
    QueryResult clay = typed(model, "clay");
    R1_EXPECT(ids(model, clay) == (std::vector<std::string>{"clay", "clay-buildup"}));
    R1_EXPECT(clay.tiles[0].exact && clay.tiles[0].nextLetter == 0 && !clay.tiles[0].nextUnique);
    R1_EXPECT(clay.tiles[1].nextLetter == U'b' && clay.tiles[1].nextUnique);
    R1_EXPECT(!clay.unique.has_value());
    R1_EXPECT(typed(model, "clayb").unique == model.indexOfId("clay-buildup"));
    // Typing past the end of every name matches nothing.
    R1_EXPECT(typed(model, "claybuildupx").tiles.empty());
    R1_EXPECT(typed(model, "zzz").tiles.empty() && typed(model, "zzz").matchCount == 0);

    // Digits and non-key characters: characters that are no letters do not narrow.
    R1_EXPECT(typed(model, "!!").tiles.size() == model.size());

    // Categories restrict the candidates and the uniqueness.
    QueryResult sculptS = typed(model, "s", "Sculpt");
    R1_EXPECT(ids(model, sculptS) == (std::vector<std::string>{"standard"}) && sculptS.unique == model.indexOfId("standard"));
    R1_EXPECT(sculptS.totalCount == 8);
    R1_EXPECT(typed(model, "", "Surface").tiles.size() == 4);

    // Dimming mode keeps the others at the end with matched = false.
    QueryRequest dim;
    dim.text = "s";
    dim.hideUnmatched = false;
    const QueryResult dimmed = model.query(dim);
    R1_EXPECT(dimmed.tiles.size() == model.size() && dimmed.matchCount == 4);
    R1_EXPECT(dimmed.tiles[3].matched && !dimmed.tiles[4].matched);
    R1_EXPECT(ids(model, dimmed, false).size() == model.size());
  }

  // ---- order: exact, then user letter, then favourite, then alphabetical ----
  {
    BrushLibraryModel model;
    std::vector<BrushInfo> list = {brush("a", "Sa"), brush("b", "Sb"), brush("c", "Sc"), brush("d", "Sd"), brush("e", "S")};
    model.setBrushes(list);
    R1_EXPECT(ids(model, typed(model, "s")) == (std::vector<std::string>{"e", "a", "b", "c", "d"}));
    model.setFavourite("c", true);
    R1_EXPECT(ids(model, typed(model, "s")) == (std::vector<std::string>{"e", "c", "a", "b", "d"}));
    // Browsing (nothing typed): favourites first, then alphabetical.
    R1_EXPECT(ids(model, typed(model, "")) == (std::vector<std::string>{"c", "e", "a", "b", "d"}));
    // A user letter outranks a favourite: the key of "d" is now "s" + "sd", so it ranks right after the exact match.
    model.setUserLetter("d", "s");
    const std::vector<std::string> order = ids(model, typed(model, "s"));
    R1_EXPECT(order.size() == 5 && order[0] == "e" && order[1] == "d" && order[2] == "c");
  }
  {
    // Equal names: the host's list position breaks the tie, so the answer is stable.
    BrushLibraryModel model;
    model.setBrushes({brush("x1", "Same"), brush("x2", "Same"), brush("x3", "Same")});
    R1_EXPECT(ids(model, typed(model, "sa")) == (std::vector<std::string>{"x1", "x2", "x3"}));
    R1_EXPECT(ids(model, typed(model, "same")) == (std::vector<std::string>{"x1", "x2", "x3"}));
    model.setFavourite("x3", true);
    R1_EXPECT(ids(model, typed(model, "sa")).front() == "x3");
  }

  // ---- Recent section, active brush ----
  {
    BrushLibraryModel model;
    model.setBrushes(r1test::sampleBrushes());
    model.noteUsed("pinch");
    model.noteUsed("blob");
    model.noteUsed("smooth");
    model.setActiveId("move");
    const QueryResult all = typed(model, "");
    R1_EXPECT(all.recentCount == 3 && all.tiles.size() == model.size() + 3);
    R1_EXPECT(model.brushes()[all.tiles[0].brush].id == "smooth" && all.tiles[0].recent && all.tiles[2].recent && !all.tiles[3].recent);
    R1_EXPECT(model.brushes()[all.tiles[all.defaultHighlight].brush].id == "move" && all.tiles[all.defaultHighlight].active && !all.tiles[all.defaultHighlight].recent);
    // No Recent section while typing, in a category, or when asked not to.
    R1_EXPECT(typed(model, "s").recentCount == 0);
    R1_EXPECT(typed(model, "", "Sculpt").recentCount == 0);
    QueryRequest noRecents;
    noRecents.includeRecents = false;
    R1_EXPECT(model.query(noRecents).recentCount == 0);
    // A recent brush that became disabled or unlisted is skipped.
    std::vector<BrushInfo> list = r1test::sampleBrushes();
    list[7].enabled = false;   // pinch
    list.erase(list.begin() + 9);   // blob
    model.setBrushes(list);
    R1_EXPECT(typed(model, "").recentCount == 1);
  }

  // ---- search anywhere ----
  {
    BrushLibraryModel model;
    model.setBrushes(r1test::sampleBrushes());
    QueryRequest search;
    search.mode = QueryMode::SearchAnywhere;
    search.text = "POLISH";
    QueryResult found = model.query(search);
    R1_EXPECT(ids(model, found) == (std::vector<std::string>{"polish", "hpolish"}));   // prefix match first
    R1_EXPECT(!found.unique.has_value());
    for (const TileInfo& tile : found.tiles) R1_EXPECT(tile.nextLetter == 0);
    search.text = "uild";
    R1_EXPECT(ids(model, model.query(search)) == (std::vector<std::string>{"clay-buildup"}));
    search.text = "clay b";   // spaces count inside a search
    R1_EXPECT(ids(model, model.query(search)) == (std::vector<std::string>{"clay-buildup"}));
    search.text = "   ";
    R1_EXPECT(model.query(search).tiles.size() == model.size());
    search.text = "zzz";
    R1_EXPECT(model.query(search).tiles.empty());
  }

  // ---- hostile names ----
  {
    BrushLibraryModel model;
    std::vector<BrushInfo> list = {brush("digit", "3D Pen"), brush("sym", "***Star"), brush("cyr", "\xD0\x9A\xD0\xBB\xD0\xB5\xD0\xB9"), brush("accent", "\xC3\x89""clair"),
                                   brush("bytes", "\xff\xfe""Zed"), brush("huge", std::string(10000, 'q')), brush("empty-ish", "!!!"), brush("nul", std::string("n\0ul", 4))};
    model.setBrushes(list);
    R1_EXPECT(ids(model, typed(model, "3")) == (std::vector<std::string>{"digit"}));
    R1_EXPECT(ids(model, typed(model, "s")) == (std::vector<std::string>{"sym"}));
    R1_EXPECT(ids(model, typed(model, "\xD0\xBA")) == (std::vector<std::string>{"cyr"}));        // lower-case Cyrillic matches the capital name
    R1_EXPECT(ids(model, typed(model, "\xD0\x9A")) == (std::vector<std::string>{"cyr"}));
    R1_EXPECT(ids(model, typed(model, "e")) == (std::vector<std::string>{"accent"}));
    R1_EXPECT(ids(model, typed(model, "z")) == (std::vector<std::string>{"bytes"}));
    R1_EXPECT(ids(model, typed(model, "q")) == (std::vector<std::string>{"huge"}));
    R1_EXPECT(ids(model, typed(model, "nul")) == (std::vector<std::string>{"nul"}));   // the NUL became a space
    R1_EXPECT(model.unkeyableCount() == 1);
    R1_EXPECT(typed(model, "").tiles.size() == model.size());   // unkeyable brushes are still listed
    // Hostile query text never throws or hangs.
    const std::string longText(1 << 20, 'a');
    R1_EXPECT(typed(model, longText).tiles.empty());
    R1_EXPECT(typed(model, std::string(1 << 20, '!')).tiles.size() == model.size());
    R1_EXPECT(typed(model, "\xff\xff\xff").tiles.size() == model.size());
    (void)typed(model, std::string("a\0b", 3));
    QueryRequest search;
    search.mode = QueryMode::SearchAnywhere;
    search.text = longText;
    R1_EXPECT(model.query(search).tiles.empty());
    // An empty library answers an empty query.
    BrushLibraryModel empty;
    R1_EXPECT(typed(empty, "").tiles.empty() && typed(empty, "a").tiles.empty() && !typed(empty, "a").unique.has_value());
  }

  // ---- properties over seeded random libraries ----
  for (uint64_t seed = 1; seed <= 40; ++seed) {
    BrushLibraryModel model;
    const size_t count = 5 + static_cast<size_t>(seed * 7 % 120);
    model.setBrushes(r1test::randomBrushes(count, seed));
    // Some favourites and letters so the order rules take part.
    r1test::Rng rng(seed * 31);
    for (size_t i = 0; i < count / 5; ++i) model.setFavourite("b" + std::to_string(rng.below(count)), true);
    for (size_t i = 0; i < count / 8; ++i) model.setUserLetter("b" + std::to_string(rng.below(count)), std::string(1, static_cast<char>('a' + rng.below(5))));

    const QueryResult again = typed(model, "");
    R1_EXPECT(ids(model, again) == ids(model, typed(model, "")));   // deterministic

    for (uint32_t b = 0; b < model.size(); ++b) {
      const std::string key = model.keyText(b);
      if (key.empty() || !model.brushes()[b].enabled) continue;
      // Typing the key one letter at a time: the brush stays in the matches, the matches shrink or stay, and
      // every match starts with the prefix; at the end the brush is among the exact matches.
      const std::u32string folded = keyOf(key);
      size_t previous = model.size() + 1;
      std::string prefix;
      for (size_t i = 0; i < folded.size(); ++i) {
        r1ui::commands::brushes::appendUtf8(prefix, folded[i]);
        const QueryResult result = typed(model, prefix);
        R1_EXPECT(result.matchCount <= previous);
        previous = result.matchCount;
        bool found = false;
        for (const TileInfo& tile : result.tiles) {
          const std::string other = model.keyText(tile.brush);
          R1_EXPECT(model.brushes()[tile.brush].enabled);
          R1_EXPECT(keyOf(other).compare(0, i + 1, folded, 0, i + 1) == 0);
          found = found || tile.brush == b;
          // A truthful hint: typing the next letter leaves exactly this brush when nextUnique says so.
          if (tile.nextLetter != 0) {
            std::string longer = prefix;
            r1ui::commands::brushes::appendUtf8(longer, tile.nextLetter);
            const QueryResult narrowed = typed(model, longer);
            R1_EXPECT(tile.nextUnique == (narrowed.matchCount == 1));
            if (tile.nextUnique) R1_EXPECT(narrowed.unique == tile.brush);
          }
        }
        R1_EXPECT(found);
        R1_EXPECT(result.unique.has_value() == (result.matchCount == 1));
      }
      // The badge is unique unless the brush needs Enter, and typing it then leaves this brush alone.
      const std::string badge = model.badge(b);
      R1_EXPECT(!badge.empty());
      const QueryResult byBadge = typed(model, badge);
      if (!model.needsEnter(b)) R1_EXPECT(byBadge.unique == b);
      // Brushes in a conflict group, or whose key starts another's, are the only ones that need Enter.
      if (model.needsEnter(b)) {
        bool shares = false;
        for (uint32_t o = 0; o < model.size() && !shares; ++o) {
          if (o == b || !model.brushes()[o].enabled) continue;
          const std::u32string other = keyOf(model.keyText(o));
          shares = other.size() >= folded.size() && other.compare(0, folded.size(), folded) == 0;
        }
        R1_EXPECT(shares);
      }
    }
    // Every tile of the unfiltered list is a distinct listed brush (the main list; Recent may repeat).
    std::set<uint32_t> seen;
    for (size_t t = again.recentCount; t < again.tiles.size(); ++t) R1_EXPECT(seen.insert(again.tiles[t].brush).second);
    R1_EXPECT(seen.size() == model.size());
  }

  // ---- timing: 2000 brushes, queries well under a frame ----
  {
    BrushLibraryModel model;
    model.setBrushes(r1test::randomBrushes(2000, 99));
    (void)typed(model, "");   // builds the derived data
    using Clock = std::chrono::steady_clock;
    double worst = 0.0;
    double total = 0.0;
    int rounds = 0;
    for (const char* text : {"", "a", "s", "ca", "cat", "mm", "e", "x"}) {
      const auto begin = Clock::now();
      const QueryResult result = typed(model, text);
      const double ms = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
      worst = std::max(worst, ms);
      total += ms;
      ++rounds;
      R1_EXPECT(result.tiles.size() <= 2000);
    }
    const auto begin = Clock::now();
    model.setBrushes(r1test::randomBrushes(2000, 100));
    (void)typed(model, "");
    const double rebuild = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    std::printf("brush query, 2000 brushes: worst %.3f ms, mean %.3f ms; set + rebuild + first query %.3f ms\n", worst, total / rounds, rebuild);
#ifdef NDEBUG
    R1_EXPECT(worst < 8.0);
    R1_EXPECT(rebuild < 60.0);
#else
    R1_EXPECT(worst < 60.0);
#endif
  }
  return r1test::finish();
}

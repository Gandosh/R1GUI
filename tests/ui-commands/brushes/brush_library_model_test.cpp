// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of BrushLibraryModel's data side: sanitising of hostile brush definitions (duplicate ids,
//   empty names, huge names, NUL bytes, invalid UTF-8, bad letters and icons), favourites, recents, user
//   letters, picture keys, notifications and the keys, badges and conflicts that follow from the names.
// Callers: CTest (fast).
#include <set>
#include <string>

#include "BrushFixtures.h"

using namespace r1ui::commands::brushes;
using r1test::brush;

int main() {
  // ---- hostile definitions ----
  {
    BrushLibraryModel model;
    std::vector<BrushInfo> input;
    input.push_back(brush("a", "Alpha"));
    input.push_back(brush("a", "Alpha again"));               // duplicate id: rejected
    input.push_back(brush("", "No id"));                      // empty id: rejected
    input.push_back(brush("   ", "Blank id"));                // blank id: rejected
    input.push_back(brush("e", ""));                          // empty name: the id is shown
    input.push_back(brush("big", std::string(10000, 'x')));   // huge name: cut
    input.push_back(brush(std::string("nul\0id", 6), std::string("na\0me", 5)));  // control bytes become spaces
    input.push_back(brush("bad\xff", "Bad \xff\xfe bytes"));  // invalid UTF-8 is replaced
    BrushInfo icon = brush("icon", "Icon");
    icon.icon = "not a valid icon!";
    input.push_back(icon);
    BrushInfo letter = brush("letter", "Letter");
    letter.letter = "ab";                                      // two letters: dropped
    input.push_back(letter);
    BrushInfo goodLetter = brush("letter2", "Second");
    goodLetter.letter = "Q";
    input.push_back(goodLetter);
    BrushInfo noCategory = brush("nocat", "NoCat", "");
    input.push_back(noCategory);
    const SetBrushesResult result = model.setBrushes(input);
    R1_EXPECT(result.accepted == 9);
    R1_EXPECT(result.rejected == 3);
    R1_EXPECT(!result.issues.empty() && result.issues.size() <= 64);
    R1_EXPECT(model.size() == 9);
    R1_EXPECT(model.brushes()[1].name == "e");                         // empty name -> id
    R1_EXPECT(model.brushes()[2].name.size() == kMaxBrushNameBytes);   // cut
    R1_EXPECT(model.brushes()[3].id == "nul id" && model.brushes()[3].name == "na me");
    R1_EXPECT(model.brushes()[4].id.find('\xff') == std::string::npos);
    R1_EXPECT(model.brushes()[5].icon.empty());
    R1_EXPECT(model.brushes()[6].letter.empty());
    R1_EXPECT(model.brushes()[7].letter == "Q");
    R1_EXPECT(model.brushes()[0].category == "Sculpt");
    R1_EXPECT(model.brushes()[8].category == "Other");
  }
  {
    BrushLibraryModel model;
    BrushInfo noCategory = brush("nocat", "NoCat", "");
    model.setBrushes({noCategory});
    R1_EXPECT(model.brushes()[0].category == "Other");
    R1_EXPECT(model.categories().size() == 1 && model.categories()[0] == "Other");
  }
  {
    // More than the limit: the rest is rejected, nothing throws.
    BrushLibraryModel model;
    std::vector<BrushInfo> many;
    for (size_t i = 0; i < kMaxBrushes + 5; ++i) many.push_back(brush("id" + std::to_string(i), "Name " + std::to_string(i)));
    const SetBrushesResult result = model.setBrushes(many);
    R1_EXPECT(result.accepted == kMaxBrushes && result.rejected == 5);
    R1_EXPECT(model.size() == kMaxBrushes);
  }

  // ---- user state ----
  {
    BrushLibraryModel model;
    model.setBrushes(r1test::sampleBrushes());
    int notifications = 0;
    const auto listener = model.subscribe([&] { ++notifications; });
    const uint64_t stateVersion = model.stateVersion();
    R1_EXPECT(model.setFavourite("smooth", true) && model.isFavourite("smooth"));
    R1_EXPECT(model.stateVersion() == stateVersion + 1 && notifications == 1);
    R1_EXPECT(model.setFavourite("smooth", true));        // already: no change
    R1_EXPECT(notifications == 1);
    R1_EXPECT(!model.setFavourite("nope", true));         // unknown brush
    R1_EXPECT(model.setFavourite("smooth", false) && !model.isFavourite("smooth"));
    model.setActiveId("pinch");
    R1_EXPECT(model.stateVersion() == stateVersion + 2);  // the active brush is not persisted
    R1_EXPECT(model.version() > model.stateVersion());   // the active brush changed the version only

    // Recents: most recent first, bounded, disabled and unknown brushes refused.
    for (int i = 0; i < 12; ++i) R1_EXPECT(model.noteUsed(r1test::sampleBrushes()[static_cast<size_t>(i)].id));
    R1_EXPECT(model.recents().size() == kMaxRecents);
    R1_EXPECT(model.recents().front() == r1test::sampleBrushes()[11].id);
    R1_EXPECT(model.noteUsed(r1test::sampleBrushes()[5].id) && model.recents().front() == r1test::sampleBrushes()[5].id);
    R1_EXPECT(model.recents().size() == kMaxRecents);
    R1_EXPECT(!model.noteUsed("nope"));
    {
      std::vector<BrushInfo> list = r1test::sampleBrushes();
      list[0].enabled = false;
      model.setBrushes(list);
      R1_EXPECT(!model.noteUsed(list[0].id));
    }
    model.unsubscribe(listener);
    const int before = notifications;
    model.setFavourite("pinch", true);
    R1_EXPECT(notifications == before);
  }
  {
    // A listener that unsubscribes itself or another one during a notification is safe.
    BrushLibraryModel model;
    model.setBrushes(r1test::sampleBrushes());
    BrushLibraryModel::ListenerId second = 0;
    int firstCalls = 0;
    int secondCalls = 0;
    BrushLibraryModel::ListenerId first = 0;
    first = model.subscribe([&] {
      ++firstCalls;
      model.unsubscribe(first);
      model.unsubscribe(second);
    });
    second = model.subscribe([&] { ++secondCalls; });
    model.setFavourite("smooth", true);
    model.setFavourite("pinch", true);
    R1_EXPECT(firstCalls == 1 && secondCalls == 0);
  }

  // ---- letters, keys, badges ----
  {
    BrushLibraryModel model;
    model.setBrushes(r1test::sampleBrushes());
    const auto index = [&](const char* id) { return *model.indexOfId(id); };
    R1_EXPECT(model.letterOf(index("standard")) == "S");
    R1_EXPECT(model.keyText(index("clay-buildup")) == "CLAYBUILDUP");
    // "Clay" is the start of "Clay Buildup": neither has a unique prefix shorter than itself, Clay needs Enter.
    R1_EXPECT(model.needsEnter(index("clay")));
    R1_EXPECT(!model.needsEnter(index("clay-buildup")));
    R1_EXPECT(model.badge(index("clay-buildup")) == "CLAYB");
    R1_EXPECT(model.badge(index("clay")) == "CLAY");
    R1_EXPECT(model.badge(index("inflate")) == "I");
    R1_EXPECT(model.badge(index("move")) == "MO");
    R1_EXPECT(model.badge(index("mask-pen")) == "MA");
    R1_EXPECT(model.badge(index("blob")) == "B");
    R1_EXPECT(model.badge(index("polish")) == "PO");
    R1_EXPECT(model.badge(index("pinch")) == "PI");
    R1_EXPECT(model.badge(index("standard")) == "ST");
    R1_EXPECT(model.badge(index("smooth")) == "SM");
    R1_EXPECT(model.badge(index("snake-hook")) == "SN");
    R1_EXPECT(model.badge(index("slash")) == "SL");
    R1_EXPECT(model.conflicts().empty());
    R1_EXPECT(model.unkeyableCount() == 0);

    // A user letter takes the first position of the key.
    const LetterResult assigned = model.setUserLetter("pinch", "X");
    R1_EXPECT(assigned.ok() && assigned.sharedWith == 0);
    R1_EXPECT(model.userLetter("pinch") == "X");
    R1_EXPECT(model.letterOf(index("pinch")) == "X");
    R1_EXPECT(model.badge(index("pinch")) == "X");
    R1_EXPECT(model.hasLetterOverride(index("pinch")));
    R1_EXPECT(model.badge(index("polish")) == "P");  // P is free again for Polish
    // Sharing is reported.
    const LetterResult shared = model.setUserLetter("blob", "s");
    R1_EXPECT(shared.ok() && shared.sharedWith == 4);   // Standard, Smooth, Snake Hook, Slash
    R1_EXPECT(shared.message.find("4 other brushes") != std::string::npos);
    // Invalid letters change nothing.
    R1_EXPECT(model.setUserLetter("blob", "ab").status == LetterStatus::NotSingleLetter);
    R1_EXPECT(model.setUserLetter("blob", "-").status == LetterStatus::InvalidLetter);
    R1_EXPECT(model.setUserLetter("blob", "\xff").status == LetterStatus::InvalidLetter);
    R1_EXPECT(model.setUserLetter("nope", "a").status == LetterStatus::UnknownBrush);
    R1_EXPECT(model.userLetter("blob") == "S");
    // Clearing brings the name back.
    R1_EXPECT(model.setUserLetter("blob", "").ok());
    R1_EXPECT(model.userLetter("blob").empty() && model.letterOf(index("blob")) == "B");
    // The letter survives a new brush list (keyed by id).
    model.setBrushes(r1test::sampleBrushes());
    R1_EXPECT(model.userLetter("pinch") == "X" && model.letterOf(*model.indexOfId("pinch")) == "X");
    R1_EXPECT(model.sharingLetter("x") == 1 && model.sharingLetter("x", *model.indexOfId("pinch")) == 0);
  }
  {
    // Conflicts: equal keys, disabled brushes excluded, best tile first (override, favourite, alphabetical).
    BrushLibraryModel model;
    std::vector<BrushInfo> list = {brush("1", "Dup"), brush("2", "dup"), brush("3", "D-U-P"), brush("4", "Other"), brush("5", "\xE2\x98\x85"), brush("6", "Dup")};
    list[5].enabled = false;
    model.setBrushes(list);
    model.setFavourite("2", true);
    const auto conflicts = model.conflicts();
    R1_EXPECT(conflicts.size() == 1 && conflicts[0].key == "DUP");
    R1_EXPECT(conflicts[0].brushes.size() == 3);
    R1_EXPECT(conflicts[0].brushes[0] == 1);   // the favourite first
    R1_EXPECT(conflicts[0].brushes[1] == 2 && conflicts[0].brushes[2] == 0);   // "D-U-P" sorts before "Dup"
    R1_EXPECT(model.unkeyableCount() == 1);    // the star-only name
    R1_EXPECT(model.needsEnter(0) && model.needsEnter(1));
    R1_EXPECT(model.badge(4).empty());
  }

  // ---- picture keys ----
  {
    BrushLibraryModel model;
    std::vector<BrushInfo> list = r1test::sampleBrushes();
    list[0].thumbnailKey = "shared";
    list[1].thumbnailKey = "shared";
    model.setBrushes(list);
    R1_EXPECT(model.thumbnailKey(0) == model.thumbnailKey(1) && model.indexOfThumbnailKey(model.thumbnailKey(1)) == 0u);
    std::set<uint64_t> keys;
    for (uint32_t i = 1; i < model.size(); ++i) keys.insert(model.thumbnailKey(i));
    R1_EXPECT(keys.size() == model.size() - 1);   // all distinct sources have distinct keys
    for (uint32_t i = 2; i < model.size(); ++i) R1_EXPECT(model.indexOfThumbnailKey(model.thumbnailKey(i)) == i);
    R1_EXPECT(!model.indexOfThumbnailKey(12345).has_value());
    R1_EXPECT(model.thumbnailKey(9999) == 0);
  }

  // ---- replaceUserState repairs hostile state ----
  {
    BrushLibraryModel model;
    model.setBrushes(r1test::sampleBrushes());
    BrushUserState state;
    state.favourites = {"smooth", "smooth", "", "dormant-id", std::string(300, 'x')};
    for (int i = 0; i < 20; ++i) state.recents.push_back("r" + std::to_string(i));
    state.letters = {{"pinch", "Y"}, {"blob", "ab"}, {"", "q"}, {"slash", "\xff"}};
    state.pickOnUnique = false;
    model.replaceUserState(state);
    const BrushUserState back = model.userState();
    R1_EXPECT(back.favourites.size() == 3 || back.favourites.size() == 2);   // smooth, dormant-id (the 300-byte id is cut to a legal one)
    R1_EXPECT(back.recents.size() == kMaxRecents);
    R1_EXPECT(back.letters.size() == 1 && back.letters[0].first == "pinch" && back.letters[0].second == "y");
    R1_EXPECT(!back.pickOnUnique && !model.pickOnUniqueOption());
    R1_EXPECT(model.isFavourite("dormant-id"));   // kept although the host does not list it
    R1_EXPECT(model.setFavourite("dormant-id", false));
  }
  return r1test::finish();
}

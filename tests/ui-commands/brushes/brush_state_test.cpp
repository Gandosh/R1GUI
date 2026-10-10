// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of BrushState.h: the golden text of the file, the round trip, strict rejection of hostile
//   files (nothing changes, the file is moved aside), repair of single entries, the limits, and the
//   storage that saves after a change. Also one real file round trip through FileTextStore.
// Callers: CTest (fast).
#include <filesystem>
#include <fstream>
#include <string>

#include "BrushFixtures.h"
#include "r1ui/commands/brushes/BrushState.h"

using namespace r1ui::commands::brushes;
using r1ui::commands::customize::FileTextStore;
using r1ui::commands::customize::MemoryTextStore;

namespace {

BrushUserState sampleState() {
  BrushUserState state;
  state.favourites = {"smooth", "pinch"};
  state.recents = {"blob", "smooth"};
  state.letters = {{"pinch", "x"}, {"slash", "\xD0\x9A"}};
  state.pickOnUnique = false;
  return state;
}

const char* const kGolden =
    "{\n"
    "  \"format\":\"r1ui-brush-library\",\n"
    "  \"version\":1,\n"
    "  \"pickOnUnique\":false,\n"
    "  \"favourites\":[\n"
    "    \"smooth\",\n"
    "    \"pinch\"\n"
    "  ],\n"
    "  \"recents\":[\n"
    "    \"blob\",\n"
    "    \"smooth\"\n"
    "  ],\n"
    "  \"letters\":[\n"
    "    {\"brush\":\"pinch\",\"letter\":\"x\"},\n"
    "    {\"brush\":\"slash\",\"letter\":\"\xD0\x9A\"}\n"
    "  ]\n"
    "}\n";

}  // namespace

int main() {
  // ---- golden text and round trip ----
  R1_EXPECT(exportBrushState(sampleState()) == kGolden);
  {
    const BrushStateParse parsed = parseBrushState(kGolden);
    R1_EXPECT(parsed.ok && parsed.issues.empty());
    R1_EXPECT(parsed.state.favourites == sampleState().favourites && parsed.state.recents == sampleState().recents);
    R1_EXPECT(parsed.state.letters == sampleState().letters && !parsed.state.pickOnUnique);
    R1_EXPECT(exportBrushState(parsed.state) == kGolden);   // stable
    const BrushStateParse empty = parseBrushState(exportBrushState(BrushUserState{}));
    R1_EXPECT(empty.ok && empty.state.favourites.empty() && empty.state.pickOnUnique);
  }

  // ---- whole-file rejection ----
  {
    const std::string hostile[] = {
        "",
        "not json",
        "[]",
        "null",
        "{}",
        R"({"format":"other","version":1})",
        R"({"format":"r1ui-brush-library"})",
        R"({"format":"r1ui-brush-library","version":0})",
        R"({"format":"r1ui-brush-library","version":1.5})",
        R"({"format":"r1ui-brush-library","version":2})",
        R"({"format":"r1ui-brush-library","version":1,"pickOnUnique":"yes"})",
        R"({"format":"r1ui-brush-library","version":1,"favourites":"smooth"})",
        R"({"format":"r1ui-brush-library","version":1,"recents":{}})",
        R"({"format":"r1ui-brush-library","version":1,"letters":5})",
        R"({"format":"r1ui-brush-library","version":1,"version":1})",                        // duplicate key
        R"({"format":"r1ui-brush-library","version":1,"favourites":[[[[[[[["x"]]]]]]]]})",   // too deep
        std::string("{\"format\":\"r1ui-brush-library\",\"version\":1,\"x\":\"\xff\"}"),     // invalid UTF-8
    };
    for (const std::string& text : hostile) {
      const BrushStateParse parsed = parseBrushState(text);
      R1_EXPECT(!parsed.ok && !parsed.error.empty());
    }
    R1_EXPECT(!parseBrushState(std::string(kMaxBrushStateBytes + 1, ' ')).ok);
  }

  // ---- single entries are skipped or repaired ----
  {
    const BrushStateParse parsed = parseBrushState(
        R"({"format":"r1ui-brush-library","version":1,"extra":[1,2],)"
        R"("favourites":["a","a",5,"","b",null],)"
        R"("recents":["1","2","3","4","5","6","7","8","9","10"],)"
        R"("letters":[{"brush":"a","letter":"x"},{"brush":"a","letter":"y"},{"brush":"b","letter":"xy"},{"brush":"","letter":"x"},{"brush":"c"},7,{"brush":"d","letter":"-"}]})");
    R1_EXPECT(parsed.ok);
    R1_EXPECT((parsed.state.favourites == std::vector<std::string>{"a", "b"}));
    R1_EXPECT(parsed.state.recents.size() == kMaxRecents);
    R1_EXPECT(parsed.state.letters.size() == 1 && parsed.state.letters[0].first == "a");
    R1_EXPECT(parsed.issues.size() >= 8 && parsed.issues.size() <= 64);
    // An id longer than the limit is skipped.
    const BrushStateParse longId = parseBrushState(std::string(R"({"format":"r1ui-brush-library","version":1,"favourites":[")") + std::string(300, 'x') + "\"]}");
    R1_EXPECT(longId.ok && longId.state.favourites.empty() && longId.issues.size() == 1);
  }

  // ---- loading into a model through a store ----
  {
    r1ui::commands::brushes::BrushLibraryModel model;
    model.setBrushes(r1test::sampleBrushes());
    MemoryTextStore store;
    // A missing file is fine and changes nothing.
    BrushStateLoadReport missing = loadBrushState(model, store);
    R1_EXPECT(missing.ok && !missing.loaded);
    store.setContents(kGolden);
    const BrushStateLoadReport loaded = loadBrushState(model, store);
    R1_EXPECT(loaded.ok && loaded.loaded);
    R1_EXPECT(model.isFavourite("smooth") && model.userLetter("pinch") == "X" && !model.pickOnUniqueOption());
    R1_EXPECT(model.recents().size() == 2 && model.recents()[0] == "blob");

    // A damaged file changes nothing and is kept aside.
    store.setContents(R"({"format":"r1ui-brush-library","version":1,"favourites":5})");
    const BrushStateLoadReport bad = loadBrushState(model, store);
    R1_EXPECT(!bad.ok && !bad.loaded && !bad.error.empty());
    R1_EXPECT(model.isFavourite("smooth") && model.isFavourite("pinch"));   // the previous state stays
    R1_EXPECT(store.asides().size() == 1 && !store.contents().has_value());
  }

  // ---- storage saves after a change, not after a load, not for non-persisted changes ----
  {
    BrushLibraryModel model;
    model.setBrushes(r1test::sampleBrushes());
    MemoryTextStore store;
    store.setContents(kGolden);
    BrushStateStorage storage(model, store);
    R1_EXPECT(storage.load().loaded);
    R1_EXPECT(storage.saves() == 0);
    model.setActiveId("move");
    model.setBrushes(r1test::sampleBrushes());
    R1_EXPECT(storage.saves() == 0);
    model.setFavourite("blob", true);
    R1_EXPECT(storage.saves() == 1);
    R1_EXPECT(store.contents().has_value() && store.contents()->find("\"blob\"") != std::string::npos);
    model.noteUsed("trim");
    model.setUserLetter("trim", "q");
    R1_EXPECT(storage.saves() == 3);
    storage.setAutoSave(false);
    model.setFavourite("layer", true);
    R1_EXPECT(storage.saves() == 3);
    R1_EXPECT(storage.save() && storage.saves() == 4);
    // The saved text loads into a fresh model with the same state.
    BrushLibraryModel other;
    other.setBrushes(r1test::sampleBrushes());
    MemoryTextStore copy;
    copy.setContents(*store.contents());
    R1_EXPECT(loadBrushState(other, copy).loaded);
    R1_EXPECT(exportBrushState(other) == exportBrushState(model));
  }

  // ---- a real file: atomic save, corrupt file moved aside ----
  {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "r1ui-brush-state-test";
    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / "brushes.json";
    {
      FileTextStore store(file);
      BrushLibraryModel model;
      model.setBrushes(r1test::sampleBrushes());
      model.setFavourite("smooth", true);
      std::string error;
      R1_EXPECT(saveBrushState(model, store, error) && error.empty());
      R1_EXPECT(std::filesystem::exists(file) && !std::filesystem::exists(dir / "brushes.json.tmp"));
      BrushLibraryModel back;
      back.setBrushes(r1test::sampleBrushes());
      R1_EXPECT(loadBrushState(back, store).loaded && back.isFavourite("smooth"));
    }
    {
      std::ofstream(file, std::ios::binary | std::ios::trunc) << "{ broken";
      FileTextStore store(file);
      BrushLibraryModel model;
      const BrushStateLoadReport report = loadBrushState(model, store);
      R1_EXPECT(!report.ok && !report.keptAside.empty());
      R1_EXPECT(!std::filesystem::exists(file) && std::filesystem::exists(dir / "brushes.json.corrupt-1"));
    }
    std::filesystem::remove_all(dir, ignored);
  }
  return r1test::finish();
}

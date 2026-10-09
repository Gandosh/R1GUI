// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of the override file: export / import round trip (chords, sequences, unbound, key-release
//   triggers), replace and merge, live application (version bump, keymap change), unknown commands
//   reported and ignored, malformed chords and entries rejected one by one, whole-file rejection that
//   leaves the live state untouched (bad JSON, wrong format or version, too large, too many entries,
//   too deep, invalid UTF-8, duplicate keys), "no usable entry changes nothing", dormant overrides not
//   exported, and the stores (memory, file with missing, oversized, unwritable and atomic replace).
// Callers: CTest (label fast).
#include <filesystem>
#include <fstream>

#include "TestSupport.h"
#include "r1ui/commands/Keymap.h"
#include "r1ui/commands/OverrideIo.h"

namespace {

using namespace r1test;

struct Fixture {
  CommandRegistry reg;
  KeybindingOverrides over{reg};
  Keymap map{reg, over};

  Fixture() {
    reg.add(makeCommand("edit.undo", "Undo", kGlobalContext, seq(kZ, Mod::kCtrl)));
    reg.add(makeCommand("edit.redo", "Redo", kGlobalContext, seq(kY, Mod::kCtrl)));
    reg.addContext("layers", kWindowContext);
    reg.add(makeCommand("layers.delete", "Remove layer", "layers", seq(Key::Delete)));
  }
};

std::string file(const std::string& overrides) { return "{\"format\":\"r1ui-keybindings\",\"version\":1,\"overrides\":[" + overrides + "]}"; }
std::string entry(const std::string& command, int slot, const std::string& chordJson) {
  return "{\"command\":\"" + command + "\",\"slot\":" + std::to_string(slot) + ",\"chord\":" + chordJson + "}";
}

void testRoundTrip() {
  Fixture f;
  f.over.set("edit.undo", 0, seq2(kK, Mod::kCtrl, kU, Mod::kCtrl));
  f.over.set("edit.undo", 1, std::nullopt);
  KeyChord release = chord(kR, Mod::kAlt);
  release.onKeyUp = true;
  f.over.set("edit.redo", 0, ChordSequence::single(release));
  f.over.set("layers.delete", 0, seq(Key::Backspace));
  const std::string text = exportOverrides(f.reg, f.over);

  Fixture g;
  const ImportReport report = importOverrides(text, g.reg, g.over);
  R1_EXPECT(report.ok && report.changed && report.applied == 4 && report.issues.empty());
  R1_EXPECT(g.over.size() == 4);
  R1_EXPECT(g.map.effective("edit.undo", 0) == seq2(kK, Mod::kCtrl, kU, Mod::kCtrl) && !g.map.effective("edit.undo", 1));
  R1_EXPECT(g.map.effective("edit.redo", 0) == ChordSequence::single(release));
  R1_EXPECT(g.map.effective("layers.delete", 0) == seq(Key::Backspace));
  R1_EXPECT(exportOverrides(g.reg, g.over) == text);  // stable output
  R1_EXPECT(text.find("\"context\":\"layers\"") != std::string::npos);
}

void testLiveApplication() {
  Fixture f;
  int notifications = 0;
  f.reg.subscribe([&] { ++notifications; });
  const uint64_t before = f.reg.version();
  const ImportReport report = importOverrides(file(entry("edit.undo", 0, "\"Ctrl+Shift+U\"") + "," + entry("edit.redo", 0, "null")), f.reg, f.over);
  R1_EXPECT(report.ok && report.applied == 2 && notifications == 1 && f.reg.version() > before);  // one notification for the whole import
  R1_EXPECT(f.map.displayText("edit.undo") == "Ctrl+Shift+U" && f.map.displayText("edit.redo").empty());
  f.over.resetAll();  // reset applies live too
  R1_EXPECT(f.map.displayText("edit.undo") == "Ctrl+Z" && f.map.displayText("edit.redo") == "Ctrl+Y");
}

void testReplaceAndMerge() {
  Fixture f;
  f.over.set("edit.undo", 0, seq(kU));
  R1_EXPECT(importOverrides(file(entry("edit.redo", 0, "\"Ctrl+R\"")), f.reg, f.over, ImportMode::Merge).changed);
  R1_EXPECT(f.over.size() == 2);  // merge keeps what was there
  R1_EXPECT(importOverrides(file(entry("edit.redo", 1, "\"F2\"")), f.reg, f.over, ImportMode::Replace).changed);
  R1_EXPECT(f.over.size() == 1 && f.over.find("edit.redo", 1) != nullptr && f.over.find("edit.undo", 0) == nullptr);  // replace swaps the table (spec 07 rule 59)
  // The later of two entries for one slot wins.
  R1_EXPECT(importOverrides(file(entry("edit.redo", 0, "\"Ctrl+1\"") + "," + entry("edit.redo", 0, "\"Ctrl+2\"")), f.reg, f.over).applied == 2);
  R1_EXPECT(f.over.find("edit.redo", 0)->chord == seq(static_cast<Key>('2'), Mod::kCtrl));
}

void testEntryIssues() {
  Fixture f;
  const std::string entries = entry("edit.undo", 0, "\"Ctrl+Shift+U\"") + "," + entry("no.such.command", 0, "\"Ctrl+Q\"") + "," + entry("edit.redo", 0, "\"Ctrl+Bogus\"") + "," +
                              entry("edit.redo", 5, "\"Ctrl+R\"") + "," + entry("edit.redo", 0, "42") + ",7,{\"slot\":0,\"chord\":null}," +
                              "{\"command\":\"edit.redo\",\"slot\":0,\"chord\":\"Ctrl+R\",\"context\":\"layers\"}," + "{\"command\":\"edit.redo\",\"slot\":1.5,\"chord\":null}," +
                              "{\"command\":\"edit.redo\",\"slot\":0}," + "{\"command\":\"bad id!\",\"slot\":0,\"chord\":null}," + entry("edit.redo", 1, "\"Ctrl+K, Ctrl+Bogus\"") + "," +
                              "{\"command\":\"edit.redo\",\"context\":\"global\",\"slot\":1,\"chord\":\"Ctrl+Alt+R\",\"extra\":[1,2]}";
  const ImportReport report = importOverrides(file(entries), f.reg, f.over);
  R1_EXPECT(report.ok && report.applied == 2 && report.ignoredUnknown == 1 && report.issues.size() == 11);
  R1_EXPECT(f.over.size() == 2);
  R1_EXPECT(f.map.displayText("edit.undo") == "Ctrl+Shift+U");
  size_t unknown = 0;
  for (const ImportIssue& issue : report.issues) {
    if (issue.unknownCommand) {
      ++unknown;
      R1_EXPECT(issue.commandId == "no.such.command" && issue.index == 1);
    }
    R1_EXPECT(!issue.message.empty());
  }
  R1_EXPECT(unknown == 1);
}

void testWholeFileRejection() {
  Fixture f;
  f.over.set("edit.undo", 0, seq(kU));
  const std::string before = exportOverrides(f.reg, f.over);
  const auto rejected = [&](const std::string& text) {
    const ImportReport r = importOverrides(text, f.reg, f.over);
    return !r.ok && !r.changed && !r.error.empty() && exportOverrides(f.reg, f.over) == before;
  };
  R1_EXPECT(rejected(""));
  R1_EXPECT(rejected("not json"));
  R1_EXPECT(rejected("[]"));
  R1_EXPECT(rejected("{}"));
  R1_EXPECT(rejected("{\"format\":\"other\",\"version\":1,\"overrides\":[]}"));
  R1_EXPECT(rejected("{\"format\":\"r1ui-keybindings\",\"version\":2,\"overrides\":[]}"));
  R1_EXPECT(rejected("{\"format\":\"r1ui-keybindings\",\"version\":\"1\",\"overrides\":[]}"));
  R1_EXPECT(rejected("{\"format\":\"r1ui-keybindings\",\"version\":1}"));
  R1_EXPECT(rejected("{\"format\":\"r1ui-keybindings\",\"version\":1,\"overrides\":{}}"));
  R1_EXPECT(rejected("{\"format\":\"r1ui-keybindings\",\"version\":1,\"version\":1,\"overrides\":[]}"));  // duplicate key
  R1_EXPECT(rejected("{\"format\":\"r1ui-keybindings\",\"version\":1,\"overrides\":[{\"command\":\"a\xFF\"}]}"));  // invalid UTF-8
  R1_EXPECT(rejected(file("") + "trailing"));
  // Too deep (the parser limit is 8 levels), too large and too many entries.
  std::string deep = file(entry("edit.undo", 0, "null"));
  deep.insert(deep.size() - 2, ",{\"x\":" + std::string(40, '[') + std::string(40, ']') + "}");
  R1_EXPECT(rejected(deep));
  R1_EXPECT(rejected(std::string(kMaxImportBytes + 1, ' ')));
  std::string many;
  for (size_t i = 0; i <= kMaxImportEntries; ++i) many += (i == 0 ? "0" : ",0");
  R1_EXPECT(rejected(file(many)));
  // A valid file with no usable entry (only unknown commands, or empty) changes nothing (rule 59).
  const ImportReport none = importOverrides(file(entry("ghost", 0, "null")), f.reg, f.over);
  R1_EXPECT(none.ok && !none.changed && none.applied == 0 && none.ignoredUnknown == 1 && exportOverrides(f.reg, f.over) == before);
  const ImportReport empty = importOverrides(file(""), f.reg, f.over);
  R1_EXPECT(empty.ok && !empty.changed && f.over.size() == 1);
}

void testDormant() {
  Fixture f;
  f.over.set("layers.delete", 0, seq(Key::Backspace));
  f.reg.remove("layers.delete");
  R1_EXPECT(exportOverrides(f.reg, f.over).find("layers.delete") == std::string::npos);  // not exported while its module is unloaded
  R1_EXPECT(f.over.find("layers.delete", 0) != nullptr);
}

void testMemoryStore() {
  Fixture f;
  MemoryKeybindingStore store;
  ImportReport missing = loadOverrides(store, f.reg, f.over);
  R1_EXPECT(missing.ok && !missing.changed);  // first start: nothing saved yet
  f.over.set("edit.undo", 0, seq(kU));
  std::string error;
  R1_EXPECT(saveOverrides(f.reg, f.over, store, error) && store.contents().has_value());
  Fixture g;
  const ImportReport loaded = loadOverrides(store, g.reg, g.over);
  R1_EXPECT(loaded.ok && loaded.changed && g.map.effective("edit.undo", 0) == seq(kU));
  store.setContents("garbage");
  Fixture h;
  h.over.set("edit.redo", 0, seq(kR));
  R1_EXPECT(!loadOverrides(store, h.reg, h.over).ok && h.over.size() == 1);  // unreadable: the live state stays
}

void testFileStore() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "r1ui-commands-io-test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  const fs::path path = dir / "nested" / "keybindings.json";
  Fixture f;
  FileKeybindingStore store(path);
  R1_EXPECT(loadOverrides(store, f.reg, f.over).ok);  // missing file
  f.over.set("edit.undo", 0, seq(kU));
  std::string error;
  R1_EXPECT(saveOverrides(f.reg, f.over, store, error));  // creates the folders
  R1_EXPECT(fs::exists(path) && !fs::exists(fs::path(path.string() + ".tmp")));
  f.over.set("edit.undo", 0, seq(kV));
  R1_EXPECT(saveOverrides(f.reg, f.over, store, error));  // replaces the earlier file
  Fixture g;
  R1_EXPECT(loadOverrides(store, g.reg, g.over).changed && g.map.effective("edit.undo", 0) == seq(kV));

  // An oversized file is refused without being read into the live state.
  {
    std::ofstream big(path, std::ios::binary | std::ios::trunc);
    big << std::string(kMaxImportBytes + 10, 'x');
  }
  Fixture h;
  const ImportReport tooBig = loadOverrides(store, h.reg, h.over);
  R1_EXPECT(!tooBig.ok && !tooBig.error.empty() && h.over.size() == 0);
  // The target path is a directory: saving fails with a message and does not throw.
  FileKeybindingStore blocked(dir);
  R1_EXPECT(!saveOverrides(f.reg, f.over, blocked, error) && !error.empty());
  fs::remove_all(dir, ec);
}

}  // namespace

int main() {
  testRoundTrip();
  testLiveApplication();
  testReplaceAndMerge();
  testEntryIssues();
  testWholeFileRejection();
  testDormant();
  testMemoryStore();
  testFileStore();
  return r1test::finish();
}

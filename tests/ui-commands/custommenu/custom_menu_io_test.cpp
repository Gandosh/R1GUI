// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the tests of the custom menu files: the golden .r1mn text of a pie and a panel, round trips,
//   whole-file rejections (size, JSON, format, version, kind, slot counts, limits), repairs (labels, icons,
//   bad command ids, panel settings), unknown commands kept, mutated and random garbage input, unicode
//   names, import with both collision policies, atomic writes on a real directory (including a failed
//   write that must leave the old file intact), the set store (golden, round trip, 1000 menus, damaged
//   store moved aside, nothing changed on rejection) and CustomMenuStorage (save on change).
// Callers: CTest (label fast).
#include <filesystem>

#include "MenuFixtures.h"
#include "r1ui/commands/custommenu/CustomMenuIo.h"
#include "r1ui/commands/custommenu/TextFile.h"

namespace {

using namespace r1test;
using cm::CollisionPolicy;
using cm::CustomMenuSet;
using cm::MenuKind;

const char* kGoldenPie =
    "{\n"
    "  \"format\":\"r1ui-custom-menu\",\n"
    "  \"version\":1,\n"
    "  \"kind\":\"pie\",\n"
    "  \"name\":\"Tools\",\n"
    "  \"slotCount\":8,\n"
    "  \"slots\":[\n"
    "    {\"command\":\"tool.move\"},\n"
    "    null,\n"
    "    {\"command\":\"tool.rotate\",\"label\":\"Rotate\",\"icon\":\"rotate-cw\"},\n"
    "    null,\n"
    "    null,\n"
    "    {\"command\":\"plugin.gone\"},\n"
    "    null,\n"
    "    null\n"
    "  ]\n"
    "}\n";

const char* kGoldenPanel =
    "{\n"
    "  \"format\":\"r1ui-custom-menu\",\n"
    "  \"version\":1,\n"
    "  \"kind\":\"panel\",\n"
    "  \"name\":\"Quick\",\n"
    "  \"columns\":2,\n"
    "  \"buttonSize\":36,\n"
    "  \"showLabels\":true,\n"
    "  \"panelSize\":[240,180],\n"
    "  \"entries\":[\n"
    "    {\"command\":\"tool.move\",\"label\":\"Move\",\"icon\":\"move\"},\n"
    "    {\"command\":\"edit.undo\"},\n"
    "    {\"command\":\"plugin.gone\",\"label\":\"Gone\"}\n"
    "  ]\n"
    "}\n";

// A file text with one member replaced, for the rejection tests.
std::string withPie(const std::string& body) {
  return "{\"format\":\"r1ui-custom-menu\",\"version\":1," + body + "}";
}

void testGoldenAndRoundTrip() {
  R1_EXPECT(cm::exportMenuFile(samplePie()) == kGoldenPie);
  R1_EXPECT(cm::exportMenuFile(samplePanel()) == kGoldenPanel);
  for (const cm::CustomMenu& original : {samplePie(), samplePanel()}) {
    const cm::MenuParseResult parsed = cm::parseMenuFile(cm::exportMenuFile(original));
    R1_EXPECT(parsed.ok && parsed.issues.empty() && parsed.repaired == 0);
    cm::CustomMenu expected = original;
    expected.id.clear();
    expected.serial = 0;
    R1_EXPECT(parsed.menu == expected);
    R1_EXPECT(cm::exportMenuFile(parsed.menu) == cm::exportMenuFile(original));
  }
  // Unknown command ids survive the trip.
  const cm::MenuParseResult unknown = cm::parseMenuFile(kGoldenPie);
  R1_EXPECT(unknown.ok && unknown.menu.entries[5].commandId == "plugin.gone");
  // Unicode and escapes in names and labels.
  cm::CustomMenu fancy = samplePanel("Caf\xC3\xA9 \"quoted\" \\ \xF0\x9F\x98\x80");
  fancy.entries[0].label = "Line \\ \"x\" \xE2\x82\xAC";
  const cm::MenuParseResult back = cm::parseMenuFile(cm::exportMenuFile(fancy));
  R1_EXPECT(back.ok && back.menu.name == fancy.name && back.menu.entries[0].label == fancy.entries[0].label);
  // An extension-less path gets ".r1mn"; another extension is kept.
  R1_EXPECT(cm::withMenuExtension("a/b/menu").extension() == ".r1mn" && cm::withMenuExtension("a/b/menu.txt").extension() == ".txt");
}

void testWholeFileRejections() {
  R1_EXPECT(!cm::parseMenuFile("").ok && !cm::parseMenuFile("   ").ok && !cm::parseMenuFile("[]").ok && !cm::parseMenuFile("null").ok);
  R1_EXPECT(!cm::parseMenuFile("{").ok && !cm::parseMenuFile("{\"format\":").ok);
  R1_EXPECT(cm::parseMenuFile("{\"format\":\"other\",\"version\":1}").error == "not a custom menu file");
  R1_EXPECT(!cm::parseMenuFile(withPie("\"kind\":\"pie\",\"name\":\"x\",\"slots\":[null,null,null,null]").substr(0, 40)).ok);
  const std::string fmt = "{\"format\":\"r1ui-custom-menu\",";
  R1_EXPECT(!cm::parseMenuFile(fmt + "\"kind\":\"pie\",\"name\":\"x\",\"slots\":[null,null,null,null]}").ok);  // no version
  R1_EXPECT(!cm::parseMenuFile(fmt + "\"version\":0,\"kind\":\"pie\",\"name\":\"x\",\"slots\":[null,null,null,null]}").ok);
  R1_EXPECT(!cm::parseMenuFile(fmt + "\"version\":1.5,\"kind\":\"pie\",\"name\":\"x\",\"slots\":[null,null,null,null]}").ok);
  R1_EXPECT(!cm::parseMenuFile(fmt + "\"version\":1e308,\"kind\":\"pie\",\"name\":\"x\",\"slots\":[null,null,null,null]}").ok);
  R1_EXPECT(cm::parseMenuFile(fmt + "\"version\":2,\"kind\":\"pie\",\"name\":\"x\",\"slots\":[null,null,null,null]}").error.find("newer") != std::string::npos);
  R1_EXPECT(!cm::parseMenuFile(withPie("\"kind\":\"ring\",\"name\":\"x\",\"slots\":[]")).ok);
  R1_EXPECT(!cm::parseMenuFile(withPie("\"kind\":\"pie\",\"slots\":[null,null,null,null]")).ok);                       // no name
  R1_EXPECT(!cm::parseMenuFile(withPie("\"kind\":\"pie\",\"name\":\"  \",\"slots\":[null,null,null,null]")).ok);       // blank name
  R1_EXPECT(!cm::parseMenuFile(withPie("\"kind\":\"pie\",\"name\":\"x\"")).ok);                                         // no slots
  R1_EXPECT(!cm::parseMenuFile(withPie("\"kind\":\"pie\",\"name\":\"x\",\"slots\":[null,null,null]")).ok);              // 3 slots
  R1_EXPECT(!cm::parseMenuFile(withPie("\"kind\":\"pie\",\"name\":\"x\",\"slotCount\":4,\"slots\":[null,null]")).ok);   // mismatch
  R1_EXPECT(!cm::parseMenuFile(withPie("\"kind\":\"pie\",\"name\":\"x\",\"slotCount\":5,\"slots\":[null,null,null,null,null]")).ok);
  R1_EXPECT(!cm::parseMenuFile(withPie("\"kind\":\"pie\",\"name\":\"x\",\"slotCount\":4.5,\"slots\":[null,null,null,null]")).ok);
  R1_EXPECT(!cm::parseMenuFile(withPie("\"kind\":\"pie\",\"name\":\"x\",\"slots\":[null,null,null,null,null,null,null,null,null]")).ok);
  R1_EXPECT(!cm::parseMenuFile(withPie("\"kind\":\"panel\",\"name\":\"x\"")).ok);                                       // no entries
  R1_EXPECT(!cm::parseMenuFile(withPie("\"kind\":\"panel\",\"name\":\"x\",\"entries\":5")).ok);
  // Duplicate keys, invalid UTF-8, depth bomb, size bomb.
  R1_EXPECT(!cm::parseMenuFile(withPie("\"kind\":\"pie\",\"kind\":\"pie\",\"name\":\"x\",\"slots\":[null,null,null,null]")).ok);
  R1_EXPECT(!cm::parseMenuFile(withPie("\"kind\":\"pie\",\"name\":\"x\xff\",\"slots\":[null,null,null,null]")).ok);
  R1_EXPECT(!cm::parseMenuFile(std::string(2000, '[') + std::string(2000, ']')).ok);
  R1_EXPECT(!cm::parseMenuFile(std::string(cm::kMaxMenuFileBytes + 1, ' ')).ok);
  // Too many panel entries.
  std::string many = withPie("\"kind\":\"panel\",\"name\":\"x\",\"entries\":[");
  many.pop_back();
  for (size_t i = 0; i <= cm::kMaxPanelEntries; ++i) many += std::string(i ? "," : "") + "{\"command\":\"a.b\"}";
  many += "]}";
  R1_EXPECT(!cm::parseMenuFile(many).ok);
}

void testRepairs() {
  // Bad pieces are repaired or dropped, the rest survives, and each is reported.
  const std::string text = withPie(
      "\"kind\":\"pie\",\"name\":\"  Padded\\u0001 name \",\"slotCount\":4,\"slots\":["
      "{\"command\":\"ok.cmd\",\"label\":\"A\\u0007B\",\"icon\":\"bad icon!\"},"
      "{\"command\":\"not valid!\"},"
      "17,"
      "{\"command\":\"\",\"label\":\"orphan\"}]");
  const cm::MenuParseResult r = cm::parseMenuFile(text);
  R1_EXPECT(r.ok && r.menu.name == "Padded  name");
  R1_EXPECT(r.menu.entries[0].commandId == "ok.cmd" && r.menu.entries[0].label == "A B" && r.menu.entries[0].icon.empty());
  R1_EXPECT(r.menu.entries[1].commandId.empty() && r.menu.entries[2].commandId.empty() && r.menu.entries[3].commandId.empty());
  R1_EXPECT(r.repaired >= 4 && !r.issues.empty());
  // Panel settings out of range fall back to defaults; bad entries are dropped.
  const cm::MenuParseResult panel = cm::parseMenuFile(withPie(
      "\"kind\":\"panel\",\"name\":\"P\",\"columns\":99,\"buttonSize\":\"big\",\"showLabels\":3,\"panelSize\":[10,20],"
      "\"entries\":[{\"command\":\"a.b\"},{\"nope\":1},\"text\",{\"command\":\"x y\"},{\"command\":\"c.d\"}]"));
  R1_EXPECT(panel.ok && panel.menu.panel.columns == 3 && panel.menu.panel.buttonSize == 40 && panel.menu.panel.showLabels);
  R1_EXPECT(panel.menu.panel.width == 0.0 && panel.menu.entries.size() == 2 && panel.menu.entries[1].commandId == "c.d");
  // Unknown members are ignored; slotCount may be omitted for a legal slot list.
  const cm::MenuParseResult implicit = cm::parseMenuFile(withPie("\"kind\":\"pie\",\"name\":\"I\",\"future\":{\"a\":[1,2]},\"slots\":[null,null,null,null,null,null]"));
  R1_EXPECT(implicit.ok && implicit.menu.slotCount == 6);
  // A NUL in a name ends up as a space, never in the stored text.
  const cm::MenuParseResult nul = cm::parseMenuFile(withPie("\"kind\":\"pie\",\"name\":\"a\\u0000b\",\"slots\":[null,null,null,null]"));
  R1_EXPECT(nul.ok && nul.menu.name == "a b");
}

void testFuzz() {
  const std::string seeds[] = {cm::exportMenuFile(samplePie()), cm::exportMenuFile(samplePanel())};
  size_t accepted = 0;
  for (uint64_t i = 0; i < 4000; ++i) {
    Rng rng(i + 1);
    std::string text = seeds[i % 2];
    for (size_t k = 0, n = 1 + rng.below(4); k < n; ++k) text = mutate(text, rng);
    const cm::MenuParseResult r = cm::parseMenuFile(text);
    if (r.ok) {
      ++accepted;
      std::string reason;
      cm::CustomMenu probe = r.menu;
      probe.id = "menu.1";
      probe.serial = 1;
      if (!cm::validateMenu(probe, reason)) {
        std::fprintf(stderr, "accepted an illegal menu (seed %llu): %s\n", static_cast<unsigned long long>(i), reason.c_str());
        R1_EXPECT(false);
      }
    } else {
      R1_EXPECT(!r.error.empty());
    }
  }
  R1_EXPECT(accepted > 100);  // many mutations hit insignificant bytes
  for (uint64_t i = 0; i < 1000; ++i) {
    Rng rng(9000 + i);
    R1_EXPECT(!cm::parseMenuFile(randomGarbage(rng, rng.below(300))).ok);
  }
}

void testImportPolicies() {
  CustomMenuSet set;
  set.createMenu(MenuKind::Pie, "Tools");
  const std::string text = cm::exportMenuFile(samplePie("Tools"));
  const auto renamed = cm::importMenuText(set, text, CollisionPolicy::Rename);
  R1_EXPECT(renamed.ok && renamed.adopted.renamed && renamed.adopted.name == "Tools (2)" && set.size() == 2);
  const auto replaced = cm::importMenuText(set, text, CollisionPolicy::Replace);
  R1_EXPECT(replaced.ok && replaced.adopted.replaced && set.size() == 2 && set.menus()[0].entries[0].commandId == "tool.move");
  // A corrupt file reports and changes nothing.
  const uint64_t version = set.version();
  const auto corrupt = cm::importMenuText(set, text.substr(0, text.size() / 2), CollisionPolicy::Rename);
  R1_EXPECT(!corrupt.ok && !corrupt.error.empty() && set.version() == version && set.size() == 2);
  // A full set refuses without damage.
  CustomMenuSet full;
  for (size_t i = 0; i < cm::kMaxMenus; ++i) full.createMenu(MenuKind::Panel, "M" + std::to_string(i));
  R1_EXPECT(!cm::importMenuText(full, text, CollisionPolicy::Rename).ok && full.size() == cm::kMaxMenus);
}

void testFiles() {
  TempDir dir("r1mn");
  const auto path = dir.path() / "tools.r1mn";
  std::string error;
  R1_EXPECT(cm::saveMenuFile(samplePie(), path, error) && error.empty());
  R1_EXPECT(slurp(path) == kGoldenPie);
  R1_EXPECT(countFiles(dir.path()) == 1);  // no temporary file left behind
  const cm::MenuParseResult loaded = cm::loadMenuFile(path);
  R1_EXPECT(loaded.ok && loaded.menu.name == "Tools");
  // Overwriting replaces the content completely.
  R1_EXPECT(cm::saveMenuFile(samplePanel(), path, error) && slurp(path) == kGoldenPanel && countFiles(dir.path()) == 1);
  // Missing and oversized files are reported.
  R1_EXPECT(!cm::loadMenuFile(dir.path() / "missing.r1mn").ok);
  spit(dir.path() / "huge.r1mn", std::string(cm::kMaxMenuFileBytes + 10, 'x'));
  const auto huge = cm::loadMenuFile(dir.path() / "huge.r1mn");
  R1_EXPECT(!huge.ok && huge.error.find("larger") != std::string::npos);
  spit(dir.path() / "junk.r1mn", "this is not json");
  R1_EXPECT(!cm::loadMenuFile(dir.path() / "junk.r1mn").ok);
  // A failed write (target is a directory) leaves everything intact and no temp file.
  std::filesystem::create_directories(dir.path() / "blocker.r1mn");
  const size_t before = countFiles(dir.path());
  R1_EXPECT(!cm::saveMenuFile(samplePie(), dir.path() / "blocker.r1mn", error) && !error.empty());
  R1_EXPECT(countFiles(dir.path()) == before && std::filesystem::is_directory(dir.path() / "blocker.r1mn"));
  // A parent that cannot be created (a file is in the way) fails cleanly.
  spit(dir.path() / "afile", "x");
  R1_EXPECT(!cm::saveMenuFile(samplePie(), dir.path() / "afile" / "sub" / "m.r1mn", error));
  // Importing from a file.
  CustomMenuSet set;
  const auto imported = cm::importMenuFile(set, path, CollisionPolicy::Rename);
  R1_EXPECT(imported.ok && set.size() == 1 && set.menus()[0].name == "Quick");
  R1_EXPECT(!cm::importMenuFile(set, dir.path() / "junk.r1mn", CollisionPolicy::Rename).ok && set.size() == 1);
  // Atomic writer with a unicode file name.
  const auto unicodePath = dir.path() / std::filesystem::path(u8"menü €.r1mn");
  R1_EXPECT(cm::saveMenuFile(samplePie(), unicodePath, error) && cm::loadMenuFile(unicodePath).ok);
}

CustomMenuSet bigSet(size_t menus) {
  CustomMenuSet set;
  for (size_t i = 0; i < menus; ++i) {
    const auto r = set.createMenu(i % 3 == 0 ? MenuKind::Pie : MenuKind::Panel, "Menu " + std::to_string(i));
    for (size_t k = 0; k < 4; ++k) {
      if (i % 3 == 0) {
        set.setSlot(r.id, k, "cmd." + std::to_string(i) + "." + std::to_string(k), k == 1 ? "Label" : "", "");
      } else {
        set.addEntry(r.id, "cmd." + std::to_string(i) + "." + std::to_string(k));
      }
    }
  }
  return set;
}

void testSetStore() {
  CustomMenuSet set = bigSet(3);
  set.deleteMenu("menu.2");  // leaves a gap in the serials
  const std::string text = cm::exportSet(set);
  R1_EXPECT(text.find("\"serial\":4") != std::string::npos);
  const cm::SetParseResult parsed = cm::parseSet(text);
  R1_EXPECT(parsed.ok && parsed.menus.size() == 2 && parsed.nextSerial == 4 && parsed.skipped == 0);
  CustomMenuSet reloaded;
  R1_EXPECT(reloaded.replaceAll(parsed.menus, parsed.nextSerial).ok);
  R1_EXPECT(cm::exportSet(reloaded) == text);
  R1_EXPECT(reloaded.createMenu(MenuKind::Pie, "New").id == "menu.4");  // a deleted id is never reused

  // 1000 menus round trip, with the stable ids and the counter.
  CustomMenuSet big = bigSet(1000);
  const std::string bigText = cm::exportSet(big);
  const cm::SetParseResult bigParsed = cm::parseSet(bigText);
  R1_EXPECT(bigParsed.ok && bigParsed.menus.size() == 1000 && bigParsed.skipped == 0);
  CustomMenuSet bigBack;
  R1_EXPECT(bigBack.replaceAll(bigParsed.menus, bigParsed.nextSerial).ok && cm::exportSet(bigBack) == bigText);

  // Whole-file rejections.
  R1_EXPECT(!cm::parseSet("").ok && !cm::parseSet("{}").ok && !cm::parseSet("[1]").ok);
  R1_EXPECT(!cm::parseSet("{\"format\":\"r1ui-custom-menus\",\"version\":1}").ok);
  R1_EXPECT(!cm::parseSet("{\"format\":\"r1ui-custom-menus\",\"version\":7,\"menus\":[]}").ok);
  R1_EXPECT(cm::parseSet("{\"format\":\"r1ui-custom-menus\",\"version\":1,\"menus\":[]}").ok);
  R1_EXPECT(!cm::parseSet("{\"format\":\"r1ui-custom-menus\",\"version\":1,\"menus\":[1,2]}").ok);  // none usable
  // Per-menu problems skip only that menu: a bad kind, a duplicate name, a duplicate id.
  const std::string mixed =
      "{\"format\":\"r1ui-custom-menus\",\"version\":1,\"serial\":10,\"menus\":["
      "{\"id\":\"menu.1\",\"kind\":\"pie\",\"name\":\"A\",\"slots\":[null,null,null,null]},"
      "{\"id\":\"menu.2\",\"kind\":\"wheel\",\"name\":\"B\",\"slots\":[]},"
      "{\"id\":\"menu.3\",\"kind\":\"pie\",\"name\":\"a\",\"slots\":[null,null,null,null]},"
      "{\"id\":\"menu.1\",\"kind\":\"pie\",\"name\":\"C\",\"slots\":[null,null,null,null]},"
      "{\"kind\":\"panel\",\"name\":\"D\",\"entries\":[{\"command\":\"x.y\"}]}]}";
  const cm::SetParseResult m = cm::parseSet(mixed);
  R1_EXPECT(m.ok && m.menus.size() == 2 && m.skipped == 3 && m.menus[1].id == "menu.10" && m.nextSerial == 11);
  // Limits.
  std::string toomany = "{\"format\":\"r1ui-custom-menus\",\"version\":1,\"menus\":[";
  for (size_t i = 0; i <= cm::kMaxMenus; ++i) toomany += std::string(i ? "," : "") + "{}";
  R1_EXPECT(!cm::parseSet(toomany + "]}").ok);
  // Mutations and garbage never crash and never produce an illegal set.
  for (uint64_t i = 0; i < 1500; ++i) {
    Rng rng(i + 77);
    std::string t = text;
    for (size_t k = 0, n = 1 + rng.below(5); k < n; ++k) t = mutate(t, rng);
    const cm::SetParseResult r = cm::parseSet(t);
    if (!r.ok) continue;
    CustomMenuSet probe;
    if (!probe.replaceAll(r.menus, r.nextSerial).ok) {
      std::fprintf(stderr, "parseSet accepted menus that replaceAll refuses (seed %llu)\n", static_cast<unsigned long long>(i));
      R1_EXPECT(false);
    }
  }
  for (uint64_t i = 0; i < 500; ++i) {
    Rng rng(5000 + i);
    R1_EXPECT(!cm::parseSet(randomGarbage(rng, rng.below(400))).ok);
  }
}

void testStorage() {
  // Memory store: load, save-on-change, corrupt content moved aside and the live set untouched.
  r1ui::commands::customize::MemoryTextStore store;
  CustomMenuSet set;
  cm::CustomMenuStorage storage(set, store);
  R1_EXPECT(storage.load().ok && !store.contents());  // nothing stored yet: nothing loaded
  set.createMenu(MenuKind::Pie, "Pie");
  R1_EXPECT(store.contents() && store.contents()->find("\"Pie\"") != std::string::npos);  // saved by the change
  CustomMenuSet second;
  cm::CustomMenuStorage storage2(second, store);
  const auto report = storage2.load();
  R1_EXPECT(report.ok && report.loaded && second.size() == 1 && second.menus()[0].name == "Pie");
  const std::string savedBefore = *store.contents();
  second.setSlot("menu.1", 0, "a.b");  // autosave rewrites the store
  R1_EXPECT(*store.contents() != savedBefore);
  // A damaged store is rejected, kept aside, and the live set stays as it was.
  store.setContents("{\"format\":\"r1ui-custom-menus\",\"version\":1,\"menus\":[1]}");
  const auto bad = storage2.load();
  R1_EXPECT(!bad.ok && !bad.keptAside.empty() && second.size() == 1 && !store.contents());
  R1_EXPECT(store.asides().size() == 1);
  store.setContents("not json at all");
  R1_EXPECT(!storage2.load().ok && second.size() == 1);

  // File store: the full cycle on disk.
  TempDir dir("menus");
  r1ui::commands::customize::FileTextStore file(dir.path() / "custom-menus.json");
  CustomMenuSet onDisk;
  cm::CustomMenuStorage diskStorage(onDisk, file);
  onDisk.createMenu(MenuKind::Panel, "Disk");
  onDisk.addEntry("menu.1", "tool.move");
  CustomMenuSet restarted;
  cm::CustomMenuStorage restartedStorage(restarted, file);
  R1_EXPECT(restartedStorage.load().loaded && restarted.menus()[0].entries[0].commandId == "tool.move");
  spit(dir.path() / "custom-menus.json", "{broken");
  CustomMenuSet damaged;
  cm::CustomMenuStorage damagedStorage(damaged, file);
  const auto dr = damagedStorage.load();
  R1_EXPECT(!dr.ok && damaged.size() == 0 && std::filesystem::exists(dir.path() / "custom-menus.json.corrupt-1"));
  R1_EXPECT(damagedStorage.lastError() == dr.error);
}

}  // namespace

int main() {
  testGoldenAndRoundTrip();
  testWholeFileRejections();
  testRepairs();
  testFuzz();
  testImportPolicies();
  testFiles();
  testSetStore();
  testStorage();
  return r1test::finish();
}

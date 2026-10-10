// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the tests of CustomMenuSet and the validators: creation, rename, delete, names (duplicates,
//   unicode, hostile bytes), every pie and panel operation with its refusals, preview (dry run), listeners,
//   id stability, the limits (1000 menus, entries), adopt with both collision policies, replaceAll, and a
//   random operation sequence that keeps the set legal.
// Callers: CTest (label fast).
#include <cmath>
#include <set>

#include "MenuFixtures.h"
#include "r1ui/commands/Text.h"

namespace {

using namespace r1test;
using cm::CustomMenuSet;
using cm::MenuError;
using cm::MenuKind;

const std::string kBad = std::string("a\0b\xff", 4) + "c\x01";

void testCreateRenameDelete() {
  CustomMenuSet set;
  const auto a = set.createMenu(MenuKind::Pie, "  Tools  ");
  R1_EXPECT(a.ok && a.id == "menu.1");
  R1_EXPECT(set.find(a.id)->name == "Tools");  // trimmed
  R1_EXPECT(set.find(a.id)->entries.size() == 8 && set.find(a.id)->slotCount == 8);
  const auto b = set.createMenu(MenuKind::Panel, "Quick");
  R1_EXPECT(b.ok && b.id == "menu.2" && set.find(b.id)->entries.empty());

  // Names: duplicates are ASCII case-insensitive, blank and hostile ones are refused or cleaned.
  R1_EXPECT(set.createMenu(MenuKind::Pie, "tools").error == MenuError::DuplicateName);
  R1_EXPECT(set.createMenu(MenuKind::Pie, "   ").error == MenuError::InvalidName);
  R1_EXPECT(set.createMenu(MenuKind::Pie, std::string("\0\0", 2)).error == MenuError::InvalidName);
  const auto weird = set.createMenu(MenuKind::Pie, kBad);
  R1_EXPECT(weird.ok);
  const std::string weirdName = set.find(weird.id)->name;
  R1_EXPECT(weirdName.find('\0') == std::string::npos && weirdName.find('\x01') == std::string::npos);
  R1_EXPECT(cm::cleanName(weirdName) == weirdName);  // idempotent
  const auto unicode = set.createMenu(MenuKind::Panel, "Werkzeuge \xC3\xA4\xE2\x82\xAC \xF0\x9F\x98\x80");
  R1_EXPECT(unicode.ok);
  R1_EXPECT(set.createMenu(MenuKind::Panel, std::string(5000, 'x')).ok);
  R1_EXPECT(set.menus().back().name.size() == cm::kMaxNameBytes);
  // A name cut inside a multi-byte sequence stays valid UTF-8.
  std::string longEuro;
  while (longEuro.size() < 200) longEuro += "\xE2\x82\xAC";
  const auto cut = set.createMenu(MenuKind::Panel, longEuro);
  R1_EXPECT(cut.ok && r1ui::commands::isValidUtf8(set.find(cut.id)->name) && set.find(cut.id)->name.size() <= cm::kMaxNameBytes);

  R1_EXPECT(set.renameMenu(a.id, "Quick").error == MenuError::DuplicateName);
  R1_EXPECT(set.renameMenu(a.id, "TOOLS").ok);  // renaming to its own name (other case) is allowed
  R1_EXPECT(set.find(a.id)->name == "TOOLS");
  R1_EXPECT(set.renameMenu("menu.99", "x").error == MenuError::UnknownMenu);
  R1_EXPECT(set.renameMenu(a.id, "").error == MenuError::InvalidName && set.find(a.id)->name == "TOOLS");

  // Ids are never reused after a delete.
  R1_EXPECT(set.deleteMenu(b.id).ok && set.find(b.id) == nullptr);
  R1_EXPECT(set.deleteMenu(b.id).error == MenuError::UnknownMenu);
  const auto c = set.createMenu(MenuKind::Panel, "Quick");
  R1_EXPECT(c.ok && c.id != b.id && c.id != a.id);
  R1_EXPECT(set.uniqueName("Quick") == "Quick (2)");
  R1_EXPECT(set.uniqueName("Free") == "Free");
  R1_EXPECT(set.uniqueName("  ").empty());
}

void testPieOperations() {
  CustomMenuSet set;
  const std::string id = set.createMenu(MenuKind::Pie, "Pie").id;
  R1_EXPECT(set.setSlot(id, 0, "tool.move", "Move", "move").ok);
  R1_EXPECT(set.setSlot(id, 3, "edit.undo").ok);
  R1_EXPECT(set.setSlot(id, 8, "edit.undo").error == MenuError::OutOfRange);
  R1_EXPECT(set.setSlot(id, 1, "").error == MenuError::InvalidEntry);                 // use clearSlot for empty
  R1_EXPECT(set.setSlot(id, 1, "bad id!").error == MenuError::InvalidEntry);
  R1_EXPECT(set.setSlot(id, 1, "ok.id", "", "bad icon!").error == MenuError::InvalidEntry);
  R1_EXPECT(set.find(id)->entries[1].commandId.empty());
  R1_EXPECT(set.setSlot(id, 1, "unknown.command").ok);  // unknown commands are kept
  R1_EXPECT(set.addEntry(id, "file.save").index == 2);  // first empty slot
  R1_EXPECT(set.setEntryAppearance(id, 2, "Save it", "save").ok && set.find(id)->entries[2].label == "Save it");
  R1_EXPECT(set.setEntryAppearance(id, 4, "x", "").error == MenuError::InvalidEntry);  // empty slot
  R1_EXPECT(set.moveEntry(id, 0, 5).ok);  // pie: swap
  R1_EXPECT(set.find(id)->entries[5].commandId == "tool.move" && set.find(id)->entries[0].commandId.empty());
  R1_EXPECT(set.clearSlot(id, 5).ok && set.find(id)->entries[5].commandId.empty());
  R1_EXPECT(set.clearSlot(id, 5).ok);  // clearing an empty slot is harmless
  R1_EXPECT(set.setPanelColumns(id, 2).error == MenuError::WrongKind);
  // Fill every slot, then addEntry has no room.
  for (size_t i = 0; i < 8; ++i) R1_EXPECT(set.setSlot(id, i, "c." + std::to_string(i)).ok);
  R1_EXPECT(set.addEntry(id, "one.more").error == MenuError::LimitReached);
  // Slot count: shrinking is refused while a vanishing slot holds a command.
  R1_EXPECT(set.setPieSlotCount(id, 6).error == MenuError::WouldLoseEntries && set.find(id)->entries.size() == 8);
  R1_EXPECT(set.setPieSlotCount(id, 5).error == MenuError::OutOfRange);
  R1_EXPECT(set.clearSlot(id, 6).ok && set.clearSlot(id, 7).ok && set.setPieSlotCount(id, 6).ok);
  R1_EXPECT(set.find(id)->entries.size() == 6 && set.find(id)->slotCount == 6);
  R1_EXPECT(set.setPieSlotCount(id, 8).ok && set.find(id)->entries.size() == 8 && set.find(id)->entries[7].commandId.empty());
  std::string reason;
  R1_EXPECT(cm::validateMenu(*set.find(id), reason));
}

void testPanelOperations() {
  CustomMenuSet set;
  const std::string id = set.createMenu(MenuKind::Panel, "Panel").id;
  R1_EXPECT(set.addEntry(id, "a.one").index == 0 && set.addEntry(id, "a.two").index == 1 && set.addEntry(id, "a.zero", 0).index == 0);
  R1_EXPECT(set.find(id)->entries[0].commandId == "a.zero" && set.find(id)->entries[2].commandId == "a.two");
  R1_EXPECT(set.addEntry(id, "a.far", 9).error == MenuError::OutOfRange);
  R1_EXPECT(set.moveEntry(id, 0, 2).ok);  // list move: zero goes last
  R1_EXPECT(set.find(id)->entries[0].commandId == "a.one" && set.find(id)->entries[2].commandId == "a.zero");
  R1_EXPECT(set.moveEntry(id, 0, 3).error == MenuError::OutOfRange);
  R1_EXPECT(set.setSlot(id, 1, "a.replaced", "R", "").ok && set.find(id)->entries[1].commandId == "a.replaced");
  R1_EXPECT(set.removeEntry(id, 1).ok && set.find(id)->entries.size() == 2);
  R1_EXPECT(set.removeEntry(id, 5).error == MenuError::OutOfRange);
  R1_EXPECT(set.addEntry(id, "").error == MenuError::InvalidEntry);  // a panel entry needs a command
  R1_EXPECT(set.setPanelColumns(id, 0).error == MenuError::OutOfRange && set.setPanelColumns(id, 13).error == MenuError::OutOfRange);
  R1_EXPECT(set.setPanelColumns(id, 4).ok && set.find(id)->panel.columns == 4);
  R1_EXPECT(set.setPanelButtonSize(id, 10).error == MenuError::OutOfRange && set.setPanelButtonSize(id, 60).ok);
  R1_EXPECT(set.setPanelShowLabels(id, false).ok && !set.find(id)->panel.showLabels);
  R1_EXPECT(set.setPanelSize(id, 300, 200).ok && set.find(id)->panel.width == 300.0);
  R1_EXPECT(set.setPanelSize(id, 0, 0).ok);
  R1_EXPECT(set.setPanelSize(id, 50, 200).error == MenuError::OutOfRange);
  R1_EXPECT(set.setPanelSize(id, std::nan(""), 200).error == MenuError::OutOfRange);
  R1_EXPECT(set.setPanelSize(id, 200, HUGE_VAL).error == MenuError::OutOfRange);
  R1_EXPECT(set.find(id)->panel.width == 0.0);
  R1_EXPECT(set.setPieSlotCount(id, 4).error == MenuError::WrongKind);
  for (size_t i = set.find(id)->entries.size(); i < cm::kMaxPanelEntries; ++i) R1_EXPECT(set.addEntry(id, "c." + std::to_string(i)).ok);
  R1_EXPECT(set.addEntry(id, "overflow").error == MenuError::LimitReached);
}

void testPreviewAndListeners() {
  CustomMenuSet set;
  int calls = 0;
  const auto token = set.subscribe([&] { ++calls; });
  const std::string id = set.createMenu(MenuKind::Pie, "Pie").id;
  R1_EXPECT(calls == 1);
  const uint64_t version = set.version();
  const std::string before = cm::exportMenuFile(*set.find(id));
  // A dry run gives the real answer and changes nothing.
  const auto ok = set.preview([&](CustomMenuSet& s) { return s.setSlot(id, 2, "x.y"); });
  R1_EXPECT(ok.ok && set.find(id)->entries[2].commandId.empty());
  const auto bad = set.preview([&](CustomMenuSet& s) { return s.setSlot(id, 22, "x.y"); });
  R1_EXPECT(!bad.ok && bad.error == MenuError::OutOfRange);
  const auto created = set.preview([&](CustomMenuSet& s) { return s.createMenu(MenuKind::Panel, "Other"); });
  R1_EXPECT(created.ok && set.size() == 1 && set.nextSerial() == 2);
  R1_EXPECT(calls == 1 && set.version() == version && cm::exportMenuFile(*set.find(id)) == before);
  R1_EXPECT(!set.preview(nullptr).ok);
  // The next real create after a preview takes the serial the preview saw (nothing leaked).
  R1_EXPECT(set.createMenu(MenuKind::Panel, "Other").id == created.id);
  // Refused operations do not notify.
  const int callsBefore = calls;
  R1_EXPECT(!set.setSlot(id, 99, "a.b").ok);
  R1_EXPECT(calls == callsBefore);
  // A listener may unsubscribe itself and others while being called.
  CustomMenuSet::ListenerId second = 0;
  int secondCalls = 0;
  set.subscribe([&] { set.unsubscribe(second); });
  second = set.subscribe([&] { ++secondCalls; });
  set.renameMenu(id, "Renamed");
  set.renameMenu(id, "Renamed again");
  R1_EXPECT(secondCalls <= 1);
  set.unsubscribe(token);
}

void testAdoptAndReplace() {
  CustomMenuSet set;
  set.createMenu(MenuKind::Pie, "Tools");
  cm::CustomMenu incoming = samplePie("tools");
  const auto renamed = set.adopt(incoming, cm::CollisionPolicy::Rename);
  R1_EXPECT(renamed.ok && renamed.renamed && !renamed.replaced && renamed.name == "tools (2)");
  R1_EXPECT(set.find(renamed.id)->entries[0].commandId == "tool.move");
  const auto again = set.adopt(incoming, cm::CollisionPolicy::Rename);
  R1_EXPECT(again.name == "tools (3)" && set.size() == 3);
  const std::string firstId = set.menus()[0].id;
  const auto replaced = set.adopt(incoming, cm::CollisionPolicy::Replace);
  R1_EXPECT(replaced.ok && replaced.replaced && replaced.id == firstId && set.size() == 3);
  R1_EXPECT(set.find(firstId)->entries[2].label == "Rotate");
  // A menu that is not legal changes nothing.
  cm::CustomMenu broken = samplePie("Broken");
  broken.entries.resize(3);
  const uint64_t version = set.version();
  R1_EXPECT(!set.adopt(broken, cm::CollisionPolicy::Rename).ok && set.version() == version);
  cm::CustomMenu nameless = samplePie("  ");
  R1_EXPECT(set.adopt(nameless, cm::CollisionPolicy::Rename).error == MenuError::InvalidName);
  // A very long name still gets a numbered variant inside the limit.
  CustomMenuSet longSet;
  const std::string longName(cm::kMaxNameBytes, 'n');
  longSet.createMenu(MenuKind::Pie, longName);
  cm::CustomMenu longMenu = samplePie(longName);
  const auto lr = longSet.adopt(longMenu, cm::CollisionPolicy::Rename);
  R1_EXPECT(lr.ok && lr.name.size() <= cm::kMaxNameBytes && lr.name.ends_with(" (2)"));
}

void testLimitsAndReplaceAll() {
  CustomMenuSet set;
  for (size_t i = 0; i < 1000; ++i) R1_EXPECT(set.createMenu(i % 2 == 0 ? MenuKind::Pie : MenuKind::Panel, "Menu " + std::to_string(i)).ok);
  R1_EXPECT(set.size() == 1000);
  std::set<std::string> ids;
  for (const auto& m : set.menus()) ids.insert(m.id);
  R1_EXPECT(ids.size() == 1000);
  for (size_t i = 1000; i < cm::kMaxMenus; ++i) set.createMenu(MenuKind::Panel, "Menu " + std::to_string(i));
  R1_EXPECT(set.size() == cm::kMaxMenus);
  R1_EXPECT(set.createMenu(MenuKind::Pie, "One too many").error == MenuError::LimitReached);

  // replaceAll validates everything first.
  CustomMenuSet fresh;
  std::vector<cm::CustomMenu> good = {samplePie("A"), samplePanel("B")};
  R1_EXPECT(fresh.replaceAll(good, 1).ok && fresh.nextSerial() == 3 && fresh.size() == 2);
  const uint64_t version = fresh.version();
  std::vector<cm::CustomMenu> dupName = good;
  dupName[1].name = "a";
  R1_EXPECT(fresh.replaceAll(dupName, 5).error == MenuError::DuplicateName && fresh.size() == 2 && fresh.version() == version);
  std::vector<cm::CustomMenu> dupId = good;
  dupId[1].id = dupId[0].id;
  dupId[1].serial = dupId[0].serial;
  R1_EXPECT(!fresh.replaceAll(dupId, 5).ok);
  std::vector<cm::CustomMenu> mismatch = good;
  mismatch[1].serial = 9;
  R1_EXPECT(!fresh.replaceAll(mismatch, 5).ok);
  std::vector<cm::CustomMenu> hugeSerial = good;
  hugeSerial[1].serial = 0xFFFFFFFFu;
  hugeSerial[1].id = cm::menuIdFor(hugeSerial[1].serial);
  R1_EXPECT(!fresh.replaceAll(hugeSerial, 5).ok);
  R1_EXPECT(fresh.size() == 2 && fresh.version() == version);
}

// Random operations never leave an illegal set, whatever the arguments.
void testRandomOperations() {
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    Rng rng(seed);
    CustomMenuSet set;
    for (int step = 0; step < 400; ++step) {
      const std::string id = set.size() == 0 || rng.below(8) == 0 ? "menu.404" : set.menus()[rng.below(set.size())].id;
      const size_t index = rng.below(12);
      const std::string cmd = rng.below(5) == 0 ? randomGarbage(rng, 1 + rng.below(6)) : "c." + std::to_string(rng.below(30));
      switch (rng.below(14)) {
        case 0: set.createMenu(rng.below(2) ? MenuKind::Pie : MenuKind::Panel, rng.below(3) == 0 ? randomGarbage(rng, 4) : "M" + std::to_string(rng.below(40))); break;
        case 1: set.renameMenu(id, "M" + std::to_string(rng.below(40))); break;
        case 2: set.deleteMenu(id); break;
        case 3: set.setSlot(id, index, cmd, rng.below(2) ? "L" : randomGarbage(rng, 3), rng.below(3) ? "" : "icon"); break;
        case 4: set.clearSlot(id, index); break;
        case 5: set.addEntry(id, cmd, rng.below(3) == 0 ? CustomMenuSet::npos : index); break;
        case 6: set.moveEntry(id, index, rng.below(12)); break;
        case 7: set.removeEntry(id, index); break;
        case 8: set.setPieSlotCount(id, static_cast<int>(rng.below(10))); break;
        case 9: set.setPanelColumns(id, static_cast<int>(rng.below(16)) - 2); break;
        case 10: set.setPanelButtonSize(id, static_cast<int>(rng.below(120))); break;
        case 11: set.setPanelSize(id, static_cast<double>(rng.below(500)), static_cast<double>(rng.below(500))); break;
        case 12: set.setEntryAppearance(id, index, "T", ""); break;
        default: set.preview([&](CustomMenuSet& s) { return s.setSlot(id, index, cmd); }); break;
      }
      for (const auto& m : set.menus()) {
        std::string reason;
        if (!cm::validateMenu(m, reason)) {
          std::fprintf(stderr, "illegal menu after step %d (seed %llu): %s\n", step, static_cast<unsigned long long>(seed), reason.c_str());
          R1_EXPECT(false);
          return;
        }
      }
    }
    std::set<std::string> names;
    for (const auto& m : set.menus()) R1_EXPECT(names.insert(m.name).second);
  }
}

}  // namespace

int main() {
  testCreateRenameDelete();
  testPieOperations();
  testPanelOperations();
  testPreviewAndListeners();
  testAdoptAndReplace();
  testLimitsAndReplaceAll();
  testRandomOperations();
  return r1test::finish();
}

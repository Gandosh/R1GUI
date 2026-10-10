// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the tests of MenuDraft, the working copy behind the creator window: creating a pie and a panel,
//   every edit operation and its refusals, the dry runs, the name rules (required, unique, case-insensitive,
//   hostile), switching the type, editing an existing menu (rename, content, id and serial kept), commit
//   refusals that leave the live set untouched, and a menu deleted while its draft is open.
// Callers: CTest (label fast).
#include "MenuFixtures.h"
#include "r1ui/commands/custommenu/MenuDraft.h"

namespace {

using namespace r1test;
using cm::CustomMenuSet;
using cm::MenuDraft;
using cm::MenuError;
using cm::MenuKind;

void testPieDraft() {
  CustomMenuSet live;
  auto d = MenuDraft::create(MenuKind::Pie, "My pie");
  R1_EXPECT(d && !d->editing() && d->kind() == MenuKind::Pie && d->menu().slotCount == 8 && d->entryCount() == 8 && d->filledCount() == 0);
  R1_EXPECT(!d->changed());
  R1_EXPECT(d->issues(live).size() == 1);  // the name is fine, no action yet

  const uint64_t v0 = d->version();
  R1_EXPECT(d->setSlot(0, "tool.move").ok && d->setSlot(3, "tool.rotate", "Turn").ok && d->version() > v0);
  R1_EXPECT(d->menu().entries[3].label == "Turn" && d->filledCount() == 2 && d->changed());
  R1_EXPECT(d->addEntry("tool.scale").ok && d->menu().entries[1].commandId == "tool.scale");  // the first empty slot
  R1_EXPECT(d->moveEntry(0, 3).ok && d->menu().entries[3].commandId == "tool.move" && d->menu().entries[0].commandId == "tool.rotate");  // a swap
  R1_EXPECT(d->setLabel(0, "Spin").ok && d->menu().entries[0].label == "Spin");
  R1_EXPECT(d->clearSlot(1).ok && d->menu().entries[1].commandId.empty());
  R1_EXPECT(!d->setSlot(8, "tool.move").ok && d->setSlot(8, "tool.move").error == MenuError::OutOfRange);
  R1_EXPECT(!d->setSlot(0, "not a valid id!").ok);

  // Dry runs agree with the real answer and change nothing.
  const uint64_t v1 = d->version();
  R1_EXPECT(d->canSetSlot(5, "edit.undo").ok && !d->canSetSlot(9, "edit.undo").ok && d->canMoveEntry(0, 2).ok && !d->canMoveEntry(0, 99).ok);
  R1_EXPECT(d->version() == v1 && d->menu().entries[5].commandId.empty());

  // Fewer slots only when the vanishing ones are empty.
  R1_EXPECT(d->setSlot(7, "edit.redo").ok);
  const auto lose = d->setPieSlotCount(4);
  R1_EXPECT(!lose.ok && lose.error == MenuError::WouldLoseEntries && d->menu().slotCount == 8);
  R1_EXPECT(d->clearSlot(7).ok && d->setPieSlotCount(4).ok && d->entryCount() == 4);
  R1_EXPECT(!d->setPieSlotCount(5).ok);

  // A pie rejects panel settings.
  R1_EXPECT(!d->setPanelColumns(2).ok);

  // Commit creates a menu with a fresh id.
  R1_EXPECT(d->issues(live).empty());
  const auto made = d->commit(live);
  R1_EXPECT(made.ok && live.size() == 1 && live.find(made.id) != nullptr);
  const cm::CustomMenu* m = live.find(made.id);
  R1_EXPECT(m->name == "My pie" && m->kind == MenuKind::Pie && m->slotCount == 4 && m->entries[0].commandId == "tool.rotate" && m->entries[0].label == "Spin");
}

void testPanelDraft() {
  CustomMenuSet live;
  auto d = MenuDraft::create(MenuKind::Panel, "Tools");
  R1_EXPECT(d && d->kind() == MenuKind::Panel && d->entryCount() == 0);
  R1_EXPECT(d->addEntry("tool.move").ok && d->addEntry("tool.rotate").ok && d->addEntry("tool.scale", 0).ok);
  R1_EXPECT(d->menu().entries.size() == 3 && d->menu().entries[0].commandId == "tool.scale");
  R1_EXPECT(d->moveEntry(0, 2).ok && d->menu().entries[2].commandId == "tool.scale");
  R1_EXPECT(d->clearSlot(1).ok && d->entryCount() == 2);
  R1_EXPECT(d->setPanelColumns(2).ok && d->setPanelButtonSize(48).ok && d->setPanelShowLabels(false).ok);
  R1_EXPECT(!d->setPanelColumns(0).ok && !d->setPanelColumns(13).ok && !d->setPanelButtonSize(10).ok);
  R1_EXPECT(!d->setPieSlotCount(4).ok);
  R1_EXPECT(!d->addEntry("tool.move", 99).ok);
  const auto made = d->commit(live);
  R1_EXPECT(made.ok);
  const cm::CustomMenu* m = live.find(made.id);
  R1_EXPECT(m && m->panel.columns == 2 && m->panel.buttonSize == 48 && !m->panel.showLabels && m->entries.size() == 2);
}

void testNameRules() {
  CustomMenuSet live;
  live.createMenu(MenuKind::Pie, "Taken");
  auto d = MenuDraft::create(MenuKind::Pie, "Fresh");
  d->setSlot(0, "tool.move");
  R1_EXPECT(d->issues(live).empty());
  d->setName("");
  R1_EXPECT(d->issues(live).size() == 1 && d->issues(live)[0].find("name") != std::string::npos);
  d->setName("   \t ");
  R1_EXPECT(d->issues(live).size() == 1);
  d->setName("taken");  // ASCII case-insensitive
  R1_EXPECT(d->issues(live).size() == 1 && d->issues(live)[0].find("already exists") != std::string::npos);
  const auto refused = d->commit(live);
  R1_EXPECT(!refused.ok && live.size() == 1);  // nothing changed
  d->setName(std::string(10000, 'x'));         // cut to the model's limit, still a usable name
  R1_EXPECT(d->issues(live).empty());
  d->setName(std::string("bad\xff\xfe") + "name\x01");
  R1_EXPECT(d->issues(live).empty());
  const auto ok = d->commit(live);
  R1_EXPECT(ok.ok && live.size() == 2);
  const cm::CustomMenu* m = live.find(ok.id);
  R1_EXPECT(m && !m->name.empty() && m->name.size() <= cm::kMaxNameBytes && r1ui::commands::isValidUtf8(m->name));
}

void testSwitchKind() {
  CustomMenuSet live;
  auto d = MenuDraft::create(MenuKind::Pie, "X");
  d->setSlot(2, "tool.move");
  d->setSlot(5, "tool.rotate");
  R1_EXPECT(d->setKind(MenuKind::Panel).ok && d->kind() == MenuKind::Panel && d->entryCount() == 2);
  R1_EXPECT(d->menu().entries[0].commandId == "tool.move" && d->menu().entries[1].commandId == "tool.rotate");
  R1_EXPECT(d->setKind(MenuKind::Pie).ok && d->kind() == MenuKind::Pie && d->filledCount() == 2 && d->entryCount() == 8);
  // A panel with more entries than a pie holds keeps the first eight.
  d->setKind(MenuKind::Panel);
  for (int i = 0; i < 12; ++i) d->addEntry("cmd." + std::to_string(i));
  R1_EXPECT(d->setKind(MenuKind::Pie).ok && d->filledCount() == 8);
  R1_EXPECT(!d->changed() || d->changed());  // no crash; value is unspecified after a switch

  live.createMenu(MenuKind::Panel, "Existing");
  auto e = MenuDraft::edit(live, "menu.1");
  R1_EXPECT(e && !e->setKind(MenuKind::Pie).ok);  // an existing menu keeps its type
}

void testEditExisting() {
  CustomMenuSet live;
  const std::string a = live.createMenu(MenuKind::Pie, "Alpha").id;
  const std::string b = live.createMenu(MenuKind::Panel, "Beta").id;
  live.setSlot(a, 0, "tool.move");
  live.addEntry(b, "tool.rotate");
  const uint64_t liveVersion = live.version();

  R1_EXPECT(!MenuDraft::edit(live, "menu.99"));
  auto d = MenuDraft::edit(live, a);
  R1_EXPECT(d && d->editing() && d->editingId() == a && d->name() == "Alpha" && !d->changed());
  R1_EXPECT(d->menu().entries[0].commandId == "tool.move");
  d->setSlot(1, "tool.scale");
  d->setName("Alpha 2");
  R1_EXPECT(d->changed() && live.version() == liveVersion);  // the live set is untouched until commit
  R1_EXPECT(live.find(a)->entries[1].commandId.empty());
  d->setName("beta");  // the other menu's name
  R1_EXPECT(!d->issues(live).empty());
  d->setName("ALPHA");  // the menu's own name in other case is allowed
  R1_EXPECT(d->issues(live).empty());
  d->setName("Alpha 2");
  const auto done = d->commit(live);
  R1_EXPECT(done.ok && done.id == a && live.size() == 2);
  const cm::CustomMenu* m = live.find(a);
  R1_EXPECT(m && m->name == "Alpha 2" && m->serial == 1 && m->entries[1].commandId == "tool.scale" && live.version() > liveVersion);
  R1_EXPECT(live.find(b)->name == "Beta");

  // Cancel is just dropping the draft.
  {
    auto cancelled = MenuDraft::edit(live, b);
    cancelled->clearSlot(0);
    cancelled->setName("Gone");
  }
  R1_EXPECT(live.find(b)->name == "Beta" && live.find(b)->entries.size() == 1);

  // The menu is deleted while the draft is open.
  auto orphan = MenuDraft::edit(live, b);
  live.deleteMenu(b);
  orphan->setName("Anything");
  const auto lost = orphan->commit(live);
  R1_EXPECT(!lost.ok && lost.error == MenuError::UnknownMenu && live.size() == 1);

  // An edit that empties the menu cannot be committed.
  auto empty = MenuDraft::edit(live, a);
  empty->clearSlot(0);
  empty->clearSlot(1);
  R1_EXPECT(!empty->issues(live).empty() && !empty->commit(live).ok);
  R1_EXPECT(live.find(a)->entries[0].commandId == "tool.move");
}

}  // namespace

int main() {
  testPieDraft();
  testPanelDraft();
  testNameRules();
  testSwitchKind();
  testEditExisting();
  return r1test::finish();
}

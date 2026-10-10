// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CustomMenuSet, the live collection of one user's custom menus: creation, rename, deletion,
//   every entry and setting operation, adoption of a menu loaded from a .r1mn file (with name collision
//   handling), the version counter and the change listeners that make the UI follow.
// Why: the menu creator window (a later slice), the pie trigger, the dockable panels, the "Custom
//   Menus" main menu and the persistence all read one source of truth; every change is validated and
//   leaves the set legal, so no consumer ever sees a half-edited menu.
// Callers: the widget layer, the host (storage in CustomMenuIo.h), the creator window, tests.
// Editing contract (same as customize::Customization): operations never throw; each returns a
//   MenuEditResult; on failure nothing changed and `reason` is a sentence fit for a message line.
//   preview(op) runs any operation in dry-run mode (identical result, nothing stored, nobody notified)
//   so the creator window can enable a drop target or a button only where the edit would be accepted.
// Identity: a menu id ("menu.<serial>") is assigned at creation and never reused inside the set, even
//   after deletion (the serial counter only grows and is persisted), so a panel registration or a
//   stored reference can never silently point at a different menu.
// Names: unique inside the set, ASCII case-insensitive. Collisions on create and rename are refused
//   with DuplicateName; adopt() resolves them by policy (rename to "Name (2)" or replace).
// Pie rules: a pie holds exactly slotCount entries (4, 6 or 8). Reducing slotCount is refused with
//   WouldLoseEntries when a slot that would disappear holds a command, so no work is lost silently.
// Threading: UI thread only. Listeners run synchronously after a change, in subscription order, and
//   must not edit the set.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "r1ui/commands/custommenu/CustomMenu.h"

namespace r1ui::commands::custommenu {

enum class CollisionPolicy : uint8_t { Rename, Replace };

struct AdoptResult {
  bool ok = false;
  MenuError error = MenuError::None;
  std::string reason;
  std::string id;          // id of the menu now in the set
  std::string name;        // final name
  bool renamed = false;    // the name was changed to avoid a collision
  bool replaced = false;   // an existing menu of that name was replaced (its id and serial are kept)
  explicit operator bool() const { return ok; }
};

class CustomMenuSet {
 public:
  using Listener = std::function<void()>;
  using ListenerId = uint32_t;
  static constexpr size_t npos = static_cast<size_t>(-1);

  // ---- reading --------------------------------------------------------------------------------
  const std::vector<CustomMenu>& menus() const { return menus_; }
  const CustomMenu* find(const std::string& id) const;
  const CustomMenu* findByName(const std::string& name) const;
  size_t size() const { return menus_.size(); }
  size_t totalEntries() const;
  uint64_t version() const { return version_; }
  // Next serial that createMenu / adopt will use (persisted so deleted ids are never reused).
  uint32_t nextSerial() const { return nextSerial_; }
  // "Base", else "Base (2)", "Base (3)", ... that no menu uses; the base is cleaned first and the
  // numbered form still fits kMaxNameBytes. Empty only when the base cleans to nothing.
  std::string uniqueName(const std::string& base) const;

  ListenerId subscribe(Listener listener);
  void unsubscribe(ListenerId id);

  // Runs `op` (any editing operation) in dry-run mode: validated exactly as a real call, but nothing is
  // stored and nobody is notified. Cost: one copy of the set.
  MenuEditResult preview(const std::function<MenuEditResult(CustomMenuSet&)>& op);

  // ---- menus ----------------------------------------------------------------------------------
  // A new pie (8 empty slots) or panel (no entries). The name must be usable and unused.
  MenuEditResult createMenu(MenuKind kind, const std::string& name);
  MenuEditResult renameMenu(const std::string& id, const std::string& name);
  MenuEditResult deleteMenu(const std::string& id);
  // Adds a menu read from a file as a new menu (fresh id and serial). A name collision is resolved by
  // `policy`: Rename gives "Name (2)", Replace overwrites the menu of that name keeping its id.
  // The menu must pass validateMenu apart from its id, which is ignored.
  AdoptResult adopt(CustomMenu menu, CollisionPolicy policy);

  // ---- entries --------------------------------------------------------------------------------
  // Pie: puts the command in slot `index` (replacing what was there). Panel: replaces entry `index`.
  // label and icon are optional overrides.
  MenuEditResult setSlot(const std::string& id, size_t index, const std::string& commandId, const std::string& label = {}, const std::string& icon = {});
  // Pie: empties the slot. Panel: removes the entry.
  MenuEditResult clearSlot(const std::string& id, size_t index);
  // Pie: the first empty slot gets the command (LimitReached when none). Panel: inserts at `index`
  // (npos = at the end), LimitReached at kMaxPanelEntries.
  MenuEditResult addEntry(const std::string& id, const std::string& commandId, size_t index = npos, const std::string& label = {}, const std::string& icon = {});
  // Pie: swaps the two slots. Panel: moves entry `from` so that it ends up at index `to`.
  MenuEditResult moveEntry(const std::string& id, size_t from, size_t to);
  // Pie: empties the slot (same as clearSlot). Panel: removes the entry.
  MenuEditResult removeEntry(const std::string& id, size_t index);
  // Changes the label and icon override of an entry (empty = the command's own).
  MenuEditResult setEntryAppearance(const std::string& id, size_t index, const std::string& label, const std::string& icon);

  // ---- kind-specific settings -----------------------------------------------------------------
  MenuEditResult setPieSlotCount(const std::string& id, int slotCount);
  MenuEditResult setPanelColumns(const std::string& id, int columns);
  MenuEditResult setPanelButtonSize(const std::string& id, int buttonSize);
  MenuEditResult setPanelShowLabels(const std::string& id, bool show);
  // 0 for a dimension = let the dock decide.
  MenuEditResult setPanelSize(const std::string& id, double width, double height);

  // ---- loading --------------------------------------------------------------------------------
  // Replaces the whole content (a store load). Every menu must pass validateMenu with a unique id and
  // name, within the limits; otherwise nothing changes and the reason is returned. `nextSerial` is raised
  // above every serial in use.
  MenuEditResult replaceAll(std::vector<CustomMenu> menus, uint32_t nextSerial);

 private:
  CustomMenu* mutableFind(const std::string& id);
  MenuEditResult fail(MenuError error, std::string reason) const;
  MenuEditResult unknown(const std::string& id) const;
  MenuEditResult done(const std::string& id, size_t index = 0);
  void changed();

  std::vector<CustomMenu> menus_;
  uint32_t nextSerial_ = 1;
  uint64_t version_ = 1;
  bool dryRun_ = false;
  std::vector<std::pair<ListenerId, Listener>> listeners_;
  ListenerId nextListener_ = 1;
};

}  // namespace r1ui::commands::custommenu

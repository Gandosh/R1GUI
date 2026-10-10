// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: MenuDraft, the working copy behind the "Create Custom Menu" window (slice 5.17): one menu being
//   created or edited, the name the user typed, every edit operation (each one goes through the
//   CustomMenuSet API of a private working set, so the limits and refusals are the model's own), the
//   problems that stop a commit, and the commit into the live set.
// Why: the window must let a user drop actions, swap slots and change settings freely, and must be able to
//   throw all of it away (Cancel). Editing a private copy and committing on Create/Save gives that without
//   touching the live set (and so the dock panels, the Custom Menus menu and the files) before the user
//   decides. Decision recorded for the owner's "live or on Save" question: edits are applied on Save; Cancel
//   discards the copy, so there is nothing to revert.
// Callers: the creator window and its two preview editors (widgets), tests. Calls: CustomMenuSet only.
// Name: the typed name is kept as raw text (it may be empty or taken while the user is typing) and only
//   validated by issues(); the working menu carries a fixed placeholder name.
// Kind: a new draft may switch between pie and panel (the commands placed so far are carried over, as
//   many as fit); an edit keeps the kind of the menu it was made from.
// Commit: a new menu is adopted into the live set under the validated name; an edit renames the live menu
//   when the name changed and replaces its content keeping its id and serial. Both steps are dry-run first
//   (CustomMenuSet::preview), so a refusal leaves the live set untouched.
// Failure behavior: nothing throws; every operation returns the model's MenuEditResult.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "r1ui/commands/custommenu/CustomMenuSet.h"

namespace r1ui::commands::custommenu {

class MenuDraft {
 public:
  static constexpr size_t npos = CustomMenuSet::npos;

  // A new, empty menu of `kind` named `suggestedName` (the caller makes it unique with live.uniqueName).
  static std::unique_ptr<MenuDraft> create(MenuKind kind, const std::string& suggestedName);
  // A copy of an existing menu to edit; nullptr when the id is unknown.
  static std::unique_ptr<MenuDraft> edit(const CustomMenuSet& live, const std::string& menuId);
  // A new menu that starts with the content (kind, entries, settings, name) of `menu`, for example one
  // read from a .r1mn file; nullptr when the menu does not pass the model's validation.
  static std::unique_ptr<MenuDraft> fromContent(const CustomMenu& menu);

  bool editing() const { return !editingId_.empty(); }
  const std::string& editingId() const { return editingId_; }
  // The working menu (always present).
  const CustomMenu& menu() const;
  MenuKind kind() const { return menu().kind; }
  // Bumped by every successful change: widgets compare it to know when to rebuild.
  uint64_t version() const { return version_; }

  // ---- name ----
  const std::string& name() const { return name_; }
  void setName(const std::string& text);

  // ---- kind (a new menu only) ----
  MenuEditResult setKind(MenuKind kind);

  // ---- entries (pie: slots; panel: a list) ----
  MenuEditResult setSlot(size_t index, const std::string& commandId, const std::string& label = {});
  // Pie: the first empty slot. Panel: inserted at `index` (npos = at the end).
  MenuEditResult addEntry(const std::string& commandId, size_t index = npos);
  MenuEditResult moveEntry(size_t from, size_t to);
  MenuEditResult clearSlot(size_t index);
  MenuEditResult setLabel(size_t index, const std::string& label);
  // The entry count a drop may use: pie slotCount, panel entries.size().
  size_t entryCount() const { return menu().entries.size(); }
  size_t filledCount() const;

  // ---- settings ----
  MenuEditResult setPieSlotCount(int slotCount);
  MenuEditResult setPanelColumns(int columns);
  MenuEditResult setPanelButtonSize(int size);
  MenuEditResult setPanelShowLabels(bool show);

  // ---- dry runs (the same answer the real call would give, nothing changes) ----
  MenuEditResult canSetSlot(size_t index, const std::string& commandId);
  MenuEditResult canAddEntry(const std::string& commandId, size_t index = npos);
  MenuEditResult canMoveEntry(size_t from, size_t to);

  // ---- validation and commit ----
  // Sentences for a message line; empty = the draft can be committed into `live`.
  std::vector<std::string> issues(const CustomMenuSet& live) const;
  // Applies the draft to the live set. The result's id is the live menu's id.
  MenuEditResult commit(CustomMenuSet& live);
  // True when the draft differs from what it started as (name, kind, content).
  bool changed() const;
  // The menu as it would be stored: the working content under the cleaned typed name (id and serial are
  // the working copy's; the file writer ignores them). Empty name when the user typed none.
  CustomMenu snapshot() const;

 private:
  MenuDraft() = default;
  void seed(CustomMenu menu);
  // Runs a mutating operation on the working set and bumps the version when it succeeded.
  template <class Op>
  MenuEditResult run(Op&& op) {
    MenuEditResult result = op(work_, workId_);
    if (result.ok) ++version_;
    return result;
  }

  CustomMenuSet work_;
  std::string workId_;
  std::string name_;
  std::string editingId_;
  CustomMenu original_;
  std::string originalName_;
  uint64_t version_ = 1;
};

}  // namespace r1ui::commands::custommenu

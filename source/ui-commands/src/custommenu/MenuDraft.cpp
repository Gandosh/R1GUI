// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of MenuDraft.h.
// Invariants: the working set always holds exactly one menu (workId_); every edit is the model's own
//   operation on that set, so a draft can never hold something the live set would refuse; the live set is
//   touched only by commit(), after a dry run of the same steps succeeded.
// Callers: the creator window and its editors, tests.
#include "r1ui/commands/custommenu/MenuDraft.h"

#include <utility>

namespace r1ui::commands::custommenu {

namespace {

constexpr const char* kPlaceholderName = "Draft";

MenuEditResult refused(MenuError error, std::string reason) {
  MenuEditResult result;
  result.error = error;
  result.reason = std::move(reason);
  return result;
}

}  // namespace

// ---- construction -----------------------------------------------------------------------------

void MenuDraft::seed(CustomMenu menu) {
  menu.name = kPlaceholderName;
  work_ = CustomMenuSet();
  const AdoptResult adopted = work_.adopt(std::move(menu), CollisionPolicy::Rename);
  workId_ = adopted.ok ? adopted.id : std::string();
  if (const CustomMenu* m = work_.find(workId_)) original_ = *m;
}

std::unique_ptr<MenuDraft> MenuDraft::create(MenuKind kind, const std::string& suggestedName) {
  std::unique_ptr<MenuDraft> draft(new MenuDraft());
  CustomMenu menu = makeEmptyMenu(kind, kPlaceholderName);
  draft->seed(std::move(menu));
  draft->name_ = cleanName(suggestedName);
  draft->originalName_ = draft->name_;
  return draft->workId_.empty() ? nullptr : std::move(draft);
}

std::unique_ptr<MenuDraft> MenuDraft::edit(const CustomMenuSet& live, const std::string& menuId) {
  const CustomMenu* source = live.find(menuId);
  if (source == nullptr) return nullptr;
  std::unique_ptr<MenuDraft> draft(new MenuDraft());
  draft->seed(*source);
  draft->editingId_ = menuId;
  draft->name_ = source->name;
  draft->originalName_ = source->name;
  return draft->workId_.empty() ? nullptr : std::move(draft);
}

std::unique_ptr<MenuDraft> MenuDraft::fromContent(const CustomMenu& menu) {
  std::string reason;
  CustomMenu content = menu;
  content.name = kPlaceholderName;
  if (content.id.empty() || content.serial == 0) {
    content.serial = 1;
    content.id = menuIdFor(1);
  }
  if (!validateMenu(content, reason)) return nullptr;
  std::unique_ptr<MenuDraft> draft(new MenuDraft());
  draft->seed(std::move(content));
  if (draft->workId_.empty()) return nullptr;
  draft->name_ = cleanName(menu.name);
  draft->originalName_.clear();  // a loaded menu counts as changed: it has not been created yet
  return draft;
}

CustomMenu MenuDraft::snapshot() const {
  CustomMenu out = menu();
  out.name = cleanName(name_);
  return out;
}

const CustomMenu& MenuDraft::menu() const {
  // The working set is never empty after construction, so find() cannot fail; fall back to the snapshot
  // rather than dereference null if that invariant is ever broken.
  const CustomMenu* m = work_.find(workId_);
  return m != nullptr ? *m : original_;
}

void MenuDraft::setName(const std::string& text) {
  if (text == name_) return;
  name_ = text;
  ++version_;
}

size_t MenuDraft::filledCount() const {
  size_t count = 0;
  for (const MenuEntry& e : menu().entries) count += e.commandId.empty() ? 0 : 1;
  return count;
}

// ---- kind -------------------------------------------------------------------------------------

MenuEditResult MenuDraft::setKind(MenuKind kind) {
  if (editing()) return refused(MenuError::WrongKind, "the type of an existing menu cannot change");
  if (kind == menu().kind) {
    MenuEditResult same;
    same.ok = true;
    same.id = workId_;
    return same;
  }
  std::vector<std::string> commands;
  for (const MenuEntry& e : menu().entries) {
    if (!e.commandId.empty()) commands.push_back(e.commandId);
  }
  seed(makeEmptyMenu(kind, kPlaceholderName));
  for (const std::string& command : commands) {
    if (!work_.addEntry(workId_, command).ok) break;  // a pie holds at most its slot count
  }
  original_ = menu();  // switching is not a content change by itself
  ++version_;
  MenuEditResult done;
  done.ok = true;
  done.id = workId_;
  return done;
}

// ---- entries ----------------------------------------------------------------------------------

MenuEditResult MenuDraft::setSlot(size_t index, const std::string& commandId, const std::string& label) {
  return run([&](CustomMenuSet& s, const std::string& id) { return s.setSlot(id, index, commandId, label); });
}

MenuEditResult MenuDraft::addEntry(const std::string& commandId, size_t index) {
  return run([&](CustomMenuSet& s, const std::string& id) { return s.addEntry(id, commandId, index); });
}

MenuEditResult MenuDraft::moveEntry(size_t from, size_t to) {
  return run([&](CustomMenuSet& s, const std::string& id) { return s.moveEntry(id, from, to); });
}

MenuEditResult MenuDraft::clearSlot(size_t index) {
  return run([&](CustomMenuSet& s, const std::string& id) { return s.clearSlot(id, index); });
}

MenuEditResult MenuDraft::setLabel(size_t index, const std::string& label) {
  if (index >= menu().entries.size()) return refused(MenuError::OutOfRange, "no such entry");
  const std::string icon = menu().entries[index].icon;
  return run([&](CustomMenuSet& s, const std::string& id) { return s.setEntryAppearance(id, index, label, icon); });
}

// ---- settings ---------------------------------------------------------------------------------

MenuEditResult MenuDraft::setPieSlotCount(int slotCount) {
  return run([&](CustomMenuSet& s, const std::string& id) { return s.setPieSlotCount(id, slotCount); });
}

MenuEditResult MenuDraft::setPanelColumns(int columns) {
  return run([&](CustomMenuSet& s, const std::string& id) { return s.setPanelColumns(id, columns); });
}

MenuEditResult MenuDraft::setPanelButtonSize(int size) {
  return run([&](CustomMenuSet& s, const std::string& id) { return s.setPanelButtonSize(id, size); });
}

MenuEditResult MenuDraft::setPanelShowLabels(bool show) {
  return run([&](CustomMenuSet& s, const std::string& id) { return s.setPanelShowLabels(id, show); });
}

// ---- dry runs ---------------------------------------------------------------------------------

MenuEditResult MenuDraft::canSetSlot(size_t index, const std::string& commandId) {
  return work_.preview([&](CustomMenuSet& s) { return s.setSlot(workId_, index, commandId); });
}

MenuEditResult MenuDraft::canAddEntry(const std::string& commandId, size_t index) {
  return work_.preview([&](CustomMenuSet& s) { return s.addEntry(workId_, commandId, index); });
}

MenuEditResult MenuDraft::canMoveEntry(size_t from, size_t to) {
  return work_.preview([&](CustomMenuSet& s) { return s.moveEntry(workId_, from, to); });
}

// ---- validation and commit --------------------------------------------------------------------

std::vector<std::string> MenuDraft::issues(const CustomMenuSet& live) const {
  std::vector<std::string> out;
  const std::string cleaned = cleanName(name_);
  if (cleaned.empty()) {
    out.push_back("Enter a name for the menu.");
  } else {
    for (const CustomMenu& other : live.menus()) {
      if (other.id != editingId_ && sameName(other.name, cleaned)) {
        out.push_back("A custom menu named \"" + other.name + "\" already exists. Choose another name.");
        break;
      }
    }
  }
  if (filledCount() == 0) out.push_back(kind() == MenuKind::Pie ? "Add at least one action to a slot." : "Add at least one action to the panel.");
  return out;
}

bool MenuDraft::changed() const {
  return name_ != originalName_ || menu().kind != original_.kind || menu().slotCount != original_.slotCount || menu().entries != original_.entries ||
         menu().panel != original_.panel;
}

MenuEditResult MenuDraft::commit(CustomMenuSet& live) {
  const std::vector<std::string> problems = issues(live);
  if (!problems.empty()) return refused(MenuError::Invalid, problems.front());
  CustomMenu content = menu();
  content.name = cleanName(name_);

  if (!editing()) {
    const AdoptResult adopted = live.adopt(std::move(content), CollisionPolicy::Rename);
    MenuEditResult result;
    result.ok = adopted.ok;
    result.error = adopted.error;
    result.reason = adopted.reason;
    result.id = adopted.id;
    return result;
  }

  const CustomMenu* existing = live.find(editingId_);
  if (existing == nullptr) return refused(MenuError::UnknownMenu, "the menu was deleted while it was being edited");
  const bool renamed = existing->name != content.name;
  const std::string id = editingId_;
  const auto apply = [&](CustomMenuSet& target) {
    MenuEditResult result;
    if (renamed) {
      result = target.renameMenu(id, content.name);
      if (!result.ok) return result;
    }
    const AdoptResult adopted = target.adopt(content, CollisionPolicy::Replace);
    result.ok = adopted.ok;
    result.error = adopted.error;
    result.reason = adopted.reason;
    result.id = adopted.id;
    return result;
  };
  const MenuEditResult dry = live.preview(apply);
  if (!dry.ok) return dry;
  return apply(live);
}

}  // namespace r1ui::commands::custommenu

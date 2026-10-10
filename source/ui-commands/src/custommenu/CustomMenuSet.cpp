// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CustomMenuSet (CustomMenuSet.h): lookup, menu creation / rename / deletion / adoption, loading,
//   the listener list and preview. Entry and setting operations live in CustomMenuSetEdit.cpp.
// Invariants: after every public call the set is legal: ids and names unique, every menu passes
//   validateMenu, counts within the limits, nextSerial above every serial in use. A refused call leaves
//   the set (and the version) untouched. preview() restores the exact previous state.
// Callers: widget layer, hosts, tests.
#include <algorithm>
#include <limits>
#include <unordered_set>

#include "r1ui/commands/custommenu/CustomMenuSet.h"

namespace r1ui::commands::custommenu {

// ---- helpers ------------------------------------------------------------------------------------

MenuEditResult CustomMenuSet::fail(MenuError error, std::string reason) const {
  MenuEditResult r;
  r.error = error;
  r.reason = std::move(reason);
  return r;
}

MenuEditResult CustomMenuSet::unknown(const std::string& id) const { return fail(MenuError::UnknownMenu, "there is no custom menu '" + id + "'"); }

MenuEditResult CustomMenuSet::done(const std::string& id, size_t index) {
  MenuEditResult r;
  r.ok = true;
  r.id = id;
  r.index = index;
  changed();
  return r;
}

void CustomMenuSet::changed() {
  if (dryRun_) return;
  ++version_;
  // Listeners run on a copy: one may unsubscribe itself (or another) while being called.
  const auto copy = listeners_;
  for (const auto& entry : copy) {
    if (entry.second) entry.second();
  }
}

CustomMenu* CustomMenuSet::mutableFind(const std::string& id) {
  for (CustomMenu& m : menus_) {
    if (m.id == id) return &m;
  }
  return nullptr;
}

const CustomMenu* CustomMenuSet::find(const std::string& id) const {
  for (const CustomMenu& m : menus_) {
    if (m.id == id) return &m;
  }
  return nullptr;
}

const CustomMenu* CustomMenuSet::findByName(const std::string& name) const {
  const std::string clean = cleanName(name);
  for (const CustomMenu& m : menus_) {
    if (sameName(m.name, clean)) return &m;
  }
  return nullptr;
}

size_t CustomMenuSet::totalEntries() const {
  size_t total = 0;
  for (const CustomMenu& m : menus_) {
    for (const MenuEntry& e : m.entries) total += e.commandId.empty() ? 0 : 1;
  }
  return total;
}

CustomMenuSet::ListenerId CustomMenuSet::subscribe(Listener listener) {
  const ListenerId id = nextListener_++;
  listeners_.emplace_back(id, std::move(listener));
  return id;
}

void CustomMenuSet::unsubscribe(ListenerId id) {
  listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(), [id](const auto& l) { return l.first == id; }), listeners_.end());
}

MenuEditResult CustomMenuSet::preview(const std::function<MenuEditResult(CustomMenuSet&)>& op) {
  if (!op || dryRun_) return fail(MenuError::Invalid, "nothing to preview");
  const std::vector<CustomMenu> savedMenus = menus_;
  const uint32_t savedSerial = nextSerial_;
  const uint64_t savedVersion = version_;
  dryRun_ = true;
  MenuEditResult result = op(*this);
  dryRun_ = false;
  menus_ = savedMenus;
  nextSerial_ = savedSerial;
  version_ = savedVersion;
  return result;
}

std::string CustomMenuSet::uniqueName(const std::string& base) const {
  const std::string clean = cleanName(base);
  if (clean.empty()) return {};
  if (findByName(clean) == nullptr) return clean;
  for (size_t n = 2; n < menus_.size() + 3; ++n) {
    const std::string suffix = " (" + std::to_string(n) + ")";
    size_t keep = std::min(clean.size(), kMaxNameBytes - suffix.size());
    // Never cut inside a UTF-8 sequence: back up over continuation bytes.
    while (keep > 0 && keep < clean.size() && (static_cast<unsigned char>(clean[keep]) & 0xC0) == 0x80) --keep;
    std::string candidate = clean.substr(0, keep);
    while (!candidate.empty() && candidate.back() == ' ') candidate.pop_back();
    candidate += suffix;
    if (findByName(candidate) == nullptr) return candidate;
  }
  return {};
}

// ---- menus --------------------------------------------------------------------------------------

MenuEditResult CustomMenuSet::createMenu(MenuKind kind, const std::string& name) {
  const std::string clean = cleanName(name);
  if (clean.empty()) return fail(MenuError::InvalidName, "give the menu a name");
  if (findByName(clean) != nullptr) return fail(MenuError::DuplicateName, "a custom menu named '" + clean + "' already exists");
  if (menus_.size() >= kMaxMenus) return fail(MenuError::LimitReached, "too many custom menus");
  if (nextSerial_ == std::numeric_limits<uint32_t>::max()) return fail(MenuError::LimitReached, "the menu counter is exhausted");
  CustomMenu menu = makeEmptyMenu(kind, clean);
  menu.serial = nextSerial_++;
  menu.id = menuIdFor(menu.serial);
  const std::string id = menu.id;
  menus_.push_back(std::move(menu));
  return done(id);
}

MenuEditResult CustomMenuSet::renameMenu(const std::string& id, const std::string& name) {
  CustomMenu* menu = mutableFind(id);
  if (menu == nullptr) return unknown(id);
  const std::string clean = cleanName(name);
  if (clean.empty()) return fail(MenuError::InvalidName, "give the menu a name");
  for (const CustomMenu& other : menus_) {
    if (other.id != id && sameName(other.name, clean)) return fail(MenuError::DuplicateName, "a custom menu named '" + clean + "' already exists");
  }
  menu->name = clean;
  return done(id);
}

MenuEditResult CustomMenuSet::deleteMenu(const std::string& id) {
  const auto it = std::find_if(menus_.begin(), menus_.end(), [&](const CustomMenu& m) { return m.id == id; });
  if (it == menus_.end()) return unknown(id);
  menus_.erase(it);
  return done(id);
}

AdoptResult CustomMenuSet::adopt(CustomMenu menu, CollisionPolicy policy) {
  AdoptResult result;
  const auto refuse = [&](MenuError error, std::string reason) {
    result.error = error;
    result.reason = std::move(reason);
    return result;
  };
  menu.name = cleanName(menu.name);
  if (menu.name.empty()) return refuse(MenuError::InvalidName, "the menu has no name");
  CustomMenu* existing = nullptr;
  for (CustomMenu& m : menus_) {
    if (sameName(m.name, menu.name)) existing = &m;
  }
  bool replacing = existing != nullptr && policy == CollisionPolicy::Replace;
  if (existing != nullptr && !replacing) {
    const std::string unique = uniqueName(menu.name);
    if (unique.empty()) return refuse(MenuError::DuplicateName, "no free name for the menu");
    menu.name = unique;
    result.renamed = true;
  }
  if (!replacing && menus_.size() >= kMaxMenus) return refuse(MenuError::LimitReached, "too many custom menus");
  if (!replacing && nextSerial_ == std::numeric_limits<uint32_t>::max()) return refuse(MenuError::LimitReached, "the menu counter is exhausted");
  if (replacing) {
    menu.serial = existing->serial;
    menu.id = existing->id;
  } else {
    menu.serial = nextSerial_;
    menu.id = menuIdFor(menu.serial);
  }
  std::string reason;
  if (!validateMenu(menu, reason)) return refuse(MenuError::Invalid, reason);
  size_t incoming = 0;
  for (const MenuEntry& e : menu.entries) incoming += e.commandId.empty() ? 0 : 1;
  size_t outgoing = 0;
  if (replacing) {
    for (const MenuEntry& e : existing->entries) outgoing += e.commandId.empty() ? 0 : 1;
  }
  if (totalEntries() - outgoing + incoming > kMaxTotalEntries) return refuse(MenuError::LimitReached, "too many entries in the custom menus");
  result.ok = true;
  result.id = menu.id;
  result.name = menu.name;
  result.replaced = replacing;
  if (replacing) {
    *existing = std::move(menu);
  } else {
    ++nextSerial_;
    menus_.push_back(std::move(menu));
  }
  changed();
  return result;
}

MenuEditResult CustomMenuSet::replaceAll(std::vector<CustomMenu> menus, uint32_t nextSerial) {
  if (menus.size() > kMaxMenus) return fail(MenuError::LimitReached, "too many custom menus");
  std::unordered_set<std::string> ids;
  std::vector<std::string> names;
  size_t total = 0;
  uint32_t highest = 0;
  for (const CustomMenu& m : menus) {
    std::string reason;
    if (!validateMenu(m, reason)) return fail(MenuError::Invalid, "menu '" + m.name + "': " + reason);
    if (m.id != menuIdFor(m.serial) || m.serial == 0) return fail(MenuError::Invalid, "menu '" + m.name + "': the id does not match its serial");
    if (!ids.insert(m.id).second) return fail(MenuError::Invalid, "two menus share the id " + m.id);
    for (const std::string& n : names) {
      if (sameName(n, m.name)) return fail(MenuError::DuplicateName, "two menus are named '" + m.name + "'");
    }
    names.push_back(m.name);
    for (const MenuEntry& e : m.entries) total += e.commandId.empty() ? 0 : 1;
    highest = std::max(highest, m.serial);
  }
  if (total > kMaxTotalEntries) return fail(MenuError::LimitReached, "too many entries in the custom menus");
  if (highest == std::numeric_limits<uint32_t>::max()) return fail(MenuError::Invalid, "a menu serial is out of range");
  menus_ = std::move(menus);
  nextSerial_ = std::max(nextSerial, highest + 1);
  return done({});
}

}  // namespace r1ui::commands::custommenu

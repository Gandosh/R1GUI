// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the file side of custom menus (slice 5.16): the .r1mn file of one menu (export, strict parse,
//   save and load through files, import into a set), and the JSON store of the whole set (export, strict
//   parse, load/save through a customize::TextStore, CustomMenuStorage that saves on every change).
// Why: owner requirement 2026-10-10: "Save custom menu" writes one menu to a .r1mn file that a user can
//   load after a clean installation to recreate it; the user's own menus also survive a restart. Damaged
//   or hostile files must never change the live state or crash the application.
// Callers: the host (menu commands, startup), the creator window, tests.
//
// .r1mn file (version 1), one JSON object with this member order (a pie shown first, a panel second):
//   {"format":"r1ui-custom-menu","version":1,"kind":"pie","name":TEXT,"slotCount":4|6|8,
//    "slots":[null|{"command":ID,"label":TEXT,"icon":NAME}, ...]}            exactly slotCount items
//   {"format":"r1ui-custom-menu","version":1,"kind":"panel","name":TEXT,"columns":N,"buttonSize":N,
//    "showLabels":BOOL,"panelSize":[W,H],"entries":[{"command":ID,"label":TEXT,"icon":NAME}, ...]}
//   label and icon are optional overrides. Unknown members are ignored. Unknown command ids are KEPT
//   (the host shows them as missing); command ids that are not even well-formed are not.
// The set store is {"format":"r1ui-custom-menus","version":1,"serial":N,"menus":[MENU, ...]} where each
//   MENU is the object above without format/version but with "id":"menu.N".
//
// Rejection of the WHOLE file (nothing changes): larger than the limit, not valid JSON (including invalid
//   UTF-8, duplicate keys, nesting deeper than 8), not this format, a newer version, a kind that is not
//   pie or panel, no usable name, a pie whose slots do not match its slotCount, more than
//   kMaxPanelEntries entries, or (set store) more than kMaxMenus menus / no usable menu in a non-empty
//   list. Single problems are repaired or skipped with an issue: a text cleaned or cut, an entry whose
//   command id is malformed (empty slot / dropped entry), an icon override that is not a valid icon
//   name (dropped), panel settings out of range (default used), a menu of a set store that fails
//   validation or repeats an id or name (skipped).
//
// Name collisions on load are the caller's decision: importMenuFile(set, ..., CollisionPolicy) renames to
//   "Name (2)" or replaces the menu of that name. Writes are atomic (TextFile.h). Nothing throws.
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/commands/custommenu/CustomMenuSet.h"
#include "r1ui/commands/customize/CustomizationIo.h"

namespace r1ui::commands::custommenu {

inline constexpr const char* kMenuFileFormat = "r1ui-custom-menu";
inline constexpr const char* kSetFileFormat = "r1ui-custom-menus";
inline constexpr int kMenuFileVersion = 1;
inline constexpr const char* kMenuFileExtension = ".r1mn";
inline constexpr size_t kMaxMenuFileBytes = size_t{1} << 20;
inline constexpr size_t kMaxSetFileBytes = size_t{24} << 20;

struct MenuIssue {
  std::string where;  // "slots[3]"
  std::string message;
};

// ---- one menu (.r1mn) --------------------------------------------------------------------------

struct MenuParseResult {
  bool ok = false;
  std::string error;  // why the file was rejected as a whole
  CustomMenu menu;    // id and serial are empty / 0: the set assigns them
  std::vector<MenuIssue> issues;
  size_t repaired = 0;  // pieces repaired or dropped
};

std::string exportMenuFile(const CustomMenu& menu);
MenuParseResult parseMenuFile(std::string_view text);

// The path with ".r1mn" appended when it has no extension (a path with another extension is kept).
std::filesystem::path withMenuExtension(const std::filesystem::path& path);
bool saveMenuFile(const CustomMenu& menu, const std::filesystem::path& path, std::string& error);
MenuParseResult loadMenuFile(const std::filesystem::path& path);

struct ImportMenuResult {
  bool ok = false;
  std::string error;  // file problem or refusal; nothing changed when !ok
  AdoptResult adopted;
  std::vector<MenuIssue> issues;
};

// Reads a .r1mn file and adds its menu to `set` (see CollisionPolicy).
ImportMenuResult importMenuFile(CustomMenuSet& set, const std::filesystem::path& path, CollisionPolicy policy);
ImportMenuResult importMenuText(CustomMenuSet& set, std::string_view text, CollisionPolicy policy);

// ---- the whole set ----------------------------------------------------------------------------

struct SetParseResult {
  bool ok = false;
  std::string error;
  std::vector<CustomMenu> menus;
  uint32_t nextSerial = 1;
  std::vector<MenuIssue> issues;
  size_t skipped = 0;
};

std::string exportSet(const CustomMenuSet& set);
SetParseResult parseSet(std::string_view text);

struct MenuLoadReport {
  bool ok = true;
  bool loaded = false;
  std::string error;
  std::vector<MenuIssue> issues;
  std::string keptAside;  // new name of a corrupt file
};

// Reads the store into `set`. A missing file leaves the set as it is; a rejected or unreadable file
// changes nothing and a corrupt one is moved aside.
MenuLoadReport loadMenus(CustomMenuSet& set, customize::TextStore& store);
bool saveMenus(const CustomMenuSet& set, customize::TextStore& store, std::string& error);

// Saves the set to its store after every change (the changes are rare user actions, so the write is
// synchronous). A failed save is remembered in lastError() and retried at the next change or save().
class CustomMenuStorage {
 public:
  CustomMenuStorage(CustomMenuSet& set, customize::TextStore& store);
  ~CustomMenuStorage();
  CustomMenuStorage(const CustomMenuStorage&) = delete;
  CustomMenuStorage& operator=(const CustomMenuStorage&) = delete;

  MenuLoadReport load();
  bool save();
  const std::string& lastError() const { return lastError_; }
  // Suspends the save-on-change (during load, and by hosts that batch edits); save() still works.
  void setAutoSave(bool on) { autoSave_ = on; }

 private:
  CustomMenuSet& set_;
  customize::TextStore& store_;
  CustomMenuSet::ListenerId listener_ = 0;
  bool autoSave_ = true;
  std::string lastError_;
};

}  // namespace r1ui::commands::custommenu

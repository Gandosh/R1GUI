// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the file side of the brush library (slice 5.20): the versioned JSON of the user's state
//   (favourites, recents, letter overrides, the pick-on-unique option), strict validating parse, export,
//   load and save through a customize::TextStore, and BrushStateStorage that saves after every change.
// Why: favourites, recents and letters must survive a restart, and a damaged or hostile file must never
//   change the live state or crash the application (guard register: file/serialization security).
// Format (version 1), deterministic text:
//   {"format":"r1ui-brush-library","version":1,"pickOnUnique":true,
//    "favourites":["id", ...],"recents":["id", ...],"letters":[{"brush":"id","letter":"x"}, ...]}
//   Unknown members are ignored. Ids need not be listed by the host right now.
// Rejection of the WHOLE file (nothing changes, the file is moved aside as "<name>.corrupt-<n>"): larger than
//   kMaxBrushStateBytes, not valid JSON (this includes invalid UTF-8, duplicate keys and nesting deeper
//   than 6), not an object, wrong format name, no valid version or a newer one, a member of the wrong type.
//   Single entries are skipped or repaired with an issue: an id that is empty, not a string, longer than
//   the id limit or a duplicate, a letter that is not one letter or digit, entries past the limits
//   (kMaxStateEntries; kMaxRecents for recents).
// Stores: customize::TextStore (memory for tests, FileTextStore for the application: temporary file then
//   atomic rename, size limit on load, setAside for a corrupt file). Nothing throws.
// Callers: the host at startup and shutdown, tests.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "r1ui/commands/brushes/BrushLibraryModel.h"
#include "r1ui/commands/customize/CustomizationIo.h"

namespace r1ui::commands::brushes {

inline constexpr const char* kBrushStateFormat = "r1ui-brush-library";
inline constexpr int kBrushStateVersion = 1;
inline constexpr size_t kMaxBrushStateBytes = size_t{4} << 20;

struct BrushStateIssue {
  std::string where;  // "favourites[3]"
  std::string message;
};

struct BrushStateParse {
  bool ok = false;
  std::string error;  // why the whole file was rejected
  BrushUserState state;
  std::vector<BrushStateIssue> issues;
};

std::string exportBrushState(const BrushUserState& state);
inline std::string exportBrushState(const BrushLibraryModel& model) { return exportBrushState(model.userState()); }
BrushStateParse parseBrushState(std::string_view json);

struct BrushStateLoadReport {
  bool ok = true;           // false when the file was rejected or could not be read
  bool loaded = false;      // a file was applied
  std::string error;
  std::string keptAside;    // new name of a corrupt file
  std::vector<BrushStateIssue> issues;
};

// Applies the stored state to the model. A missing file changes nothing (ok, not loaded); a rejected file
// changes nothing and is moved aside.
BrushStateLoadReport loadBrushState(BrushLibraryModel& model, customize::TextStore& store);
bool saveBrushState(const BrushLibraryModel& model, customize::TextStore& store, std::string& error);

// Wires a model to its store: saves whenever the persisted state changed (not for a loading, not for a
// change of the brush list or the active brush).
class BrushStateStorage {
 public:
  BrushStateStorage(BrushLibraryModel& model, customize::TextStore& store);
  ~BrushStateStorage();
  BrushStateStorage(const BrushStateStorage&) = delete;
  BrushStateStorage& operator=(const BrushStateStorage&) = delete;

  BrushStateLoadReport load();
  bool save();
  void setAutoSave(bool on) { autoSave_ = on; }
  const std::string& lastError() const { return lastError_; }
  size_t saves() const { return saves_; }

 private:
  BrushLibraryModel& model_;
  customize::TextStore& store_;
  BrushLibraryModel::ListenerId listener_ = 0;
  uint64_t savedVersion_ = 0;
  bool autoSave_ = true;
  size_t saves_ = 0;
  std::string lastError_;
};

}  // namespace r1ui::commands::brushes

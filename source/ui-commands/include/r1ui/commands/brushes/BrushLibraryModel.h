// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the headless brush library (slice 5.20): the host-supplied brush definitions (sanitised), the
//   user's favourites, recently used brushes and letter overrides, the quick letter algorithm (keys,
//   badges, conflicts) and query(), which turns what the user typed into the ordered tiles of the popup.
// Why: owner requirement 2026-10-10: pressing B opens a library whose brushes are reached by typing their
//   letters. Every rule that is not painting (what a letter means, which tile comes first, when a prefix
//   is unique, what is persisted) lives here so it is testable without a window and shared by any host.
// Callers: the host (setBrushes, setActiveId, persistence), BrushLibraryController and the popup in
//   ui-widgets, tests. Calls: Text.h (sanitising), BrushLetters.h. Host-neutral: no application is named.
//
// Brush data: BrushInfo is validated at this boundary. Ids are 1..128 bytes of text (control characters
//   become spaces); a duplicate id, an empty id or an entry past kMaxBrushes is rejected with an issue; an
//   empty name shows the id, a name is cut at kMaxBrushNameBytes (an issue says so), a letter override that
//   is not one letter or digit is dropped. Nothing throws.
// Keys: a brush's key is its optional single-letter override (the user's, else the host's) followed by the
//   letters and digits of its name, folded (BrushLetters.h). The override takes the first position, so a
//   brush with the override X answers to X and no longer to its own first letter. The badge of a brush is
//   the shortest prefix of its key that no other enabled brush shares; when no prefix is unique (equal keys,
//   or one key is the start of another) the badge is the whole key and the brush needs Enter.
// Conflicts: two enabled brushes with the same non-empty key cannot be told apart by typing; conflicts()
//   lists them (best tile first), unkeyableCount() the brushes whose key is empty (reachable by arrows,
//   search and mouse only).
// User state: favourites (in the order they were added), recents (most recent first, at most 8) and user
//   letters are keyed by brush id and kept even for ids the host does not list right now, so a brush that
//   is missing for one session does not lose its star. stateVersion() moves only when something that is
//   persisted changed; version() moves on every change. Listeners are called synchronously after a change,
//   on a copy of the listener list.
// Performance: derived data (keys, badges, order) is rebuilt once per change of the brush list or the
//   letters, in O(n log n); query() is O(n) plus a sort of the matches (about 1 ms for 2000 brushes).
// Threading: one thread (the UI thread).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "r1ui/commands/brushes/BrushQuery.h"

namespace r1ui::commands::brushes {

inline constexpr size_t kMaxBrushes = 20000;
inline constexpr size_t kMaxBrushIdBytes = 128;
inline constexpr size_t kMaxBrushNameBytes = 256;
inline constexpr size_t kMaxRecents = 8;
inline constexpr size_t kMaxStateEntries = 20000;  // favourites and letter overrides each

// One brush as the host describes it.
struct BrushInfo {
  std::string id;            // stable, unique
  std::string name;
  std::string category;      // empty = "Other"
  std::string description;
  std::string icon;          // icon name shown until a picture exists
  std::string thumbnailKey;  // key of the picture for the provider; empty = the id (equal keys share one picture)
  std::string letter;        // host's letter override: one letter or digit, or empty
  bool enabled = true;
};

struct BrushIssue {
  std::string where;  // "brush 12 (clay)"
  std::string message;
};

struct SetBrushesResult {
  size_t accepted = 0;
  size_t rejected = 0;
  std::vector<BrushIssue> issues;  // at most 64
};

enum class LetterStatus : uint8_t { Ok, UnknownBrush, NotSingleLetter, InvalidLetter };

struct LetterResult {
  LetterStatus status = LetterStatus::Ok;
  size_t sharedWith = 0;  // other enabled brushes whose key starts with the same letter after the change
  std::string message;    // one sentence for the user
  bool ok() const { return status == LetterStatus::Ok; }
};

// Enabled brushes that cannot be told apart by typing (same key); `brushes` best tile first.
struct LetterConflict {
  std::string key;  // upper-case
  std::vector<uint32_t> brushes;
};

// What is persisted (BrushState.h writes and reads it).
struct BrushUserState {
  std::vector<std::string> favourites;
  std::vector<std::string> recents;
  std::vector<std::pair<std::string, std::string>> letters;  // brush id, letter (UTF-8)
  bool pickOnUnique = true;
};

class BrushLibraryModel {
 public:
  using ListenerId = uint64_t;

  BrushLibraryModel();
  ~BrushLibraryModel();
  BrushLibraryModel(const BrushLibraryModel&) = delete;
  BrushLibraryModel& operator=(const BrushLibraryModel&) = delete;

  // ---- brushes ----
  // Replaces the whole list (after sanitising; rejected entries are skipped). The active brush is kept
  // when it is still listed.
  SetBrushesResult setBrushes(std::vector<BrushInfo> brushes);
  const std::vector<BrushInfo>& brushes() const { return infos_; }
  size_t size() const { return infos_.size(); }
  std::optional<uint32_t> indexOfId(std::string_view id) const;
  // Picture keys: unique per distinct thumbnailKey (or id); the provider maps one back with indexOfThumbnailKey.
  uint64_t thumbnailKey(uint32_t index) const;
  std::optional<uint32_t> indexOfThumbnailKey(uint64_t key) const;
  // Distinct categories, alphabetical.
  const std::vector<std::string>& categories() const;

  // The brush the host has active (outlined in the popup); not persisted. Unknown ids are kept, matched by id.
  const std::string& activeId() const { return activeId_; }
  void setActiveId(std::string id);

  // ---- user state ----
  bool isFavourite(std::string_view id) const { return favouriteSet_.count(std::string(id)) != 0; }
  // False for an unknown brush or at the limit. A repeated call with the same value changes nothing.
  bool setFavourite(std::string_view id, bool favourite);
  const std::vector<std::string>& favourites() const { return favourites_; }
  // Most recent first, at most kMaxRecents; includes ids the host does not list now.
  const std::vector<std::string>& recents() const { return recents_; }
  // Moves a listed, enabled brush to the front of the recents.
  bool noteUsed(std::string_view id);
  // `letter` is one letter or digit (UTF-8); empty clears the override.
  LetterResult setUserLetter(std::string_view id, std::string_view letter);
  // The user's override of a brush as displayed (upper case, UTF-8), empty when none.
  std::string userLetter(std::string_view id) const;
  bool pickOnUniqueOption() const { return pickOnUnique_; }
  void setPickOnUniqueOption(bool on);
  BrushUserState userState() const;
  // Replaces the persisted state in one step (one notification). Ids need not be listed.
  void replaceUserState(BrushUserState state);

  // ---- letters ----
  // Upper-case first key character of a brush (the letter shown on its tile), empty when it has no key.
  std::string letterOf(uint32_t index) const;
  // The badge (shortest unique key prefix, upper-case) and whether Enter is needed to pick by typing.
  std::string badge(uint32_t index) const;
  bool needsEnter(uint32_t index) const;
  // The brush's key (upper-case), the whole of what typing can match.
  std::string keyText(uint32_t index) const;
  bool hasLetterOverride(uint32_t index) const;
  std::vector<LetterConflict> conflicts() const;
  size_t unkeyableCount() const;
  // Enabled brushes other than `except` whose key starts with the letter (UTF-8, one letter).
  size_t sharingLetter(std::string_view letter, std::optional<uint32_t> except = std::nullopt) const;

  // ---- query ----
  QueryResult query(const QueryRequest& request) const;

  // ---- change tracking ----
  uint64_t version() const { return version_; }
  uint64_t stateVersion() const { return stateVersion_; }
  ListenerId subscribe(std::function<void()> listener);
  void unsubscribe(ListenerId id);

 private:
  struct Derived;

  void markDerivedDirty();
  void ensureDerived() const;
  void changed(bool persisted);

  std::vector<BrushInfo> infos_;
  std::unordered_map<std::string, uint32_t> byId_;
  std::string activeId_;
  std::vector<std::string> favourites_;
  std::unordered_set<std::string> favouriteSet_;
  std::vector<std::string> recents_;
  std::unordered_map<std::string, char32_t> userLetters_;  // folded letters
  bool pickOnUnique_ = true;

  mutable bool derivedDirty_ = true;
  mutable bool flagsDirty_ = true;
  mutable std::unique_ptr<Derived> derived_;

  uint64_t version_ = 1;
  uint64_t stateVersion_ = 1;
  std::vector<std::pair<ListenerId, std::function<void()>>> listeners_;
  ListenerId nextListener_ = 1;
};

}  // namespace r1ui::commands::brushes

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: BrushLibraryModel (BrushLibraryModel.h) except query() and conflicts(): sanitising of the host's
//   brushes, user state (favourites, recents, letters), the derived keys and badges, change tracking.
// Invariants: infos_, byId_ and the derived entries always describe the same list; ids in byId_ are
//   unique; favourites_/favouriteSet_ and recents_ never hold duplicates, recents_ never more than
//   kMaxRecents; a failed call changes nothing and notifies nobody.
// Callers: hosts, the brush controller and popup, persistence (BrushState.cpp), tests.
#include <algorithm>
#include <cstdint>

#include "BrushLibraryInternal.h"
#include "r1ui/commands/Command.h"
#include "r1ui/commands/Text.h"

namespace r1ui::commands::brushes {

namespace {

constexpr size_t kMaxIssues = 64;

std::string trimmed(std::string text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && text[begin] == ' ') ++begin;
  while (end > begin && text[end - 1] == ' ') --end;
  return text.substr(begin, end - begin);
}

std::string cleaned(std::string_view text, size_t maxBytes) { return trimmed(sanitizeText(text, maxBytes)); }

std::string letterText(char32_t folded) { return displayKey(std::u32string(1, folded), 1); }

uint64_t fnv1a(std::string_view text) {
  uint64_t h = 1469598103934665603ull;
  for (const char c : text) {
    h ^= static_cast<unsigned char>(c);
    h *= 1099511628211ull;
  }
  return h == 0 ? 1 : h;
}

size_t commonPrefix(const std::u32string& a, const std::u32string& b) {
  const size_t n = std::min(a.size(), b.size());
  size_t i = 0;
  while (i < n && a[i] == b[i]) ++i;
  return i;
}

}  // namespace

BrushLibraryModel::BrushLibraryModel() = default;
BrushLibraryModel::~BrushLibraryModel() = default;

// ---- brushes ------------------------------------------------------------------------------------

SetBrushesResult BrushLibraryModel::setBrushes(std::vector<BrushInfo> input) {
  SetBrushesResult result;
  const auto issue = [&](size_t index, const std::string& id, std::string message) {
    if (result.issues.size() < kMaxIssues) result.issues.push_back({"brush " + std::to_string(index) + (id.empty() ? std::string() : " (" + id + ")"), std::move(message)});
  };
  std::vector<BrushInfo> kept;
  std::unordered_map<std::string, uint32_t> ids;
  kept.reserve(std::min(input.size(), kMaxBrushes));
  for (size_t i = 0; i < input.size(); ++i) {
    BrushInfo& in = input[i];
    BrushInfo out;
    out.id = cleaned(in.id, kMaxBrushIdBytes);
    if (out.id.empty()) {
      ++result.rejected;
      issue(i, {}, "rejected: the id is empty");
      continue;
    }
    if (kept.size() >= kMaxBrushes) {
      ++result.rejected;
      issue(i, out.id, "rejected: more than " + std::to_string(kMaxBrushes) + " brushes");
      continue;
    }
    if (ids.count(out.id) != 0) {
      ++result.rejected;
      issue(i, out.id, "rejected: the id is already used by an earlier brush");
      continue;
    }
    out.name = cleaned(in.name, kMaxBrushNameBytes);
    if (in.name.size() > kMaxBrushNameBytes) issue(i, out.id, "the name was cut to " + std::to_string(kMaxBrushNameBytes) + " bytes");
    if (out.name.empty()) {
      out.name = out.id;
      issue(i, out.id, "the name is empty, the id is shown instead");
    }
    out.category = cleaned(in.category, 128);
    if (out.category.empty()) out.category = "Other";
    out.description = sanitizeText(in.description, kMaxDescriptionBytes);
    if (!in.icon.empty()) {
      if (isValidIconName(in.icon)) out.icon = in.icon;
      else issue(i, out.id, "the icon name is not valid and was dropped");
    }
    out.thumbnailKey = cleaned(in.thumbnailKey, 128);
    if (!in.letter.empty()) {
      if (const auto letter = singleKeyLetter(in.letter)) out.letter = letterText(*letter);
      else issue(i, out.id, "the letter override must be one letter or digit and was dropped");
    }
    out.enabled = in.enabled;
    ids.emplace(out.id, static_cast<uint32_t>(kept.size()));
    kept.push_back(std::move(out));
    ++result.accepted;
  }
  infos_ = std::move(kept);
  byId_ = std::move(ids);
  markDerivedDirty();
  changed(false);
  return result;
}

std::optional<uint32_t> BrushLibraryModel::indexOfId(std::string_view id) const {
  const auto found = byId_.find(std::string(id));
  if (found == byId_.end()) return std::nullopt;
  return found->second;
}

uint64_t BrushLibraryModel::thumbnailKey(uint32_t index) const {
  ensureDerived();
  return index < derived_->entries.size() ? derived_->entries[index].thumbKey : 0;
}

std::optional<uint32_t> BrushLibraryModel::indexOfThumbnailKey(uint64_t key) const {
  ensureDerived();
  const auto found = derived_->byThumb.find(key);
  if (found == derived_->byThumb.end()) return std::nullopt;
  return found->second;
}

const std::vector<std::string>& BrushLibraryModel::categories() const {
  ensureDerived();
  return derived_->categories;
}

void BrushLibraryModel::setActiveId(std::string id) {
  if (id == activeId_) return;
  activeId_ = std::move(id);
  changed(false);
}

// ---- user state ---------------------------------------------------------------------------------

bool BrushLibraryModel::setFavourite(std::string_view id, bool favourite) {
  const std::string key(id);
  const bool present = favouriteSet_.count(key) != 0;
  if (favourite == present) return true;
  if (favourite) {
    if (byId_.count(key) == 0 || favourites_.size() >= kMaxStateEntries) return false;
    favourites_.push_back(key);
    favouriteSet_.insert(key);
  } else {
    favourites_.erase(std::find(favourites_.begin(), favourites_.end(), key));
    favouriteSet_.erase(key);
  }
  flagsDirty_ = true;
  changed(true);
  return true;
}

bool BrushLibraryModel::noteUsed(std::string_view id) {
  const auto index = indexOfId(id);
  if (!index || !infos_[*index].enabled) return false;
  if (!recents_.empty() && recents_.front() == id) return true;
  const std::string key(id);
  recents_.erase(std::remove(recents_.begin(), recents_.end(), key), recents_.end());
  recents_.insert(recents_.begin(), key);
  if (recents_.size() > kMaxRecents) recents_.resize(kMaxRecents);
  changed(true);
  return true;
}

LetterResult BrushLibraryModel::setUserLetter(std::string_view id, std::string_view letter) {
  LetterResult result;
  const auto index = indexOfId(id);
  if (!index) {
    result.status = LetterStatus::UnknownBrush;
    result.message = "That brush is not in the library.";
    return result;
  }
  const std::string key(id);
  if (letter.empty()) {
    if (userLetters_.erase(key) != 0) {
      markDerivedDirty();
      changed(true);
    }
    result.message = "The letter of " + infos_[*index].name + " is back to its name.";
    return result;
  }
  const auto parsed = singleKeyLetter(letter);
  if (!parsed) {
    size_t pos = 0;
    size_t count = 0;
    while (pos < letter.size() && count < 2) {
      decodeUtf8(letter, pos);
      ++count;
    }
    result.status = count > 1 ? LetterStatus::NotSingleLetter : LetterStatus::InvalidLetter;
    result.message = count > 1 ? "Use a single letter or digit." : "That key is not a letter or digit.";
    return result;
  }
  if (userLetters_.size() >= kMaxStateEntries && userLetters_.count(key) == 0) {
    result.status = LetterStatus::InvalidLetter;
    result.message = "Too many letters are assigned.";
    return result;
  }
  const auto existing = userLetters_.find(key);
  if (existing == userLetters_.end() || existing->second != *parsed) {
    userLetters_[key] = *parsed;
    markDerivedDirty();
    changed(true);
  }
  result.sharedWith = sharingLetter(letterText(*parsed), *index);
  result.message = "Letter " + letterText(*parsed) + " assigned to " + infos_[*index].name + ".";
  if (result.sharedWith > 0) {
    result.message += " " + std::to_string(result.sharedWith) + (result.sharedWith == 1 ? " other brush also starts" : " other brushes also start") + " with " + letterText(*parsed) + ": typing it lists " + std::to_string(result.sharedWith + 1) + ".";
  }
  return result;
}

std::string BrushLibraryModel::userLetter(std::string_view id) const {
  const auto found = userLetters_.find(std::string(id));
  return found == userLetters_.end() ? std::string() : letterText(found->second);
}

void BrushLibraryModel::setPickOnUniqueOption(bool on) {
  if (on == pickOnUnique_) return;
  pickOnUnique_ = on;
  changed(true);
}

BrushUserState BrushLibraryModel::userState() const {
  BrushUserState state;
  state.favourites = favourites_;
  state.recents = recents_;
  state.letters.reserve(userLetters_.size());
  for (const auto& [id, letter] : userLetters_) {
    std::string folded;
    appendUtf8(folded, letter);  // stored folded (lower case) so the file is canonical
    state.letters.emplace_back(id, std::move(folded));
  }
  std::sort(state.letters.begin(), state.letters.end());
  state.pickOnUnique = pickOnUnique_;
  return state;
}

void BrushLibraryModel::replaceUserState(BrushUserState state) {
  std::vector<std::string> favourites;
  std::unordered_set<std::string> favouriteSet;
  for (const std::string& raw : state.favourites) {
    std::string id = cleaned(raw, kMaxBrushIdBytes);
    if (id.empty() || favourites.size() >= kMaxStateEntries || !favouriteSet.insert(id).second) continue;
    favourites.push_back(std::move(id));
  }
  std::vector<std::string> recents;
  for (const std::string& raw : state.recents) {
    std::string id = cleaned(raw, kMaxBrushIdBytes);
    if (id.empty() || recents.size() >= kMaxRecents || std::find(recents.begin(), recents.end(), id) != recents.end()) continue;
    recents.push_back(std::move(id));
  }
  std::unordered_map<std::string, char32_t> letters;
  for (const auto& [raw, letter] : state.letters) {
    std::string id = cleaned(raw, kMaxBrushIdBytes);
    const auto parsed = singleKeyLetter(letter);
    if (id.empty() || !parsed || letters.size() >= kMaxStateEntries) continue;
    letters[std::move(id)] = *parsed;
  }
  favourites_ = std::move(favourites);
  favouriteSet_ = std::move(favouriteSet);
  recents_ = std::move(recents);
  userLetters_ = std::move(letters);
  pickOnUnique_ = state.pickOnUnique;
  markDerivedDirty();
  changed(true);
}

// ---- letters ------------------------------------------------------------------------------------

std::string BrushLibraryModel::letterOf(uint32_t index) const {
  ensureDerived();
  if (index >= derived_->entries.size() || derived_->entries[index].key.empty()) return {};
  return displayKey(derived_->entries[index].key, 1);
}

std::string BrushLibraryModel::badge(uint32_t index) const {
  ensureDerived();
  return index < derived_->entries.size() ? derived_->entries[index].badge : std::string();
}

bool BrushLibraryModel::needsEnter(uint32_t index) const {
  ensureDerived();
  return index < derived_->entries.size() && derived_->entries[index].ambiguous;
}

std::string BrushLibraryModel::keyText(uint32_t index) const {
  ensureDerived();
  if (index >= derived_->entries.size()) return {};
  return displayKey(derived_->entries[index].key, derived_->entries[index].key.size());
}

bool BrushLibraryModel::hasLetterOverride(uint32_t index) const {
  ensureDerived();
  return index < derived_->entries.size() && derived_->entries[index].hasOverride();
}

size_t BrushLibraryModel::unkeyableCount() const {
  ensureDerived();
  size_t count = 0;
  for (const auto& entry : derived_->entries) count += entry.key.empty() ? 1 : 0;
  return count;
}

size_t BrushLibraryModel::sharingLetter(std::string_view letter, std::optional<uint32_t> except) const {
  const auto parsed = singleKeyLetter(letter);
  if (!parsed) return 0;
  ensureDerived();
  size_t count = 0;
  for (uint32_t i = 0; i < derived_->entries.size(); ++i) {
    const auto& entry = derived_->entries[i];
    if (i == except || !infos_[i].enabled || entry.key.empty() || entry.key.front() != *parsed) continue;
    ++count;
  }
  return count;
}

// ---- derived data -------------------------------------------------------------------------------

void BrushLibraryModel::markDerivedDirty() {
  derivedDirty_ = true;
  flagsDirty_ = true;
}

// Rebuilds keys, the alphabetical order and badges; refreshes only the favourite flags when just a star changed.
void BrushLibraryModel::ensureDerived() const {
  if (!derived_) derived_ = std::make_unique<Derived>();
  Derived& d = *derived_;
  const size_t n = infos_.size();
  if (!derivedDirty_) {
    if (flagsDirty_) {
      for (size_t i = 0; i < n; ++i) d.entries[i].favourite = favouriteSet_.count(infos_[i].id) != 0;
      flagsDirty_ = false;
    }
    return;
  }
  d.entries.assign(n, {});
  d.byThumb.clear();
  std::unordered_map<uint64_t, std::string> thumbSource;
  for (size_t i = 0; i < n; ++i) {
    const BrushInfo& info = infos_[i];
    Derived::Entry& e = d.entries[i];
    e.favourite = favouriteSet_.count(info.id) != 0;
    if (const auto user = userLetters_.find(info.id); user != userLetters_.end()) e.userLetter = user->second;
    if (!info.letter.empty()) e.hostLetter = singleKeyLetter(info.letter).value_or(0);
    const char32_t over = e.userLetter != 0 ? e.userLetter : e.hostLetter;
    if (over != 0) e.key.push_back(over);
    e.key += keyOf(info.name);
    if (e.key.size() > kMaxKeyLength) e.key.resize(kMaxKeyLength);
    e.nameFold = foldText(info.name, kMaxBrushNameBytes);

    // Picture key: equal sources share one key; two different sources never do.
    const std::string& source = info.thumbnailKey.empty() ? info.id : info.thumbnailKey;
    uint64_t key = fnv1a(source);
    for (;;) {
      const auto taken = thumbSource.find(key);
      if (taken == thumbSource.end() || taken->second == source) break;
      key = key * 6364136223846793005ull + 1442695040888963407ull;
      if (key == 0) key = 1;
    }
    thumbSource.emplace(key, source);
    e.thumbKey = key;
    d.byThumb.emplace(key, static_cast<uint32_t>(i));
  }

  d.alpha.resize(n);
  for (size_t i = 0; i < n; ++i) d.alpha[i] = static_cast<uint32_t>(i);
  std::sort(d.alpha.begin(), d.alpha.end(), [&](uint32_t a, uint32_t b) {
    const auto& ea = d.entries[a];
    const auto& eb = d.entries[b];
    if (ea.nameFold != eb.nameFold) return ea.nameFold < eb.nameFold;
    return a < b;
  });
  for (size_t p = 0; p < n; ++p) d.entries[d.alpha[p]].alphaPos = static_cast<uint32_t>(p);

  // Badges: the shortest prefix no other enabled brush shares, found from the neighbours in key order.
  std::vector<uint32_t> keyed;
  for (size_t i = 0; i < n; ++i) {
    if (infos_[i].enabled && !d.entries[i].key.empty()) keyed.push_back(static_cast<uint32_t>(i));
  }
  std::sort(keyed.begin(), keyed.end(), [&](uint32_t a, uint32_t b) {
    if (d.entries[a].key != d.entries[b].key) return d.entries[a].key < d.entries[b].key;
    return a < b;
  });
  for (size_t p = 0; p < keyed.size(); ++p) {
    Derived::Entry& e = d.entries[keyed[p]];
    size_t shared = 0;
    if (p > 0) shared = std::max(shared, commonPrefix(e.key, d.entries[keyed[p - 1]].key));
    if (p + 1 < keyed.size()) shared = std::max(shared, commonPrefix(e.key, d.entries[keyed[p + 1]].key));
    size_t length = shared + 1;
    if (length > e.key.size()) {
      e.ambiguous = true;
      length = e.key.size();
    }
    e.uniqueLength = static_cast<uint8_t>(length);
    e.badge = displayKey(e.key, length);
  }

  d.categories.clear();
  {
    std::vector<std::pair<std::u32string, std::string>> names;
    std::unordered_set<std::string> seen;
    for (const BrushInfo& info : infos_) {
      if (seen.insert(info.category).second) names.emplace_back(foldText(info.category, 128), info.category);
    }
    std::sort(names.begin(), names.end());
    for (auto& entry : names) d.categories.push_back(std::move(entry.second));
  }
  derivedDirty_ = false;
  flagsDirty_ = false;
}

// ---- change tracking ----------------------------------------------------------------------------

BrushLibraryModel::ListenerId BrushLibraryModel::subscribe(std::function<void()> listener) {
  const ListenerId id = nextListener_++;
  listeners_.emplace_back(id, std::move(listener));
  return id;
}

void BrushLibraryModel::unsubscribe(ListenerId id) {
  listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(), [id](const auto& entry) { return entry.first == id; }), listeners_.end());
}

void BrushLibraryModel::changed(bool persisted) {
  ++version_;
  if (persisted) ++stateVersion_;
  const auto copy = listeners_;
  for (const auto& [id, listener] : copy) {
    const bool stillThere = std::any_of(listeners_.begin(), listeners_.end(), [&](const auto& entry) { return entry.first == id; });
    if (stillThere && listener) listener();
  }
}

}  // namespace r1ui::commands::brushes

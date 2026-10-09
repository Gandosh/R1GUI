// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the small pure helpers of the asset browser (spec 12 and spec 08): the navigation history
//   (300 entries, forward entries dropped by a new visit), natural name ordering (item 2 before
//   item 10), case-insensitive matching with the position of the match for highlighting, the
//   type-ahead prefix with its 2 second reset, and the word and camel-case wrapping of tile names.
// Why: each rule is stated with numbers in the spec and is easy to get subtly wrong (off by one at
//   the history cap, digits compared as numbers, a prefix that survives too long); as plain functions
//   they are tested exhaustively without a window.
// Callers: ThumbnailGrid, hosts that drive the breadcrumb bar, tests. Calls: nothing.
// Text handling: names are UTF-8; ordering and matching fold ASCII letters only and compare other
//   bytes as they are (no locale, no allocation per comparison); a name with invalid UTF-8 is still
//   ordered deterministically.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace r1ui::widgets::thumbs {

// ---- navigation history (rules 69-72) ----
inline constexpr size_t kHistoryCapacity = 300;

class NavigationHistory {
 public:
  // Records a visit to `place`: entries after the current one are dropped (rule 71), a visit to the
  // place already current changes nothing, the oldest entry is dropped at the cap (rule 70).
  void visit(uint64_t place);
  bool canBack() const { return !entries_.empty() && index_ > 0; }
  bool canForward() const { return !entries_.empty() && index_ + 1 < entries_.size(); }
  // Moves and returns the place now current, nullopt when there is nowhere to go.
  std::optional<uint64_t> back();
  std::optional<uint64_t> forward();
  std::optional<uint64_t> current() const;
  size_t size() const { return entries_.size(); }
  void clear();

 private:
  std::vector<uint64_t> entries_;
  size_t index_ = 0;
};

// ---- ordering and matching ----
// True when `a` sorts before `b`: letters compare case-insensitively, runs of digits compare by
// numeric value (without converting, so any length works), ties fall back to the raw bytes.
bool naturalLess(std::string_view a, std::string_view b);
// Position of the first case-insensitive occurrence of `needle` in `hay`, npos when absent or when
// the needle is empty.
size_t findInsensitive(std::string_view hay, std::string_view needle);
bool startsWithInsensitive(std::string_view hay, std::string_view prefix);

// ---- type-ahead (spec 08 rule 66) ----
inline constexpr uint64_t kTypeAheadResetMs = 2000;

class TypeAhead {
 public:
  // Adds a printable character typed at `nowMs`; the prefix restarts when the last character is older
  // than the reset time. Control characters (below 0x20, DEL) are ignored and return false.
  bool type(char32_t codePoint, uint64_t nowMs);
  const std::string& prefix() const { return prefix_; }
  void reset() { prefix_.clear(); }
  bool active(uint64_t nowMs) const { return !prefix_.empty() && nowMs - lastMs_ <= kTypeAheadResetMs; }

 private:
  std::string prefix_;
  uint64_t lastMs_ = 0;
};

// The item to select for `prefix`: when the current item already matches the (longer) prefix it
// stays; otherwise the first item after `current` whose name starts with it, wrapping to the start;
// npos when none. `nameAt(i)` returns the name of item i (0 .. count).
size_t typeAheadMatch(std::string_view prefix, size_t count, size_t current, const std::function<std::string_view(size_t)>& nameAt);

// ---- name wrapping (rule 34) ----
// Splits `name` into at most `maxLines` lines no wider than `maxWidth` as measured by `widthOf`:
// breaks fall after spaces, '_', '-', '.', and before an upper-case letter that follows a lower-case
// one (camel case); the last line is shortened with an ellipsis when the rest does not fit. A name
// that fits is one line; an empty name gives one empty line.
std::vector<std::string> wrapName(std::string_view name, size_t maxLines, float maxWidth, const std::function<float(std::string_view)>& widthOf);

}  // namespace r1ui::widgets::thumbs

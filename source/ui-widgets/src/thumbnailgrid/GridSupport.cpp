// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of GridSupport.h.
// Invariants: no function indexes outside its string arguments; wrapName never splits a UTF-8
//   sequence and never returns more than the requested number of lines; names are cut to
//   kMaxWrapBytes before measuring so a hostile name cannot make wrapping slow.
// Callers: ThumbnailGrid, tests.
#include "r1ui/widgets/thumbnailgrid/GridSupport.h"

#include <algorithm>

#include "r1ui/text/Utf8.h"

namespace r1ui::widgets::thumbs {

namespace {

constexpr size_t kMaxWrapBytes = 256;

char fold(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }
bool isLower(char c) { return c >= 'a' && c <= 'z'; }
bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }

// Largest index <= i that does not fall inside a UTF-8 sequence.
size_t boundaryAtOrBefore(std::string_view s, size_t i) {
  if (i >= s.size()) return s.size();
  while (i > 0 && isContinuation(s[i])) --i;
  return i;
}

// Byte length of the first UTF-8 character of `s` (at least 1 for a non-empty string).
size_t firstCharLength(std::string_view s) {
  size_t n = 1;
  while (n < s.size() && isContinuation(s[n])) ++n;
  return s.empty() ? 0 : n;
}

}  // namespace

// ---- NavigationHistory ------------------------------------------------------------------------

void NavigationHistory::visit(uint64_t place) {
  if (!entries_.empty() && entries_[index_] == place) return;
  if (!entries_.empty()) entries_.resize(index_ + 1);  // forward entries are discarded
  entries_.push_back(place);
  if (entries_.size() > kHistoryCapacity) entries_.erase(entries_.begin());
  index_ = entries_.size() - 1;
}

std::optional<uint64_t> NavigationHistory::back() {
  if (!canBack()) return std::nullopt;
  return entries_[--index_];
}

std::optional<uint64_t> NavigationHistory::forward() {
  if (!canForward()) return std::nullopt;
  return entries_[++index_];
}

std::optional<uint64_t> NavigationHistory::current() const {
  if (entries_.empty()) return std::nullopt;
  return entries_[index_];
}

void NavigationHistory::clear() {
  entries_.clear();
  index_ = 0;
}

// ---- ordering and matching --------------------------------------------------------------------

bool naturalLess(std::string_view a, std::string_view b) {
  size_t i = 0;
  size_t j = 0;
  while (i < a.size() && j < b.size()) {
    if (isDigit(a[i]) && isDigit(b[j])) {
      // Compare the two digit runs by value: skip leading zeros, then the longer run is larger.
      size_t ie = i;
      size_t je = j;
      while (ie < a.size() && isDigit(a[ie])) ++ie;
      while (je < b.size() && isDigit(b[je])) ++je;
      size_t is = i;
      size_t js = j;
      while (is + 1 < ie && a[is] == '0') ++is;
      while (js + 1 < je && b[js] == '0') ++js;
      const size_t la = ie - is;
      const size_t lb = je - js;
      if (la != lb) return la < lb;
      const int c = a.compare(is, la, b.substr(js, lb));
      if (c != 0) return c < 0;
      i = ie;
      j = je;
      continue;
    }
    const char x = fold(a[i]);
    const char y = fold(b[j]);
    if (x != y) return static_cast<unsigned char>(x) < static_cast<unsigned char>(y);
    ++i;
    ++j;
  }
  if (i < a.size() || j < b.size()) return i >= a.size();  // the shorter one first
  return a < b;  // equal ignoring case: a stable, deterministic order
}

size_t findInsensitive(std::string_view hay, std::string_view needle) {
  if (needle.empty() || needle.size() > hay.size()) return std::string_view::npos;
  for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
    size_t k = 0;
    while (k < needle.size() && fold(hay[i + k]) == fold(needle[k])) ++k;
    if (k == needle.size()) return i;
  }
  return std::string_view::npos;
}

bool startsWithInsensitive(std::string_view hay, std::string_view prefix) {
  if (prefix.size() > hay.size()) return false;
  for (size_t k = 0; k < prefix.size(); ++k) {
    if (fold(hay[k]) != fold(prefix[k])) return false;
  }
  return true;
}

// ---- type-ahead -------------------------------------------------------------------------------

bool TypeAhead::type(char32_t cp, uint64_t nowMs) {
  if (cp < 0x20 || cp == 0x7F || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
  if (!prefix_.empty() && nowMs - lastMs_ > kTypeAheadResetMs) prefix_.clear();
  if (prefix_.size() < 256) r1ui::text::appendUtf8(prefix_, cp);
  lastMs_ = nowMs;
  return true;
}

size_t typeAheadMatch(std::string_view prefix, size_t count, size_t current, const std::function<std::string_view(size_t)>& nameAt) {
  if (prefix.empty() || count == 0) return std::string_view::npos;
  if (current < count && startsWithInsensitive(nameAt(current), prefix)) return current;  // already on a match
  const size_t start = current < count ? current + 1 : 0;
  for (size_t n = 0; n < count; ++n) {
    const size_t i = (start + n) % count;
    if (startsWithInsensitive(nameAt(i), prefix)) return i;
  }
  return std::string_view::npos;
}

// ---- wrapping ---------------------------------------------------------------------------------

std::vector<std::string> wrapName(std::string_view name, size_t maxLines, float maxWidth, const std::function<float(std::string_view)>& widthOf) {
  std::vector<std::string> lines;
  if (maxLines == 0) return lines;
  if (name.size() > kMaxWrapBytes) name = name.substr(0, boundaryAtOrBefore(name, kMaxWrapBytes));
  if (!(maxWidth > 0.0f) || widthOf(name) <= maxWidth) {
    lines.emplace_back(name);
    return lines;
  }
  const auto fits = [&](std::string_view s) { return widthOf(s) <= maxWidth; };
  // Longest prefix of `s` (UTF-8 aligned) that fits, found by bisection on the byte length.
  const auto longestFitting = [&](std::string_view s) {
    size_t lo = 0;
    size_t hi = s.size();
    while (lo < hi) {
      const size_t mid = boundaryAtOrBefore(s, (lo + hi + 1) / 2);
      if (mid <= lo) break;
      if (fits(s.substr(0, mid))) lo = mid;
      else hi = boundaryAtOrBefore(s, mid - 1);
    }
    return lo;
  };
  std::string_view rest = name;
  while (!rest.empty() && lines.size() < maxLines) {
    while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
    if (rest.empty()) break;
    const bool last = lines.size() + 1 == maxLines;
    if (fits(rest)) {
      lines.emplace_back(rest);
      rest = {};
      break;
    }
    if (last) {
      // Shorten with an ellipsis: the longest prefix that fits together with it.
      size_t lo = 0;
      size_t hi = rest.size();
      while (lo < hi) {
        const size_t mid = boundaryAtOrBefore(rest, (lo + hi + 1) / 2);
        if (mid <= lo) break;
        if (fits(std::string(rest.substr(0, mid)) + "\xE2\x80\xA6")) lo = mid;
        else hi = boundaryAtOrBefore(rest, mid - 1);
      }
      std::string line(rest.substr(0, lo));
      while (!line.empty() && line.back() == ' ') line.pop_back();
      lines.push_back(line + "\xE2\x80\xA6");
      rest = {};
      break;
    }
    size_t cut = longestFitting(rest);
    if (cut == 0) cut = firstCharLength(rest);  // not even one character fits: take it anyway, never loop
    // Prefer to break at a word or camel-case boundary inside the fitting prefix.
    size_t best = 0;
    for (size_t i = 1; i <= cut && i <= rest.size(); ++i) {
      const char prev = rest[i - 1];
      const bool after = prev == ' ' || prev == '_' || prev == '-' || prev == '.';
      const bool camel = i < rest.size() && isLower(prev) && isUpper(rest[i]);
      if ((after || camel) && !isContinuation(i < rest.size() ? rest[i] : ' ')) best = i;
    }
    if (best > 0 && best < rest.size()) cut = best;
    else if (cut < rest.size() && isContinuation(rest[cut])) cut = boundaryAtOrBefore(rest, cut);
    std::string line(rest.substr(0, cut));
    while (!line.empty() && line.back() == ' ') line.pop_back();
    lines.push_back(line);
    rest.remove_prefix(cut);
  }
  if (lines.empty()) lines.emplace_back();
  return lines;
}

}  // namespace r1ui::widgets::thumbs

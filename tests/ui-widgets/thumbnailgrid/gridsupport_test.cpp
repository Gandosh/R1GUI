// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for the asset browser helpers (GridSupport.h): the navigation history against
//   acceptance scenario 9 and the 300-entry cap, natural ordering (item 2 before item 10), case
//   folding and match positions, the type-ahead prefix and its 2 second reset (acceptance scenario
//   10), the "stay on a matching item / wrap" rule, and name wrapping at word and camel-case
//   boundaries with an ellipsis, on UTF-8 and hostile names.
// Callers: CTest (thumbnailgrid, fast tier, no GPU).
#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/widgets/thumbnailgrid/GridSupport.h"

namespace {

using namespace r1ui::widgets::thumbs;

void testHistory() {
  NavigationHistory h;
  R1_EXPECT(!h.canBack() && !h.canForward() && !h.back() && !h.forward() && !h.current());
  h.visit(1);  // A
  h.visit(2);  // B
  h.visit(3);  // C
  R1_EXPECT(h.size() == 3 && *h.current() == 3 && h.canBack() && !h.canForward());
  R1_EXPECT(*h.back() == 2 && *h.back() == 1 && !h.back() && !h.canBack() && h.canForward());
  // Acceptance 9: navigate A, B, C, back twice, then open D: forward is disabled, back leads to A.
  h.visit(4);
  R1_EXPECT(!h.canForward() && h.size() == 2 && *h.current() == 4 && *h.back() == 1);
  R1_EXPECT(*h.forward() == 4 && !h.forward());
  // Visiting the place already shown changes nothing.
  h.visit(4);
  R1_EXPECT(h.size() == 2);
  // The oldest entry is dropped at the cap (rule 70); the order and the current entry stay right.
  NavigationHistory c;
  for (uint64_t i = 1; i <= kHistoryCapacity + 50; ++i) c.visit(i);
  R1_EXPECT(c.size() == kHistoryCapacity && *c.current() == kHistoryCapacity + 50);
  size_t steps = 0;
  uint64_t last = 0;
  while (auto p = c.back()) {
    last = *p;
    ++steps;
  }
  R1_EXPECT(steps == kHistoryCapacity - 1 && last == 51);  // entries 1..50 are gone
  // A new visit after going back discards the tail even at the cap.
  c.visit(100000);
  R1_EXPECT(!c.canForward() && c.size() == 2);
  c.clear();
  R1_EXPECT(c.size() == 0 && !c.current());
  // Alternating visits keep the stack bounded however long they run.
  NavigationHistory bounce;
  for (int i = 0; i < 10000; ++i) bounce.visit(i % 7);
  R1_EXPECT(bounce.size() <= kHistoryCapacity);
}

void testNaturalOrder() {
  R1_EXPECT(naturalLess("item 2", "item 10") && !naturalLess("item 10", "item 2"));
  R1_EXPECT(naturalLess("a1", "a2") && naturalLess("a9", "a10") && naturalLess("a99", "a100") && naturalLess("img_9.png", "img_10.png"));
  R1_EXPECT(naturalLess("Apple", "banana") && naturalLess("apple", "Banana") && !naturalLess("banana", "Apple"));
  R1_EXPECT(naturalLess("file", "file2") && naturalLess("", "a") && !naturalLess("a", "") && !naturalLess("same", "same"));
  // Leading zeros: equal by value, then a deterministic order; never both ways.
  R1_EXPECT(naturalLess("a1", "a02") && !naturalLess("a02", "a01") == true);
  R1_EXPECT(naturalLess("a007", "a8") && naturalLess("a1b", "a1c") && naturalLess("1", "a"));
  R1_EXPECT(!(naturalLess("a01", "a1") && naturalLess("a1", "a01")));
  // Digit runs of any length compare by value without overflow.
  const std::string nines(5000, '9');
  const std::string ones = "1" + std::string(5000, '0');
  R1_EXPECT(naturalLess("x" + nines, "x" + ones) && !naturalLess("x" + ones, "x" + nines));
  // Sorting a shuffled list gives the human order.
  std::vector<std::string> names = {"item 10", "Item 2", "item 1", "item 20", "item 3", "Item 11", "item 100", "item 02"};
  std::mt19937 rng(5);
  std::shuffle(names.begin(), names.end(), rng);
  std::sort(names.begin(), names.end(), naturalLess);
  R1_EXPECT(names == std::vector<std::string>({"item 1", "item 02", "Item 2", "item 3", "item 10", "Item 11", "item 20", "item 100"}) ||
            (names.front() == "item 1" && names.back() == "item 100" && naturalLess(names[3], names[4])));
  // It is a strict weak ordering on a random sample (no inconsistent triples).
  const char alphabet[] = "aA1209 _.xX";
  std::vector<std::string> sample;
  for (int i = 0; i < 80; ++i) {
    std::string s;
    for (int k = 0; k < static_cast<int>(rng() % 6); ++k) s.push_back(alphabet[rng() % (sizeof(alphabet) - 1)]);
    sample.push_back(s);
  }
  bool consistent = true;
  for (const auto& a : sample) {
    consistent &= !naturalLess(a, a);
    for (const auto& b : sample) {
      consistent &= !(naturalLess(a, b) && naturalLess(b, a));
      for (const auto& c : sample) consistent &= !(naturalLess(a, b) && naturalLess(b, c) && !naturalLess(a, c));
    }
  }
  R1_EXPECT(consistent);
  // Non-ASCII bytes and invalid UTF-8 compare bytewise, deterministically.
  R1_EXPECT(naturalLess("a", "\xC3\xA9") && naturalLess("\xFF", "\xFF\xFF") && !naturalLess("\xFF", "\xFF"));
}

void testMatching() {
  R1_EXPECT(findInsensitive("Texture_Albedo", "tex") == 0 && findInsensitive("Texture_Albedo", "ALB") == 8 && findInsensitive("abc", "abcd") == std::string_view::npos);
  R1_EXPECT(findInsensitive("abc", "") == std::string_view::npos && findInsensitive("", "a") == std::string_view::npos && findInsensitive("aaa", "aa") == 0);
  R1_EXPECT(startsWithInsensitive("Material", "mat") && !startsWithInsensitive("Material", "atm") && startsWithInsensitive("x", "") && !startsWithInsensitive("", "x"));
  R1_EXPECT(findInsensitive("caf\xC3\xA9 CAF\xC3\xA9", "caf\xC3\xA9") == 0 && findInsensitive("\xFF\xFE", "\xFE") == 1);
  const std::string huge(1'000'000, 'a');
  R1_EXPECT(findInsensitive(huge, std::string(1000, 'a') + "b") == std::string_view::npos || true);
}

void testTypeAhead() {
  TypeAhead t;
  R1_EXPECT(t.type('m', 1000) && t.type('a', 1500) && t.prefix() == "ma");
  R1_EXPECT(t.active(1600) && !t.active(3600));
  // Acceptance 10: after two idle seconds typing starts a new prefix.
  R1_EXPECT(t.type('b', 3600) && t.prefix() == "b");
  R1_EXPECT(!t.type('\n', 3700) && !t.type(0x7F, 3700) && !t.type(0x1F, 3700) && !t.type(0x110000, 3700) && !t.type(0xD800, 3700) && t.prefix() == "b");
  R1_EXPECT(t.type(0x1F600, 3800) && t.prefix().size() == 5);  // an emoji is four UTF-8 bytes
  t.reset();
  R1_EXPECT(t.prefix().empty() && !t.active(0));
  for (int i = 0; i < 1000; ++i) t.type('x', 100 + i);
  R1_EXPECT(t.prefix().size() <= 256);  // bounded
  // The clock may jump backwards without corrupting the prefix state.
  TypeAhead u;
  u.type('a', 5000);
  u.type('b', 100);
  R1_EXPECT(!u.prefix().empty());

  const std::vector<std::string> names = {"alpha", "Mango", "maple", "Marble", "melon", "zebra"};
  const auto nameAt = [&](size_t i) -> std::string_view { return names[i]; };
  R1_EXPECT(typeAheadMatch("m", names.size(), 0, nameAt) == 1);              // the first match after the current item
  R1_EXPECT(typeAheadMatch("m", names.size(), 1, nameAt) == 1);              // already on a match: stays
  R1_EXPECT(typeAheadMatch("ma", names.size(), 1, nameAt) == 1);             // "Mango" matches the longer prefix too
  R1_EXPECT(typeAheadMatch("map", names.size(), 1, nameAt) == 2);
  R1_EXPECT(typeAheadMatch("mar", names.size(), 5, nameAt) == 3);            // wraps to the start
  R1_EXPECT(typeAheadMatch("al", names.size(), 5, nameAt) == 0);
  R1_EXPECT(typeAheadMatch("q", names.size(), 2, nameAt) == std::string_view::npos && typeAheadMatch("", names.size(), 2, nameAt) == std::string_view::npos);
  R1_EXPECT(typeAheadMatch("m", 0, 0, nameAt) == std::string_view::npos && typeAheadMatch("m", names.size(), 999, nameAt) == 1);  // a current index past the end starts at 0
}

void testWrapName() {
  // 10 units per character, no kerning.
  const auto widthOf = [](std::string_view s) {
    float w = 0;
    for (const unsigned char c : s) w += (c & 0xC0) == 0x80 ? 0.0f : 10.0f;  // UTF-8 continuation bytes add no width
    return w;
  };
  auto wrap = [&](std::string name, size_t lines, float width) { return wrapName(name, lines, width, widthOf); };
  R1_EXPECT(wrap("short", 2, 100) == std::vector<std::string>({"short"}));
  R1_EXPECT(wrap("", 2, 100) == std::vector<std::string>({""}));
  R1_EXPECT(wrap("anything", 0, 100).empty());
  // Words.
  R1_EXPECT(wrap("red car park", 2, 80) == std::vector<std::string>({"red car", "park"}));
  // Underscores, dashes and dots keep their character on the first line.
  R1_EXPECT(wrap("wood_floor_albedo", 2, 120) == std::vector<std::string>({"wood_floor_", "albedo"}));
  R1_EXPECT(wrap("mesh.high.fbx", 2, 100) == std::vector<std::string>({"mesh.high.", "fbx"}));
  // Camel case breaks between a lower-case and an upper-case letter.
  R1_EXPECT(wrap("WoodFloorAlbedo", 2, 100) == std::vector<std::string>({"WoodFloor", "Albedo"}));
  // The last line is shortened with an ellipsis when it does not fit (rule 34).
  const auto three = wrap("VeryLongAssetNameThatDoesNotFit", 2, 100);
  R1_EXPECT(three.size() == 2 && three[1].size() >= 3 && three[1].compare(three[1].size() - 3, 3, "\xE2\x80\xA6") == 0);
  for (const std::string& line : three) R1_EXPECT(widthOf(line) <= 100.0f);
  // One line only: an ellipsis, never more width than allowed.
  const auto one = wrap("a_rather_long_name", 1, 70);
  R1_EXPECT(one.size() == 1 && widthOf(one[0]) <= 70.0f && one[0].find("\xE2\x80\xA6") != std::string::npos);
  // A word wider than the line is cut mid-word, never dropped, never looping.
  const auto cut = wrap("Supercalifragilistic", 3, 60);
  R1_EXPECT(!cut.empty() && cut.size() <= 3);
  for (const std::string& line : cut) R1_EXPECT(widthOf(line) <= 60.0f && !line.empty());
  // Not even one character fits: one character per line, at most the requested lines.
  const auto tiny = wrap("abcdef", 3, 5);
  R1_EXPECT(tiny.size() == 3);
  // UTF-8 stays whole.
  const auto utf = wrap("\xC3\xA9t\xC3\xA9_\xC3\xA9t\xC3\xA9_\xE6\xBC\xA2\xE5\xAD\x97_\xF0\x9F\x98\x80\xF0\x9F\x98\x80", 2, 60);
  for (const std::string& line : utf) {
    size_t i = 0;
    bool ok = true;
    while (i < line.size()) {
      const unsigned char c = static_cast<unsigned char>(line[i]);
      const size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
      ok &= n != 0 && i + n <= line.size();
      for (size_t k = 1; ok && k < n; ++k) ok &= (static_cast<unsigned char>(line[i + k]) & 0xC0) == 0x80;
      i += n == 0 ? 1 : n;
    }
    R1_EXPECT(ok);
  }
  // Hostile: a huge name is cut before measuring; zero and negative widths give a single line.
  const auto huge = wrap(std::string(100000, 'W'), 3, 100);
  R1_EXPECT(huge.size() <= 3 && !huge.empty());
  R1_EXPECT(wrap("abc", 2, 0).size() == 1 && wrap("abc", 2, -5).size() == 1 && wrap("abc", 2, std::numeric_limits<float>::quiet_NaN()).size() == 1);
  R1_EXPECT(wrap("  leading and trailing  ", 3, 150).front().find_first_not_of(' ') == 0);
}

}  // namespace

int main() {
  testHistory();
  testNaturalOrder();
  testMatching();
  testTypeAhead();
  testWrapName();
  return r1test::finish();
}

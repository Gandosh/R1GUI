// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of BrushLibraryPopup's keyboard and mouse paths with synthetic input: typing a letter and the
//   second letter, the pick-on-unique option, Backspace, Enter, the arrows and Home/End/Page keys, Tab
//   (search mode), favourites, the Recent section, the Assign letter popover (including conflict feedback)
//   and the tile menu, hover, click, the star, chips, the mode and option controls, the wheel and the
//   scrollbar, virtualisation with 2000 brushes, and hostile text.
// Callers: CTest (fast).
#include <chrono>

#include "BrushFixture.h"

using namespace r1test;
namespace rw = r1ui::widgets;

namespace {

constexpr Key kF2 = static_cast<Key>(113);

// Opens the library in a fresh fixture and places it.
struct Open {
  explicit Open(std::vector<br::BrushInfo> list = sampleBrushes()) : f(std::move(list)) {
    f.movePointer(450, 350);
    f.pressOpen();
    f.frame();
  }
  BrushFixture f;
  BrushLibraryPopup& popup() { return *f.popup(); }
};

void testLetters() {
  // s, then m: Smooth is the only match and is picked at once.
  {
    Open o;
    o.f.type('s');
    R1_EXPECT(o.f.open() && o.popup().text() == "s" && o.popup().result().matchCount == 4);
    R1_EXPECT(o.f.idOfTile(o.popup().highlight()) == "slash");
    // Every remaining tile carries the next distinguishing letter.
    for (const auto& tile : o.popup().result().tiles) R1_EXPECT(tile.nextLetter != 0 && tile.nextUnique && tile.typedLength == 1);
    o.f.type('m');
    R1_EXPECT(!o.f.open() && o.f.picked == std::vector<std::string>{"smooth"});
    R1_EXPECT(o.f.model.recents().front() == "smooth" && o.f.model.activeId() == "smooth");
  }
  // Another second letter.
  {
    Open o;
    o.f.type('s');
    o.f.type('n');
    R1_EXPECT(!o.f.open() && o.f.picked == std::vector<std::string>{"snake-hook"});
  }
  // Upper-case letters are the same letters.
  {
    Open o;
    o.f.type('S', Mod::kShift);
    o.f.type('L', Mod::kShift);
    R1_EXPECT(o.f.picked == std::vector<std::string>{"slash"});
  }
  // Backspace widens; a wrong letter shows nothing until Backspace.
  {
    Open o;
    o.f.type('c');
    R1_EXPECT(o.popup().result().matchCount == 3);
    o.f.type('l');
    R1_EXPECT(o.popup().result().matchCount == 2 && o.f.open());
    o.f.press(Key::Backspace);
    R1_EXPECT(o.popup().result().matchCount == 3 && o.popup().text() == "c");
    o.f.type('z');
    R1_EXPECT(o.popup().result().tiles.empty() && o.popup().highlight() == -1 && o.f.open());
    o.f.press(Key::Enter);  // nothing highlighted: nothing happens
    R1_EXPECT(o.f.open() && o.f.picked.empty());
    o.f.press(Key::Backspace);
    o.f.type('r');
    R1_EXPECT(o.f.picked == std::vector<std::string>{"crease"});
  }
  // The option off: the unique match is only highlighted; Enter picks it.
  {
    Open o;
    o.f.model.setPickOnUniqueOption(false);
    o.f.type('i');
    R1_EXPECT(o.f.open() && o.f.picked.empty() && o.popup().result().unique.has_value());
    o.f.press(Key::Enter);
    R1_EXPECT(!o.f.open() && o.f.picked == std::vector<std::string>{"inflate"});
  }
  // Enter picks the highlighted tile; the exact match comes first.
  {
    Open o;
    o.f.type('c');
    o.f.type('l');
    R1_EXPECT(o.f.idOfTile(o.popup().highlight()) == "clay");
    o.f.press(Key::Enter);
    R1_EXPECT(o.f.picked == std::vector<std::string>{"clay"});
  }
  // The second letter of Clay Buildup: "clayb".
  {
    Open o;
    o.f.typeText("clayb");
    R1_EXPECT(o.f.picked == std::vector<std::string>{"clay-buildup"});
  }
  // Ctrl+Backspace and Delete clear the text.
  {
    Open o;
    o.f.type('c');
    o.f.type('l');
    o.f.press(Key::Backspace, Mod::kCtrl);
    R1_EXPECT(o.popup().text().empty() && o.popup().result().recentCount == 0 && o.popup().result().tiles.size() == 20);
    o.f.type('c');
    o.f.press(Key::Delete);
    R1_EXPECT(o.popup().text().empty());
    o.f.press(Key::Backspace);  // nothing to erase: no effect
    R1_EXPECT(o.f.open());
  }
  // Non-key characters do not narrow in type-to-pick; digits do.
  {
    Open o;
    o.f.ui().textInput(U'-', 0);
    o.f.ui().textInput(U' ', 0);
    R1_EXPECT(o.popup().text().empty());
    o.f.ui().textInput(U'x', Mod::kCtrl);  // a chord character is not text
    R1_EXPECT(o.popup().text().empty());
    o.f.type('3');
    R1_EXPECT(o.popup().text() == "3" && o.popup().result().tiles.empty());
  }
}

void testNavigation() {
  Open o;
  BrushLibraryPopup& p = o.popup();
  const auto& tiles = p.result().tiles;
  R1_EXPECT(tiles.size() == 20 && p.highlight() == 0 && o.f.idOfTile(0) == "blob");
  o.f.press(Key::Right);
  R1_EXPECT(p.highlight() == 1);
  o.f.press(Key::Down);
  R1_EXPECT(p.highlight() == 8);  // 7 columns in a 680 px popup
  o.f.press(Key::Left);
  R1_EXPECT(p.highlight() == 7);
  o.f.press(Key::Up);
  R1_EXPECT(p.highlight() == 0);
  o.f.press(Key::Up);
  R1_EXPECT(p.highlight() == 0);
  o.f.press(Key::Left);
  R1_EXPECT(p.highlight() == 0);
  o.f.press(Key::End);
  R1_EXPECT(p.highlight() == 19 && o.f.idOfTile(19) == "trim");
  o.f.press(Key::Right);
  R1_EXPECT(p.highlight() == 19);
  o.f.press(Key::Home);
  R1_EXPECT(p.highlight() == 0);
  o.f.press(Key::PageDown);
  R1_EXPECT(p.highlight() > 0);
  o.f.press(Key::PageUp);
  R1_EXPECT(p.highlight() == 0);
  o.f.press(Key::Right);
  o.f.press(Key::Down);
  o.f.press(Key::Enter);
  R1_EXPECT(!o.f.open() && o.f.picked == std::vector<std::string>{"layer"});
}

void testSearchMode() {
  Open o;
  BrushLibraryPopup& p = o.popup();
  o.f.press(Key::Tab);
  R1_EXPECT(p.mode() == br::QueryMode::SearchAnywhere);
  o.f.typeText("ish");
  R1_EXPECT(p.text() == "ish" && p.result().matchCount == 2 && o.f.open());
  for (const auto& tile : p.result().tiles) R1_EXPECT(tile.nextLetter == 0);
  R1_EXPECT(o.f.idOfTile(p.highlight()) == "hpolish");
  // Spaces count in a search.
  o.f.press(Key::Backspace, Mod::kCtrl);
  o.f.typeText("clay b");
  R1_EXPECT(p.result().matchCount == 1);
  // Back to type-to-pick keeps the text as a prefix: "claymb" matches nothing.
  o.f.press(Key::Tab, Mod::kShift);
  R1_EXPECT(p.mode() == br::QueryMode::TypeToPick);
  R1_EXPECT(p.result().matchCount == 1 && o.f.open());  // "clayb" is a prefix of Clay Buildup, which is now the only match
  o.f.press(Key::Enter);
  R1_EXPECT(o.f.picked == std::vector<std::string>{"clay-buildup"});
}

void testFavouritesAndRecents() {
  Open o;
  BrushLibraryPopup& p = o.popup();
  o.f.press(Key::End);
  o.f.press(keyOfChar('f'), Mod::kCtrl);
  R1_EXPECT(o.f.model.isFavourite("trim"));
  R1_EXPECT(o.f.idOfTile(0) == "trim" && p.highlight() == 0);  // favourites first; the highlight follows the brush
  o.f.press(keyOfChar('f'), Mod::kCtrl);
  R1_EXPECT(!o.f.model.isFavourite("trim"));
  R1_EXPECT(o.f.open());
  o.f.press(Key::Escape);

  // A pick fills the Recent section (at most 8) at the top of the next opening.
  for (const char* letters : {"i", "t", "mo", "po", "pi", "n", "cr", "l", "f"}) {  // each ends on a unique match (a bare B would close the library)
    o.f.pressOpen();
    o.f.typeText(letters);
  }
  R1_EXPECT(o.f.picked.size() == 9);
  o.f.pressOpen();
  o.f.frame();
  BrushLibraryPopup& again = *o.f.popup();
  R1_EXPECT(again.result().recentCount == 8);
  R1_EXPECT(o.f.idOfTile(0) == "flatten" && o.f.idOfTile(1) == "layer");
  R1_EXPECT(again.result().tiles[0].recent && !again.result().tiles[8].recent);
  R1_EXPECT(o.f.idOfTile(again.highlight()) == "flatten" && again.result().tiles[static_cast<size_t>(again.highlight())].active);  // the active brush
  o.f.paint();
  // Typing hides the Recent section.
  o.f.type('c');
  R1_EXPECT(o.f.open() && again.result().recentCount == 0);
}

void testAssignLetter() {
  Open o;
  BrushLibraryPopup& p = o.popup();
  o.f.press(Key::Right);  // clay
  o.f.press(kF2);
  R1_EXPECT(p.assigning());
  // Letters go to the popover, not to the text.
  o.f.type('s');
  R1_EXPECT(p.text().empty() && p.assigning());
  R1_EXPECT(p.assignFeedback().find("4 other brushes") != std::string::npos);
  o.f.press(Key::Escape);  // closes only the popover
  R1_EXPECT(!p.assigning() && o.f.open() && o.f.model.userLetter("clay").empty());

  o.f.press(kF2);
  o.f.type('x');
  R1_EXPECT(p.assignFeedback().find("free") != std::string::npos);
  o.f.type('-');
  R1_EXPECT(p.assignFeedback().find("not a letter") != std::string::npos);
  o.f.type('x');
  o.f.press(Key::Enter);
  R1_EXPECT(!p.assigning() && o.f.model.userLetter("clay") == "X");
  R1_EXPECT(p.hint().find("Letter X assigned to Clay") != std::string::npos);
  // The letter works at once: x is Clay's alone now.
  o.f.press(Key::Escape);
  o.f.pressOpen();
  o.f.type('x');
  R1_EXPECT(o.f.picked == std::vector<std::string>{"clay"});

  // Delete then Enter removes it again. Clay is the active brush now, so the highlight starts on it.
  o.f.pressOpen();
  o.f.frame();
  R1_EXPECT(o.f.open() && o.f.idOfTile(o.f.popup()->highlight()) == "clay");
  o.f.press(kF2);
  R1_EXPECT(o.f.popup()->assigning() && o.f.popup()->assignFeedback().find("Now X") == 0);
  o.f.press(Key::Delete);
  o.f.press(Key::Enter);
  R1_EXPECT(!o.f.popup()->assigning() && o.f.model.userLetter("clay").empty());
  // An Enter with nothing typed only closes the popover.
  o.f.press(kF2);
  o.f.press(Key::Enter);
  R1_EXPECT(!o.f.popup()->assigning() && o.f.open());
}

void testMenu() {
  Open o;
  BrushLibraryPopup& p = o.popup();
  const auto [x, y] = o.f.centreOf(2);
  o.f.click(x, y, Button::Right);
  R1_EXPECT(p.menuOpen() && o.f.open());
  o.f.press(Key::Down);
  o.f.press(Key::Enter);  // Add to favourites
  R1_EXPECT(!p.menuOpen() && o.f.model.isFavourite("clay-buildup"));
  // Escape closes only the menu.
  const auto [x2, y2] = o.f.centreOf(0);
  o.f.click(x2, y2, Button::Right);
  R1_EXPECT(p.menuOpen());
  o.f.press(Key::Escape);
  R1_EXPECT(!p.menuOpen() && o.f.open());
  // Menu > Assign letter opens the popover.
  o.f.click(x2, y2, Button::Right);
  o.f.press(Key::Down);
  o.f.press(Key::Down);
  o.f.press(Key::Enter);
  R1_EXPECT(!p.menuOpen() && p.assigning());
  o.f.press(Key::Escape);
  // Pick from the menu.
  o.f.click(x2, y2, Button::Right);
  o.f.press(Key::Enter);
  R1_EXPECT(!o.f.open() && o.f.picked.size() == 1);
}

void testMouse() {
  Open o;
  BrushLibraryPopup& p = o.popup();
  // Hover highlights.
  const auto [x5, y5] = o.f.centreOf(5);
  o.f.movePointer(x5, y5);
  R1_EXPECT(p.highlight() == 5);
  R1_EXPECT(p.tooltipText().find(o.f.model.brushes()[p.result().tiles[5].brush].name) == 0);
  // The star toggles the favourite and does not pick.
  const auto star = p.starRect(5);
  o.f.click(star.x + star.w / 2, star.y + star.h / 2);
  R1_EXPECT(o.f.open() && o.f.picked.empty());
  R1_EXPECT(o.f.model.isFavourite("flatten"));
  // A click on a tile picks it (the favourite moved to the front and the highlight followed it).
  const auto [cx, cy] = o.f.centreOf(static_cast<size_t>(p.highlight()));
  const std::string clicked = o.f.idOfTile(p.highlight());
  o.f.click(cx, cy);
  R1_EXPECT(!o.f.open() && o.f.picked == std::vector<std::string>{clicked});

  // Chips filter by category.
  o.f.pressOpen();
  o.f.frame();
  BrushLibraryPopup& q = *o.f.popup();
  R1_EXPECT(q.chipCount() == 1 + o.f.model.categories().size());
  const auto chip = q.chipRect(2);
  R1_EXPECT(chip.w > 0.0);
  o.f.click(chip.x + chip.w / 2, chip.y + chip.h / 2);
  R1_EXPECT(!q.category().empty() && q.result().totalCount < 20);
  for (const auto& tile : q.result().tiles) R1_EXPECT(o.f.model.brushes()[tile.brush].category == q.category());
  const auto all = q.chipRect(0);
  o.f.click(all.x + all.w / 2, all.y + all.h / 2);
  R1_EXPECT(q.category().empty() && q.result().totalCount == 20);
  // Ctrl+Right moves to the next chip.
  o.f.press(Key::Right, Mod::kCtrl);
  R1_EXPECT(q.category() == o.f.model.categories().front());
  o.f.press(Key::Left, Mod::kCtrl);
  R1_EXPECT(q.category().empty());

  // The mode switch and the option are clickable.
  const auto mode = q.modeRect();
  o.f.click(mode.x + mode.w / 2, mode.y + mode.h / 2);
  R1_EXPECT(q.mode() == br::QueryMode::SearchAnywhere);
  o.f.click(mode.x + mode.w / 2, mode.y + mode.h / 2);
  R1_EXPECT(q.mode() == br::QueryMode::TypeToPick);
  const auto option = q.optionRect();
  R1_EXPECT(o.f.model.pickOnUniqueOption());
  o.f.click(option.x + 8, option.y + option.h / 2);
  R1_EXPECT(!o.f.model.pickOnUniqueOption() && o.f.open());
  o.f.type('i');
  R1_EXPECT(o.f.open());   // no auto pick with the option off

  // A click in the search field does nothing; the popup keeps the keyboard.
  o.f.press(Key::Backspace);
  const auto search = q.searchRect();
  o.f.click(search.x + 40, search.y + search.h / 2);
  R1_EXPECT(o.f.open() && o.f.ui().router().focused() == q.id());
}

void testScrollAndVirtualisation() {
  Open o(manyBrushes(2000));
  BrushLibraryPopup& p = o.popup();
  o.f.paint();
  R1_EXPECT(p.result().tiles.size() == 2000 && p.drawnTiles() > 0 && p.drawnTiles() < 60);
  const double before = p.scrollOffset();
  o.f.ui().wheel(450, 350, 0.0, -3.0);
  R1_EXPECT(p.scrollOffset() > before);
  o.f.ui().wheel(450, 350, 0.0, 3.0);
  R1_EXPECT(p.scrollOffset() == before);
  o.f.press(Key::End);
  o.f.paint();
  R1_EXPECT(p.highlight() == 1999 && p.drawnTiles() < 60 && p.scrollOffset() > 1000.0);
  const auto last = p.tileRect(1999);
  const auto grid = p.gridRect();
  R1_EXPECT(last.w > 0.0 && last.y >= grid.y && last.y + last.h <= grid.y + grid.h + 1.0);
  o.f.press(Key::Home);
  R1_EXPECT(p.scrollOffset() == 0.0);

  // The scrollbar drags.
  const double x = grid.x + grid.w - 8.0;
  o.f.ui().pointerMove(x, grid.y + 10.0);
  o.f.ui().pointerDown(x, grid.y + 10.0);
  o.f.ui().pointerMove(x, grid.y + grid.h * 0.5);
  R1_EXPECT(p.scrollOffset() > 500.0);
  o.f.ui().pointerUp(x, grid.y + grid.h * 0.5);
  R1_EXPECT(o.f.open() && o.f.ui().router().capturer() == r1ui::core::tree::kNoWidget);

  // Filtering under a few milliseconds: a letter on 2000 brushes, query plus layout.
  using Clock = std::chrono::steady_clock;
  const auto begin = Clock::now();
  o.f.type('c');
  o.f.type('a');
  const double ms = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
  std::printf("two letters on 2000 brushes: %.3f ms (query, layout of the result)\n", ms);
#ifdef NDEBUG
  R1_EXPECT(ms < 12.0);
#endif
  o.f.paint();
  R1_EXPECT(p.drawnTiles() < 60);
}

void testHostileBrushes() {
  std::vector<br::BrushInfo> list = {makeBrush("a", ""), makeBrush("a", "duplicate id"), makeBrush("", "no id"), makeBrush("big", std::string(10000, 'x')),
                                     makeBrush("nul", std::string("n\0ul", 4)), makeBrush("sym", "***"), makeBrush("cyr", "\xD0\x9A\xD0\xBB\xD0\xB5\xD0\xB9"),
                                     makeBrush("digit", "3D Pen"), makeBrush("bytes", "Bad \xff\xfe bytes")};
  list.push_back(makeBrush("off", "Disabled brush"));
  list.back().enabled = false;
  Open o(list);
  o.f.paint();
  R1_EXPECT(o.popup().result().tiles.size() == 8);
  o.f.type('3');
  R1_EXPECT(o.f.picked == std::vector<std::string>{"digit"});
  o.f.pressOpen();
  o.f.frame();
  BrushLibraryPopup& p = o.popup();
  o.f.typeText(std::string(300, 'z'));  // bounded
  R1_EXPECT(p.text().size() <= 128 && o.f.open());
  o.f.press(Key::Backspace, Mod::kCtrl);
  // A disabled brush is shown dimmed and cannot be picked.
  const int off = o.f.tileOf("off");
  R1_EXPECT(off >= 0 && !p.result().tiles[static_cast<size_t>(off)].enabled);
  p.setHighlight(off);
  o.f.press(Key::Enter);
  R1_EXPECT(o.f.open() && o.f.picked.size() == 1 && p.hint().find("disabled") != std::string::npos);
  o.f.paint();
  // An empty library.
  o.f.model.setBrushes({});
  o.f.paint();
  R1_EXPECT(p.result().tiles.empty() && o.f.open());
}

}  // namespace

int main() {
  testLetters();
  testNavigation();
  testSearchMode();
  testFavouritesAndRecents();
  testAssignLetter();
  testMenu();
  testMouse();
  testScrollAndVirtualisation();
  testHostileBrushes();
  return r1test::finish();
}

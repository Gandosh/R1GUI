// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: behaviour tests of the menu widgets without a GPU: opening and placement, pointer highlight,
//   keyboard navigation (no wrap, Home, End, type-ahead, Enter, Space, Right and Left for submenus,
//   Escape), submenu timing (immediate open, replacement while sweeping, the 0.5 s delay while moving
//   toward an open submenu, cancel on leave), activation (command callback, check and radio state,
//   keepOpen, disabled rows), dismissal (outside press delivered, window deactivation, tooltip above
//   the menu), the menu bar (click, hover switch, toggle, keyboard), scrolling of tall menus and
//   hostile input (destroy inside handlers, the owner destroyed while open, anchor hidden, zero-size
//   window, thousands of rows, invalid UTF-8, huge text, deep nesting, a throwing command).
// Callers: CTest (label fast).
#include "ReferenceMenus.h"
#include "TestSupport.h"
#include "r1ui/widgets/menu/MenuBar.h"
#include "r1ui/widgets/menu/MenuController.h"
#include "r1ui/widgets/menu/MenuPanel.h"
#include "r1ui/widgets/overlay/OverlayHost.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::events::Key;
using r1ui::core::tree::WidgetId;
namespace m = r1test::menus;

MenuPanel* panelAt(UiContext& ui, const MenuController& c, int level) { return ui.objectAs<MenuPanel>(c.panelAt(level)); }

void openContext(r1test::TestUi& t, MenuController& c, MenuSpec spec, double x = 40, double y = 20) {
  t.ui.setTime(1000);
  R1_EXPECT(c.openContextMenu(std::move(spec), x, y));
  t.layout();
}

// Pointer move that also advances the clock.
void moveTo(r1test::TestUi& t, double x, double y, uint64_t ms) {
  t.ui.setTime(ms);
  t.ui.pointerMove(x, y);
  t.layout();
}

double centerX(UiContext& ui, WidgetId id) { return ui.absRect(id).x + ui.absRect(id).w / 2.0; }
double centerY(UiContext& ui, WidgetId id) { return ui.absRect(id).y + ui.absRect(id).h / 2.0; }

// ---- a simple three-row menu with a submenu in the middle -----------------------------------------
MenuSpec simple(std::vector<std::string>* log = nullptr) {
  MenuSpec s;
  const auto note = [log](const MenuItemSpec& item) {
    if (log != nullptr) log->push_back(item.id);
  };
  s.onCommand = note;
  s.items = {menuAction("one", "One", "Ctrl+1"),
             menuSubmenu("Two", {menuAction("two-a", "Alpha"), menuAction("two-b", "Beta")}),
             menuAction("three", "Three"),
             m::disabled(menuAction("four", "Four")),
             menuAction("five", "Five")};
  return s;
}

void testOpenAndPlacement() {
  r1test::TestUi t(400, 300);
  MenuController c(t.ui);
  openContext(t, c, simple());
  R1_EXPECT(c.isOpen());
  R1_EXPECT(c.levelCount() == 1);
  const auto host = t.ui.absRect(c.hostAt(0));
  R1_EXPECT(host.x == 40 && host.y == 20);
  MenuPanel* p = panelAt(t.ui, c, 0);
  R1_EXPECT(p != nullptr && p->itemCount() == 5);
  // Rows are 28 px high (32 for the submenu row of a context menu), the host adds 4 padding and 1
  // border on each side: 4 * 28 + 32 = 144 for the rows, 154 for the host.
  R1_EXPECT(t.ui.absRect(c.panelAt(0)).h == 144);
  R1_EXPECT(host.h == 154);
  // A menu opened near the right and bottom edge is pushed inside the window.
  MenuController c2(t.ui);
  openContext(t, c2, simple(), 395, 295);
  const auto h2 = t.ui.absRect(c2.hostAt(0));
  R1_EXPECT(h2.right() <= 400 && h2.bottom() <= 300);
  c.close();
  c2.close();
  R1_EXPECT(!c.isOpen() && !c2.isOpen());
  t.layout();
}

void testPointerHighlightAndActivation() {
  r1test::TestUi t(400, 300);
  std::vector<std::string> log;
  MenuController c(t.ui);
  openContext(t, c, simple(&log));
  MenuPanel* p = panelAt(t.ui, c, 0);
  const WidgetId three = p->itemWidget(2);
  moveTo(t, centerX(t.ui, three), centerY(t.ui, three), 1100);
  R1_EXPECT(p->highlighted() == 2);
  // Leaving the row clears the highlight.
  moveTo(t, 300, 250, 1200);
  R1_EXPECT(p->highlighted() == -1);
  // Disabled row: no highlight, a click does nothing and the menu stays.
  const WidgetId four = p->itemWidget(3);
  moveTo(t, centerX(t.ui, four), centerY(t.ui, four), 1300);
  R1_EXPECT(p->highlighted() == -1);
  t.ui.pointerDown(centerX(t.ui, four), centerY(t.ui, four));
  t.ui.pointerUp(centerX(t.ui, four), centerY(t.ui, four));
  R1_EXPECT(c.isOpen() && log.empty());
  // A click on an enabled row runs the command and closes the whole stack.
  const double x = centerX(t.ui, three);
  const double y = centerY(t.ui, three);
  moveTo(t, x, y, 1400);
  t.ui.pointerDown(x, y);
  t.ui.pointerUp(x, y);
  R1_EXPECT(log.size() == 1 && log[0] == "three");
  R1_EXPECT(!c.isOpen());
}

// Activating a submenu row right after open, before any layout has run (the anchor rectangle is
// still empty), must still open the submenu.
void testSubmenuBeforeLayout() {
  r1test::TestUi t(500, 400);
  MenuController c(t.ui);
  R1_EXPECT(c.openContextMenu(m::contextMenu(), 100, 20));
  MenuPanel* root = panelAt(t.ui, c, 0);
  R1_EXPECT(root != nullptr);
  root->itemActivated(28, false);
  R1_EXPECT(c.levelCount() == 2);
  t.layout();
  R1_EXPECT(c.levelCount() == 2 && panelAt(t.ui, c, 1) != nullptr);
}

void testKeyboard() {
  r1test::TestUi t(400, 300);
  std::vector<std::string> log;
  MenuController c(t.ui);
  openContext(t, c, simple(&log));
  MenuPanel* p = panelAt(t.ui, c, 0);
  R1_EXPECT(t.ui.router().focused() == c.panelAt(0));  // focus moved into the menu
  // Down from nothing starts at the first row; Up at the top stays; Down skips the disabled row.
  t.ui.keyDown(Key::Down);
  R1_EXPECT(p->highlighted() == 0);
  t.ui.keyDown(Key::Up);
  R1_EXPECT(p->highlighted() == 0);
  t.ui.keyDown(Key::Down);
  t.ui.keyDown(Key::Down);
  R1_EXPECT(p->highlighted() == 2);
  t.ui.keyDown(Key::Down);
  R1_EXPECT(p->highlighted() == 4);  // row 3 is disabled
  t.ui.keyDown(Key::Down);
  R1_EXPECT(p->highlighted() == 4);  // no wrap
  t.ui.keyDown(Key::Home);
  R1_EXPECT(p->highlighted() == 0);
  t.ui.keyDown(Key::End);
  R1_EXPECT(p->highlighted() == 4);
  // Type-ahead: "t" jumps to the next row starting with t, again to the next one.
  t.ui.setTime(2000);
  t.ui.keyDown(Key::Home);
  t.ui.textInput(U't');
  R1_EXPECT(p->highlighted() == 1);
  t.ui.setTime(2100);
  t.ui.textInput(U't');
  R1_EXPECT(p->highlighted() == 2);
  t.ui.setTime(2200);
  t.ui.textInput(U'h');  // "th" refines
  R1_EXPECT(p->highlighted() == 2);
  // Enter activates, closes and restores focus.
  t.ui.keyDown(Key::Enter);
  R1_EXPECT(log.size() == 1 && log[0] == "three" && !c.isOpen());
}

void testSubmenuKeysAndEscape() {
  r1test::TestUi t(400, 300);
  MenuController c(t.ui);
  openContext(t, c, simple());
  MenuPanel* p = panelAt(t.ui, c, 0);
  t.ui.keyDown(Key::Down);
  t.ui.keyDown(Key::Down);  // row 1: the submenu row
  R1_EXPECT(p->highlighted() == 1);
  t.ui.keyDown(Key::Right);
  t.layout();
  R1_EXPECT(c.levelCount() == 2);
  MenuPanel* sub = panelAt(t.ui, c, 1);
  R1_EXPECT(sub != nullptr && sub->highlighted() == 0);  // focus and highlight on the first row
  R1_EXPECT(t.ui.router().focused() == c.panelAt(1));
  // The submenu opens to the right of its row with its first row at the level of the parent row.
  const auto parentRow = t.ui.absRect(p->itemWidget(1));
  const auto subRow = t.ui.absRect(sub->itemWidget(0));
  R1_EXPECT(t.ui.absRect(c.hostAt(1)).x >= parentRow.right());
  R1_EXPECT(t.ui.absRect(c.hostAt(1)).y == parentRow.y);  // the submenu's top edge is level with its row
  R1_EXPECT(subRow.y == parentRow.y + 5);
  t.ui.keyDown(Key::Down);
  R1_EXPECT(sub->highlighted() == 1);
  // Left closes the submenu and returns focus to the parent row.
  t.ui.keyDown(Key::Left);
  t.layout();
  R1_EXPECT(c.levelCount() == 1 && p->highlighted() == 1);
  R1_EXPECT(t.ui.router().focused() == c.panelAt(0));
  // Escape closes the whole stack, one press (spec 10 rule 37 and 47).
  t.ui.keyDown(Key::Right);
  t.layout();
  R1_EXPECT(c.levelCount() == 2);
  t.ui.keyDown(Key::Escape);
  R1_EXPECT(!c.isOpen());
  R1_EXPECT(t.ui.overlays().count() == 0);
}

void testActivationKinds() {
  r1test::TestUi t(400, 300);
  std::vector<std::string> log;
  MenuSpec spec;
  spec.onCommand = [&](const MenuItemSpec& item) { log.push_back(item.id + (item.checked ? "+" : "-")); };
  MenuItemSpec stay = menuCheck("stay", "Stay", false);
  stay.keepOpen = true;
  spec.items = {menuCheck("check", "Check", false), stay, menuRadio("r1", "R1", true), menuRadio("r2", "R2", false), menuRadio("r3", "R3", false)};
  MenuController c(t.ui);
  openContext(t, c, spec);
  MenuPanel* p = panelAt(t.ui, c, 0);
  // keepOpen toggles the check and leaves the menu open.
  p->itemActivated(1, true);
  R1_EXPECT(c.isOpen() && p->item(1).checked && log.size() == 1 && log[0] == "stay+");
  p->itemActivated(1, true);
  R1_EXPECT(!p->item(1).checked && log.back() == "stay-");
  // A radio row selects itself and unchecks its group; the menu then closes.
  p->itemActivated(3, true);
  R1_EXPECT(log.back() == "r2+" && !c.isOpen());
  // Own onActivate wins over onCommand.
  MenuSpec own;
  bool ran = false;
  MenuItemSpec item = menuAction("own", "Own");
  item.onActivate = [&](const MenuItemSpec&) { ran = true; };
  own.items = {item};
  own.onCommand = [&](const MenuItemSpec&) { log.push_back("wrong"); };
  openContext(t, c, own);
  panelAt(t.ui, c, 0)->itemActivated(0, true);
  R1_EXPECT(ran && log.back() != "wrong");
}

void testDismissal() {
  r1test::TestUi t(400, 300);
  MenuController c(t.ui);
  // An outside press closes the menu and is delivered to the widget under it.
  struct Counter : WidgetObject {
    const char* typeName() const override { return "Counter"; }
    void onAttached() override { style().width = r1ui::core::layout::Length::px(100); style().height = r1ui::core::layout::Length::px(100); style().position = r1ui::core::layout::Position::Absolute; style().inset[r1ui::core::layout::kLeft] = r1ui::core::layout::Length::px(250); style().inset[r1ui::core::layout::kTop] = r1ui::core::layout::Length::px(150); }
    void onPointerDown(Event&) override { ++downs; }
    int downs = 0;
  };
  Counter& under = t.ui.create<Counter>(t.ui.root());
  t.layout();
  openContext(t, c, simple());
  t.ui.pointerDown(300, 200);
  t.ui.pointerUp(300, 200);
  R1_EXPECT(!c.isOpen() && under.downs == 1);
  // Window deactivation closes it too.
  openContext(t, c, simple());
  t.ui.setWindowActive(false);
  R1_EXPECT(!c.isOpen());
  t.ui.setWindowActive(true);
  // Escape closes the stack.
  openContext(t, c, simple());
  R1_EXPECT(c.isOpen());
  t.ui.keyDown(Key::Escape);
  R1_EXPECT(!c.isOpen());
}

void testSubmenuTiming() {
  r1test::TestUi t(500, 400);
  MenuSpec s;
  s.items = {menuSubmenu("A", {menuAction("a1", "A1"), menuAction("a2", "A2")}), menuSubmenu("B", {menuAction("b1", "B1")}),
             menuAction("plain", "Plain"), menuAction("plain2", "Plain two")};
  MenuController c(t.ui);
  openContext(t, c, s, 20, 20);
  MenuPanel* p = panelAt(t.ui, c, 0);
  const auto at = [&](int row) { return std::pair<double, double>{centerX(t.ui, p->itemWidget(row)), centerY(t.ui, p->itemWidget(row))}; };
  // Entering a submenu row with nothing open opens it at once.
  moveTo(t, at(0).first, at(0).second, 1000);
  R1_EXPECT(c.levelCount() == 2);
  // Sweeping to another submenu row within 0.5 s replaces it at once.
  moveTo(t, at(1).first - 2, at(1).second, 1200);
  R1_EXPECT(c.levelCount() == 2 && p->highlighted() == 1);
  R1_EXPECT(panelAt(t.ui, c, 1)->item(0).id == "b1");
  // After 0.5 s, moving toward the submenu (rightwards) onto a plain row delays the close by 0.5 s.
  moveTo(t, at(2).first + 5, at(2).second, 2000);
  R1_EXPECT(c.levelCount() == 2);  // still open: delayed
  t.ui.setTime(2400);
  t.ui.tick();
  R1_EXPECT(c.levelCount() == 2);
  t.ui.setTime(2500);
  t.ui.tick();
  t.layout();
  R1_EXPECT(c.levelCount() == 1);
  // Leaving the row before the delay cancels the pending close.
  moveTo(t, at(0).first, at(0).second, 3000);
  R1_EXPECT(c.levelCount() == 2);
  moveTo(t, at(2).first + 5, at(2).second, 3600);  // submenu is older than 0.5 s, moving right: delayed
  R1_EXPECT(c.levelCount() == 2);
  moveTo(t, at(3).first - 5, at(3).second, 3700);  // moving away (left) onto another plain row: at once
  R1_EXPECT(c.levelCount() == 1);
  // Not moving toward the submenu closes at once.
  moveTo(t, at(1).first, at(1).second, 4000);
  R1_EXPECT(c.levelCount() == 2);
  moveTo(t, at(2).first, at(2).second, 4001);
  t.ui.setTime(4700);
  t.ui.tick();
  R1_EXPECT(c.levelCount() == 1);
  c.close();
}

void testMenuBar() {
  r1test::TestUi t(500, 300);
  t.ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
  t.ui.rootStyle().alignItems = r1ui::core::layout::Align::Start;
  MenuBar& bar = t.ui.create<MenuBar>(t.ui.root());
  t.layout();
  R1_EXPECT(bar.addMenu("File", m::fileMenu()) == 0);
  R1_EXPECT(bar.addMenu("Edit", m::editMenu()) == 1);
  R1_EXPECT(bar.addMenu("", m::editMenu()) == -1);
  t.layout();
  const WidgetId file = bar.itemWidget(0);
  const WidgetId edit = bar.itemWidget(1);
  R1_EXPECT(t.ui.absRect(file).h == 24);
  // Hovering a closed bar opens nothing; a click opens the menu under the title.
  moveTo(t, centerX(t.ui, edit), centerY(t.ui, edit), 1000);
  R1_EXPECT(!bar.controller().isOpen());
  t.ui.pointerDown(centerX(t.ui, file), centerY(t.ui, file));
  t.ui.pointerUp(centerX(t.ui, file), centerY(t.ui, file));
  t.layout();
  R1_EXPECT(bar.openIndex() == 0 && bar.controller().isOpen());
  R1_EXPECT(t.ui.absRect(bar.controller().hostAt(0)).y >= t.ui.absRect(file).bottom());
  // Hovering another title switches immediately (0 s delay).
  moveTo(t, centerX(t.ui, edit), centerY(t.ui, edit), 1001);
  R1_EXPECT(bar.openIndex() == 1 && bar.controller().levelCount() == 1);
  // Clicking the open title closes it (toggle) and does not reopen it.
  t.ui.pointerDown(centerX(t.ui, edit), centerY(t.ui, edit));
  t.ui.pointerUp(centerX(t.ui, edit), centerY(t.ui, edit));
  R1_EXPECT(bar.openIndex() == -1 && !bar.controller().isOpen());
  // Keyboard: Down opens the menu with the first row highlighted; Right switches to the next menu.
  t.ui.router().focus(file, r1ui::core::events::FocusReason::Keyboard);
  t.ui.keyDown(Key::Down);
  t.layout();
  R1_EXPECT(bar.openIndex() == 0);
  R1_EXPECT(panelAt(t.ui, bar.controller(), 0)->highlighted() == 0);
  t.ui.keyDown(Key::Right);  // the highlighted row has no submenu: the bar moves to the next title
  t.layout();
  R1_EXPECT(bar.openIndex() == 1);
  t.ui.keyDown(Key::Escape);
  R1_EXPECT(bar.openIndex() == -1);
  R1_EXPECT(t.ui.router().focused() == edit || t.ui.router().focused() == file);
  // The owner destroyed while its menu is open closes the menu.
  bar.openMenu(0);
  t.layout();
  R1_EXPECT(t.ui.overlays().count() == 1);
  t.ui.destroy(bar.id());
  R1_EXPECT(t.ui.overlays().count() == 0);
}

// Spec 10 rule 10: a row's tooltip is its description followed by the shortcut in brackets; rows
// without a description have none. The row also names itself for assistive technology.
void testItemTooltipAndName() {
  r1test::TestUi t(400, 300);
  MenuController c(t.ui);
  MenuSpec spec;
  MenuItemSpec dup = menuAction("dup", "Duplicate", "Ctrl+D");
  dup.tooltip = "Duplicate the selection";
  spec.items = {dup, menuAction("plain", "Plain", "Ctrl+P")};
  openContext(t, c, spec);
  MenuPanel* p = panelAt(t.ui, c, 0);
  const WidgetObject* withTip = t.ui.object(p->itemWidget(0));
  const WidgetObject* without = t.ui.object(p->itemWidget(1));
  R1_EXPECT(withTip->tooltipText() == "Duplicate the selection (Ctrl+D)");
  R1_EXPECT(without->tooltipText().empty());
  R1_EXPECT(withTip->accessibleName() == "Duplicate" && withTip->cursor() == Cursor::Pointer);
}

void testScrolling() {
  r1test::TestUi t(400, 300);
  MenuController c(t.ui);
  MenuSpec spec;
  for (int i = 0; i < 40; ++i) spec.items.push_back(menuAction("r" + std::to_string(i), "Row " + std::to_string(i)));
  openContext(t, c, spec, 20, 10);
  MenuPanel* p = panelAt(t.ui, c, 0);
  // The menu is limited to 80% of the window height and scrolls inside.
  R1_EXPECT(t.ui.absRect(c.hostAt(0)).h <= 240);
  R1_EXPECT(p->maxScroll() > 0 && p->scrollOffset() == 0);
  const auto r = t.ui.absRect(c.panelAt(0));
  t.ui.wheel(r.x + 20, r.y + 20, 0, -2);  // wheel down (negative = away)
  t.layout();
  R1_EXPECT(p->scrollOffset() > 0);
  t.ui.wheel(r.x + 20, r.y + 20, 0, 50);   // far up: clamped at the top
  R1_EXPECT(p->scrollOffset() == 0);
  // End moves the highlight to the last row and scrolls it into view.
  t.ui.keyDown(Key::End);
  t.layout();
  R1_EXPECT(p->highlighted() == 39 && p->scrollOffset() > 0);
  const auto last = t.ui.absRect(p->itemWidget(39));
  const auto view = t.ui.absRect(c.panelAt(0));
  R1_EXPECT(last.bottom() <= view.bottom() && last.y >= view.y);
  t.ui.keyDown(Key::Home);
  t.layout();
  R1_EXPECT(p->scrollOffset() == 0);
  // A menu that fits does not scroll and ignores the wheel.
  MenuController small(t.ui);
  openContext(t, small, simple(), 200, 10);
  R1_EXPECT(panelAt(t.ui, small, 0)->maxScroll() == 0);
  R1_EXPECT(!t.ui.wheel(210, 20, 0, -1));
}

void testHostileInput() {
  r1test::TestUi t(400, 300);
  MenuController c(t.ui);
  // Invalid UTF-8, empty and megabyte labels are sanitised and bounded; a separator has no label.
  MenuSpec bad;
  bad.items = {menuAction("a", std::string("ab\xFF\xFE\xC0\xAF" "cd")), menuAction("b", ""), menuAction("c", std::string(1 << 20, 'x'), std::string(5000, 's')),
               menuSeparator(), menuAction("d", "ok")};
  openContext(t, c, bad);
  MenuPanel* p = panelAt(t.ui, c, 0);
  R1_EXPECT(p->item(0).label.find('\xFF') == std::string::npos && p->item(0).label.find("\xEF\xBF\xBD") != std::string::npos);  // replaced by U+FFFD
  R1_EXPECT(p->item(2).label.size() <= kMaxMenuTextBytes && p->item(2).shortcut.size() <= kMaxMenuTextBytes);
  R1_EXPECT(t.ui.absRect(c.hostAt(0)).w < 5000);
  c.close();
  // Thousands of rows: capped, no hang.
  MenuSpec many;
  for (int i = 0; i < 5000; ++i) many.items.push_back(menuAction("r" + std::to_string(i), "Row"));
  openContext(t, c, many);
  R1_EXPECT(panelAt(t.ui, c, 0)->itemCount() == static_cast<int>(kMaxMenuItems) && panelAt(t.ui, c, 0)->truncated());
  c.close();
  // Submenus nested far deeper than the limit: the extra levels are dropped.
  MenuItemSpec nest = menuAction("leaf", "Leaf");
  for (int i = 0; i < 30; ++i) nest = menuSubmenu("L" + std::to_string(i), {nest});
  MenuSpec deep;
  deep.items = {nest};
  openContext(t, c, deep, 5, 5);
  for (int i = 0; i < 20; ++i) {
    t.ui.keyDown(Key::Down);
    t.ui.keyDown(Key::Right);
    t.layout();
  }
  R1_EXPECT(c.levelCount() <= kMaxMenuDepth);
  c.close();
  // An empty menu does not open; a non-finite point is refused.
  R1_EXPECT(!c.open(MenuSpec{}, MenuOpenOptions{}));
  R1_EXPECT(!c.openContextMenu(simple(), std::nan(""), 0));
  // A command that closes the controller, one that throws and one that destroys the menu bar.
  MenuSpec closer;
  closer.items = {menuAction("x", "X")};
  closer.onCommand = [&](const MenuItemSpec&) { c.close(); };
  openContext(t, c, closer);
  panelAt(t.ui, c, 0)->itemActivated(0, true);
  R1_EXPECT(!c.isOpen());
  MenuSpec thrower;
  thrower.items = {menuAction("x", "X")};
  thrower.onCommand = [](const MenuItemSpec&) { throw std::runtime_error("command failed"); };
  openContext(t, c, thrower);
  bool caught = false;
  try {
    panelAt(t.ui, c, 0)->itemActivated(0, true);
  } catch (const std::runtime_error&) {
    caught = true;
  }
  R1_EXPECT(caught && !c.isOpen());
  // The controller destroyed while its menu is open leaves an inert menu that Escape still closes.
  {
    MenuController temp(t.ui);
    openContext(t, temp, simple());
  }
  R1_EXPECT(t.ui.overlays().count() == 1);
  t.ui.keyDown(Key::Escape);
  R1_EXPECT(t.ui.overlays().count() == 0);
  // A command destroys the menu bar that owns the open menu.
  MenuBar& bar = t.ui.create<MenuBar>(t.ui.root());
  MenuSpec killer;
  killer.items = {menuAction("kill", "Kill")};
  const WidgetId barId = bar.id();
  killer.onCommand = [&](const MenuItemSpec&) { t.ui.destroy(barId); };
  bar.addMenu("Menu", killer);
  t.layout();
  bar.openMenu(0);
  t.layout();
  panelAt(t.ui, bar.controller(), 0)->itemActivated(0, true);
  R1_EXPECT(!t.ui.alive(barId) && t.ui.overlays().count() == 0);
  // Zero-size window.
  r1test::TestUi tiny(0, 0);
  MenuController tc(tiny.ui);
  R1_EXPECT(tc.openContextMenu(simple(), 10, 10));
  tiny.layout();
  tiny.layout();
  tc.close();
  // A hidden or destroyed anchor widget closes its menu (the bar title, here a plain widget).
  r1test::TestUi t2(400, 300);
  struct Anchor : WidgetObject {
    const char* typeName() const override { return "Anchor"; }
    void onAttached() override { style().width = r1ui::core::layout::Length::px(50); style().height = r1ui::core::layout::Length::px(20); }
  };
  Anchor& anchor = t2.ui.create<Anchor>(t2.ui.root());
  t2.layout();
  MenuController ac(t2.ui);
  MenuOpenOptions o;
  o.anchor = t2.ui.absRect(anchor.id());
  o.anchorWidget = anchor.id();
  R1_EXPECT(ac.open(simple(), o));
  t2.layout();
  t2.ui.invalidator().setVisible(anchor.id(), false);
  t2.ui.setTime(t2.ui.now() + 300);
  t2.ui.tick();
  R1_EXPECT(!ac.isOpen());
}

}  // namespace

int main() {
  testOpenAndPlacement();
  testPointerHighlightAndActivation();
  testSubmenuBeforeLayout();
  testKeyboard();
  testSubmenuKeysAndEscape();
  testActivationKinds();
  testDismissal();
  testSubmenuTiming();
  testMenuBar();
  testItemTooltipAndName();
  testScrolling();
  testHostileInput();
  return r1test::finish();
}

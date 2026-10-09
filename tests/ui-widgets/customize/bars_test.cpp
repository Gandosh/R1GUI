// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of the bound bars outside edit mode (CustomizableMenuBar and CustomizableToolbar) and of the
//   layout conversions: a customization-free layout builds exactly what bindCommandMenuBar and
//   bindCommandToolbar build (same menus, same rows, same button ids, icons, tooltips and sizes), every
//   change of the model applies live (hide, move, rename incl. the label of a command entry, a user menu
//   after the built-in ones, a hidden menu, a missing command, a user toolbar item, size step, gap, a
//   spacer), locked menus stay as the owner shipped them, and the conversions between flat command
//   layouts and the section tree round trip.
// Callers: CTest (label fast).
#include <algorithm>

#include "CustomizeFixture.h"
#include "r1ui/widgets/commands/CommandMenus.h"
#include "r1ui/widgets/commands/CommandToolbar.h"
#include "r1ui/widgets/customize/CustomizableBars.h"
#include "r1ui/widgets/menu/MenuPanel.h"

namespace {

using namespace r1test;
using namespace r1ui::widgets;
namespace layout = r1ui::core::layout;

// The rows of the open menu of a bar, as "kind:id:label:shortcut".
std::vector<std::string> openRows(CustomizeFixture& f, MenuBar& bar, int index, int level = 0) {
  std::vector<std::string> rows;
  if (!bar.openMenu(index)) return rows;
  f.t.layout();
  const MenuPanel* panel = f.t.ui.objectAs<MenuPanel>(bar.controller().panelAt(level));
  for (int i = 0; panel != nullptr && i < panel->itemCount(); ++i) {
    const MenuItemSpec& item = panel->item(i);
    rows.push_back(std::to_string(static_cast<int>(item.kind)) + ":" + item.id + ":" + item.label + ":" + item.shortcut);
  }
  return rows;
}

std::vector<std::string> menuTitles(MenuBar& bar, UiContext& ui) {
  std::vector<std::string> out;
  for (int i = 0; i < bar.menuCount(); ++i) {
    const MenuBarItem* item = ui.objectAs<MenuBarItem>(bar.itemWidget(i));
    out.push_back(item != nullptr ? item->title() : std::string("?"));
  }
  return out;
}

void testEqualsCommandBinders() {
  CustomizeFixture f;
  CustomizableMenuBar& custom = f.t.ui.create<CustomizableMenuBar>(f.t.ui.root(), f.controller);
  // The reference: the same menus through bindCommandMenuBar directly.
  MenuBar& direct = f.t.ui.create<MenuBar>(f.t.ui.root());
  direct.style().alignSelf = layout::Align::Start;
  using E = CommandMenuEntry;
  bindCommandMenuBar(direct, f.services(),
                     {{"File", {E::command("file.open"), E::command("file.save")}},
                      {"Edit", {E::command("edit.undo"), E::command("edit.redo"), E::separator(), E::command("edit.cut"), E::command("edit.copy"), E::command("edit.paste")}},
                      {"View", {E::command("view.grid"), E::submenu("Zoom", {E::command("view.zoomIn")})}},
                      {"Help", {E::command("help.about")}}});
  f.t.layout();
  MenuBar* bar = custom.menuBar();
  R1_EXPECT(bar != nullptr && bar->menuCount() == 4 && direct.menuCount() == 4);
  R1_EXPECT(menuTitles(*bar, f.t.ui) == menuTitles(direct, f.t.ui));
  for (int i = 0; i < 4; ++i) {
    const std::vector<std::string> a = openRows(f, *bar, i), b = openRows(f, direct, i);
    R1_EXPECT(!a.empty() && a == b);
    bar->closeMenu();
    direct.closeMenu();
  }
  // The sub-menu level matches too.
  R1_EXPECT(!openRows(f, *bar, 2).empty());
  const MenuPanel* view = f.t.ui.objectAs<MenuPanel>(bar->controller().panelAt(0));
  R1_EXPECT(view != nullptr && view->item(1).kind == MenuItemKind::Submenu && view->item(1).children.size() == 1 && view->item(1).children[0].id == "view.zoomIn");
  bar->closeMenu();
  // The geometry of the menu titles is identical.
  for (int i = 0; i < 4; ++i) {
    const auto a = f.t.ui.absRect(bar->itemWidget(i)), b = f.t.ui.absRect(direct.itemWidget(i));
    R1_EXPECT(a.w == b.w && a.h == b.h && a.x == b.x);
  }

  // Toolbar: same buttons, icons, tooltips and rectangles as bindCommandToolbar.
  CustomizableToolbar& tool = f.t.ui.create<CustomizableToolbar>(f.t.ui.root(), f.controller, "tb.main");
  Toolbar& plain = f.t.ui.create<Toolbar>(f.t.ui.root());
  using I = CommandToolbarItem;
  auto binding = bindCommandToolbar(f.t.ui, plain, f.sync, {I::command("tool.select"), I::command("tool.pen"), I::separator(), I::command("edit.undo"), I::group({"tool.rect", "tool.ellipse"})});
  f.t.layout();
  Toolbar* real = tool.toolbar();
  R1_EXPECT(real != nullptr && real->buttonCount() == plain.buttonCount() && real->buttonCount() == 4);
  for (size_t i = 0; i < real->buttonCount(); ++i) {
    ToolbarButton* a = real->button(i);
    ToolbarButton* b = plain.button(i);
    R1_EXPECT(a->toolId() == b->toolId() && a->icon() == b->icon() && a->tooltipText() == b->tooltipText() && a->kind() == b->kind() && a->active() == b->active());
    const auto ra = f.t.ui.absRect(a->id()), rb = f.t.ui.absRect(b->id());
    R1_EXPECT(ra.w == rb.w && ra.h == rb.h);
  }
  R1_EXPECT(f.t.ui.absRect(real->id()).w == f.t.ui.absRect(plain.id()).w && f.t.ui.absRect(real->id()).h == f.t.ui.absRect(plain.id()).h);
  R1_EXPECT(real->style().gapColumn == plain.style().gapColumn && real->button(0)->style().width.value == plain.button(0)->style().width.value);
  // The buttons work: a click runs the command.
  f.clickWidget(real->button(1)->id());
  R1_EXPECT(f.runs["tool.pen"] == 1 && real->button(1)->active() && real->activeTool() == "tool.pen");
}

void testLiveMenus() {
  CustomizeFixture f;
  CustomizableMenuBar& custom = f.t.ui.create<CustomizableMenuBar>(f.t.ui.root(), f.controller);
  f.t.layout();
  const auto editRows = [&] {
    MenuBar* b = custom.menuBar();
    std::vector<std::string> rows = openRows(f, *b, 1);
    b->closeMenu();
    return rows;
  };
  const size_t before = editRows().size();
  // Hide: the row goes at once (outside edit mode too), restoring brings it back.
  f.model.hideEntry("menu.edit.edit.copy");
  R1_EXPECT(editRows().size() == before - 1);
  f.model.showEntry("menu.edit.edit.copy");
  R1_EXPECT(editRows().size() == before);
  // Move an entry to another menu: the row appears there.
  f.model.move("menu.edit.edit.paste", {"menu.file.s", "menu.file.file.open", cz::Side::Before});
  MenuBar* file = custom.menuBar();
  const std::vector<std::string> fileRows = openRows(f, *file, 0);
  R1_EXPECT(fileRows.size() == 3 && fileRows[0].find(":edit.paste:") != std::string::npos);
  file->closeMenu();
  // A user label on a command entry shows, keeps working and does not get reset by the live refresh.
  f.model.renameLabel("menu.file.file.save", "Save a copy");
  MenuBar* bar2 = custom.menuBar();
  R1_EXPECT(bar2->openMenu(0));
  f.t.layout();
  const MenuPanel* panel = f.t.ui.objectAs<MenuPanel>(bar2->controller().panelAt(0));
  const MenuItemSpec* saveRow = nullptr;
  for (int i = 0; panel != nullptr && i < panel->itemCount(); ++i) {
    if (panel->item(i).label == "Save a copy") saveRow = &panel->item(i);
  }
  R1_EXPECT(saveRow != nullptr && saveRow->shortcut == "Ctrl+S" && saveRow->id == "customize:file.save");
  f.sync.refresh();  // the per-frame refresh of open menus must not put the command's own label back
  panel = f.t.ui.objectAs<MenuPanel>(bar2->controller().panelAt(0));
  bool still = false;
  for (int i = 0; panel != nullptr && i < panel->itemCount(); ++i) still = still || panel->item(i).label == "Save a copy";
  R1_EXPECT(still);
  const MenuItemSpec row = *saveRow;
  row.onActivate(row);
  R1_EXPECT(f.runs["file.save"] == 1);
  bar2->closeMenu();
  // Rename a title and a section heading; sub-menus and headings keep their text.
  f.model.renameLabel("menu.view", "Display");
  R1_EXPECT(menuTitles(*custom.menuBar(), f.t.ui)[2] == "Display");
  // A user menu comes after the built-in menus; a hidden menu is gone.
  const cz::EditResult user = f.model.addUserMenu("Mine");
  f.model.addCommand(user.id, "tool.pen");
  R1_EXPECT((menuTitles(*custom.menuBar(), f.t.ui) == std::vector<std::string>{"File", "Edit", "Display", "Help", "Mine"}));
  f.model.hideEntry("menu.view");
  R1_EXPECT((menuTitles(*custom.menuBar(), f.t.ui) == std::vector<std::string>{"File", "Edit", "Help", "Mine"}));
  const std::vector<std::string> mine = openRows(f, *custom.menuBar(), 3);
  R1_EXPECT(mine.size() == 1 && mine[0].find(":tool.pen:Pen:P") != std::string::npos);
  custom.menuBar()->closeMenu();
  // A command that disappears leaves the menu; when it returns, so does the row.
  cmd::CommandDef saved = *f.registry.find("edit.cut");
  f.registry.remove("edit.cut");
  std::vector<std::string> rows = openRows(f, *custom.menuBar(), 1);
  R1_EXPECT(std::none_of(rows.begin(), rows.end(), [](const std::string& r) { return r.find(":edit.cut:") != std::string::npos; }));
  custom.menuBar()->closeMenu();
  R1_EXPECT(f.registry.add(saved).ok);
  rows = openRows(f, *custom.menuBar(), 1);
  R1_EXPECT(std::any_of(rows.begin(), rows.end(), [](const std::string& r) { return r.find(":edit.cut:") != std::string::npos; }));
  custom.menuBar()->closeMenu();
}

void testLockedMenuStaysAsShipped() {
  CustomizeFixture f;
  CustomizableMenuBar& custom = f.t.ui.create<CustomizableMenuBar>(f.t.ui.root(), f.controller);
  f.t.layout();
  // The model refuses edits of the locked menu; a delta that carries them anyway (an older file) is ignored.
  R1_EXPECT(!f.model.hideEntry("menu.help.help.about").ok && !f.model.renameLabel("menu.help", "Aide").ok);
  cz::Delta stale;
  stale.edits["menu.help.help.about"].hidden = true;
  stale.edits["menu.help"].label = "Aide";
  stale.moves.push_back({"menu.help.help.about", {"menu.file.s", "", cz::Side::End}});
  f.model.setUserDelta(stale);
  f.t.layout();
  const std::vector<std::string> titles = menuTitles(*custom.menuBar(), f.t.ui);
  R1_EXPECT(titles.back() == "Help");
  const std::vector<std::string> rows = openRows(f, *custom.menuBar(), 3);
  R1_EXPECT(rows.size() == 1 && rows[0].find(":help.about:About") != std::string::npos);
  custom.menuBar()->closeMenu();
  R1_EXPECT(f.model.effective().report.count(cz::ReportEntry::Code::Locked) == 3);
}

void testLiveToolbars() {
  CustomizeFixture f;
  CustomizableToolbar& tool = f.t.ui.create<CustomizableToolbar>(f.t.ui.root(), f.controller, "tb.main");
  f.t.layout();
  const double defaultWidth = f.t.ui.absRect(tool.toolbar()->id()).w;
  // Hide, restore, add.
  f.model.hideEntry("tb.main.tool.pen");
  R1_EXPECT(tool.toolbar()->buttonCount() == 3 && tool.button("tool.pen") == nullptr);
  f.model.showEntry("tb.main.tool.pen");
  R1_EXPECT(tool.toolbar()->buttonCount() == 4 && tool.button("tool.pen") != nullptr);
  f.model.addCommand("tb.main", "tool.hand", "tb.main.tool.select", cz::Side::Before);
  f.t.layout();
  R1_EXPECT(tool.toolbar()->buttonCount() == 5 && tool.toolbar()->button(0)->toolId() == "tool.hand");
  // Size step and gap.
  f.model.setToolbarSizeStep("tb.main", cz::SizeStep::Large);
  f.model.setToolbarGap("tb.main", 6);
  f.t.layout();
  Toolbar* t = tool.toolbar();
  const auto a = f.t.ui.absRect(t->button(0)->id()), b = f.t.ui.absRect(t->button(1)->id());
  R1_EXPECT(a.w == 40 && a.h == 40 && b.x - (a.x + a.w) == 6);
  f.model.setToolbarSizeStep("tb.main", cz::SizeStep::Small);
  f.t.layout();
  R1_EXPECT(f.t.ui.absRect(tool.toolbar()->button(0)->id()).w == 26);
  f.model.resetMenu("tb.main");  // removes the user's Hand button, the size step and the gap
  f.t.layout();
  R1_EXPECT(tool.toolbar()->buttonCount() == 4 && f.t.ui.absRect(tool.toolbar()->id()).w == defaultWidth);
  // A spacer splits the bound items into runs and grows to fill a wider toolbar.
  f.model.addSpacer("tb.main", "tb.main.sep", cz::Side::After);
  f.t.layout();
  R1_EXPECT(tool.toolbar()->buttonCount() == 4);
  tool.toolbar()->style().width = layout::Length::px(300);
  tool.toolbar()->requestLayout();
  f.t.layout();
  const auto select = f.t.ui.absRect(tool.toolbar()->button(0)->id());
  const auto undo = f.t.ui.absRect(tool.button("edit.undo")->id());
  R1_EXPECT(undo.x - select.x > 150);  // the items after the spacer moved right
  f.clickWidget(tool.button("edit.undo")->id());
  R1_EXPECT(f.runs["edit.undo"] == 1);
  // Radio exclusivity still holds across the runs.
  f.clickWidget(tool.button("tool.pen")->id());
  R1_EXPECT(tool.button("tool.pen")->active() && !tool.button("tool.select")->active());
  // A user toolbar shows its items; a deleted toolbar shows nothing.
  const cz::EditResult user = f.model.addUserToolbar("Mine", cz::Orientation::Vertical);
  f.model.addCommand(user.id, "view.grid");
  CustomizableToolbar& mine = f.t.ui.create<CustomizableToolbar>(f.t.ui.root(), f.controller, user.id);
  f.t.layout();
  R1_EXPECT(mine.toolbar() != nullptr && mine.toolbar()->orientation() == ToolbarOrientation::Vertical && mine.toolbar()->buttonCount() == 1);
  f.model.deleteUserToolbar(user.id);
  R1_EXPECT(mine.toolbar() == nullptr);
  // Locked toolbars follow the owner.
  CustomizableToolbar& locked = f.t.ui.create<CustomizableToolbar>(f.t.ui.root(), f.controller, "tb.locked");
  f.t.layout();
  R1_EXPECT(!f.model.hideEntry("tb.locked.file.open").ok && locked.toolbar()->buttonCount() == 1);
  // Command missing: the button leaves the toolbar.
  cmd::CommandDef saved = *f.registry.find("edit.undo");
  f.registry.remove("edit.undo");
  R1_EXPECT(tool.button("edit.undo") == nullptr);
  f.registry.add(saved);
  R1_EXPECT(tool.button("edit.undo") != nullptr);
}

void testConversions() {
  using E = CommandMenuEntry;
  const std::vector<E> entries = {E::command("a"), E::command("b"), E::separator(), E::heading("Group"), E::command("c"), E::separator(), E::command("d"),
                                  E::heading("Late"), E::submenu("Sub", {E::command("e"), E::separator(), E::command("f")})};
  const cz::Node menu = menuNodeFromEntries("m", "M", entries);
  R1_EXPECT(menu.kind == cz::Kind::Menu && menu.children.size() == 3);
  R1_EXPECT(menu.children[0].children.size() == 2 && menu.children[0].label.empty());
  R1_EXPECT(menu.children[1].label == "Group" && menu.children[1].children.size() == 1);  // a heading right after a boundary becomes the section heading
  R1_EXPECT(menu.children[2].children.size() == 3 && menu.children[2].children[1].kind == cz::Kind::Heading);  // a heading inside stays an entry
  R1_EXPECT(menu.children[2].children[2].kind == cz::Kind::Submenu && menu.children[2].children[2].children.size() == 2);
  cz::Customization model(cz::LayoutSet{});
  const std::vector<E> back = flattenSections(menu.children, model, nullptr);
  R1_EXPECT(back.size() == entries.size());
  for (size_t i = 0; i < back.size() && i < entries.size(); ++i) {
    R1_EXPECT(back[i].kind == entries[i].kind && back[i].commandId == entries[i].commandId && back[i].text == entries[i].text);
  }
  R1_EXPECT(back[8].children.size() == 3 && back[8].children[1].kind == E::Kind::Separator);
  // Unique ids even for repeated commands; every id is a valid node id.
  const cz::Node twice = menuNodeFromEntries("m", "M", {E::command("x"), E::separator(), E::command("x")});
  R1_EXPECT(twice.children[0].children[0].id != twice.children[1].children[0].id);
  cz::LayoutSet set;
  set.menuBar.menus.push_back(menu);
  set.menuBar.menus.push_back(menuNodeFromEntries("n", "N", {E::command("x"), E::separator(), E::command("x")}));
  R1_EXPECT(cz::validateLayout(set).empty());  // the generated ids form a legal layout
  // Empty sections are skipped; hidden entries vanish.
  cz::LayoutSet one;
  one.menuBar.menus.push_back(cz::Node::menu("a", "A", {cz::Node::section("a1", ""), cz::Node::section("a2", "", {cz::Node::command("a.x", "x")})}));
  const MenuConversion conv = convertMenuBar(one.menuBar, model);
  R1_EXPECT(conv.titles.size() == 1 && conv.titles[0].entries.size() == 1 && conv.titles[0].entries[0].commandId == "x");
  // Toolbars: runs between spacers.
  cz::ToolbarLayout tb;
  tb.items = {cz::Node::command("1", "a"), cz::Node::spacer("s"), cz::Node::group("g", {cz::Node::command("2", "b"), cz::Node::command("3", "c")}), cz::Node::separator("sep")};
  const std::vector<ToolbarRun> runs = convertToolbar(tb);
  R1_EXPECT(runs.size() == 2 && runs[0].items.size() == 1 && runs[0].spacerAfter && runs[1].items.size() == 2 && !runs[1].spacerAfter);
  R1_EXPECT(runs[1].items[0].kind == CommandToolbarItem::Kind::Group && runs[1].items[0].commandIds.size() == 2);
  const cz::ToolbarLayout fromItems = toolbarFromItems("t", "T", {CommandToolbarItem::command("a"), CommandToolbarItem::separator(), CommandToolbarItem::group({"b", "c"})});
  R1_EXPECT(fromItems.items.size() == 3 && fromItems.items[2].children.size() == 2 && fromItems.items[2].kind == cz::Kind::Group);
}

}  // namespace

int main() {
  testEqualsCommandBinders();
  testLiveMenus();
  testLockedMenuStaysAsShipped();
  testLiveToolbars();
  testConversions();
  return r1test::finish();
}

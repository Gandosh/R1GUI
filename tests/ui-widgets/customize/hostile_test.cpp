// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: hostile-input tests of the customize widgets: a menu of 3000 entries (edit display, keyboard moves,
//   the bound menu bar capped at the menu limit), a palette over 20000 commands, the drag hub with
//   non-finite coordinates and a second begin, a drag whose source entry vanishes, edit mode switched off or
//   the editor destroyed in the middle of a drag, in-place rename with invalid and over-long text,
//   free-form geometry at the extremes (largest panel, absurd positions and sizes), a damaged
//   customization file loaded under live bars, deeply nested sub-menus in the edit display, and a
//   destroyed controller owner order (widgets first).
// Callers: CTest (label fast).
#include <cmath>
#include <limits>

#include "CustomizeFixture.h"
#include "r1ui/commands/Text.h"
#include "r1ui/widgets/customize/CommandPalette.h"
#include "r1ui/widgets/customize/CustomizableBars.h"
#include "r1ui/widgets/customize/FreeFormPanel.h"
#include "r1ui/widgets/menu/MenuPanel.h"

namespace {

using namespace r1test;
using namespace r1ui::widgets;
using r1ui::core::layout::RectD;

void testHugeMenu() {
  CustomizeFixture f(900, 700);
  cz::LayoutSet set = fixtureLayouts();
  std::vector<cz::Node> entries;
  for (int i = 0; i < 3000; ++i) entries.push_back(cz::Node::command("big" + std::to_string(i), (i % 2) != 0 ? "edit.cut" : "edit.copy"));
  set.menuBar.menus.push_back(cz::Node::menu("menu.big", "Big", {cz::Node::section("big.s", "", std::move(entries))}));
  f.model.setBuiltin(set);
  CustomizableMenuBar& bar = f.t.ui.create<CustomizableMenuBar>(f.t.ui.root(), f.controller);
  f.t.layout();
  // The bound menu is capped at the menu widget's own item limit and opens.
  MenuBar* real = bar.menuBar();
  R1_EXPECT(real != nullptr && real->menuCount() == 5 && real->openMenu(4));
  real->closeMenu();
  f.enterEditMode();
  MenuEditor* ed = bar.editor();
  R1_EXPECT(ed->setCurrentMenu("menu.big"));
  f.t.layout();
  R1_EXPECT(ed->rowCount() == 3001);
  // Keyboard edits on the far end of the list.
  f.t.ui.focusWidget(ed->id());
  f.t.ui.keyDown(Key::End);
  R1_EXPECT(ed->cursorId() == "big2999");
  f.t.ui.keyDown(Key::Up, Mod::kAlt);
  R1_EXPECT(f.model.effective().layout.menuBar.menus.back().children[0].children[2998].id == "big2999");
  f.t.ui.keyDown(Key::Delete);
  R1_EXPECT(!f.model.find("big2999")->visible);
  // A pointer far below the window is not a drop target; the display survives it.
  const auto row = ed->row(5);
  f.drag(row.handle.x + 5, row.handle.y + 5, 450, 690);
  R1_EXPECT(!f.controller.drag().active());
  // Painting the display touches only the visible rows (a frame completes).
  r1ui::render::Painter painter;
  painter.begin(900, 700);
  f.t.ui.paint(painter);
  painter.end();
}

void testPaletteWithManyCommands() {
  CustomizeFixture f;
  for (int i = 0; i < 20000; ++i) {
    cmd::CommandDef def;
    def.id = "bulk." + std::to_string(i);
    def.label = "Bulk command " + std::to_string(i);
    def.category = "Bulk " + std::to_string(i % 7);
    def.icon = i % 3 == 0 ? "nonexistent-icon-name" : "";
    R1_EXPECT(f.registry.add(def).ok);
  }
  CommandPalette& palette = f.t.ui.create<CommandPalette>(f.t.ui.root(), f.controller);
  palette.style().width = r1ui::core::layout::Length::px(300);
  palette.style().height = r1ui::core::layout::Length::px(300);
  f.t.layout();
  R1_EXPECT(palette.rows().size() > 20000);
  palette.setFilter("bulk command 19999");
  R1_EXPECT(palette.rows().size() == 2 && palette.selectedCommand() == "bulk.19999");
  palette.setFilter("");
  palette.list().setScrollOffset(1e12);  // clamps
  R1_EXPECT(palette.list().scrollOffset() <= palette.list().contentHeight());
  palette.list().setScrollOffset(std::nan(""));
  R1_EXPECT(palette.list().scrollOffset() == 0.0);
  // Painting rows whose icon does not exist falls back instead of failing the frame.
  r1ui::render::Painter painter;
  painter.begin(900, 700);
  f.t.ui.paint(painter);
  painter.end();
}

void testHubAndDragLifetimes() {
  CustomizeFixture f;
  DragHub& hub = f.controller.drag();
  DragPayload p;
  p.kind = DragPayload::Kind::Command;
  p.commandId = "edit.cut";
  p.text = "Cut";
  const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
  R1_EXPECT(!hub.begin(p, nan, 5) && !hub.begin(p, 5, inf) && !hub.active());
  R1_EXPECT(hub.begin(p, 5, 5) && hub.active());
  R1_EXPECT(!hub.begin(p, 6, 6));  // one drag at a time
  hub.move(nan, nan);
  hub.move(inf, 3);
  R1_EXPECT(!hub.end(nan, nan) && !hub.active());
  R1_EXPECT(!hub.end(1, 1));  // nothing active
  hub.cancel();
  R1_EXPECT(!f.t.ui.overlays().any());

  CustomizableMenuBar& bar = f.t.ui.create<CustomizableMenuBar>(f.t.ui.root(), f.controller);
  f.enterEditMode();
  MenuEditor* ed = bar.editor();
  ed->setCurrentMenu("menu.edit");
  f.t.layout();
  const auto added = f.model.addCommand("menu.edit.s-2", "tool.pen");
  f.t.layout();
  // The dragged entry disappears while the drag runs: the drop changes nothing and breaks nothing.
  const auto pen = ed->row(static_cast<size_t>(ed->rowIndex(added.id)));
  const auto undo = ed->row(static_cast<size_t>(ed->rowIndex("menu.edit.edit.undo")));
  f.drag(pen.handle.x + 5, pen.handle.y + 5, undo.text.x + 40, undo.rect.y + 4,
         [&] {
           R1_EXPECT(f.controller.drag().active());
           f.model.removeUserEntry(added.id);
         });
  R1_EXPECT(!f.controller.drag().active() && f.model.find(added.id) == nullptr && f.model.userDelta().moves.empty());
  // Edit mode switched off in the middle of a drag: the drag ends, the editor is gone, the release is harmless.
  f.t.layout();
  const auto cut = ed->row(static_cast<size_t>(ed->rowIndex("menu.edit.edit.cut")));
  f.t.ui.pointerMove(cut.handle.x + 5, cut.handle.y + 5);
  f.t.ui.pointerDown(cut.handle.x + 5, cut.handle.y + 5);
  f.t.ui.pointerMove(cut.handle.x + 60, cut.handle.y + 40);
  R1_EXPECT(f.controller.drag().active());
  f.controller.setEditMode(false);
  f.t.layout();
  R1_EXPECT(!f.controller.drag().active() && !f.t.ui.overlays().any());
  f.t.ui.pointerUp(cut.handle.x + 60, cut.handle.y + 40);
  R1_EXPECT(f.model.userDelta().empty());
  // The editor destroyed directly during a drag.
  f.enterEditMode();
  ed = bar.editor();
  ed->setCurrentMenu("menu.edit");
  f.t.layout();
  const auto redo = ed->row(static_cast<size_t>(ed->rowIndex("menu.edit.edit.redo")));
  f.t.ui.pointerMove(redo.handle.x + 5, redo.handle.y + 5);
  f.t.ui.pointerDown(redo.handle.x + 5, redo.handle.y + 5);
  f.t.ui.pointerMove(redo.handle.x + 60, redo.handle.y + 40);
  R1_EXPECT(f.controller.drag().active());
  f.t.ui.destroy(ed->id());
  f.t.layout();
  R1_EXPECT(!f.controller.drag().active());  // the hub keeps no pointer to the destroyed editor
  f.t.ui.pointerUp(redo.handle.x + 60, redo.handle.y + 40);
  f.t.ui.pointerMove(redo.handle.x + 70, redo.handle.y + 50);
  hub.move(10, 10);
}

void testRenameTexts() {
  CustomizeFixture f;
  CustomizableMenuBar& bar = f.t.ui.create<CustomizableMenuBar>(f.t.ui.root(), f.controller);
  f.enterEditMode();
  MenuEditor* ed = bar.editor();
  ed->setCurrentMenu("menu.edit");
  f.t.layout();
  const auto rename = [&](const std::vector<char32_t>& text) {
    R1_EXPECT(ed->beginRename("menu.edit.edit.cut"));
    for (const char32_t c : text) f.t.ui.textInput(c);
    f.t.ui.keyDown(Key::Enter);
    f.t.layout();
    return f.model.find("menu.edit.edit.cut")->userLabel;
  };
  const std::string odd = rename({0xD800, 0xDFFF, 0x110000, 0x7F, 0x1, 'o', 'k', 0xFFFF});
  R1_EXPECT(r1ui::commands::isValidUtf8(odd) && odd.size() <= cz::kMaxLabelBytes);
  for (const char c : odd) R1_EXPECT(static_cast<unsigned char>(c) >= 0x20 && c != 0x7F);
  std::vector<char32_t> many(500, U'w');
  const std::string longText = rename(many);
  R1_EXPECT(longText.size() <= 64 && !longText.empty());  // the field takes at most 64 characters
  R1_EXPECT(rename({U' ', U' ', U' '}).empty());          // blank restores the default
  // Emoji and combining text survive whole.
  const std::string emoji = rename({0x1F600, 'a', 0x301});
  R1_EXPECT(r1ui::commands::isValidUtf8(emoji) && !emoji.empty());
}

void testFreeFormExtremes() {
  CustomizeFixture f(1000, 800);
  const auto panel = f.model.addUserPanel("Huge", cz::kMaxPanelSize, cz::kMaxPanelSize);
  R1_EXPECT(panel.ok);
  FreeFormPanel& widget = f.t.ui.create<FreeFormPanel>(f.t.ui.root(), f.controller, panel.id);
  f.enterEditMode();
  FreeFormCanvas& canvas = widget.canvas();
  R1_EXPECT(f.t.ui.absRect(canvas.id()).w == cz::kMaxPanelSize);
  const double huge = 1e300, nan = std::nan("");
  const auto placed = f.model.placeButton(panel.id, "edit.cut", {huge, -huge, huge, 1e-300});
  R1_EXPECT(placed.ok && placed.rect.x + placed.rect.w <= cz::kMaxPanelSize && placed.rect.y >= 0.0 && placed.rect.h >= cz::kMinButtonSize);
  R1_EXPECT(!f.model.setButtonRect(placed.id, {nan, 0, 32, 32}).ok && !f.model.moveButton(placed.id, std::numeric_limits<double>::infinity(), 0).ok);
  R1_EXPECT(f.model.resizeButton(placed.id, -50, -50).rect.w == cz::kMinButtonSize);
  canvas.select(placed.id);
  canvas.nudge(1000000, -1000000, true);  // arithmetic in doubles: stays inside
  const auto r = f.model.find(placed.id)->rect;
  R1_EXPECT(r.x >= 0 && r.y >= 0 && r.x + r.w <= cz::kMaxPanelSize && r.y + r.h <= cz::kMaxPanelSize);
  // Snap with a tiny and a huge grid.
  R1_EXPECT(f.model.setPanelSnap(panel.id, true, 1.0).ok && f.model.setPanelSnap(panel.id, true, 256.0).ok && !f.model.setPanelSnap(panel.id, true, 0.0).ok);
  const auto fitted = f.model.fitRect(panel.id, {3, 3, 5, 5});
  R1_EXPECT(fitted.w >= cz::kMinButtonSize && std::fmod(fitted.w, 256.0) == 0.0);
}

void testDamagedFileUnderLiveBars() {
  CustomizeFixture f;
  CustomizableMenuBar& bar = f.t.ui.create<CustomizableMenuBar>(f.t.ui.root(), f.controller);
  CustomizableToolbar& tool = f.t.ui.create<CustomizableToolbar>(f.t.ui.root(), f.controller, "tb.main");
  FreeFormPanel& panel = f.t.ui.create<FreeFormPanel>(f.t.ui.root(), f.controller, "fp.main");
  f.t.layout();
  const std::string text =
      "{\"format\":\"r1ui-customization\",\"version\":1,\"serial\":9,"
      "\"edits\":[{\"node\":\"menu.edit\",\"hidden\":true},{\"node\":\"ghost\",\"hidden\":true},{\"node\":\"fp.undo\",\"rect\":[1e9,1e9,32,32]}],"
      "\"moves\":[{\"node\":\"menu.file\",\"parent\":\"menu.file\"},{\"node\":\"tb.main.tool.pen\",\"parent\":\"menu.file.s\"},"
      "{\"node\":\"menu.edit.s\",\"parent\":\"menu.edit.s-2\"},{\"node\":\"fp.save\",\"parent\":\"tb.main\"}],"
      "\"added\":[{\"id\":\"u1\",\"kind\":\"command\",\"command\":\"no.such\",\"parent\":\"menu.file.s\"},"
      "{\"id\":\"u2\",\"kind\":\"menu\",\"parent\":\"u2\"},{\"id\":\"u3\",\"kind\":\"submenu\",\"label\":\"S\",\"parent\":\"menu.file.s\"},"
      "{\"id\":\"u4\",\"kind\":\"submenu\",\"label\":\"S\",\"parent\":\"u4\"}],"
      "\"toolbarEdits\":[{\"id\":\"tb.main\",\"sizeStep\":\"large\",\"gap\":1000}]}";
  cz::MemoryTextStore store;
  store.setContents(text);
  cz::CustomizationStorage storage(f.model, store);
  const cz::LoadReport report = storage.load();
  R1_EXPECT(report.ok);
  f.t.layout();
  R1_EXPECT(cz::validateLayout(f.model.effective().layout).empty() && cz::validateLayout(f.model.editView().layout).empty());
  R1_EXPECT(!f.model.effective().report.clean());
  // The bars are still there and usable; edit mode over the same damaged state works.
  R1_EXPECT(bar.menuBar() != nullptr && tool.toolbar() != nullptr);
  R1_EXPECT(f.model.effective().layout.menuBar.menus.size() == 3);  // Edit is hidden by the file; File, View and the locked Help stay
  f.enterEditMode();
  R1_EXPECT(bar.editor() != nullptr && tool.editor() != nullptr && panel.canvas().editing());
  f.t.layout();
  f.controller.setEditMode(false);
  f.t.layout();
  R1_EXPECT(bar.menuBar() != nullptr);
}

void testDeepSubmenus() {
  CustomizeFixture f;
  cz::LayoutSet set = fixtureLayouts();
  cz::Node inner = cz::Node::section("d.s9", "", {cz::Node::command("d.cmd", "edit.cut")});
  for (int level = 8; level >= 1; --level) {
    cz::Node sub = cz::Node::submenu("d.m" + std::to_string(level), "Level " + std::to_string(level), {std::move(inner)});
    inner = cz::Node::section("d.s" + std::to_string(level - 1), "", {std::move(sub)});
  }
  set.menuBar.menus.push_back(cz::Node::menu("menu.deep", "Deep", {std::move(inner)}));
  f.model.setBuiltin(set);
  CustomizableMenuBar& bar = f.t.ui.create<CustomizableMenuBar>(f.t.ui.root(), f.controller);
  f.enterEditMode();
  MenuEditor* ed = bar.editor();
  R1_EXPECT(ed->setCurrentMenu("menu.deep"));
  f.t.layout();
  R1_EXPECT(ed->rowCount() == 11 && f.model.effective().report.count(cz::ReportEntry::Code::Limit) > 0);  // cut at the depth limit, and reported
  const auto last = ed->row(ed->rowCount() - 1);
  R1_EXPECT(last.depth == 10 && last.text.w >= 0.0 && last.text.x < last.eye.x);
  r1ui::render::Painter painter;
  painter.begin(900, 700);
  f.t.ui.paint(painter);
  painter.end();
}

}  // namespace

int main() {
  testHugeMenu();
  testPaletteWithManyCommands();
  testHubAndDragLifetimes();
  testRenameTexts();
  testFreeFormExtremes();
  testDamagedFileUnderLiveBars();
  testDeepSubmenus();
  return r1test::finish();
}

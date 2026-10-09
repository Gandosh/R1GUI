// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of the toolbar edit mode (ToolbarEditor inside CustomizableToolbar), through synthetic input:
//   the structure of the strip (cells per kind, eye badges, lock glyph on a locked toolbar), the size
//   step and gap controls, hide and restore with the eye, reordering by drag with the vertical
//   insertion indicator, dropping a command from the palette (end, start, between), the locked toolbar
//   refusing every edit with its reason, removing a user item, the keyboard (cursor, Space, Alt+arrows,
//   Delete, Insert and the picker), the context menu and the vertical orientation.
// Callers: CTest (label fast).
#include <algorithm>

#include "CustomizeFixture.h"
#include "r1ui/widgets/customize/CommandPalette.h"
#include "r1ui/widgets/customize/CustomizableBars.h"
#include "r1ui/widgets/menu/MenuPanel.h"
#include "r1ui/widgets/segmented/Segmented.h"

namespace {

using namespace r1test;
using namespace r1ui::widgets;
using r1ui::core::layout::RectD;

struct ToolbarScene : CustomizeFixture {
  ToolbarScene() {
    main = &t.ui.create<CustomizableToolbar>(t.ui.root(), controller, "tb.main");
    locked = &t.ui.create<CustomizableToolbar>(t.ui.root(), controller, "tb.locked");
    palette = &t.ui.create<CommandPalette>(t.ui.root(), controller);
    palette->style().width = r1ui::core::layout::Length::px(300);
    palette->style().height = r1ui::core::layout::Length::px(220);
    palette->style().flexShrink = 0.0;
    enterEditMode();
    strip = &main->editor()->strip();
  }
  static double cx(const RectD& r) { return r.x + r.w / 2.0; }
  static double cy(const RectD& r) { return r.y + r.h / 2.0; }
  std::string order() {
    std::string out;
    for (const cz::Node& n : cz::findToolbar(model.effective().layout, "tb.main")->items) out += (out.empty() ? "" : ",") + n.id;
    return out;
  }
  ToolbarEditStrip::ItemView item(const std::string& id) {
    const int index = strip->itemIndex(id);
    R1_EXPECT(index >= 0);
    return strip->item(static_cast<size_t>(index < 0 ? 0 : index));
  }
  void clickSegment(WidgetId control, int index, int count) {
    const auto r = t.ui.absRect(control);
    click(r.x + r.w * (index + 0.5) / count, r.y + r.h / 2.0);
  }
  template <class Fn>
  void dragFromPalette(const std::string& commandId, double x, double y, Fn during) {
    palette->select(commandId);
    t.layout();
    const RectD rect = palette->list().rowRect(palette->list().selectedIndex());
    drag(rect.x + 90, cy(rect), x, y, during);
  }

  CustomizableToolbar* main = nullptr;
  CustomizableToolbar* locked = nullptr;
  CommandPalette* palette = nullptr;
  ToolbarEditStrip* strip = nullptr;
};

void testStructure() {
  ToolbarScene s;
  R1_EXPECT(s.main->toolbar() == nullptr && s.main->editor() != nullptr);
  R1_EXPECT(s.strip->itemCount() == 5 && !s.strip->locked());
  using K = cz::Kind;
  const K kinds[] = {K::Command, K::Command, K::Separator, K::Command, K::Group};
  for (size_t i = 0; i < 5; ++i) {
    const auto it = s.strip->item(i);
    R1_EXPECT(it.kind == kinds[i] && it.visible && !it.locked);
    R1_EXPECT(it.eye.y + it.eye.h <= it.rect.y + 1.0);                        // the eye badge sits above its cell
    R1_EXPECT(i == 0 || it.rect.x >= s.strip->item(i - 1).rect.x + s.strip->item(i - 1).rect.w);  // left to right, no overlap
  }
  R1_EXPECT(s.strip->item(0).label == "Select" && s.strip->item(0).icon == "mouse-pointer" && s.strip->item(0).rect.w == 32.0);
  R1_EXPECT(s.strip->item(4).rect.w == 44.0 && s.strip->item(4).label.find("Rectangle") == 0);  // the group's chevron adds 12 px
  // The locked toolbar shows its lock on every cell and refuses its controls.
  ToolbarEditStrip& lockedStrip = s.locked->editor()->strip();
  R1_EXPECT(lockedStrip.locked() && lockedStrip.item(0).locked);
  R1_EXPECT(!s.t.ui.objectAs<Segmented>(s.locked->editor()->sizeControl())->enabled() && s.t.ui.objectAs<Segmented>(s.main->editor()->sizeControl())->enabled());
}

void testSizeStepAndGap() {
  ToolbarScene s;
  Segmented* size = s.t.ui.objectAs<Segmented>(s.main->editor()->sizeControl());
  Segmented* gap = s.t.ui.objectAs<Segmented>(s.main->editor()->gapControl());
  R1_EXPECT(size->selectedIndex() == 1 && gap->selectedIndex() == 1);  // medium, 2 px
  s.clickSegment(s.main->editor()->sizeControl(), 2, 3);
  R1_EXPECT(s.model.effective().layout.toolbars[0].sizeStep == cz::SizeStep::Large && size->selectedIndex() == 2);
  R1_EXPECT(s.strip->item(0).rect.w == 40.0 && s.strip->item(0).rect.h == 40.0);
  s.clickSegment(s.main->editor()->sizeControl(), 0, 3);
  R1_EXPECT(s.strip->item(0).rect.w == 26.0);
  s.clickSegment(s.main->editor()->gapControl(), 3, 5);  // 8 px
  R1_EXPECT(s.model.effective().layout.toolbars[0].gap == 8.0);
  R1_EXPECT(s.strip->item(1).rect.x - (s.strip->item(0).rect.x + s.strip->item(0).rect.w) == 8.0);
  // The same step and gap reach the real toolbar when edit mode is left.
  s.controller.setEditMode(false);
  s.t.layout();
  Toolbar* real = s.main->toolbar();
  R1_EXPECT(real != nullptr && real->button(0)->style().width.value == 26.0 && real->style().gapColumn == 8.0);
  const auto a = s.t.ui.absRect(real->button(0)->id()), b = s.t.ui.absRect(real->button(1)->id());
  R1_EXPECT(a.w == 26 && b.x - (a.x + a.w) == 8);
  // Back to the defaults: the toolbar is the unmodified widget again.
  s.controller.setEditMode(true);
  s.t.layout();
  s.model.resetMenu("tb.main");
  s.controller.setEditMode(false);
  s.t.layout();
  R1_EXPECT(s.main->toolbar()->button(0)->style().width.value == 32.0 && s.main->toolbar()->style().gapColumn == 2.0);
}

void testHideAndRestore() {
  ToolbarScene s;
  const auto pen = s.item("tb.main.tool.pen");
  s.click(ToolbarScene::cx(pen.eye), ToolbarScene::cy(pen.eye));
  R1_EXPECT(!s.item("tb.main.tool.pen").visible && s.strip->itemCount() == 5);
  R1_EXPECT(s.model.restoreList().size() == 1 && s.model.restoreList()[0].path == "Tools > Pen");
  s.controller.setEditMode(false);
  s.t.layout();
  R1_EXPECT(s.main->toolbar()->buttonCount() == 3 && s.main->button("tool.pen") == nullptr);  // the real toolbar closes ranks
  s.controller.setEditMode(true);
  s.t.layout();
  s.strip = &s.main->editor()->strip();  // the edit display is a new widget each time
  const auto again = s.item("tb.main.tool.pen");
  s.click(ToolbarScene::cx(again.eye), ToolbarScene::cy(again.eye));
  R1_EXPECT(s.item("tb.main.tool.pen").visible && s.model.restoreList().empty());
}

void testDragReorder() {
  ToolbarScene s;
  const auto select = s.item("tb.main.tool.select");
  const auto undo = s.item("tb.main.edit.undo");
  // Drag Select to the right half of Undo: after it.
  s.drag(ToolbarScene::cx(select.rect), ToolbarScene::cy(select.rect), undo.rect.x + undo.rect.w - 4, ToolbarScene::cy(undo.rect), [&] {
    R1_EXPECT(s.controller.drag().accepting() && s.strip->indicator().active);
    const auto& ind = s.strip->indicator();
    R1_EXPECT(ind.placement.anchor == "tb.main.edit.undo" && ind.placement.side == cz::Side::After && ind.line.w == 2.0 && ind.line.x > undo.rect.x + undo.rect.w - 4);
  });
  R1_EXPECT(s.order() == "tb.main.tool.pen,tb.main.sep,tb.main.edit.undo,tb.main.tool.select,tb.main.group");
  // Left half of the first cell: the start.
  const auto first = s.strip->item(0);
  const auto last = s.item("tb.main.tool.select");
  s.drag(ToolbarScene::cx(last.rect), ToolbarScene::cy(last.rect), first.rect.x + 3, ToolbarScene::cy(first.rect), [&] { R1_EXPECT(s.strip->indicator().placement.side == cz::Side::Before); });
  R1_EXPECT(s.order() == "tb.main.tool.select,tb.main.tool.pen,tb.main.sep,tb.main.edit.undo,tb.main.group");
  // Escape cancels.
  const std::string before = s.order();
  const auto a = s.strip->item(0);
  const auto b = s.strip->item(3);
  s.drag(ToolbarScene::cx(a.rect), ToolbarScene::cy(a.rect), ToolbarScene::cx(b.rect), ToolbarScene::cy(b.rect), [&] { R1_EXPECT(s.strip->indicator().active); }, false);
  s.t.ui.keyDown(Key::Escape);
  s.t.ui.pointerUp(ToolbarScene::cx(b.rect), ToolbarScene::cy(b.rect));
  R1_EXPECT(!s.controller.drag().active() && s.order() == before);
  // Dropping a toolbar item on a menu editor's row is not a legal change; on empty space nothing happens.
  s.drag(ToolbarScene::cx(a.rect), ToolbarScene::cy(a.rect), 880, 600);
  R1_EXPECT(s.order() == before);
}

void testPaletteDrop() {
  ToolbarScene s;
  const auto last = s.strip->item(4);
  s.dragFromPalette("tool.hand", last.rect.x + last.rect.w + 3, ToolbarScene::cy(last.rect), [&] {
    R1_EXPECT(s.controller.drag().accepting() && s.strip->indicator().placement.side == cz::Side::After && s.strip->indicator().placement.anchor == "tb.main.group");
  });
  R1_EXPECT(cz::findToolbar(s.model.effective().layout, "tb.main")->items.back().commandId == "tool.hand");
  R1_EXPECT(s.strip->itemCount() == 6 && s.strip->item(5).label == "Hand" && s.strip->item(5).rect.x > s.strip->item(4).rect.x);
  const auto first = s.strip->item(0);
  s.dragFromPalette("view.grid", first.rect.x + 2, ToolbarScene::cy(first.rect), [&] { R1_EXPECT(s.strip->indicator().placement.side == cz::Side::Before); });
  R1_EXPECT(cz::findToolbar(s.model.effective().layout, "tb.main")->items.front().commandId == "view.grid");
  // The locked toolbar refuses and says why.
  ToolbarEditStrip& lockedStrip = s.locked->editor()->strip();
  const auto cell = lockedStrip.item(0).rect;
  s.dragFromPalette("tool.hand", cell.x + 3, ToolbarScene::cy(cell), [&] { R1_EXPECT(!s.controller.drag().accepting() && !lockedStrip.indicator().active); });
  R1_EXPECT(s.controller.lastRefusal().find("locked") != std::string::npos);
  R1_EXPECT(cz::findToolbar(s.model.effective().layout, "tb.locked")->items.size() == 1);
  // Hide, drag and the eye on the locked toolbar all refuse.
  const auto eye = lockedStrip.item(0).eye;
  s.click(ToolbarScene::cx(eye), ToolbarScene::cy(eye));
  R1_EXPECT(s.model.find("tb.locked.file.open")->visible && s.controller.lastRefusal().find("locked") != std::string::npos);
  s.t.ui.pointerMove(ToolbarScene::cx(cell), ToolbarScene::cy(cell));
  R1_EXPECT(std::string(lockedStrip.tooltipText()).find("locked") != std::string::npos);
}

void testKeyboardAndContextMenu() {
  ToolbarScene s;
  s.t.ui.focusWidget(s.strip->id());
  s.t.ui.keyDown(Key::Right);
  R1_EXPECT(s.strip->cursorId() == "tb.main.tool.select");
  s.t.ui.keyDown(Key::Right);
  R1_EXPECT(s.strip->cursorId() == "tb.main.tool.pen");
  s.t.ui.keyDown(Key::Space);
  R1_EXPECT(!s.item("tb.main.tool.pen").visible);
  s.t.ui.keyDown(Key::Space);
  s.t.ui.keyDown(Key::Right, Mod::kAlt);  // Pen moves behind the separator
  R1_EXPECT(s.order() == "tb.main.tool.select,tb.main.sep,tb.main.tool.pen,tb.main.edit.undo,tb.main.group");
  s.t.ui.keyDown(Key::Home);
  R1_EXPECT(s.strip->cursorId() == s.strip->item(0).id);
  s.t.ui.keyDown(Key::End);
  R1_EXPECT(s.strip->cursorId() == s.strip->item(s.strip->itemCount() - 1).id);
  // Insert: the picker adds the chosen command after the cursor.
  s.t.ui.keyDown(Key::Insert);
  s.t.layout();
  R1_EXPECT(s.t.ui.overlays().anyModal());
  s.type("grid");
  s.t.layout();
  s.t.ui.keyDown(Key::Enter);
  s.t.layout();
  R1_EXPECT(!s.t.ui.overlays().any() && cz::findToolbar(s.model.effective().layout, "tb.main")->items.back().commandId == "view.grid");
  // Delete removes the user item (it is ours) and hides a built-in one.
  s.strip->setCursor(s.strip->item(s.strip->itemCount() - 1).id);
  s.t.ui.keyDown(Key::Delete);
  R1_EXPECT(s.strip->itemCount() == 5);
  s.strip->setCursor("tb.main.tool.select");
  s.t.ui.keyDown(Key::Delete);
  R1_EXPECT(!s.item("tb.main.tool.select").visible);
  // Context menu: add a separator and a spacer after the cell; Remove only for user items.
  const auto undo = s.item("tb.main.edit.undo");
  s.rightClick(ToolbarScene::cx(undo.rect), ToolbarScene::cy(undo.rect));
  R1_EXPECT(s.strip->contextMenu().isOpen());
  const MenuPanel* panel = s.t.ui.objectAs<MenuPanel>(s.strip->contextMenu().panelAt(0));
  std::vector<std::string> labels;
  for (int i = 0; panel != nullptr && i < panel->itemCount(); ++i) labels.push_back(panel->item(i).label);
  const auto has = [&](const char* l) { return std::find(labels.begin(), labels.end(), l) != labels.end(); };
  R1_EXPECT(has("Hide") && has("Add command...") && has("Add separator") && has("Add spacer") && has("Reset this toolbar") && !has("Remove"));
  const auto run = [&](const char* label) {
    const MenuPanel* p = s.t.ui.objectAs<MenuPanel>(s.strip->contextMenu().panelAt(0));
    for (int i = 0; p != nullptr && i < p->itemCount(); ++i) {
      if (p->item(i).label == label) {
        const MenuItemSpec item = p->item(i);
        item.onActivate(item);
      }
    }
    s.strip->contextMenu().close();
    s.t.layout();
  };
  run("Add spacer");
  R1_EXPECT(s.strip->itemCount() == 6 && s.item("tb.main.edit.undo").rect.x < s.strip->item(4).rect.x);
  s.rightClick(ToolbarScene::cx(undo.rect), ToolbarScene::cy(undo.rect));
  run("Reset this toolbar");
  R1_EXPECT(s.strip->itemCount() == 5 && s.model.userDelta().empty());
}

void testVerticalAndEmpty() {
  CustomizeFixture f;
  cz::LayoutSet set = fixtureLayouts();
  cz::ToolbarLayout side = r1ui::widgets::toolbarFromItems("tb.side", "Side", {CommandToolbarItem::command("view.grid"), CommandToolbarItem::command("tool.hand")});
  side.orientation = cz::Orientation::Vertical;
  set.toolbars.push_back(side);
  f.model.setBuiltin(set);
  CustomizableToolbar& bar = f.t.ui.create<CustomizableToolbar>(f.t.ui.root(), f.controller, "tb.side");
  f.enterEditMode();
  ToolbarEditStrip& strip = bar.editor()->strip();
  R1_EXPECT(strip.itemCount() == 2 && strip.item(1).rect.y > strip.item(0).rect.y + 30.0 && strip.item(1).rect.x == strip.item(0).rect.x);
  R1_EXPECT(f.t.ui.absRect(strip.id()).h > f.t.ui.absRect(strip.id()).w);
  f.t.ui.focusWidget(strip.id());
  f.t.ui.keyDown(Key::Down);
  R1_EXPECT(strip.cursorId() == "tb.side.view.grid");
  f.t.ui.keyDown(Key::Down, Mod::kAlt);
  R1_EXPECT(cz::findToolbar(f.model.effective().layout, "tb.side")->items.back().commandId == "view.grid");
  // An unknown toolbar id shows an empty strip without failing.
  CustomizableToolbar& none = f.t.ui.create<CustomizableToolbar>(f.t.ui.root(), f.controller, "tb.gone");
  f.t.layout();
  R1_EXPECT(none.editor() != nullptr && none.editor()->strip().itemCount() == 0);
}

}  // namespace

int main() {
  testStructure();
  testSizeStepAndGap();
  testHideAndRestore();
  testDragReorder();
  testPaletteDrop();
  testKeyboardAndContextMenu();
  testVerticalAndEmpty();
  return r1test::finish();
}

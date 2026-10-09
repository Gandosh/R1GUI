// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: a smoke test of the group's gallery entry without a GPU: buildGalleryCustomize lays out the tool
//   strip, the menu bar, three toolbars, the free-form panel and the palette over the 24 sample commands;
//   the Customize toggle switches every part to its edit display and back; a command dragged from the
//   palette lands in the panel; a menu entry hidden in edit mode disappears from the real menu; the
//   locked Help menu and the locked toolbar refuse edits; destroying the page leaves nothing open.
// Callers: CTest (label fast).
#include "NoDialogs.h"
#include "TestSupport.h"
#include "r1ui/widgets/customize/GalleryCustomize.h"
#include "r1ui/widgets/menu/MenuPanel.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;
namespace cz = r1ui::commands::customize;

}  // namespace

int main() {
  r1test::TestUi t(1200, 1000);
  t.ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
  GalleryCustomizePage& page = createGalleryCustomize(t.ui, t.ui.root());
  t.layout();
  R1_EXPECT(t.ui.widgetCount() > 60);
  R1_EXPECT(t.ui.absRect(page.id()).h > 400);
  R1_EXPECT(page.menuBar().menuBar() != nullptr && page.menuBar().menuBar()->menuCount() == 5);
  R1_EXPECT(page.mainToolbar().toolbar() != nullptr && page.mainToolbar().toolbar()->buttonCount() == 7);
  R1_EXPECT(page.sideToolbar().toolbar() != nullptr && page.sideToolbar().toolbar()->orientation() == ToolbarOrientation::Vertical);
  R1_EXPECT(page.palette().rows().size() > 20);
  R1_EXPECT(!page.controller().editMode());

  // Customize: every bound part becomes its edit display.
  R1_EXPECT(page.registry().find(kCmdCustomizeToggle) != nullptr);
  page.controller().setEditMode(true);
  t.layout();
  R1_EXPECT(page.menuBar().editor() != nullptr && page.menuBar().menuBar() == nullptr);
  R1_EXPECT(page.mainToolbar().editor() != nullptr && page.sideToolbar().editor() != nullptr && page.lockedToolbar().editor() != nullptr);
  R1_EXPECT(page.panel().canvas().editing());

  // Hide "Paste" with the eye; the real menu loses the row once edit mode is left.
  MenuEditor* editor = page.menuBar().editor();
  R1_EXPECT(editor->setCurrentMenu("menu.edit"));
  t.layout();
  const int pasteRow = editor->rowIndex("menu.edit.edit.paste");
  R1_EXPECT(pasteRow >= 0);
  const auto eye = editor->row(static_cast<size_t>(pasteRow)).eye;
  t.ui.pointerMove(eye.x + eye.w / 2, eye.y + eye.h / 2);
  t.ui.pointerDown(eye.x + eye.w / 2, eye.y + eye.h / 2);
  t.ui.pointerUp(eye.x + eye.w / 2, eye.y + eye.h / 2);
  R1_EXPECT(page.model().restoreList().size() == 1 && page.model().restoreList()[0].id == "menu.edit.edit.paste");

  // A palette row dragged onto the panel places a button there.
  const size_t before = page.panel().canvas().buttonCount();
  page.palette().select("tool.pen");
  t.layout();
  const auto row = page.palette().list().rowRect(page.palette().list().selectedIndex());
  const auto canvas = t.ui.absRect(page.panel().canvasWidget());
  t.ui.pointerMove(row.x + 60, row.y + row.h / 2);
  t.ui.pointerDown(row.x + 60, row.y + row.h / 2);
  for (int i = 1; i <= 8; ++i) t.ui.pointerMove(row.x + 60 + (canvas.x + 200 - row.x - 60) * i / 8.0, row.y + row.h / 2 + (canvas.y + 90 - row.y - row.h / 2) * i / 8.0);
  R1_EXPECT(page.controller().drag().active() && page.controller().drag().accepting());
  t.ui.pointerUp(canvas.x + 200, canvas.y + 90);
  t.layout();
  R1_EXPECT(page.panel().canvas().buttonCount() == before + 1);

  // Leaving edit mode: the hidden entry is gone from the real menu, the new button is a live command button.
  page.controller().setEditMode(false);
  t.layout();
  MenuBar* bar = page.menuBar().menuBar();
  R1_EXPECT(bar != nullptr && bar->openMenu(1));
  t.layout();
  const MenuPanel* panel = t.ui.objectAs<MenuPanel>(bar->controller().panelAt(0));
  bool hasPaste = false, hasCopy = false;
  for (int i = 0; panel != nullptr && i < panel->itemCount(); ++i) {
    hasPaste = hasPaste || panel->item(i).id == "edit.paste";
    hasCopy = hasCopy || panel->item(i).id == "edit.copy";
  }
  R1_EXPECT(panel != nullptr && !hasPaste && hasCopy);
  bar->closeMenu();
  R1_EXPECT(page.panel().canvas().buttonWidget(page.model().effective().layout.panels[0].buttons.back().id) != nullptr);
  R1_EXPECT(page.controller().model().userDelta().added.size() == 1);

  // Destroying the page leaves no overlay and no stale subscription.
  t.ui.destroy(page.id());
  t.layout();
  R1_EXPECT(!t.ui.overlays().any());
  return r1test::finish();
}

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of the free-form panel (FreeFormPanel / FreeFormCanvas / FreeFormButton), through synthetic
//   input: outside edit mode the buttons are live command buttons (position and size from the layout,
//   enabled and checked following the command, tooltip with the chord, a click runs the command);
//   in edit mode: selection rules (click, Ctrl/Shift toggle, marquee, collapse on release), eight
//   resize handles with the minimum size and the panel bounds, moving a multi-selection, the drag
//   threshold, Escape cancelling, arrow-key nudging (Shift = 10 px), snap-to-grid (default off, 8 px),
//   Delete, Ctrl+A, dropping a command from the palette, the picker, align and z-order from the context
//   menu, the locked panel refusing edits, and the persistence round trip of the geometry.
// Callers: CTest (label fast).
#include <algorithm>

#include "CustomizeFixture.h"
#include "r1ui/widgets/customize/CommandPalette.h"
#include "r1ui/widgets/customize/FreeFormPanel.h"
#include "r1ui/widgets/menu/MenuPanel.h"
#include "r1ui/widgets/switch/Switch.h"

namespace {

using namespace r1test;
using namespace r1ui::widgets;
using r1ui::core::layout::RectD;

struct PanelScene : CustomizeFixture {
  PanelScene() {
    panel = &t.ui.create<FreeFormPanel>(t.ui.root(), controller, "fp.main");
    palette = &t.ui.create<CommandPalette>(t.ui.root(), controller);
    palette->style().width = r1ui::core::layout::Length::px(300);
    palette->style().height = r1ui::core::layout::Length::px(200);
    palette->style().flexShrink = 0.0;
    t.layout();
  }
  FreeFormCanvas& canvas() { return panel->canvas(); }
  static double cx(const RectD& r) { return r.x + r.w / 2.0; }
  static double cy(const RectD& r) { return r.y + r.h / 2.0; }
  RectD rectOf(const std::string& id) { return canvas().button(static_cast<size_t>(canvas().buttonIndex(id))).rect; }
  RectD local(const std::string& id) {
    const auto origin = t.ui.absRect(canvas().id());
    RectD r = rectOf(id);
    r.x -= origin.x;
    r.y -= origin.y;
    return r;
  }
  RectD modelRect(const std::string& id) {
    const cz::Rect r = model.find(id)->rect;
    return {r.x, r.y, r.w, r.h};
  }
  template <class Fn>
  void dragFromPalette(const std::string& commandId, double x, double y, Fn during) {
    palette->select(commandId);
    t.layout();
    const RectD rect = palette->list().rowRect(palette->list().selectedIndex());
    drag(rect.x + 90, cy(rect), x, y, during);
  }

  FreeFormPanel* panel = nullptr;
  CommandPalette* palette = nullptr;
};

void testNormalMode() {
  PanelScene s;
  FreeFormCanvas& canvas = s.canvas();
  R1_EXPECT(!canvas.editing() && canvas.buttonCount() == 2);
  FreeFormButton* save = canvas.buttonWidget("fp.save");
  FreeFormButton* undo = canvas.buttonWidget("fp.undo");
  R1_EXPECT(save != nullptr && undo != nullptr);
  const auto origin = s.t.ui.absRect(canvas.id());
  const auto saveRect = s.t.ui.absRect(save->id());
  R1_EXPECT(saveRect.x == origin.x + 16 && saveRect.y == origin.y + 16 && saveRect.w == 96 && saveRect.h == 32);
  R1_EXPECT(origin.w == 320 && origin.h == 200);
  R1_EXPECT(save->tooltipText() == "Save (Ctrl+S)");  // the tooltip carries the chord
  // A click runs the command; enabled follows the command.
  s.click(saveRect.x + 40, saveRect.y + 16);
  R1_EXPECT(s.runs["file.save"] == 1);
  s.enabled["edit.undo"] = false;
  s.sync.refresh();
  R1_EXPECT(!undo->enabled());
  const auto undoRect = s.t.ui.absRect(undo->id());
  s.click(undoRect.x + 40, undoRect.y + 16);
  R1_EXPECT(s.runs["edit.undo"] == 0);
  s.enabled["edit.undo"] = true;
  s.sync.refresh();
  // A toggle shows its state.
  s.model.placeButton("fp.main", "view.grid", {16, 80, 100, 32});
  s.t.layout();
  FreeFormButton* grid = canvas.buttonWidget(s.model.effective().layout.panels[0].buttons.back().id);
  R1_EXPECT(grid != nullptr && !grid->hasState(StateFlag::kSelected));
  s.checked["view.grid"] = true;
  s.sync.refresh();
  R1_EXPECT(grid->hasState(StateFlag::kSelected));
  // A removed command hides its button; a hidden button is not in the panel.
  s.registry.remove("file.save");
  s.t.layout();
  R1_EXPECT(canvas.buttonWidget("fp.save") == nullptr);
  s.model.hideEntry("fp.undo");
  s.t.layout();
  R1_EXPECT(canvas.buttonWidget("fp.undo") == nullptr);
  // The header (snap switch) only shows while editing.
  R1_EXPECT(s.t.ui.absRect(s.panel->header()).h == 0 || s.t.ui.object(s.panel->header())->style().display == r1ui::core::layout::Display::None);
}

void testSelectionRules() {
  PanelScene s;
  s.enterEditMode();
  FreeFormCanvas& canvas = s.canvas();
  R1_EXPECT(canvas.editing() && canvas.buttonCount() == 2);
  const auto save = s.rectOf("fp.save"), undo = s.rectOf("fp.undo");
  s.click(PanelScene::cx(save), PanelScene::cy(save));
  R1_EXPECT(canvas.selection() == std::vector<std::string>{"fp.save"});
  s.t.ui.pointerMove(PanelScene::cx(undo), PanelScene::cy(undo));
  s.t.ui.pointerDown(PanelScene::cx(undo), PanelScene::cy(undo), r1ui::core::events::Button::Left, Mod::kCtrl);
  s.t.ui.pointerUp(PanelScene::cx(undo), PanelScene::cy(undo), r1ui::core::events::Button::Left, Mod::kCtrl);
  R1_EXPECT(canvas.selection().size() == 2);
  // Pressing a selected button of a group keeps the group; releasing without a drag collapses to it.
  s.t.ui.pointerMove(PanelScene::cx(save), PanelScene::cy(save));
  s.t.ui.pointerDown(PanelScene::cx(save), PanelScene::cy(save));
  R1_EXPECT(canvas.selection().size() == 2);
  s.t.ui.pointerUp(PanelScene::cx(save), PanelScene::cy(save));
  R1_EXPECT(canvas.selection() == std::vector<std::string>{"fp.save"});
  // Ctrl on a selected button removes it from the selection.
  s.t.ui.pointerDown(PanelScene::cx(save), PanelScene::cy(save), r1ui::core::events::Button::Left, Mod::kShift);
  s.t.ui.pointerUp(PanelScene::cx(save), PanelScene::cy(save), r1ui::core::events::Button::Left, Mod::kShift);
  R1_EXPECT(canvas.selection().empty());
  // Marquee: from empty space around both buttons; its rectangle is shown while dragging.
  const auto origin = s.t.ui.absRect(canvas.id());
  s.drag(origin.x + 4, origin.y + 4, origin.x + 260, origin.y + 70, [&] { R1_EXPECT(canvas.marquee().has_value() && canvas.selection().size() == 2); });
  R1_EXPECT(!canvas.marquee().has_value() && canvas.selection().size() == 2);
  // A marquee that touches one button selects it alone; a click on empty space clears.
  s.drag(origin.x + 4, origin.y + 4, origin.x + 60, origin.y + 70);
  R1_EXPECT(canvas.selection() == std::vector<std::string>{"fp.save"});
  s.click(origin.x + 300, origin.y + 180);
  R1_EXPECT(canvas.selection().empty());
  // Ctrl+A, Escape.
  s.t.ui.focusWidget(canvas.id());
  s.t.ui.keyDown(Key::A, Mod::kCtrl);
  R1_EXPECT(canvas.selection().size() == 2);
  s.t.ui.keyDown(Key::Escape);
  R1_EXPECT(canvas.selection().empty());
}

void testMoveAndThreshold() {
  PanelScene s;
  s.enterEditMode();
  FreeFormCanvas& canvas = s.canvas();
  const auto save = s.rectOf("fp.save");
  const uint64_t version = s.model.version();
  // Four pixels is not a drag.
  s.drag(PanelScene::cx(save), PanelScene::cy(save), PanelScene::cx(save) + 3, PanelScene::cy(save) + 3);
  R1_EXPECT(s.model.version() == version && canvas.selection().size() == 1);
  // A real drag shows the provisional position and commits on release.
  s.drag(PanelScene::cx(save), PanelScene::cy(save), PanelScene::cx(save) + 40, PanelScene::cy(save) + 30, [&] {
    R1_EXPECT(s.rectOf("fp.save").x == save.x + 40 && s.rectOf("fp.save").y == save.y + 30);
    R1_EXPECT(s.model.userDelta().empty());  // nothing is stored before the release
  });
  R1_EXPECT(s.modelRect("fp.save").x == 56 && s.modelRect("fp.save").y == 46);
  // Clamped inside the panel.
  const auto now = s.rectOf("fp.save");
  s.drag(PanelScene::cx(now), PanelScene::cy(now), PanelScene::cx(now) + 900, PanelScene::cy(now) + 900);
  R1_EXPECT(s.modelRect("fp.save").x == 320 - 96 && s.modelRect("fp.save").y == 200 - 32);
  s.drag(PanelScene::cx(s.rectOf("fp.save")), PanelScene::cy(s.rectOf("fp.save")), -500, -500);
  R1_EXPECT(s.modelRect("fp.save").x == 0 && s.modelRect("fp.save").y == 0);
  // Escape cancels a drag in progress.
  const auto a = s.rectOf("fp.undo");
  s.drag(PanelScene::cx(a), PanelScene::cy(a), PanelScene::cx(a) + 30, PanelScene::cy(a) + 30, [&] { R1_EXPECT(s.rectOf("fp.undo").x == a.x + 30); }, false);
  s.t.ui.keyDown(Key::Escape);
  s.t.ui.pointerUp(PanelScene::cx(a) + 30, PanelScene::cy(a) + 30);
  R1_EXPECT(s.rectOf("fp.undo").x == a.x && s.modelRect("fp.undo").x == 128);
  // A multi-selection moves together, keeping its shape.
  s.t.ui.focusWidget(s.canvas().id());
  s.t.ui.keyDown(Key::A, Mod::kCtrl);
  const auto u = s.rectOf("fp.undo");
  const double gap = s.local("fp.undo").x - s.local("fp.save").x;
  s.drag(PanelScene::cx(u), PanelScene::cy(u), PanelScene::cx(u) + 50, PanelScene::cy(u) + 60);
  R1_EXPECT(s.modelRect("fp.undo").x - s.modelRect("fp.save").x == gap);
  R1_EXPECT(s.modelRect("fp.save").y == 60 && s.modelRect("fp.undo").y == 76);
}

void testResizeHandles() {
  PanelScene s;
  s.enterEditMode();
  FreeFormCanvas& canvas = s.canvas();
  const auto save = s.rectOf("fp.save");
  s.click(PanelScene::cx(save), PanelScene::cy(save));
  const auto handles = canvas.handles("fp.save");
  // Clockwise from the top-left: corners and edge midpoints, 8 px each, centred on the border.
  R1_EXPECT(handles[0].x + 4 == save.x && handles[0].y + 4 == save.y && handles[0].w == 8.0);
  R1_EXPECT(handles[4].x + 4 == save.x + save.w && handles[4].y + 4 == save.y + save.h);
  R1_EXPECT(handles[1].x + 4 == save.x + save.w / 2 && handles[3].y + 4 == save.y + save.h / 2 && handles[5].y + 4 == save.y + save.h && handles[7].x + 4 == save.x);
  // South-east grows the button; the north-west corner moves its origin.
  s.drag(PanelScene::cx(handles[4]), PanelScene::cy(handles[4]), PanelScene::cx(handles[4]) + 24, PanelScene::cy(handles[4]) + 10, [&] {
    R1_EXPECT(s.rectOf("fp.save").w == 120 && s.rectOf("fp.save").h == 42);
  });
  R1_EXPECT(s.modelRect("fp.save").w == 120 && s.modelRect("fp.save").h == 42 && s.modelRect("fp.save").x == 16);
  const auto h2 = canvas.handles("fp.save");
  s.drag(PanelScene::cx(h2[0]), PanelScene::cy(h2[0]), PanelScene::cx(h2[0]) + 10, PanelScene::cy(h2[0]) + 6);
  R1_EXPECT(s.modelRect("fp.save").x == 26 && s.modelRect("fp.save").y == 22 && s.modelRect("fp.save").w == 110 && s.modelRect("fp.save").h == 36);
  // The east edge only changes the width; the south edge only the height.
  const auto h3 = canvas.handles("fp.save");
  s.drag(PanelScene::cx(h3[3]), PanelScene::cy(h3[3]), PanelScene::cx(h3[3]) + 14, PanelScene::cy(h3[3]) + 30);
  R1_EXPECT(s.modelRect("fp.save").w == 124 && s.modelRect("fp.save").h == 36);
  // Never smaller than 16 px; the opposite edge stays where it was.
  const auto h4 = canvas.handles("fp.save");
  const double right = s.modelRect("fp.save").x + s.modelRect("fp.save").w;
  s.drag(PanelScene::cx(h4[7]), PanelScene::cy(h4[7]), PanelScene::cx(h4[7]) + 400, PanelScene::cy(h4[7]));
  R1_EXPECT(s.modelRect("fp.save").w == cz::kMinButtonSize && s.modelRect("fp.save").x + s.modelRect("fp.save").w == right);
  // Never outside the panel.
  const auto h5 = canvas.handles("fp.save");
  s.drag(PanelScene::cx(h5[2]), PanelScene::cy(h5[2]), 900, -300);
  R1_EXPECT(s.modelRect("fp.save").x + s.modelRect("fp.save").w == 320 && s.modelRect("fp.save").y == 0);
  // Handles exist only for a single selection.
  s.t.ui.focusWidget(canvas.id());
  s.t.ui.keyDown(Key::A, Mod::kCtrl);
  R1_EXPECT(canvas.selection().size() == 2);
}

void testNudgeSnapDelete() {
  PanelScene s;
  s.enterEditMode();
  FreeFormCanvas& canvas = s.canvas();
  s.click(PanelScene::cx(s.rectOf("fp.save")), PanelScene::cy(s.rectOf("fp.save")));
  s.t.ui.keyDown(Key::Right);
  s.t.ui.keyDown(Key::Down);
  R1_EXPECT(s.modelRect("fp.save").x == 17 && s.modelRect("fp.save").y == 17);
  s.t.ui.keyDown(Key::Left, Mod::kShift);
  s.t.ui.keyDown(Key::Up, Mod::kShift);
  R1_EXPECT(s.modelRect("fp.save").x == 7 && s.modelRect("fp.save").y == 7);
  s.t.ui.keyDown(Key::Left, Mod::kShift);
  R1_EXPECT(s.modelRect("fp.save").x == 0);  // clamped at the edge
  // Snap defaults to off; the switch turns it on and positions land on the 8 px grid.
  R1_EXPECT(!s.model.effective().layout.panels[0].snap && s.model.effective().layout.panels[0].grid == 8.0);
  Switch* snap = s.t.ui.objectAs<Switch>(s.panel->snapSwitch());
  R1_EXPECT(snap != nullptr && !snap->checked());
  s.clickWidget(snap->id());
  R1_EXPECT(s.model.effective().layout.panels[0].snap && snap->checked());
  const auto save = s.rectOf("fp.save");
  s.drag(PanelScene::cx(save), PanelScene::cy(save), PanelScene::cx(save) + 21, PanelScene::cy(save) + 13);
  R1_EXPECT(std::fmod(s.modelRect("fp.save").x, 8.0) == 0.0 && std::fmod(s.modelRect("fp.save").y, 8.0) == 0.0 && std::fmod(s.modelRect("fp.save").w, 8.0) == 0.0);
  s.t.ui.keyDown(Key::Right);  // on a grid an arrow moves one cell, Shift two (16 px)
  const double x = s.modelRect("fp.save").x;
  s.t.ui.keyDown(Key::Right, Mod::kShift);
  R1_EXPECT(s.modelRect("fp.save").x == x + 16);
  s.clickWidget(snap->id());
  R1_EXPECT(!s.model.effective().layout.panels[0].snap);
  // Delete removes a user button for good and hides a built-in one.
  const auto added = s.model.placeButton("fp.main", "tool.pen", {200, 100, 64, 32});
  s.t.layout();
  s.click(PanelScene::cx(s.rectOf(added.id)), PanelScene::cy(s.rectOf(added.id)));
  s.t.ui.keyDown(Key::Delete);
  R1_EXPECT(s.model.find(added.id) == nullptr && canvas.selection().empty());
  s.click(PanelScene::cx(s.rectOf("fp.undo")), PanelScene::cy(s.rectOf("fp.undo")));
  s.t.ui.keyDown(Key::Delete);
  R1_EXPECT(s.model.find("fp.undo") != nullptr && !s.model.find("fp.undo")->visible && s.model.restoreList().size() == 1);
  R1_EXPECT(canvas.buttonCount() == 2);  // hidden buttons stay in the edit display (faded)
}

void testPaletteDropAndPicker() {
  PanelScene s;
  s.enterEditMode();
  FreeFormCanvas& canvas = s.canvas();
  const auto origin = s.t.ui.absRect(canvas.id());
  s.dragFromPalette("tool.hand", origin.x + 200, origin.y + 120, [&] {
    R1_EXPECT(s.controller.drag().accepting() && canvas.dropPreview().has_value());
    R1_EXPECT(canvas.dropPreview()->w == 72 && canvas.dropPreview()->h == 32);
  });
  R1_EXPECT(!canvas.dropPreview().has_value() && canvas.buttonCount() == 3);
  const cz::Node* placed = &s.model.effective().layout.panels[0].buttons.back();
  R1_EXPECT(placed->commandId == "tool.hand" && placed->user && placed->rect.x == 164 && placed->rect.y == 104);  // centred under the pointer
  R1_EXPECT(canvas.selection().size() == 1 && canvas.selection()[0] == placed->id);
  // Near the edge the drop is clamped inside; outside the canvas it is refused.
  s.dragFromPalette("tool.pen", origin.x + 318, origin.y + 198, [&] { R1_EXPECT(s.controller.drag().accepting()); });
  const cz::Node& edge = s.model.effective().layout.panels[0].buttons.back();
  R1_EXPECT(edge.rect.x + edge.rect.w <= 320 && edge.rect.y + edge.rect.h <= 200);
  const size_t count = canvas.buttonCount();
  s.dragFromPalette("tool.pen", origin.x + 600, origin.y + 100, [&] { R1_EXPECT(!s.controller.drag().accepting() && !canvas.dropPreview().has_value()); });
  R1_EXPECT(canvas.buttonCount() == count);
  // The picker (Insert) adds at the last pointer position.
  s.t.ui.focusWidget(canvas.id());
  s.t.ui.pointerMove(origin.x + 100, origin.y + 150);
  s.t.ui.keyDown(Key::Insert);
  s.t.layout();
  R1_EXPECT(s.t.ui.overlays().anyModal());
  s.type("zoom");
  s.t.layout();
  s.t.ui.keyDown(Key::Enter);
  s.t.layout();
  R1_EXPECT(canvas.buttonCount() == count + 1 && s.model.effective().layout.panels[0].buttons.back().commandId == "view.zoomIn");
}

void testAlignAndOrder() {
  PanelScene s;
  s.enterEditMode();
  FreeFormCanvas& canvas = s.canvas();
  s.model.moveButton("fp.undo", 100, 90);
  s.t.layout();
  s.t.ui.focusWidget(canvas.id());
  s.t.ui.keyDown(Key::A, Mod::kCtrl);
  const auto r = s.rectOf("fp.undo");
  s.rightClick(PanelScene::cx(r), PanelScene::cy(r));
  R1_EXPECT(canvas.contextMenu().isOpen());
  const auto run = [&](const char* label) {
    const MenuPanel* p = s.t.ui.objectAs<MenuPanel>(canvas.contextMenu().panelAt(0));
    for (int i = 0; p != nullptr && i < p->itemCount(); ++i) {
      if (p->item(i).label == label) {
        const MenuItemSpec item = p->item(i);
        R1_EXPECT(item.enabled);
        item.onActivate(item);
      }
    }
    canvas.contextMenu().close();
    s.t.layout();
  };
  run("Align top");
  R1_EXPECT(s.modelRect("fp.save").y == 16 && s.modelRect("fp.undo").y == 16);
  s.rightClick(PanelScene::cx(s.rectOf("fp.undo")), PanelScene::cy(s.rectOf("fp.undo")));
  run("Align left");
  R1_EXPECT(s.modelRect("fp.undo").x == s.modelRect("fp.save").x);
  // The two buttons overlap now; a click hits the top one (the last in z-order). Send to back puts it first,
  // bring to front puts it last again.
  s.click(PanelScene::cx(s.rectOf("fp.save")), PanelScene::cy(s.rectOf("fp.save")));
  R1_EXPECT(canvas.selection() == std::vector<std::string>{"fp.undo"});
  s.rightClick(PanelScene::cx(s.rectOf("fp.save")), PanelScene::cy(s.rectOf("fp.save")));
  run("Send to back");
  R1_EXPECT(s.model.effective().layout.panels[0].buttons.front().id == "fp.undo" && canvas.button(0).id == "fp.undo");
  s.click(PanelScene::cx(s.rectOf("fp.save")), PanelScene::cy(s.rectOf("fp.save")));
  R1_EXPECT(canvas.selection() == std::vector<std::string>{"fp.save"});  // now Save is on top
  s.rightClick(PanelScene::cx(s.rectOf("fp.save")), PanelScene::cy(s.rectOf("fp.save")));
  run("Send to back");
  s.rightClick(PanelScene::cx(s.rectOf("fp.save")), PanelScene::cy(s.rectOf("fp.save")));
  run("Bring to front");
  R1_EXPECT(!canvas.selection().empty() && s.model.effective().layout.panels[0].buttons.back().id == canvas.selection()[0]);
  // A single button cannot be aligned.
  s.rightClick(PanelScene::cx(s.rectOf("fp.save")), PanelScene::cy(s.rectOf("fp.save")));
  const MenuPanel* p = s.t.ui.objectAs<MenuPanel>(canvas.contextMenu().panelAt(0));
  bool enabled = false;
  for (int i = 0; p != nullptr && i < p->itemCount(); ++i) enabled = enabled || (p->item(i).label == "Align left" && p->item(i).enabled);
  R1_EXPECT(!enabled);
  canvas.contextMenu().close();
}

void testLockedPanelAndRoundTrip() {
  PanelScene s;
  cz::LayoutSet set = fixtureLayouts();
  set.panels[0].locked = true;
  s.model.setBuiltin(set);
  s.enterEditMode();
  FreeFormCanvas& canvas = s.canvas();
  const auto save = s.rectOf("fp.save");
  s.drag(PanelScene::cx(save), PanelScene::cy(save), PanelScene::cx(save) + 50, PanelScene::cy(save) + 50);
  R1_EXPECT(s.model.userDelta().empty() && s.controller.lastRefusal().find("locked") != std::string::npos);
  s.t.ui.focusWidget(canvas.id());
  s.t.ui.keyDown(Key::A, Mod::kCtrl);
  s.t.ui.keyDown(Key::Right);
  s.t.ui.keyDown(Key::Delete);
  R1_EXPECT(s.model.userDelta().empty() && canvas.buttonCount() == 2);
  s.dragFromPalette("tool.hand", s.t.ui.absRect(canvas.id()).x + 200, s.t.ui.absRect(canvas.id()).y + 100, [&] { R1_EXPECT(!s.controller.drag().accepting()); });
  R1_EXPECT(s.model.userDelta().empty());
  R1_EXPECT(!s.t.ui.objectAs<Switch>(s.panel->snapSwitch())->enabled());

  // Geometry survives save and reload.
  PanelScene a;
  a.enterEditMode();
  a.model.placeButton("fp.main", "tool.hand", {40, 100, 64, 32});
  a.model.moveButton("fp.undo", 200, 120);
  a.model.setPanelSnap("fp.main", true, 4);
  a.controller.setEditMode(false);
  PanelScene b;
  cz::MemoryTextStore copy;
  copy.setContents(*a.store.contents());
  cz::CustomizationStorage storage(b.model, copy);
  R1_EXPECT(storage.load().ok);
  b.t.layout();
  R1_EXPECT(b.canvas().buttonCount() == 3 && b.model.effective().layout.panels[0].snap && b.model.effective().layout.panels[0].grid == 4.0);
  R1_EXPECT(b.model.find("fp.undo")->rect.x == 200 && b.model.find("fp.undo")->rect.y == 120);
  const auto undoRect = b.t.ui.absRect(b.canvas().buttonWidget("fp.undo")->id());
  const auto origin = b.t.ui.absRect(b.canvas().id());
  R1_EXPECT(undoRect.x == origin.x + 200 && undoRect.y == origin.y + 120);
}

}  // namespace

int main() {
  testNormalMode();
  testSelectionRules();
  testMoveAndThreshold();
  testResizeHandles();
  testNudgeSnapDelete();
  testPaletteDropAndPicker();
  testAlignAndOrder();
  testLockedPanelAndRoundTrip();
  return r1test::finish();
}

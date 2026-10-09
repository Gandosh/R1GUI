// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: regression tests for the overlay lifecycle fixes of the phase 4 review: an open tooltip no
//   longer keeps the frame loop alive (H2), tick() reports work only when something changed (M1), a
//   host destroyed by its owner leaves no zombie entry (M2), global shortcuts are withheld while a
//   modal overlay is open (M4), a failed open leaves no invisible blocker (M5), focus survives a menu
//   command that opens a dialog (M6) and a destroyed MenuController runs no callbacks (M7).
// Callers: CTest (fast tier). Calls: UiContext, OverlayManager, Popover, Dialog, MenuBar.
#include <cstdio>
#include <memory>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/button/Button.h"
#include "r1ui/widgets/dialog/Dialog.h"
#include "r1ui/widgets/menu/MenuBar.h"
#include "r1ui/widgets/menu/MenuController.h"
#include "r1ui/widgets/popover/Popover.h"

using namespace r1ui::widgets;
namespace layout = r1ui::core::layout;
namespace events = r1ui::core::events;

namespace {

class Box : public WidgetObject {
 public:
  const char* typeName() const override { return "Box"; }
  void onAttached() override {
    style().width = layout::Length::px(100);
    style().height = layout::Length::px(30);
    setFocusable(true);
  }
  void onClick(Event&) override { ++clicks; }
  int clicks = 0;
};

class TipBox final : public Box {
 public:
  void onAttached() override {
    Box::onAttached();
    setTooltip("hello");
  }
};

struct Keys : events::GlobalKeyHandler {
  int calls = 0;
  bool onGlobalKey(const events::Event&, events::Router&) override {
    ++calls;
    return true;
  }
};

// One full frame as the host runs it.
void renderFrame(UiContext& ui) {
  ui.frame();
  r1ui::render::Painter painter;
  painter.begin(400, 300);
  ui.paint(painter);
  ui.finishPaint();
  painter.end();
}

}  // namespace

int main() {
  {  // H2: after the 100 ms fade an open tooltip needs no more frames
    r1test::TestUi t;
    UiContext& ui = t.ui;
    ui.setAnimationsEnabled(true);
    ui.setFrameLoopRunning(true);
    ui.create<TipBox>(ui.root());
    ui.frame();
    uint64_t now = 1000;
    ui.setTime(now);
    ui.pointerMove(20, 10);
    int busyLate = 0;
    for (int i = 0; i < 90; ++i) {  // 1.8 s: rest delay, fade-in, then idle with the tooltip open
      now += 20;
      ui.setTime(now);
      ui.tick();
      if (ui.needsFrame()) {
        renderFrame(ui);
        if (i >= 70) ++busyLate;
      }
    }
    std::printf("open tooltip: %d frames requested in the last 400 ms (needsFrame at the end: %d)\n", busyLate, ui.needsFrame() ? 1 : 0);
    R1_EXPECT(ui.overlays().count() == 1);  // the tooltip is up
    R1_EXPECT(!ui.needsFrame());
    R1_EXPECT(busyLate == 0);
  }
  {  // M1: an open popover polls its anchor on a timer; that must not ask the host for frames
    r1test::TestUi t;
    UiContext& ui = t.ui;
    Box& anchor = ui.create<Box>(ui.root());
    ui.frame();
    PopoverOptions options;
    options.anchorWidget = anchor.id();
    const auto handle = openPopover(ui, options);
    ui.create<Box>(handle.host);
    renderFrame(ui);
    renderFrame(ui);
    R1_EXPECT(!ui.needsFrame());
    int busy = 0;
    for (int i = 0; i < 20; ++i) {
      ui.setTime(1000 + 130 * (i + 1));
      if (ui.tick()) ++busy;
    }
    R1_EXPECT(busy == 0);
    R1_EXPECT(!ui.needsFrame());
    closePopover(ui, handle);
  }
  {  // M2: the owner destroys the host directly
    r1test::TestUi t;
    UiContext& ui = t.ui;
    ui.create<Box>(ui.root());
    ui.frame();
    OverlayOptions o;
    o.modal = true;
    o.anchor = {100, 100, 0, 0};
    o.surface = OverlaySurface::Menu;
    int closed = 0;
    o.onClosed = [&](DismissReason) { ++closed; };
    const auto handle = ui.overlays().open(o);
    ui.create<Box>(handle.host);
    renderFrame(ui);
    const size_t before = ui.widgetCount();
    ui.destroy(handle.host);
    R1_EXPECT(ui.needsFrame());
    ui.frame();
    R1_EXPECT(ui.overlays().count() == 0 && closed == 1);
    R1_EXPECT(ui.widgetCount() < before);  // the modal blocker went too
    R1_EXPECT(!ui.needsFrame());
    R1_EXPECT(!ui.keyDown(events::Key::Escape));  // a zombie must not eat the key
  }
  {  // M4: modal overlays isolate the keyboard
    r1test::TestUi t;
    UiContext& ui = t.ui;
    Keys keys;
    ui.setGlobalKeyHandler(&keys);
    ui.create<Box>(ui.root());
    ui.frame();
    OverlayOptions o;
    o.modal = true;
    o.surface = OverlaySurface::Dialog;
    o.anchor = {100, 100, 0, 0};
    o.placement = Placement::Center;
    const auto handle = ui.overlays().open(o);
    ui.create<Box>(handle.host);
    ui.frame();
    ui.keyDown(events::Key::Delete);
    ui.keyDown(static_cast<events::Key>('Z'), events::Mod::kCtrl);
    R1_EXPECT(keys.calls == 0);
    ui.overlays().closeAll();
    ui.keyDown(events::Key::Delete);
    R1_EXPECT(keys.calls == 1);  // no modal overlay: shortcuts work again
    o.allowGlobalShortcuts = true;
    ui.overlays().open(o);
    ui.frame();
    ui.keyDown(events::Key::Delete);
    R1_EXPECT(keys.calls == 2);  // a dialog may opt in
  }
  {  // a tooltip whose source a timer destroys goes in the same tick
    r1test::TestUi t;
    UiContext& ui = t.ui;
    TipBox& box = ui.create<TipBox>(ui.root());
    ui.frame();
    uint64_t now = 1000;
    ui.setTime(now);
    ui.pointerMove(20, 10);
    for (int i = 0; i < 20 && ui.overlays().count() == 0; ++i) {
      now += 20;
      ui.setTime(now);
      ui.tick();
      ui.frame();
    }
    R1_EXPECT(ui.overlays().count() == 1);
    const auto id = box.id();
    ui.setTimer(0, [&ui, id] { ui.destroy(id); });
    ui.setTime(now + 20);
    ui.tick();
    ui.frame();
    R1_EXPECT(ui.overlays().count() == 0);
  }
  {  // M5: an open() that fails half way leaves nothing behind
    r1test::TestUi t;
    UiContextOptions options;
    options.limits.maxNodes = 4;  // root + overlay layer + the box + one more
    UiContext ui(t.services, options);
    ui.setViewport(400, 300, 1.0f);
    Box& box = ui.create<Box>(ui.root());
    ui.frame();
    const size_t nodes = ui.widgetCount();
    OverlayOptions o;
    o.modal = true;
    o.surface = OverlaySurface::Dialog;
    o.anchor = {100, 100, 0, 0};
    o.placement = Placement::Center;
    bool threw = false;
    try {
      ui.overlays().open(o);
    } catch (const std::exception&) {
      threw = true;
    }
    R1_EXPECT(threw);
    R1_EXPECT(ui.overlays().count() == 0);
    R1_EXPECT(ui.widgetCount() == nodes);
    ui.frame();
    ui.pointerMove(10, 10);
    ui.pointerDown(10, 10);
    ui.pointerUp(10, 10);
    R1_EXPECT(box.clicks == 1);  // no invisible blocker eats the click
  }
  {  // M6: a menu command opens a dialog; after the dialog closes focus is not lost
    r1test::TestUi t(600, 400);
    UiContext& ui = t.ui;
    Button& button = ui.create<Button>(ui.root(), "Hello");
    MenuBar& bar = ui.create<MenuBar>(ui.root());
    DialogHandle dialog;
    MenuSpec file;
    file.items = {menuAction("v", "Variables")};
    file.onCommand = [&](const MenuItemSpec&) {
      DialogSpec spec;
      spec.title = "T";
      spec.actions = {{.id = "ok", .label = "OK", .kind = DialogActionKind::Primary, .isDefault = true}};
      dialog = openDialog(ui, spec);
    };
    bar.addMenu("File", file);
    t.layout();
    ui.focusWidget(button.id());
    const auto r = ui.absRect(bar.itemWidget(0));
    ui.pointerDown(r.x + 3, r.y + 3);
    ui.pointerUp(r.x + 3, r.y + 3);
    t.layout();
    ui.keyDown(events::Key::Down);
    ui.keyDown(events::Key::Enter);
    t.layout();
    R1_EXPECT(isDialogOpen(ui, dialog));
    closeDialog(ui, dialog);
    t.layout();
    R1_EXPECT(ui.router().focused().valid() && ui.alive(ui.router().focused()));
  }
  {  // M7: a controller that dies with its menu open must not get its command called
    r1test::TestUi t(600, 400);
    UiContext& ui = t.ui;
    int fired = 0;
    auto controller = std::make_unique<MenuController>(ui);
    MenuSpec spec;
    spec.items = {menuAction("a", "Alpha"), menuAction("b", "Beta")};
    spec.onCommand = [&fired](const MenuItemSpec&) { ++fired; };
    controller->openContextMenu(spec, 50, 50);
    t.layout();
    controller.reset();
    R1_EXPECT(ui.overlays().count() == 1);
    const auto r = ui.absRect(ui.overlays().hostOf(ui.overlays().stack()[0]));
    ui.pointerDown(r.x + 20, r.y + 14);
    ui.pointerUp(r.x + 20, r.y + 14);
    R1_EXPECT(fired == 0);
  }
  return r1test::finish();
}

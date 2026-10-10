// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: integration oracle for the two widget modes of the preview, headless (UiContext over
//   in-memory textures): every Gallery page builds, lays out and can be switched to and from; the
//   composed Widgets screen has the reference proportions and is live: typing in a number field
//   edits the document, the Name field renames the layer and the panel header, dragging a layer
//   reorders the model, menus, the colour picker popover and the dialog open and close with the
//   right focus behaviour, the theme tool calls the host, an idle screen has no scheduled work and
//   a hostile input storm leaves it intact.
// Why: widgets of five builders are composed here for the first time; this is where their
//   interplay (focus, overlays, selection sync, timers) is checked without a window.
// Callers: CTest (label fast).
#include <cstdio>
#include <memory>
#include <random>
#include <string>

#include "ComposedApp.h"
#include "ComposedUtil.h"
#include "GalleryApp.h"
#include "Scene.h"
#include "r1ui/render/Painter.h"
#include "r1ui/theme/Tokens.h"
#include "r1ui/widgets/menu/MenuBar.h"
#include "r1ui/widgets/numberfield/NumberField.h"
#include "r1ui/widgets/select/Select.h"
#include "r1ui/widgets/splitter/Splitter.h"
#include "r1ui/widgets/text/TextureFactory.h"
#include "r1ui/widgets/textinput/TextInput.h"
#include "r1ui/widgets/toolbar/Toolbar.h"
#include "r1ui/widgets/tree/TreeView.h"

namespace {

using namespace r1ui::widgets;
namespace events = r1ui::core::events;
using r1ui::core::tree::WidgetId;

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

struct Fixture {
  Fixture()
      : services(loadTokens(), textures, {std::string(R1UI_ASSETS_DIR) + "/fonts", {std::string(R1UI_ASSETS_DIR) + "/icons/lucide", std::string(R1UI_ASSETS_DIR) + "/icons/custom"}}),
        ui(services) {
    ui.setAnimationsEnabled(false);
    ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
    ui.rootStyle().padding[r1ui::core::layout::kTop] = preview::kTitleBarHeight;
    SectionBox& area = preview::build::flex(ui, ui.root(), true);
    preview::build::grow(area.style());
    content = area.id();
    ui.setViewport(1440, 900, 1.0f);
  }
  static std::shared_ptr<const r1ui::theme::Tokens> loadTokens() {
    auto parsed = r1ui::theme::Tokens::loadFile(std::string(R1UI_ASSETS_DIR) + "/theme/tokens.json", {.requireAllSections = true});
    if (!parsed.ok()) throw std::runtime_error("tokens: " + parsed.error);
    return std::make_shared<const r1ui::theme::Tokens>(std::move(*parsed.tokens));
  }
  void layout() { ui.frame(); }
  r1ui::core::layout::Rect rect(WidgetId id) { return ui.absRect(id); }
  void click(WidgetId id, events::Button button = events::Button::Left) {
    const auto r = rect(id);
    click(r.x + r.w / 2.0, r.y + r.h / 2.0, button);
  }
  void click(double x, double y, events::Button button = events::Button::Left) {
    ui.pointerMove(x, y);
    ui.pointerDown(x, y, button);
    ui.pointerUp(x, y, button);
    layout();
  }
  void key(events::Key k) {
    ui.keyDown(k);
    ui.keyUp(k);
    layout();
  }
  void type(const char32_t* text) {
    for (; *text != 0; ++text) ui.textInput(*text);
    layout();
  }
  NullTextureFactory textures;
  Services services;
  UiContext ui;
  WidgetId content;
};

void testGalleryPages() {
  Fixture f;
  preview::GalleryApp gallery(f.ui, f.content);
  f.layout();
  size_t first = f.ui.widgetCount();
  expect(first > 100, "the first gallery page has many widgets");
  for (size_t i = 0; i < preview::GalleryApp::kPageCount; ++i) {
    expect(gallery.selectPage(i), "page selectable");
    f.layout();
    std::printf("gallery page %zu: %zu widgets\n", i, f.ui.widgetCount());
    expect(gallery.page() == i && f.ui.widgetCount() > 10, "page built");
    const auto scroll = f.rect(gallery.scrollArea());
    expect(scroll.w > 1000 && scroll.h > 800, "the page area fills the window beside the list");
  }
  expect(!gallery.selectPage(preview::GalleryApp::kPageCount), "a bad page index is refused");
  expect(f.ui.overlays().stack().empty(), "no popup is left open by switching pages");
  // The list drives the pages: clicking the third row shows the third page.
  TreeView* list = f.ui.objectAs<TreeView>(gallery.list());
  f.click(f.rect(gallery.list()).x + 40.0, f.rect(gallery.list()).y + TreeView::kRowHeight * 2.5);
  expect(gallery.page() == 2 && list->isSelected(3), "clicking a list row selects the page");
}

struct ComposedFixture : Fixture {
  ComposedFixture() {
    preview::ComposedHost host;
    host.setDarkTheme = [this](bool dark) {
      darkRequests.push_back(dark);
      services.theme().set(dark ? r1ui::theme::ThemeId::Dark : r1ui::theme::ThemeId::Light);
    };
    host.isDark = [this] { return services.theme().id() == r1ui::theme::ThemeId::Dark; };
    host.quit = [this] { quit = true; };
    app = std::make_unique<preview::ComposedApp>(ui, content, std::move(host));
    layout();
  }
  ~ComposedFixture() { app.reset(); }
  // One frame as the shell runs it: layout, paint on a recording painter, atlas upload.
  void frameAt(uint64_t ms) {
    ui.setTime(ms);
    ui.tick();
    layout();
    r1ui::render::Painter painter;
    painter.begin(1440, 900);
    ui.paint(painter);
    ui.finishPaint();
    painter.end();
  }
  // Runs frames 50 ms apart while the context wants them; returns how many it wanted (cap 40).
  int settle(uint64_t& now) {
    int frames = 0;
    while (ui.needsFrame() && frames < 40) {
      now += 50;
      frameAt(now);
      ++frames;
    }
    return frames;
  }
  std::unique_ptr<preview::ComposedApp> app;
  std::vector<bool> darkRequests;
  bool quit = false;
};

void testComposedGeometry() {
  ComposedFixture f;
  const auto& ids = f.app->ids();
  expect(std::abs(f.rect(ids.leftPane).w - 259) <= 2, "the layer column is about 259 px (reference)");
  expect(std::abs(f.rect(ids.panelPane).w - 258) <= 2, "the properties panel is about 258 px (reference)");
  expect(f.rect(ids.tabBar).h == 36, "the tab bar is 36 px high");
  const auto x = f.rect(ids.x);
  const auto y = f.rect(ids.y);
  expect(x.h == 26 && x.w == y.w && y.x - (x.x + x.w) == 6, "X and Y are equal 26 px fields with a 6 px gap");
  expect(f.rect(ids.canvasToolbar).y + f.rect(ids.canvasToolbar).h > 850, "the tool bar floats at the bottom of the canvas");
  std::printf("composed screen: %zu widgets\n", f.ui.widgetCount());
  expect(f.ui.widgetCount() > 120, "the screen is composed of well over a hundred widgets");
  // Splitter: dragging the handle between canvas and panel grows the panel.
  Splitter* split = f.ui.objectAs<Splitter>(ids.splitter);
  const double before = split->paneSize(2);
  const auto h = split->handleRect(1);
  f.ui.pointerMove(h.x + h.w / 2.0, 400);
  f.ui.pointerDown(h.x + h.w / 2.0, 400);
  for (int i = 1; i <= 8; ++i) f.ui.pointerMove(h.x + h.w / 2.0 - 10.0 * i, 400);
  f.ui.pointerUp(h.x - 80, 400);
  f.layout();
  expect(split->paneSize(2) > before + 60.0, "dragging the handle resizes the panel");
  // The window shrinks: the panes keep their proportions and stay above the minimums.
  f.ui.setViewport(900, 600, 1.0f);
  f.layout();
  expect(f.rect(ids.panelPane).w >= 220 && f.rect(ids.canvasPane).w >= 240, "narrow window respects the pane minimums");
}

void testFieldsEditTheDocument() {
  ComposedFixture f;
  const auto& ids = f.app->ids();
  // Click the X field: it enters edit mode with the text selected; type, Enter commits.
  f.click(ids.x);
  expect(f.ui.router().focused() == ids.x, "clicking a number field focuses it");
  f.type(U"150");
  f.key(events::Key::Enter);
  expect(f.app->document().rect.x == 150.0, "committing X writes the document");
  expect(f.ui.objectAs<NumberField>(ids.x)->value() == 150.0, "the field shows the value");
  const auto canvas = static_cast<preview::CanvasView*>(f.ui.object(ids.canvas));
  expect(canvas->rectangle().x == f.rect(ids.canvas).x + 150 + static_cast<int>(preview::CanvasView::kOriginX), "the canvas follows the field");
  // The Name field renames the layer and the panel header; Escape reverts uncommitted text.
  f.click(ids.nameInput);
  f.ui.objectAs<TextInput>(ids.nameInput)->selectAll();
  f.type(U"Card");
  f.key(events::Key::Enter);
  expect(f.app->document().name == "Card", "committing the name writes the document");
  TreeView* layers = f.ui.objectAs<TreeView>(ids.layers);
  expect(layers->model()->label(preview::kRectangleNode) == "Card", "the layer tree shows the new name");
  // A field with focus keeps letters away from the application shortcuts.
  f.click(ids.nameInput);
  struct Keys final : events::GlobalKeyHandler {
    bool onGlobalKey(const events::Event&, events::Router&) override { ++count; return true; }
    int count = 0;
  } keys;
  f.ui.setGlobalKeyHandler(&keys);
  f.ui.keyDown(static_cast<events::Key>('T'));
  expect(keys.count == 0, "typing t in the Name field is not the theme shortcut");
  f.key(events::Key::Escape);
  f.ui.clearFocus();
  f.ui.keyDown(static_cast<events::Key>('T'));
  expect(keys.count == 1, "t is the shortcut again with nothing focused");
  f.ui.setGlobalKeyHandler(nullptr);
}

void testLayerTree() {
  ComposedFixture f;
  const auto& ids = f.app->ids();
  TreeView* layers = f.ui.objectAs<TreeView>(ids.layers);
  expect(layers->isSelected(preview::kRectangleNode) && f.app->document().selected, "the rectangle starts selected");
  // Selecting another layer deselects the rectangle on the canvas.
  layers->select(7);
  f.layout();
  expect(!f.app->document().selected, "selecting another layer clears the canvas selection");
  // Drag "Rectangle 2" (last row) above "Frame 1" (first row, top zone): it becomes the first root child.
  const auto last = layers->rowRect(*layers->rowOfNode(7));
  const auto first = layers->rowRect(*layers->rowOfNode(1));
  const double px = last.x + 60;
  f.ui.pointerMove(px, last.y + last.h / 2.0);
  f.ui.pointerDown(px, last.y + last.h / 2.0);
  for (int i = 1; i <= 12; ++i) f.ui.pointerMove(px, last.y + last.h / 2.0 + (first.y + 2.0 - last.y - last.h / 2.0) * i / 12.0);
  f.layout();
  expect(layers->dragging(), "moving past the threshold starts a drag");
  f.ui.pointerUp(px, first.y + 2.0);
  f.layout();
  expect(layers->model()->childAt(r1ui::widgets::kTreeRoot, 0) == 7, "the dropped layer is first in the model");
  // Rename through F2 keeps the model and the document consistent.
  layers->select(preview::kRectangleNode);
  f.ui.focusWidget(ids.layers);
  f.key(static_cast<events::Key>(113));  // F2
  expect(layers->renaming(), "F2 opens the rename field");
  layers->cancelRename();
}

void testOverlays() {
  ComposedFixture f;
  const auto& ids = f.app->ids();
  expect(f.ui.overlays().stack().empty() && !f.ui.msUntilTick().has_value(), "a settled screen schedules nothing (no polling timers)");
  // Menu bar: open File, hover Edit switches, Escape closes everything.
  MenuBar* bar = f.ui.objectAs<MenuBar>(ids.menuBar);
  f.click(bar->itemWidget(0));
  expect(bar->openIndex() == 0 && !f.ui.overlays().stack().empty(), "clicking File opens its menu");
  const auto edit = f.rect(bar->itemWidget(1));
  f.ui.pointerMove(edit.x + 4, edit.y + 4);
  f.layout();
  expect(bar->openIndex() == 1, "hovering Edit while a menu is open switches to it");
  f.key(events::Key::Escape);
  expect(f.ui.overlays().stack().empty() && bar->openIndex() == -1, "Escape closes the menu stack");
  // The fill swatch opens the picker popover; dragging in its square changes the document.
  f.click(ids.fillSwatch);
  expect(f.ui.overlays().stack().size() == 1, "the swatch opens one popover");
  expect(f.ui.msUntilTick().has_value(), "an open popover watches its anchor");
  f.key(events::Key::Escape);
  expect(f.ui.overlays().stack().empty(), "Escape closes the popover");
  expect(!f.ui.msUntilTick().has_value(), "closing it stops the watching");
  f.app->toggleFillPicker();
  f.layout();
  f.app->toggleFillPicker();
  f.layout();
  expect(f.ui.overlays().stack().empty(), "a second toggle closes the popover");
  // Select popup: open, move, choose.
  f.click(ids.blendMode);
  expect(f.ui.overlays().stack().size() == 1, "the blend mode select opens its list");
  f.key(events::Key::Down);
  f.key(events::Key::Enter);
  expect(f.ui.overlays().stack().empty() && f.ui.objectAs<Select>(ids.blendMode)->selectedValue() == "Normal", "choosing a row changes the value and closes the list");
  // Dialog: opens modal with focus inside, Tab stays inside, Escape restores focus to the opener.
  f.ui.focusWidget(ids.variablesButton);
  f.click(ids.variablesButton);
  expect(!f.ui.overlays().stack().empty(), "the Variables dialog opens");
  const WidgetId inside = f.ui.router().focused();
  expect(inside.valid() && f.ui.overlays().indexContaining(inside) >= 0, "focus starts inside the dialog");
  for (int i = 0; i < 12; ++i) {
    f.key(events::Key::Tab);
    expect(f.ui.overlays().indexContaining(f.ui.router().focused()) >= 0, "Tab never leaves the dialog");
  }
  // A focused number field takes the first Escape to leave its edit mode, as it should.
  for (int i = 0; i < 3 && !f.ui.overlays().stack().empty(); ++i) f.key(events::Key::Escape);
  expect(f.ui.overlays().stack().empty(), "Escape closes the dialog");
  expect(f.ui.router().focused() == ids.variablesButton, "focus returns to the button that opened it");
  // Context menu on the canvas.
  f.click(f.rect(ids.canvas).x + 100, f.rect(ids.canvas).y + 300, events::Button::Right);
  expect(!f.ui.overlays().stack().empty(), "right click on the canvas opens the context menu");
  f.key(events::Key::Escape);
  expect(f.ui.overlays().stack().empty(), "Escape closes the context menu");
  // Toast: shows and expires on the context clock.
  f.app->toast("Warm up");
  f.ui.setTime(f.ui.now() + 6000);
  f.ui.tick();
  f.ui.setTime(f.ui.now() + 1000);
  f.ui.tick();
  f.layout();
  const size_t before = f.ui.widgetCount();
  f.app->toast("Hello");
  f.layout();
  expect(f.ui.widgetCount() > before && f.ui.msUntilTick().has_value(), "a toast appears and schedules its lifetime");
  f.ui.setTime(f.ui.now() + 5000);
  f.ui.tick();
  f.ui.setTime(f.ui.now() + 1000);
  f.ui.tick();
  f.layout();
  expect(f.ui.widgetCount() == before, "the toast is gone after its lifetime");
}

void testIdleAfterFocus() {
  // Regression guard for the shell's idle rule: a settled screen asks for no frames, a focused field
  // keeps the caret blinking, and once the focus is gone the frames stop again.
  ComposedFixture f;
  f.ui.setAnimationsEnabled(true);
  f.ui.setFrameLoopRunning(true);
  uint64_t now = 1000;
  f.frameAt(now);
  f.settle(now);
  expect(!f.ui.needsFrame(), "a settled composed screen asks for no frames");
  f.ui.focusWidget(f.app->ids().nameInput);
  f.settle(now);
  expect(f.ui.needsFrame(), "a focused field keeps frames coming (caret blink)");
  f.ui.clearFocus();
  const int frames = f.settle(now);
  expect(frames < 40 && !f.ui.needsFrame(), "the frames stop once the focus is gone");
  // The same after the field was used with the pointer and a menu was open.
  f.click(f.app->ids().x);
  f.key(events::Key::Escape);
  f.ui.clearFocus();
  f.settle(now);
  expect(!f.ui.needsFrame(), "the frames stop after editing a number field");
}

void testThemeTool() {
  ComposedFixture f;
  const auto& ids = f.app->ids();
  Toolbar* bar = f.ui.objectAs<Toolbar>(ids.themeToolbar);
  expect(bar->activeTool() == "dark", "the toggle starts on the current theme");
  f.click(bar->button(0)->id());
  expect(f.darkRequests.size() == 1 && !f.darkRequests[0] && f.services.theme().id() == r1ui::theme::ThemeId::Light, "the sun tool asks the host for light");
  f.app->syncTheme();
  expect(bar->activeTool() == "light", "the toggle follows the theme");
  f.layout();
  expect(!f.ui.needsFrame(), "nothing is pending after the switch was drawn");
}

void testStorm() {
  ComposedFixture f;
  std::mt19937 rng(11);
  for (int i = 0; i < 6000; ++i) {
    const double x = static_cast<double>(rng() % 1500);
    const double y = static_cast<double>(rng() % 950);
    switch (rng() % 8) {
      case 0: f.ui.pointerMove(x, y); break;
      case 1: f.ui.pointerDown(x, y, rng() % 5 == 0 ? events::Button::Right : events::Button::Left); break;
      case 2: f.ui.pointerUp(x, y); break;
      case 3: f.ui.wheel(x, y, 0, static_cast<double>(static_cast<int>(rng() % 7) - 3)); break;
      case 4: f.ui.keyDown(static_cast<events::Key>(rng() % 130), static_cast<uint8_t>(rng() % 8)); break;
      case 5: f.ui.textInput(static_cast<char32_t>(32 + rng() % 200)); break;
      case 6: f.ui.keyUp(static_cast<events::Key>(rng() % 130)); break;
      default: f.ui.setTime(f.ui.now() + rng() % 700); f.ui.tick(); break;
    }
    if (i % 25 == 0) f.layout();
  }
  f.ui.overlays().closeAll();
  f.layout();
  expect(f.ui.widgetCount() > 120, "the screen survived the input storm");
}

}  // namespace

int main() {
  try {
    testGalleryPages();
    testComposedGeometry();
    testFieldsEditTheDocument();
    testLayerTree();
    testOverlays();
    testIdleAfterFocus();
    testThemeTool();
    testStorm();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "exception: %s\n", e.what());
    return 2;
  }
  if (failures == 0) std::printf("modes_test: ok\n");
  return failures == 0 ? 0 : 1;
}

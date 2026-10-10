// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: integration oracle for the preview's Editor screen (EditorApp) headless, over the in-window
//   backend and in-memory textures, with a throwaway data folder: every panel factory builds (also in a
//   second context, as a native window would), every menu/toolbar command exists and no default chord
//   was lost to a conflict, the first run seeds the three named layouts, edits of the inspector are undone
//   and redone by commands and by keys, the arrangement and the active layout name survive a restart, a
//   damaged layout file is kept aside and the default used, a rebound key and a customized menu survive a
//   restart (and a damaged keybindings file is ignored), the layout dialogs work from the keyboard, the
//   custom menus (sample menus on the first run, creating a pie and a dockable menu through the creator
//   window, the Custom Menus menu, reopening a closed panel, the right-mouse pie in the viewport and its
//   Escape, saving a .r1mn and loading it after a delete, persistence across a restart, a damaged menus
//   file), custom workspaces (save, change, load, and a bad file changes nothing), the hotkey editor panel,
//   every command having a description, and an idle screen scheduling nothing.
// Why: these behaviours span seven modules wired together for the first time in slice 5.12 and extended
//   in slice 5.19.
// Callers: CTest (label fast).
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <windows.h>

#include "ComposedUtil.h"
#include "Scene.h"
#include "editor/EditorApp.h"
#include "editor/EditorMenus.h"
#include "r1ui/commands/custommenu/CustomMenuIo.h"
#include "r1ui/commands/workspace/Workspace.h"
#include "r1ui/dock/LayoutStore.h"
#include "r1ui/widgets/actions/ActionList.h"
#include "r1ui/widgets/custommenu/CustomMenuPanel.h"
#include "r1ui/widgets/custommenu/creator/CreateCustomMenuWindow.h"
#include "r1ui/widgets/dock/InWindowFloatingBackend.h"
#include "r1ui/widgets/hotkeys/HotkeyEditor.h"
#include "r1ui/widgets/pie/PieTrigger.h"
#include "r1ui/widgets/text/TextureFactory.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace {

using namespace r1ui::widgets;
namespace ed = preview::editor;
namespace rc = r1ui::commands;
namespace fs = std::filesystem;
namespace events = r1ui::core::events;
using r1ui::core::tree::WidgetId;

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

std::shared_ptr<const r1ui::theme::Tokens> loadTokens() {
  auto parsed = r1ui::theme::Tokens::loadFile(std::string(R1UI_ASSETS_DIR) + "/theme/tokens.json", {.requireAllSections = true});
  if (!parsed.ok()) throw std::runtime_error("tokens: " + parsed.error);
  return std::make_shared<const r1ui::theme::Tokens>(std::move(*parsed.tokens));
}

// A context with the title bar's height reserved (like the app) and an app that can be restarted over the
// same data folder.
struct Rig {
  explicit Rig(fs::path dataRoot)
      : root(std::move(dataRoot)),
        services(loadTokens(), textures, {std::string(R1UI_ASSETS_DIR) + "/fonts", {std::string(R1UI_ASSETS_DIR) + "/icons/lucide", std::string(R1UI_ASSETS_DIR) + "/icons/custom"}}),
        ui(services) {
    ui.setAnimationsEnabled(false);
    ui.rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
    ui.rootStyle().padding[r1ui::core::layout::kTop] = preview::kTitleBarHeight;
    SectionBox& area = preview::build::flex(ui, ui.root(), true);
    preview::build::grow(area.style());
    content = area.id();
    ui.setViewport(1440, 900, 1.0f);
    start();
  }
  ~Rig() { stop(); }

  void start() {
    backend = std::make_unique<InWindowFloatingBackend>(ui, ui.root());
    ed::EditorHost host;
    host.dataRoot = root;
    host.setDarkTheme = [this](bool dark) { services.theme().set(dark ? r1ui::theme::ThemeId::Dark : r1ui::theme::ThemeId::Light); };
    host.isDark = [this] { return services.theme().id() == r1ui::theme::ThemeId::Dark; };
    host.quit = [this] { quit = true; };
    host.showScreen = [this](int screen) { lastScreen = screen; };
    host.screenNames = [] { return std::vector<std::string>{"Editor", "Gallery", "Widgets"}; };
    app = std::make_unique<ed::EditorApp>(ui, content, *backend, std::move(host));
    settle();
  }
  void stop() {
    app.reset();
    backend.reset();
  }
  void restart() {
    stop();
    start();
  }
  void settle() {
    for (int i = 0; i < 4; ++i) {
      ui.setTime(ui.now() + 50);
      app->update();
      ui.tick();
      ui.frame();
    }
  }
  void key(events::Key k, uint8_t mods = 0) {
    ui.setTime(ui.now() + 50);
    ui.keyDown(k, mods);
    ui.keyUp(k, mods);
    settle();
  }

  fs::path root;
  NullTextureFactory textures;
  Services services;
  UiContext ui;
  WidgetId content;
  std::unique_ptr<InWindowFloatingBackend> backend;
  std::unique_ptr<ed::EditorApp> app;
  bool quit = false;
  int lastScreen = -1;
};

events::Key letter(char c) { return static_cast<events::Key>(c); }

void writeFile(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

void collectCommands(const r1ui::commands::customize::Node& node, std::vector<std::string>& out) {
  if (!node.commandId.empty()) out.push_back(node.commandId);
  for (const auto& child : node.children) collectCommands(child, out);
}

// ---- tests ----------------------------------------------------------------------------------

void testBuildsAndSeeds(Rig& r) {
  ed::EditorApp& app = *r.app;
  expect(app.started(), "the layouts started");
  expect(app.panels().size() == ed::panel::kStandardCount + 2, "the standard panels, the creator and the sample Quick Tools panel are registered");
  const auto layouts = app.layouts().list();
  expect(layouts.size() == 3, "the first run seeds three named layouts");
  bool names = false;
  for (const auto& item : layouts) names = names || item.displayName == "Default";
  expect(names && app.layoutName() == "Default", "Default is seeded and active");
  expect(app.dock().layout().isDocked(ed::panel::kViewport) && app.dock().layout().isDocked(ed::panel::kInspector), "viewport and inspector are docked");
  expect(!app.dock().layout().isDocked(ed::panel::kShortcuts), "the shortcut editor starts closed");
  expect(r.ui.inputFaults() == 0, "no input fault");

  // Every command of the built-in menus and toolbar exists; no default chord lost to a conflict.
  std::vector<std::string> used;
  for (const auto& menu : app.customization().effective().layout.menuBar.menus) collectCommands(menu, used);
  for (const auto& bar : app.customization().effective().layout.toolbars) {
    for (const auto& item : bar.items) collectCommands(item, used);
  }
  size_t missing = 0;
  for (const std::string& id : used) missing += app.registry().find(id) == nullptr ? 1 : 0;
  expect(!used.empty() && missing == 0, "every menu and toolbar entry is a registered command");
  size_t lost = 0;
  for (const rc::CommandDef* def : app.registry().commands()) {
    for (int slot = 0; slot < 2; ++slot) {
      const rc::ChordSequence& wanted = def->defaultChords[static_cast<size_t>(slot)];
      if (wanted.count == 0) continue;
      const auto effective = app.services().keymap.effective(def->id, slot);
      if (!effective || !(*effective == wanted)) {
        std::fprintf(stderr, "  chord of %s slot %d is not in force\n", def->id.c_str(), slot);
        ++lost;
      }
    }
  }
  expect(lost == 0, "every default chord is in force (no conflicts between the Editor's commands)");
}

void testPanelsInAnotherContext(Rig& r) {
  // A native window recreates a panel from its factory in its own context: build all nine there.
  UiContext other(r.services);
  other.setViewport(900, 700, 1.0f);
  std::vector<WidgetId> made;
  for (const PanelDescriptor& d : r.app->panels().all()) made.push_back(d.factory(other, other.root()));
  other.frame();
  expect(made.size() == r.app->panels().size() && other.widgetCount() > 100, "all panels build in a second context");
  // A key in that context reaches the command router (undo is disabled here, tool keys are not).
  other.keyDown(letter('W'));
  expect(r.app->model().tool == "tool.move", "a shortcut typed in a native window's context runs the command");
  r.app->model().tool = "tool.select";
  for (const WidgetId id : made) other.destroy(id);
}

void testUndoThroughCommands(Rig& r) {
  ed::EditorModel& model = r.app->model();
  auto& ctx = model.context();
  const auto row = ctx.findRow("position");
  expect(row.has_value() && model.selection().size() == 1, "the cube is selected and has a position row");
  const double before = model.mesh(0).position.x;
  ctx.setComponent(*row, 0, before + 3.0);
  r.settle();
  expect(model.mesh(0).position.x == before + 3.0, "the inspector edit applied");
  expect(r.app->registry().find(ed::cmd::kUndo)->isEnabled(), "undo became enabled");
  r.key(letter('Z'), events::Mod::kCtrl);
  expect(model.mesh(0).position.x == before, "Ctrl+Z undid the edit");
  r.key(letter('Y'), events::Mod::kCtrl);
  expect(model.mesh(0).position.x == before + 3.0, "Ctrl+Y redid it");
  expect(r.app->run(ed::cmd::kUndo) && model.mesh(0).position.x == before, "the Undo command undid it");

  // Mixed values: select two meshes with different colours.
  model.select({1, 2});
  const auto color = ctx.findRow("color");
  expect(color && ctx.state(*color).mixed, "two objects with different colours show a mixed value");
  model.select({1});
  r.key(letter('W'));
  expect(model.tool == "tool.move", "W selects the move tool");
}

void testViewportDrag(Rig& r) {
  ed::EditorModel& model = r.app->model();
  model.select({1});
  r.app->run(ed::cmd::kToolMove);
  r.settle();
  const WidgetId view = r.app->dock().contentOf(ed::panel::kViewport);
  const auto box = r.ui.absRect(view);
  expect(box.w > 100 && box.h > 100, "the viewport has a size");
  const double x = box.x + box.w / 2.0 - 80.0, y = box.y + box.h / 2.0 - 40.0;  // the cube sits at world (-4, 2), 20 px per unit
  const double startX = model.mesh(0).position.x, startY = model.mesh(0).position.y;
  const size_t undoSteps = model.context().undo().undoCount();
  r.ui.setTime(r.ui.now() + 50);
  r.ui.pointerMove(x, y);
  r.ui.pointerDown(x, y);
  for (int i = 1; i <= 10; ++i) r.ui.pointerMove(x + 10.0 * i, y + 6.0 * i);
  r.ui.pointerUp(x + 100.0, y + 60.0);
  r.settle();
  expect(std::abs(model.mesh(0).position.x - (startX + 5.0)) < 0.01 && std::abs(model.mesh(0).position.y - (startY - 3.0)) < 0.01, "dragging the cube moved it by the pointer distance");
  expect(model.context().undo().undoCount() == undoSteps + 1, "the whole drag is one undo step");
  r.app->run(ed::cmd::kUndo);
  expect(model.mesh(0).position.x == startX && model.mesh(0).position.y == startY, "undo restores the position");
  r.app->run(ed::cmd::kToolSelect);
}

void testLayoutPersistence(Rig& r) {
  expect(r.app->run(ed::cmd::kLayoutModeling), "the Modeling command ran");
  r.settle();
  expect(r.app->layoutName() == "Modeling", "Modeling is the active layout");
  expect(r.app->dock().layout().isDocked(ed::panel::kCurves), "Modeling shows the curves");
  expect(r.app->dock().closePanel(ed::panel::kCurves), "the curves panel closed");
  r.settle();
  const std::string arrangement = r.app->dock().layout().toJson();
  r.restart();
  expect(r.app->layoutName() == "Modeling", "the active layout name survived the restart");
  expect(!r.app->dock().layout().isDocked(ed::panel::kCurves) && r.app->dock().layout().isDocked(ed::panel::kViewport), "the arrangement survived the restart");
  expect(r.app->dock().layout().toJson() == arrangement, "the restored arrangement is the one that was saved");
  expect(r.app->layouts().list().size() == 3, "no layout was seeded twice");
  r.app->run(ed::cmd::kLayoutDefault);
  expect(r.app->layoutName() == "Default", "switching back works");
}

void testDamagedLayout(Rig& r) {
  r.stop();
  writeFile(r.root / "layouts" / "editor" / "_active.layout.json", "{ this is not a layout");
  r.start();
  expect(r.app->started() && r.app->dock().layout().isDocked(ed::panel::kViewport), "a damaged active layout falls back to the default arrangement");
  bool kept = false;
  for (const auto& entry : fs::directory_iterator(r.root / "layouts" / "editor")) kept = kept || entry.path().filename().string().find("_corrupt") == 0;
  expect(kept, "the damaged file was kept aside, not deleted");
  expect(r.app->layoutName().empty(), "the arrangement is no longer a named layout");
}

void testKeybindingsPersist(Rig& r) {
  rc::KeybindingOverrides& overrides = r.app->overrides();
  expect(overrides.set(ed::cmd::kToolMove, 0, rc::ChordSequence::single({letter('M'), 0, false})) == rc::SetError::None, "rebind Move to M");
  r.app->tick();
  expect(fs::exists(r.root / "keybindings.json"), "keybindings.json was written");
  r.key(letter('M'));
  expect(r.app->model().tool == "tool.move", "the new key runs the command");
  r.app->model().tool = "tool.select";
  r.restart();
  r.key(letter('M'));
  expect(r.app->model().tool == "tool.move", "the rebinding survived the restart");
  r.stop();
  writeFile(r.root / "keybindings.json", "not json at all");
  r.start();
  r.key(letter('W'));
  expect(r.app->model().tool == "tool.move", "a damaged keybindings file is ignored and the defaults apply");
}

template <class T>
T* findWidget(UiContext& ui) {
  T* found = nullptr;
  ui.tree().forEachDescendant(ui.root(), [&](WidgetId id) {
    if (found == nullptr) found = ui.objectAs<T>(id);
  }, true);
  return found;
}

std::vector<std::string> menuTitles(ed::EditorApp& app) {
  std::vector<std::string> titles;
  for (const auto& menu : app.customization().effective().layout.menuBar.menus) titles.push_back(menu.label);
  return titles;
}

bool hasMenuNamed(ed::EditorApp& app, const std::string& name) {
  for (const auto& menu : app.menuSet().menus()) {
    if (menu.name == name) return true;
  }
  return false;
}

const r1ui::commands::custommenu::CustomMenu* menuNamed(ed::EditorApp& app, const std::string& name) {
  return app.menuSet().findByName(name);
}

void testSampleMenusAndMenu(Rig& r) {
  ed::EditorApp& app = *r.app;
  expect(hasMenuNamed(app, "Tools Pie") && hasMenuNamed(app, "Quick Tools") && app.menuSet().size() == 2, "the first run creates the Tools Pie and the Quick Tools panel");
  const auto* pie = menuNamed(app, "Tools Pie");
  expect(pie != nullptr && pie->kind == r1ui::commands::custommenu::MenuKind::Pie && pie->slotCount == 8 && pie->entries[0].commandId == ed::cmd::kToolMove, "the Tools Pie holds Move at the top");
  expect(app.viewportPieId() == pie->id, "the Tools Pie is the viewport's pie");
  const auto* quick = menuNamed(app, "Quick Tools");
  const r1ui::dock::PanelId quickPanel = app.panelForMenu(quick->id);
  expect(quickPanel != 0 && app.dock().layout().isDocked(quickPanel), "the sample dockable menu is open as a dock panel on the first run");
  r.settle();
  const auto titles = menuTitles(app);
  expect(!titles.empty() && titles.back() == "Custom Menus" && titles.size() == 8, "Custom Menus is the last of the eight top-level menus");
  expect(app.registry().find("custommenu.open." + quick->id) != nullptr && app.registry().find("custommenu.create") != nullptr, "the commands behind the Custom Menus menu exist");
  expect(r1ui::widgets::commandsWithoutDescription(app.registry()).empty(), "every command has a description");
}

// A pie created through the creator window, a dockable menu with tool commands, the menu entries and the
// panel that can be closed and opened again.
void testCreateMenusThroughCreator(Rig& r) {
  ed::EditorApp& app = *r.app;
  namespace cm = r1ui::commands::custommenu;
  expect(app.run("custommenu.create"), "Custom Menus > Create Custom Menu runs");
  r.settle();
  r.settle();
  expect(app.dock().layout().isDocked(ed::panel::kCreator), "the creator window is open");
  const auto slot = app.dock().layout().locate(ed::panel::kCreator);
  expect(slot && slot->area != r1ui::dock::kMainAreaId, "the creator window floats in a window of its own");
  CreateCustomMenuWindow* window = findWidget<CreateCustomMenuWindow>(r.ui);
  expect(window != nullptr && window->showingChooser(), "the creator first shows the type chooser");
  if (window == nullptr) return;
  window->chooseType(cm::MenuKind::Pie);
  r.settle();
  expect(!window->showingChooser() && window->pieEditor() != nullptr, "choosing Pie shows the pie preview");
  expect(window->addAction(ed::cmd::kToolScale) && window->addAction(ed::cmd::kFrame) && window->addAction(ed::cmd::kWireframe), "three actions were added to slots");
  expect(!window->addAction("no.such.command"), "an unknown action is refused");
  app.creator().draft()->setName("Mine");
  expect(window->issue().empty(), "the draft is valid");
  expect(window->create(), "Create commits");
  r.settle();
  r.settle();
  const auto* mine = menuNamed(app, "Mine");
  expect(mine != nullptr && mine->kind == cm::MenuKind::Pie && mine->entries[0].commandId == ed::cmd::kToolScale && app.viewportPieId() == mine->id, "the pie exists and became the viewport pie");
  expect(!app.dock().layout().isDocked(ed::panel::kCreator), "the creator window closed after Create");
  expect(app.registry().find("custommenu.edit." + mine->id) != nullptr, "the new pie has its Edit command");
  expect(menuTitles(app).back() == "Custom Menus", "Custom Menus stays the last menu");

  // A dockable menu with Move and Rotate.
  expect(app.run("custommenu.create"), "create again");
  r.settle();
  r.settle();
  window = findWidget<CreateCustomMenuWindow>(r.ui);
  expect(window != nullptr && window->showingChooser(), "a fresh chooser");
  if (window == nullptr) return;
  window->chooseType(cm::MenuKind::Panel);
  r.settle();
  expect(window->panelEditor() != nullptr, "the panel preview is shown");
  expect(window->addAction(ed::cmd::kToolMove) && window->addAction(ed::cmd::kToolRotate), "Move and Rotate were added");
  app.creator().draft()->setName("Move and Rotate");
  expect(window->create(), "Create commits the dockable menu");
  r.settle();
  r.settle();
  const auto* dock = menuNamed(app, "Move and Rotate");
  expect(dock != nullptr && dock->kind == cm::MenuKind::Panel && dock->entries.size() == 2, "the dockable menu exists");
  const r1ui::dock::PanelId panelId = dock != nullptr ? app.panelForMenu(dock->id) : 0;
  expect(panelId != 0 && app.dock().layout().isDocked(panelId), "its panel opened in the dock");
  r.settle();
  CustomMenuPanel* shown = nullptr;
  r.ui.tree().forEachDescendant(r.ui.root(), [&](WidgetId id) {
    if (CustomMenuPanel* p = r.ui.objectAs<CustomMenuPanel>(id); p != nullptr && dock != nullptr && p->menuId() == dock->id) shown = p;
  }, true);
  expect(shown != nullptr && shown->buttonCount() == 2, "the panel shows two buttons");
  if (shown != nullptr) {
    const auto b = r.ui.absRect(shown->button(1)->id());
    r.ui.pointerMove(b.x + b.w / 2.0, b.y + b.h / 2.0);
    r.ui.pointerDown(b.x + b.w / 2.0, b.y + b.h / 2.0);
    r.ui.pointerUp(b.x + b.w / 2.0, b.y + b.h / 2.0);
    r.settle();
    expect(app.model().tool == ed::cmd::kToolRotate, "a click on the Rotate button of the panel runs the Rotate tool");
    app.model().tool = ed::cmd::kToolSelect;
  }

  // Close the panel and open it again from the menu command.
  expect(app.dock().closePanel(panelId) && !app.dock().layout().isDocked(panelId), "the panel closes");
  expect(app.run("custommenu.open." + dock->id) && app.dock().layout().isDocked(panelId), "Custom Menus lists it and opens it again");
  // A name that is taken is refused by the window.
  app.run("custommenu.create");
  r.settle();
  r.settle();
  window = findWidget<CreateCustomMenuWindow>(r.ui);
  if (window != nullptr) {
    window->chooseType(cm::MenuKind::Pie);
    r.settle();
    window->addAction(ed::cmd::kToolMove);
    app.creator().draft()->setName("mine");
    expect(!window->issue().empty() && !window->create() && app.menuSet().size() == 4, "a duplicate name (any case) cannot be created");
    window->cancel();
    r.settle();
    r.settle();
    expect(!app.dock().layout().isDocked(ed::panel::kCreator) && app.menuSet().size() == 4, "Cancel closes the window and creates nothing");
  }
}

// Hold the right mouse button in the viewport and flick toward the top slot (Move); Escape cancels.
void testPieInViewport(Rig& r) {
  ed::EditorApp& app = *r.app;
  app.model().tool = ed::cmd::kToolSelect;
  app.menuSet();  // the Tools Pie (viewport pie) was replaced by "Mine" in the previous test: Move is slot 3? use whichever pie is current
  const auto pie = app.menuSet().find(app.viewportPieId());
  expect(pie != nullptr, "there is a viewport pie");
  if (pie == nullptr) return;
  // Slot 0 of "Mine" is Scale; flick up.
  const std::string top = pie->entries[0].commandId;
  const WidgetId view = app.dock().contentOf(ed::panel::kViewport);
  const auto box = r.ui.absRect(view);
  const double cx = box.x + box.w / 2.0 + 150.0;
  const double cy = box.y + box.h / 2.0 + 90.0;
  r.ui.setTime(r.ui.now() + 100);
  r.ui.pointerMove(cx, cy);
  r.ui.pointerDown(cx, cy, events::Button::Right);
  r.ui.setTime(r.ui.now() + 300);
  r.ui.tick();
  r.ui.frame();
  PieTrigger* trigger = findWidget<PieTrigger>(r.ui);
  expect(trigger != nullptr && trigger->gestureActive() && trigger->pieDrawn(), "holding the right button draws the pie");
  r.ui.pointerMove(cx, cy - 70.0);
  expect(trigger != nullptr && trigger->highlighted() == 0, "moving up highlights slot 0");
  r.ui.pointerUp(cx, cy - 70.0, events::Button::Right);
  r.settle();
  expect(app.model().tool == top && trigger != nullptr && !trigger->gestureActive(), "releasing over the slot ran its command");

  // Escape ends the gesture without running anything.
  app.model().tool = ed::cmd::kToolSelect;
  r.ui.setTime(r.ui.now() + 100);
  r.ui.pointerMove(cx, cy);
  r.ui.pointerDown(cx, cy, events::Button::Right);
  r.ui.pointerMove(cx + 70.0, cy);
  expect(trigger != nullptr && trigger->gestureActive(), "the gesture is running");
  r.key(events::Key::Escape);
  expect(trigger != nullptr && !trigger->gestureActive() && !trigger->pieDrawn(), "Escape cancelled the pie before it was drawn");
  r.ui.pointerUp(cx + 70.0, cy, events::Button::Right);
  r.settle();
  expect(app.model().tool == ed::cmd::kToolSelect, "the cancelled gesture ran nothing");
}

void testMenuFilesAndRestart(Rig& r) {
  namespace cm = r1ui::commands::custommenu;
  ed::EditorApp& app = *r.app;
  const auto* dock = menuNamed(app, "Move and Rotate");
  expect(dock != nullptr, "the dockable menu is there");
  if (dock == nullptr) return;
  const std::string oldId = dock->id;
  const fs::path file = r.root / "menus" / "mr.r1mn";
  expect(app.saveMenuTo(oldId, file) && fs::exists(file), "the menu was saved to a .r1mn file");
  expect(app.menuSet().deleteMenu(oldId).ok && !hasMenuNamed(app, "Move and Rotate"), "the menu is deleted");
  r.settle();
  expect(app.registry().find("custommenu.open." + oldId) == nullptr, "its commands are gone");
  expect(app.loadMenuFrom(file) && hasMenuNamed(app, "Move and Rotate"), "loading the file brings it back");
  r.settle();
  r.settle();
  const auto* back = menuNamed(app, "Move and Rotate");
  expect(back != nullptr && back->id != oldId && back->entries.size() == 2 && back->entries[0].commandId == ed::cmd::kToolMove, "with its content (and a fresh id)");
  expect(back != nullptr && app.panelForMenu(back->id) != 0 && app.dock().layout().isDocked(app.panelForMenu(back->id)), "its panel is open");
  expect(app.loadMenuFrom(file) && hasMenuNamed(app, "Move and Rotate (2)"), "loading it again adds a numbered copy");
  writeFile(r.root / "menus" / "bad.r1mn", "{ not a menu");
  const size_t before = app.menuSet().size();
  expect(!app.loadMenuFrom(r.root / "menus" / "bad.r1mn") && app.menuSet().size() == before, "a damaged menu file is refused and changes nothing");

  // Restart: the menus, their panels and the viewport pie come back.
  const auto* sample = menuNamed(app, "Quick Tools");
  expect(sample != nullptr && app.openMenuPanel(sample->id), "a dockable menu is open when the app exits");
  const std::string viewportPie = app.viewportPieId();
  const size_t count = app.menuSet().size();
  r.restart();
  ed::EditorApp& restarted = *r.app;  // the old object is gone: never touch `app` below this line
  expect(restarted.menuSet().size() == count && hasMenuNamed(restarted, "Mine") && hasMenuNamed(restarted, "Tools Pie") && hasMenuNamed(restarted, "Move and Rotate (2)"), "all custom menus survived the restart");
  expect(restarted.viewportPieId() == viewportPie, "the viewport pie survived the restart");
  const auto* quick = menuNamed(restarted, "Quick Tools");
  expect(quick != nullptr && restarted.dock().layout().isDocked(restarted.panelForMenu(quick->id)), "the sample panel is open again: it was open at exit");
  expect(menuTitles(restarted).back() == "Custom Menus", "the Custom Menus menu is there after the restart");
  expect(restarted.menuSet().menus().size() == count && restarted.registry().find("custommenu.edit." + restarted.menuSet().find(viewportPie)->id) != nullptr, "the menu commands were registered again");
}

void testWorkspaces(Rig& r) {
  namespace cm = r1ui::commands::custommenu;
  ed::EditorApp& app = *r.app;
  const fs::path file = r.root / "workspaces" / "mine.r1ws";
  const size_t menus = app.menuSet().size();
  expect(app.run(ed::cmd::kLayoutReview), "switch to Review");
  r.settle();
  const std::string arrangement = app.dock().layout().toJson();
  app.overrides().set(ed::cmd::kToolScale, 0, rc::ChordSequence::single({letter('J'), 0, false}));
  expect(app.saveWorkspaceTo(file) && fs::exists(file), "the workspace was saved");

  // Change everything it covers.
  app.run(ed::cmd::kLayoutModeling);
  r.settle();
  const std::string other = app.dock().layout().toJson();
  expect(other != arrangement, "the layout changed");
  app.overrides().resetAll();
  const std::string extra = app.menuSet().createMenu(cm::MenuKind::Pie, "Temporary").id;
  expect(!extra.empty() && app.menuSet().size() == menus + 1, "a menu was added");
  expect(app.loadWorkspaceFrom(file), "the workspace loads");
  r.settle();
  expect(app.menuSet().size() == menus && !hasMenuNamed(app, "Temporary"), "the workspace's menus replaced the current ones");
  expect(app.dock().layout().toJson() == arrangement, "the workspace's layout is back");
  const auto chord = app.services().keymap.effective(ed::cmd::kToolScale, 0);
  expect(chord && chord->count == 1 && chord->chords[0].key == letter('J'), "the workspace's key binding is back");
  app.overrides().resetAll();

  // A bad file changes nothing.
  const std::string layoutBefore = app.dock().layout().toJson();
  writeFile(r.root / "workspaces" / "bad.r1ws", "{ nope");
  expect(!app.loadWorkspaceFrom(r.root / "workspaces" / "bad.r1ws"), "a damaged workspace is refused");
  rc::KeybindingOverrides& overrides = app.overrides();
  (void)overrides;
  r1ui::commands::workspace::Workspace broken;
  broken.name = "Broken";
  broken.menus = "{\"format\":\"r1ui-custom-menus\",\"version\":1,\"serial\":1,\"menus\":[{\"nope\":true}]}";
  broken.layout = app.dock().layout().toJson();
  std::string error;
  expect(r1ui::commands::workspace::saveWorkspaceFile(broken, r.root / "workspaces" / "broken.r1ws", error), "a workspace with unusable menus was written");
  const size_t menusBefore = app.menuSet().size();
  expect(!app.loadWorkspaceFrom(r.root / "workspaces" / "broken.r1ws") && app.menuSet().size() == menusBefore && app.dock().layout().toJson() == layoutBefore, "unusable menus in a workspace change nothing");
  r1ui::commands::workspace::Workspace badLayout;
  badLayout.name = "Bad layout";
  badLayout.menus = r1ui::commands::custommenu::exportSet(app.menuSet());
  badLayout.layout = "{\"version\":2,\"main\":{\"root\":null}}";
  expect(r1ui::commands::workspace::saveWorkspaceFile(badLayout, r.root / "workspaces" / "badlayout.r1ws", error), "a workspace with an unusable layout was written");
  expect(!app.loadWorkspaceFrom(r.root / "workspaces" / "badlayout.r1ws") && app.menuSet().size() == menusBefore && app.dock().layout().toJson() == layoutBefore, "an unusable layout puts the menus back");
}

void testHotkeyEditorPanel(Rig& r) {
  ed::EditorApp& app = *r.app;
  expect(app.run("edit.shortcuts"), "Edit > Hotkey editor runs");
  r.settle();
  r.settle();
  expect(app.dock().layout().isDocked(ed::panel::kShortcuts), "the hotkey editor panel is open");
  HotkeyEditor* editor = findWidget<HotkeyEditor>(r.ui);
  expect(editor != nullptr && editor->list().view().actions().size() > 30, "the hotkey editor lists the Editor's actions");
  if (editor == nullptr) return;
  expect(editor->selectAction(ed::cmd::kToolMove), "an action can be selected");
  expect(editor->assign(ed::cmd::kToolMove, 0, rc::ChordSequence::single({letter('M'), 0, false})) == AssignOutcome::Assigned, "assigning M to Move works");
  expect(editor->saveSet("Mine"), "a hotkey set can be saved");
  r.settle();
  expect(fs::exists(r.root / "hotkey-sets" / "Mine.json"), "the hotkey set was written to the data folder");
  r.app->tick();
  expect(fs::exists(r.root / "keybindings.json"), "keybindings.json holds the change");
  app.overrides().resetAll();
  app.dock().closePanel(ed::panel::kShortcuts);
}

WidgetId findTextInput(UiContext& ui) {
  WidgetId found;
  ui.tree().forEachDescendant(ui.root(), [&](WidgetId id) {
    if (!found.valid() && ui.objectAs<TextInput>(id) != nullptr) found = id;
  }, true);
  return found;
}

void testLayoutDialogs(Rig& r) {
  r.app->run(ed::cmd::kLayoutSaveAs);
  r.settle();
  expect(!r.ui.overlays().stack().empty(), "Save layout as opens a dialog");
  const WidgetId input = findTextInput(r.ui);
  expect(input.valid() && r.ui.router().focused() == input, "the name field has focus");
  r.key(letter('A'), events::Mod::kCtrl);
  for (const char c : std::string("Sculpt")) r.ui.textInput(static_cast<char32_t>(c));
  r.settle();
  r.key(events::Key::Enter);
  bool found = false;
  for (const auto& item : r.app->layouts().list()) found = found || item.displayName == "Sculpt";
  expect(found && r.app->layoutName() == "Sculpt", "Enter in the dialog saved the layout under the typed name and made it active");

  r.app->run(ed::cmd::kLayoutRename);
  r.settle();
  r.key(letter('A'), events::Mod::kCtrl);
  for (const char c : std::string("Carve")) r.ui.textInput(static_cast<char32_t>(c));
  r.key(events::Key::Enter);
  expect(r.app->layoutName() == "Carve", "rename works");

  r.app->run(ed::cmd::kLayoutDelete);
  r.settle();
  expect(!r.ui.overlays().stack().empty(), "delete asks first");
  r.key(events::Key::Escape);
  expect(r.app->layoutName() == "Carve", "Escape cancelled the delete");
  r.app->run(ed::cmd::kLayoutDelete);
  r.settle();
  // The confirm dialog's default action is Cancel (Enter): Delete is reached by Tab then Enter.
  r.key(events::Key::Tab);
  r.key(events::Key::Enter);
  bool stillThere = false;
  for (const auto& item : r.app->layouts().list()) stillThere = stillThere || item.displayName == "Carve";
  expect(!stillThere, "the layout was deleted after confirming");
}

// A damaged custom-menus.json is kept aside and the app starts without user menus (the samples are not
// recreated: they belong to the very first start only).
void testDamagedMenusFile(Rig& r) {
  r.stop();
  writeFile(r.root / "custom-menus.json", "{ this is not a menu set");
  r.start();
  bool kept = false;
  for (const auto& entry : fs::directory_iterator(r.root)) kept = kept || entry.path().filename().string().find("custom-menus.json.corrupt") == 0;
  expect(kept, "the damaged menus file was kept aside, not deleted");
  expect(r.app->menuSet().size() == 0 && menuTitles(*r.app).back() == "Custom Menus", "the app started with no user menus and the Custom Menus menu");
  expect(r.app->viewportPieId().empty(), "no viewport pie without a pie menu");
}

void testIdle(Rig& r) {
  r.settle();
  r.settle();
  expect(!r.ui.needsFrame(), "an idle Editor needs no frame");
  if (r.ui.msUntilTick()) std::fprintf(stderr, "  timer due in %llu ms\n", static_cast<unsigned long long>(*r.ui.msUntilTick()));
  expect(!r.ui.msUntilTick().has_value(), "an idle Editor schedules no timer");
  expect(!r.app->msUntilTick().has_value() || *r.app->msUntilTick() > 0, "the auto-save countdown is not due without changes");
}

void testFloatingAndCloseOrder(Rig& r) {
  r.app->dock().openPanel(ed::panel::kCurves);
  expect(r.app->dock().floatPanel(ed::panel::kCurves), "a panel floats");
  r.settle();
  expect(!r.backend->stacking().empty(), "the floating window exists");
  r.app->run("edit.shortcuts");
  r.settle();
  expect(r.app->dock().layout().isDocked(ed::panel::kShortcuts), "the hotkey editor opened");
  // Destroying the app with a floating window and an open panel is clean (the caller's rig destroys it).
}

}  // namespace

int main() {
  const fs::path root = fs::temp_directory_path() / ("r1gui-editor-test-" + std::to_string(GetCurrentProcessId()));
  std::error_code ignored;
  fs::remove_all(root, ignored);
  try {
    {
      Rig r(root / "main");
      testBuildsAndSeeds(r);
      testSampleMenusAndMenu(r);
      testPanelsInAnotherContext(r);
      testUndoThroughCommands(r);
      testViewportDrag(r);
      testLayoutPersistence(r);
      testDamagedLayout(r);
      testKeybindingsPersist(r);
      testLayoutDialogs(r);
      testCreateMenusThroughCreator(r);
      testPieInViewport(r);
      testHotkeyEditorPanel(r);
      testMenuFilesAndRestart(r);
      testWorkspaces(r);
      testDamagedMenusFile(r);
      testIdle(r);
      testFloatingAndCloseOrder(r);
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: uncaught exception: %s\n", e.what());
    ++failures;
  }
  fs::remove_all(root, ignored);
  std::printf("%s\n", failures == 0 ? "editor_test: ok" : "editor_test: FAILED");
  return failures == 0 ? 0 : 1;
}

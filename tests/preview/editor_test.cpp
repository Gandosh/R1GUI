// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: integration oracle for the preview's Editor screen (EditorApp) headless, over the in-window
//   backend and in-memory textures, with a throwaway data folder: every panel factory builds (also in a
//   second context, as a native window would), every menu/toolbar command exists and no default chord
//   was lost to a conflict, the first run seeds the three named layouts, edits of the inspector are undone
//   and redone by commands and by keys, the arrangement and the active layout name survive a restart, a
//   damaged layout file is kept aside and the default used, a rebound key and a customized menu survive a
//   restart (and a damaged keybindings file is ignored), the layout dialogs work from the keyboard, and an
//   idle screen schedules nothing.
// Why: these behaviours span seven modules wired together for the first time in slice 5.12.
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
#include "r1ui/dock/LayoutStore.h"
#include "r1ui/widgets/dock/InWindowFloatingBackend.h"
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
  expect(app.panels().size() == ed::panel::kCount, "nine panels registered");
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
  expect(made.size() == ed::panel::kCount && other.widgetCount() > 100, "all panels build in a second context");
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

void testCustomizePersists(Rig& r) {
  expect(r.app->run("edit.customize") && r.app->controller().editMode(), "customize mode on");
  r.settle();
  expect(r.app->dock().layout().isDocked(ed::panel::kCommands), "the command palette is open while customizing");
  const size_t menus = r.app->customization().effective().layout.menuBar.menus.size();
  const std::string id = r.app->controller().createUserMenu();
  expect(!id.empty(), "a user menu was created");
  r.app->run("edit.customize");
  expect(!r.app->controller().editMode(), "customize mode off");
  expect(fs::exists(r.root / "customization.json"), "customization.json was written on leaving the mode");
  r.restart();
  expect(r.app->customization().effective().layout.menuBar.menus.size() == menus + 1, "the user menu survived the restart");
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

void testIdle(Rig& r) {
  r.settle();
  r.settle();
  expect(!r.ui.needsFrame(), "an idle Editor needs no frame");
  if (r.ui.msUntilTick()) std::fprintf(stderr, "  timer due in %llu ms\n", static_cast<unsigned long long>(*r.ui.msUntilTick()));
  expect(!r.ui.msUntilTick().has_value(), "an idle Editor schedules no timer");
  expect(!r.app->msUntilTick().has_value() || *r.app->msUntilTick() > 0, "the auto-save countdown is not due without changes");
}

void testFloatingAndCloseOrder(Rig& r) {
  expect(r.app->dock().floatPanel(ed::panel::kCurves), "a panel floats");
  r.settle();
  expect(!r.backend->stacking().empty(), "the floating window exists");
  r.app->run("edit.shortcuts");
  r.settle();
  expect(r.app->dock().layout().isDocked(ed::panel::kShortcuts), "the shortcut editor opened");
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
      testPanelsInAnotherContext(r);
      testUndoThroughCommands(r);
      testViewportDrag(r);
      testLayoutPersistence(r);
      testDamagedLayout(r);
      testKeybindingsPersist(r);
      testCustomizePersists(r);
      testLayoutDialogs(r);
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

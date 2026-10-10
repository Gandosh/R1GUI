// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the command declarations of the Editor screen (File, Edit, View, Tools, Window, Layout, Help):
//   ids, labels, icons, default chords, enabled/checked answers and what they do.
// Why: one declaration feeds the menus, the toolbar, the free-form panel, the palette, the key router and
//   the shortcut editor (spec 07); this file is the only place that knows what a command does.
// Invariants: every predicate is cheap and safe before the dock exists (menus are built first); every
//   execute ends with registry.touch() so command-driven widgets refresh at once; commands log to the
//   console and the status line. Chords use only keys the toolkit names (letters, digits, F-keys,
//   arrows, paging keys).
// Callers: EditorApp's constructor.
#include <algorithm>

#include "EditorApp.h"
#include "EditorMenus.h"
#include "r1ui/widgets/dialog/Dialog.h"

namespace preview::editor {

namespace rc = r1ui::commands;
namespace rw = r1ui::widgets;
using Key = r1ui::core::events::Key;

namespace {

rc::ChordSequence chord(Key key, uint8_t mods = 0) { return rc::ChordSequence::single({key, mods, false}); }
Key letter(char c) { return static_cast<Key>(c); }
Key digit(int d) { return static_cast<Key>(static_cast<int>(Key::Digit0) + d); }

constexpr uint8_t kCtrl = rc::Mod::kCtrl;
constexpr uint8_t kShift = rc::Mod::kShift;
constexpr uint8_t kAlt = rc::Mod::kAlt;

}  // namespace

void EditorApp::declare(std::string id, std::string label, std::string description, std::string icon, std::string category, rc::ChordSequence primary,
                        std::function<void()> run, rc::CommandKind kind, std::function<bool()> enabled, std::function<bool()> checked, rc::ChordSequence alternate) {
  rc::CommandDef def;
  def.id = std::move(id);
  def.label = label;
  def.description = std::move(description);
  def.icon = std::move(icon);
  def.category = std::move(category);
  def.kind = kind;
  if (kind == rc::CommandKind::Radio) def.radioGroup = "tools";
  def.defaultChords = {primary, alternate};
  def.enabled = std::move(enabled);
  def.checked = std::move(checked);
  def.execute = [this, run = std::move(run), label, alive = alive_](const rc::ExecuteArgs&) {
    if (!*alive) return rc::ExecuteResult::notHandled();
    setStatus(label);  // before the command: it may replace the status with a result of its own
    run();
    model_.touch();
    model_.info("Command: " + label);
    registry_.touch();
    return rc::ExecuteResult::handled();
  };
  const rc::RegisterResult added = registry_.add(std::move(def));
  if (!added.ok) model_.log(LogLevel::Error, "command not registered: " + added.message);
}

void EditorApp::registerCommands() {
  const auto hasDock = [this] { return dockPtr() != nullptr; };
  const auto hasTab = [this] { return dockPtr() != nullptr && dockPtr()->activePanel() != 0; };

  // ---- File ----
  declare(cmd::kFileNew, "New scene", "Start again with the sample scene", "file-plus", "File", chord(letter('N'), kCtrl), [this] {
    model_.select({1});
    model_.context().undo().clear();
  });
  declare(cmd::kFileSave, "Save", "Save the layout and the user settings now", "save", "File", chord(letter('S'), kCtrl), [this] { saveAll(); });
  declare(cmd::kFileExit, "Exit", "Close the preview", "x", "File", chord(letter('Q'), kCtrl), [this] {
    if (host_.quit) host_.quit();
  });

  // ---- Edit ----
  const auto undo = [this] { return model_.context().undo().canUndo(); };
  const auto redo = [this] { return model_.context().undo().canRedo(); };
  declare(cmd::kUndo, "Undo", "Undo the last property change", "undo2", "Edit", chord(letter('Z'), kCtrl), [this] {
    const std::string label = model_.context().undo().undoLabel();
    model_.context().undo().undoRouted();
    model_.info("Undone: " + label);
  }, rc::CommandKind::Action, undo);
  declare(cmd::kRedo, "Redo", "Redo the change that was undone", "redo2", "Edit", chord(letter('Y'), kCtrl), [this] {
    const std::string label = model_.context().undo().redoLabel();
    model_.context().undo().redoRouted();
    model_.info("Redone: " + label);
  }, rc::CommandKind::Action, redo, {}, chord(letter('Z'), kCtrl | kShift));
  declare(cmd::kSelectAll, "Select all", "Select every object", "scan", "Edit", chord(letter('A'), kCtrl), [this] {
    std::vector<uint64_t> all;
    for (const SceneItem& item : model_.items()) all.push_back(item.id);
    model_.select(std::move(all));
  });
  declare(cmd::kDeselect, "Deselect", "Clear the selection", "square", "Edit", chord(letter('A'), kCtrl | kShift), [this] { model_.select({}); },
          rc::CommandKind::Action, [this] { return !model_.selection().empty(); });
  declare(cmd::kResetValues, "Reset values", "Reset every property of the selection to its default", "rotate-ccw-square", "Edit", chord(letter('R'), kCtrl | kShift),
          [this] { model_.context().resetCategory(std::nullopt); }, rc::CommandKind::Action, [this] { return !model_.selection().empty(); });
  declare(cmd::kShortcuts, "Hotkey editor...", "Assign keyboard shortcuts: pick an action, then click a key on the keyboard view", "settings2", "Edit",
          rc::ChordSequence::pair({letter('K'), kCtrl, false}, {letter('S'), kCtrl, false}),
          [this] {
            rw::DockHost* d = dockPtr();
            if (d == nullptr) return;
            const bool wasOpen = d->layout().isDocked(panel::kShortcuts);
            d->openPanel(panel::kShortcuts);
            d->activatePanel(panel::kShortcuts);
            if (!wasOpen) floatPanelSized(panel::kShortcuts, 1100.0, 640.0);  // the keyboard view needs room: a window of its own
          }, rc::CommandKind::Action, hasDock);

  // ---- View ----
  declare(cmd::kTheme, "Dark theme", "Switch between the dark and the light theme", "moon", "View", chord(letter('T'), kCtrl | kShift), [this] {
    if (host_.setDarkTheme && host_.isDark) host_.setDarkTheme(!host_.isDark());
  }, rc::CommandKind::Toggle, {}, [this] { return host_.isDark && host_.isDark(); });
  declare(cmd::kGrid, "Show grid", "Show or hide the grid in the viewport", "grid-3x3", "View", chord(letter('G'), kCtrl), [this] { model_.showGrid = !model_.showGrid; },
          rc::CommandKind::Toggle, {}, [this] { return model_.showGrid; });
  declare(cmd::kLights, "Show lights", "Show or hide the lights in the viewport", "sun", "View", chord(letter('L'), kCtrl), [this] { model_.showLights = !model_.showLights; },
          rc::CommandKind::Toggle, {}, [this] { return model_.showLights; });
  declare(cmd::kFrame, "Frame selection", "Center the viewport on the selected objects (or on the whole scene when nothing is selected)", "scan", "View", chord(letter('F')), [this] { model_.frameSelection(); });
  declare(cmd::kWireframe, "Wireframe", "Draw the objects of the viewport as outlines instead of filled shapes", "box", "View", chord(letter('Z')), [this] { model_.wireframe = !model_.wireframe; },
          rc::CommandKind::Toggle, {}, [this] { return model_.wireframe; });

  // ---- Tools (one radio group) ----
  struct ToolDef {
    const char* id;
    const char* label;
    const char* icon;
    char key;
    const char* description;
  };
  const ToolDef tools[] = {{cmd::kToolSelect, "Select", "mouse-pointer", 'V', "Click objects to select them"},
                           {cmd::kToolMove, "Move", "move-3d", 'W', "Drag objects in the viewport"},
                           {cmd::kToolRotate, "Rotate", "rotate-cw", 'E', "Rotate the selection"},
                           {cmd::kToolScale, "Scale", "maximize", 'R', "Scale the selection"}};
  for (const ToolDef& tool : tools) {
    const std::string id = tool.id;
    declare(id, tool.label, tool.description, tool.icon, "Tools", chord(letter(tool.key)), [this, id] { model_.tool = id; }, rc::CommandKind::Radio, {},
            [this, id] { return model_.tool == id; });
  }

  // ---- Window ----
  for (const rw::PanelDescriptor& descriptor : panels_.all()) {
    const r1ui::dock::PanelId id = descriptor.id;
    declare(cmd::panelToggle(id), descriptor.title, "Show or hide the " + descriptor.title + " panel", descriptor.icon.empty() ? "panel-top" : descriptor.icon, "Window", {},
            [this, id] {
              rw::DockHost* d = dockPtr();
              if (d == nullptr) return;
              if (d->layout().isDocked(id)) d->closePanel(id);
              else d->openPanel(id);
            }, rc::CommandKind::Toggle, hasDock, [this, id] { return dockPtr() != nullptr && dockPtr()->layout().isDocked(id); });
  }
  declare(cmd::kFloatTab, "Float tab", "Move the active tab into its own native window", "external-link", "Window", chord(letter('F'), kCtrl | kAlt), [this] { dockPtr()->floatActiveTab(); },
          rc::CommandKind::Action, hasTab);
  declare(cmd::kMoveStack, "Move tab group to new window", "Move all tabs of the active region into one new window", "layout-panel-top", "Window", chord(letter('M'), kCtrl | kAlt),
          [this] { dockPtr()->moveStackToNewWindow(dockPtr()->activePanel()); }, rc::CommandKind::Action, hasTab);
  declare(cmd::kNextTab, "Next tab", "Show the next tab of the active region", "chevron-right", "Window", chord(Key::PageDown, kCtrl), [this] { dockPtr()->nextTab(); }, rc::CommandKind::Action, hasTab);
  declare(cmd::kPrevTab, "Previous tab", "Show the previous tab of the active region", "chevron-left", "Window", chord(Key::PageUp, kCtrl), [this] { dockPtr()->previousTab(); },
          rc::CommandKind::Action, hasTab);
  declare(cmd::kCloseTab, "Close tab", "Close the active tab", "x", "Window", chord(letter('W'), kCtrl), [this] { dockPtr()->closeActiveTab(); }, rc::CommandKind::Action, hasTab);
  if (host_.screenNames) {
    const std::vector<std::string> names = host_.screenNames();
    for (size_t i = 0; i < names.size(); ++i) {
      declare(cmd::screenCommand(static_cast<int>(i)), names[i], "Show the " + names[i] + " screen of the preview", "layout-grid", "Window", {},
              [this, i] {
                if (host_.showScreen) host_.showScreen(static_cast<int>(i));
              });
    }
  }

  // ---- Layout ----
  const auto named = [this](const char* name) { return [this, name] { return layouts_ && activeName_ == name; }; };
  declare(cmd::kLayoutDefault, "Default", "Switch to the Default layout", "layout-dashboard", "Layout", chord(digit(1), kCtrl | kAlt), [this] { loadNamed("Default", 0); },
          rc::CommandKind::Toggle, hasDock, named("Default"));
  declare(cmd::kLayoutModeling, "Modeling", "Switch to the Modeling layout", "shapes", "Layout", chord(digit(2), kCtrl | kAlt), [this] { loadNamed("Modeling", 1); },
          rc::CommandKind::Toggle, hasDock, named("Modeling"));
  declare(cmd::kLayoutReview, "Review", "Switch to the Review layout", "eye", "Layout", chord(digit(3), kCtrl | kAlt), [this] { loadNamed("Review", 2); },
          rc::CommandKind::Toggle, hasDock, named("Review"));
  declare(cmd::kLayoutSwitch, "Switch layout...", "Pick any saved layout from a list", "menu", "Layout", chord(letter('L'), kCtrl | kAlt), [this] { showLayoutMenu(); },
          rc::CommandKind::Action, hasDock);
  declare(cmd::kLayoutSave, "Save layout", "Overwrite the current layout with the arrangement on screen", "save", "Layout", {}, [this] { saveLayout(); }, rc::CommandKind::Action, hasDock);
  declare(cmd::kLayoutSaveAs, "Save layout as...", "Store the arrangement on screen under a new name", "save", "Layout", chord(letter('S'), kCtrl | kShift),
          [this] { saveLayoutAs(); }, rc::CommandKind::Action, hasDock);
  declare(cmd::kLayoutRename, "Rename layout...", "Rename the current layout", "pencil", "Layout", {}, [this] { renameLayout(); }, rc::CommandKind::Action,
          [this] { return layouts_ && !layouts_->activeKey().empty(); });
  declare(cmd::kLayoutDelete, "Delete layout...", "Delete the current layout", "trash-2", "Layout", {}, [this] { deleteLayout(); }, rc::CommandKind::Action,
          [this] { return layouts_ && !layouts_->activeKey().empty(); });
  declare(cmd::kLayoutReset, "Reset layout...", "Return to the built-in default arrangement", "rotate-ccw-square", "Layout", {}, [this] { resetLayout(); }, rc::CommandKind::Action, hasDock);
  declare(cmd::kWorkspaceSave, "Save custom workspace...", "Save the layout, custom menus, customization and key bindings into one .r1ws file", "save", "Layout", {},
          [this] { askSaveWorkspace(); }, rc::CommandKind::Action, hasDock);
  declare(cmd::kWorkspaceLoad, "Load custom workspace...", "Load a .r1ws file: replaces the layout, custom menus, customization and key bindings", "folder-open", "Layout", {},
          [this] { askLoadWorkspace(); }, rc::CommandKind::Action, hasDock);

  // ---- Help ----
  declare(cmd::kAbout, "About the preview", "About this preview", "circle-alert", "Help", chord(Key::F1), [this] {
    rw::DialogSpec spec;
    spec.title = "R1GUI preview";
    spec.description = "The Editor screen is built only from the toolkit's docking, native floating windows, command, property and customization modules.";
    spec.actions = {{"ok", "Close", rw::DialogActionKind::Primary, true, true, true}};
    spec.owner = root_;
    spec.maxWidth = 420.0;
    dialog_ = rw::openDialog(ui_, std::move(spec));
  });
}

}  // namespace preview::editor

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: EditorApp, the preview's Editor screen: a sample editor workspace built only from the Phase 5
//   toolkit. A DockHost holds nine registered panels (viewport, outliner, inspector, assets, curves,
//   console, command palette, quick actions, shortcuts); a customizable menu bar and toolbar sit above
//   it; a CommandRegistry with the usual File / Edit / View / Window / Layout commands drives menus,
//   toolbar, keys and the palette; named dock layouts (Default, Modeling, Review) are managed by a
//   LayoutManager over files; customization and key bindings are saved under the data root.
// Why: slice 5.12. The owner must be able to open one screen and try docking, native floating windows,
//   commands and shortcuts, property editing with undo and menu/toolbar customization together.
// Callers: PreviewApp (builds it for the Editor mode and calls update() every frame, forwards key
//   releases and asks msUntilTick()), tests/preview (headless, over the in-window backend). Calls:
//   ui-dock, ui-commands, ui-props and the widget library.
// Threading: UI thread only. Lifetime: the UiContext, the backend and the host callbacks outlive the
//   app; the destructor flushes the layout and the user files, then destroys its widgets (dock first, so
//   floating windows close) before any model member dies.
// Failure behavior: a file that cannot be read or written is reported in the console and the status
//   line, the previous valid state stays (layouts: the damaged file is kept aside by LayoutManager).
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "EditorModel.h"
#include "r1ui/commands/CommandRegistry.h"
#include "r1ui/commands/CommandRouter.h"
#include "r1ui/commands/Keymap.h"
#include "r1ui/commands/OverrideIo.h"
#include "r1ui/commands/Overrides.h"
#include "r1ui/commands/customize/Customization.h"
#include "r1ui/commands/customize/CustomizationIo.h"
#include "r1ui/dock/DockMonitors.h"
#include "r1ui/dock/LayoutManager.h"
#include "r1ui/dock/LayoutStore.h"
#include "r1ui/widgets/commands/CommandKeys.h"
#include "r1ui/widgets/dialog/Dialog.h"
#include "r1ui/widgets/commands/CommandUiSync.h"
#include "r1ui/widgets/customize/CustomizableBars.h"
#include "r1ui/widgets/customize/CustomizeController.h"
#include "r1ui/widgets/dock/DockHost.h"
#include "r1ui/widgets/dock/PanelRegistry.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/select/Select.h"

namespace preview::editor {

// Panel ids of the Editor screen (stable: stored in layout files).
namespace panel {
inline constexpr r1ui::dock::PanelId kViewport = 1;
inline constexpr r1ui::dock::PanelId kOutliner = 2;
inline constexpr r1ui::dock::PanelId kInspector = 3;
inline constexpr r1ui::dock::PanelId kAssets = 4;
inline constexpr r1ui::dock::PanelId kCurves = 5;
inline constexpr r1ui::dock::PanelId kConsole = 6;
inline constexpr r1ui::dock::PanelId kCommands = 7;
inline constexpr r1ui::dock::PanelId kQuickActions = 8;
inline constexpr r1ui::dock::PanelId kShortcuts = 9;
inline constexpr size_t kCount = 9;
}  // namespace panel

// What the Editor needs from the application around it.
struct EditorHost {
  std::filesystem::path dataRoot;                       // keybindings, customization and layouts live below it
  std::function<void(bool dark)> setDarkTheme;
  std::function<bool()> isDark;
  std::function<void()> quit;
  std::function<void(int screen)> showScreen;           // index of a preview screen (Window > Screen)
  std::function<std::vector<std::string>()> screenNames;  // names for the Window > Screen entries
  std::function<r1ui::dock::MonitorSet()> monitors;     // empty = no monitor information
};

// The data root of the preview: R1GUI_PREVIEW_DATA when set (tests, scripted drives), otherwise
// %LOCALAPPDATA%/R1GUI/preview, otherwise the temp directory.
std::filesystem::path defaultDataRoot();

class EditorApp {
 public:
  EditorApp(r1ui::widgets::UiContext& ui, r1ui::core::tree::WidgetId parent, r1ui::widgets::IFloatingBackend& backend, EditorHost host);
  ~EditorApp();
  EditorApp(const EditorApp&) = delete;
  EditorApp& operator=(const EditorApp&) = delete;

  // Once per drawn frame, before the context's layout: refreshes command-driven widgets, the layout
  // drop-down and the status line.
  void update();
  // Once per loop iteration, drawn frame or not: the layout auto-save and the user files.
  void tick();
  // Milliseconds until update() has timed work (layout auto-save); nullopt = none.
  std::optional<uint64_t> msUntilTick() const;
  // A key release of the main window (momentary commands, release-trigger chords).
  bool onKeyUp(r1ui::core::events::Key key, uint8_t modifiers);
  // Writes the layout and every user file now (application exit, tests).
  void flush();

  // ---- parts, for tests and the scripted drive ----
  EditorModel& model() { return model_; }
  r1ui::commands::CommandRegistry& registry() { return registry_; }
  r1ui::commands::CommandRouter& router() { return router_; }
  r1ui::commands::KeybindingOverrides& overrides() { return overrides_; }
  r1ui::widgets::CommandServices services() { return {registry_, overrides_, keymap_, router_}; }
  r1ui::widgets::DockHost& dock() { return *dockPtr(); }
  r1ui::dock::LayoutManager& layouts() { return *layouts_; }
  r1ui::widgets::CustomizeController& controller() { return controller_; }
  r1ui::commands::customize::Customization& customization() { return customization_; }
  r1ui::widgets::PanelRegistry& panels() { return panels_; }
  r1ui::widgets::CustomizableMenuBar* menuBar() { return ui_.objectAs<r1ui::widgets::CustomizableMenuBar>(menuBar_); }
  r1ui::widgets::CustomizableToolbar* toolbar() { return ui_.objectAs<r1ui::widgets::CustomizableToolbar>(toolbar_); }
  r1ui::core::tree::WidgetId rootWidget() const { return root_; }
  r1ui::core::tree::WidgetId menuBarWidget() const { return menuBar_; }
  r1ui::core::tree::WidgetId toolbarWidget() const { return toolbar_; }
  r1ui::core::tree::WidgetId layoutSelectWidget() const { return layoutSelect_; }
  r1ui::core::tree::WidgetId dockWidget() const { return dockId_; }
  r1ui::core::tree::WidgetId statusWidget() const { return statusLabel_; }
  const std::filesystem::path& dataRoot() const { return host_.dataRoot; }
  bool started() const { return started_; }
  // The display name of the named layout the arrangement came from; empty for a custom arrangement.
  std::string layoutName() const { return activeName_; }
  const std::string& statusText() const { return status_; }
  // Runs a command through the router as a menu would; false when unknown or refused.
  bool run(std::string_view commandId);

 private:
  // ---- EditorApp.cpp ----
  void buildUi(r1ui::core::tree::WidgetId parent);
  void refreshStatus();
  void saveUserFiles();
  void saveAll();
  void installKeyForwarder(r1ui::widgets::UiContext& ui);
  void setStatus(std::string text);
  r1ui::widgets::DockHost* dockPtr() { return ui_.objectAs<r1ui::widgets::DockHost>(dockId_); }

  // ---- EditorCommands.cpp ----
  void registerCommands();
  void declare(std::string id, std::string label, std::string description, std::string icon, std::string category,
               r1ui::commands::ChordSequence primary, std::function<void()> run, r1ui::commands::CommandKind kind = r1ui::commands::CommandKind::Action,
               std::function<bool()> enabled = {}, std::function<bool()> checked = {}, r1ui::commands::ChordSequence alternate = {});

  // ---- EditorPanels.cpp ----
  void registerPanels();
  void exportKeybindings();
  void importKeybindings();

  // ---- EditorLayouts.cpp ----
  r1ui::dock::DockLayout builtinLayout(int which) const;
  bool seedLayouts();
  void startLayouts();
  void rebuildLayoutSelect();
  void setActiveKey(const std::string& key);
  void loadLayoutKey(const std::string& key);
  void loadNamed(const std::string& name, int builtin);
  void saveLayout();
  void saveLayoutAs();
  void renameLayout();
  void deleteLayout();
  void resetLayout();
  void showLayoutMenu();
  void reportLayout(const r1ui::dock::LayoutReport& report, std::string_view what);
  void refreshActiveName();
  void deferred(std::function<void()> action);
  void openConfirm(const std::string& title, const std::string& description, const std::string& action, bool danger, const std::function<void()>& accept);
  void openPrompt(const std::string& title, const std::string& description, const std::string& initial, const std::string& action,
                  const std::function<void(const std::string&)>& accept);
  void confirm(const std::string& title, const std::string& description, const std::string& action, bool danger, std::function<void()> accept);
  void promptText(const std::string& title, const std::string& description, const std::string& initial, const std::string& action,
                  std::function<void(const std::string&)> accept);

  r1ui::widgets::UiContext& ui_;
  r1ui::widgets::IFloatingBackend& backend_;
  EditorHost host_;

  // Commands. Declaration order is initialisation order and destruction order reversed: widgets are
  // destroyed in the destructor body, then the key handler, the controller, the sync, the router, the
  // model.
  r1ui::commands::CommandRegistry registry_;
  r1ui::commands::KeybindingOverrides overrides_{registry_};
  r1ui::commands::Keymap keymap_{registry_, overrides_};
  r1ui::widgets::UiClock clock_;
  r1ui::commands::CommandRouter router_;
  EditorModel model_;
  r1ui::widgets::CommandUiSync sync_;
  r1ui::commands::customize::Customization customization_;
  r1ui::commands::customize::FileTextStore customizationStore_;
  r1ui::commands::customize::CustomizationStorage customizationStorage_;
  r1ui::widgets::CustomizeController controller_;
  r1ui::commands::FileKeybindingStore keyStore_;
  r1ui::widgets::CommandKeyHandler keys_;
  // One shortcut handler per native-window context that ever showed a panel (never freed before the app:
  // a context holds a raw pointer to its handler).
  std::vector<std::pair<r1ui::widgets::UiContext*, std::unique_ptr<r1ui::core::events::GlobalKeyHandler>>> floatKeys_;

  // Docking and layouts.
  r1ui::widgets::PanelRegistry panels_;
  r1ui::dock::FileLayoutStore layoutStore_;
  r1ui::dock::SteadyClock layoutClock_;
  std::unique_ptr<r1ui::dock::LayoutManager> layouts_;
  std::string activeKey_;   // the named layout the arrangement came from (the manager only restores the arrangement)
  std::string activeName_;  // its display name, read when the key changes

  r1ui::core::tree::WidgetId root_, menuBar_, toolbar_, dockId_, statusLabel_, layoutLabel_, layoutSelect_;
  r1ui::widgets::DialogHandle dialog_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  bool started_ = false;
  bool layoutSelectDirty_ = true;
  bool applyingLayoutSelect_ = false;
  bool wasEditing_ = false;
  std::string status_ = "Ready";
  std::string pendingText_;
  uint64_t savedOverridesVersion_ = 0;
  r1ui::widgets::CustomizeController::ListenerId controllerListener_ = 0;
  uint64_t undoListener_ = 0;
};

}  // namespace preview::editor

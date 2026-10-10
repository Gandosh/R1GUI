// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: EditorApp, the preview's Editor screen: a sample editor workspace built only from the Phase 5
//   toolkit. A DockHost holds the registered panels (viewport, outliner, inspector, assets, curves,
//   console, actions list, hotkey editor, the menu creator and one panel per user-made dockable menu); a
//   customizable menu bar (File ... Help, then Custom Menus) and toolbar sit above it; a CommandRegistry
//   with the usual File / Edit / View / Window / Layout commands drives menus, toolbar, keys and the
//   action list; named dock layouts (Default, Modeling, Review) are managed by a LayoutManager over
//   files; custom menus (pie and dockable), custom workspaces, customization and key bindings are saved
//   under the data root.
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
#include "r1ui/commands/custommenu/CustomMenuIo.h"
#include "r1ui/commands/custommenu/CustomMenuSet.h"
#include "r1ui/commands/customize/CustomizationIo.h"
#include "r1ui/commands/workspace/Workspace.h"
#include "r1ui/dock/DockMonitors.h"
#include "r1ui/dock/LayoutManager.h"
#include "r1ui/dock/LayoutStore.h"
#include "r1ui/widgets/commands/CommandKeys.h"
#include "r1ui/widgets/dialog/Dialog.h"
#include "r1ui/widgets/commands/CommandUiSync.h"
#include "r1ui/widgets/customize/CustomizableBars.h"
#include "r1ui/widgets/customize/CustomizeController.h"
#include "r1ui/widgets/custommenu/CustomMenuCommands.h"
#include "r1ui/widgets/custommenu/creator/CreateCustomMenuWindow.h"
#include "r1ui/widgets/custommenu/creator/CreatorSession.h"
#include "r1ui/widgets/dock/DockHost.h"
#include "r1ui/widgets/filepath/FilePathDialog.h"
#include "r1ui/widgets/pie/PieTrigger.h"
#include "r1ui/widgets/dock/PanelRegistry.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/select/Select.h"

namespace r1ui::widgets {
class HotkeyEditor;
}  // namespace r1ui::widgets

namespace preview::editor {

// Panel ids of the Editor screen (stable: stored in layout files). Id 8 was the Quick Actions panel of the
// removed Customize mode; a stored layout that names it drops it on load.
namespace panel {
inline constexpr r1ui::dock::PanelId kViewport = 1;
inline constexpr r1ui::dock::PanelId kOutliner = 2;
inline constexpr r1ui::dock::PanelId kInspector = 3;
inline constexpr r1ui::dock::PanelId kAssets = 4;
inline constexpr r1ui::dock::PanelId kCurves = 5;
inline constexpr r1ui::dock::PanelId kConsole = 6;
inline constexpr r1ui::dock::PanelId kCommands = 7;  // the actions list (searchable, with descriptions)
inline constexpr r1ui::dock::PanelId kShortcuts = 9;  // the hotkey editor
inline constexpr r1ui::dock::PanelId kCreator = 20;   // the Create Custom Menu window (opens floating)
inline constexpr r1ui::dock::PanelId kMenuBase = 1000;  // a user-made dockable menu with serial n is panel kMenuBase + n
// The panels of Window > Panels, in menu order.
inline constexpr r1ui::dock::PanelId kStandard[] = {kViewport, kOutliner, kInspector, kAssets, kCurves, kConsole, kCommands, kShortcuts};
inline constexpr size_t kStandardCount = sizeof(kStandard) / sizeof(kStandard[0]);
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

  // ---- custom menus and workspaces, for tests and the scripted drive ----
  r1ui::commands::custommenu::CustomMenuSet& menuSet() { return menuSet_; }
  r1ui::widgets::CreatorSession& creator() { return creator_; }
  // The dock panel of a dockable menu (0 for a pie menu or an unknown id).
  r1ui::dock::PanelId panelForMenu(const std::string& menuId) const;
  // The pie the right mouse button opens in the viewport: the one created, edited or loaded last.
  const std::string& viewportPieId() const { return viewportPieId_; }
  std::filesystem::path menusFolder() const { return host_.dataRoot / "menus"; }
  std::filesystem::path workspacesFolder() const { return host_.dataRoot / "workspaces"; }
  // Opens (or focuses) the panel of a dockable menu.
  bool openMenuPanel(const std::string& menuId);
  // Opens the creator window (a floating panel) on the session's draft.
  void showCreator();
  bool saveMenuTo(const std::string& menuId, const std::filesystem::path& file);
  bool loadMenuFrom(const std::filesystem::path& file);
  bool saveWorkspaceTo(const std::filesystem::path& file);
  bool loadWorkspaceFrom(const std::filesystem::path& file);
  // Ends a running pie gesture in every window (Escape).
  void cancelPies();
  // Floats an open panel into a window of its own with the content size the panel wants (capped to the main
  // window), centred over the main window. The dock would otherwise keep the size of the region it left.
  bool floatPanelSized(r1ui::dock::PanelId panelId, double width, double height);
  // Runs `action` from a timer of `ui` (a dialog must not open inside the command that asked for it).
  void deferredIn(r1ui::widgets::UiContext& ui, std::function<void()> action);

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
  void exportKeybindingsTo(const std::filesystem::path& file);
  void importKeybindingsFrom(const std::filesystem::path& file);
  void askKeybindingFile(r1ui::widgets::UiContext& ui, r1ui::core::tree::WidgetId owner, bool save);
  void persistHotkeySet(const std::string& name, const std::string& json);
  void restoreHotkeySets(r1ui::widgets::HotkeyEditor& editor);

  // ---- EditorCustomMenus.cpp ----
  void startCustomMenus();
  void createSampleMenus();
  void onMenusChanged();
  void registerMenuPanels();
  void syncMenuPanels();
  void scheduleMenuBarRefresh();
  void refreshMenuBar();
  r1ui::dock::PanelId registerMenuPanel(const r1ui::commands::custommenu::CustomMenu& menu);
  r1ui::widgets::CustomMenuHooks menuHooks();
  r1ui::widgets::CreatorHooks creatorHooks();
  void registerCreatorPanel();
  void closeCreator();
  void beginCreateMenu();
  void beginEditMenu(const std::string& menuId);
  void askSaveMenu(const std::string& menuId);
  void askLoadMenu();
  void askDeleteMenu(const std::string& menuId);
  void menuCommitted(const std::string& menuId, bool edited);
  void rememberViewportPie(const std::string& menuId);
  std::optional<r1ui::commands::custommenu::CustomMenu> viewportPie() const;
  r1ui::core::tree::WidgetId makeViewport(r1ui::widgets::UiContext& ui, r1ui::core::tree::WidgetId parent);
  void chooseFile(r1ui::widgets::UiContext& ui, r1ui::core::tree::WidgetId owner, r1ui::widgets::FilePathOptions options,
                  std::function<void(const std::filesystem::path&)> onChosen);

  // ---- EditorWorkspaces.cpp ----
  void askSaveWorkspace();
  void askLoadWorkspace();

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
  r1ui::commands::custommenu::CustomMenuSet menuSet_;  // before customization_: the Custom Menus node reads it
  r1ui::commands::customize::Customization customization_;
  r1ui::commands::customize::FileTextStore customizationStore_;
  r1ui::commands::customize::CustomizationStorage customizationStorage_;
  r1ui::widgets::CustomizeController controller_;
  r1ui::commands::FileKeybindingStore keyStore_;
  r1ui::widgets::CommandKeyHandler keys_;
  std::unique_ptr<r1ui::core::events::GlobalKeyHandler> mainKeys_;  // Escape ends a pie gesture, then keys_
  // One shortcut handler per native-window context that ever showed a panel (never freed before the app:
  // a context holds a raw pointer to its handler).
  std::vector<std::pair<r1ui::widgets::UiContext*, std::unique_ptr<r1ui::core::events::GlobalKeyHandler>>> floatKeys_;

  // Custom menus: the set, its file, the commands behind the Custom Menus menu, the creator window's state.
  r1ui::commands::customize::FileTextStore menuStore_;
  r1ui::commands::custommenu::CustomMenuStorage menuStorage_;
  std::unique_ptr<r1ui::widgets::CustomMenuCommands> menuCommands_;
  r1ui::widgets::CreatorSession creator_;
  r1ui::commands::custommenu::CustomMenuSet::ListenerId menuListener_ = 0;
  std::string viewportPieId_;
  bool firstMenuRun_ = false;
  bool menuBarRefreshPending_ = false;
  std::string menuPanelSignature_;  // serials and titles of the dockable menus the panel registry holds
  std::string dockPanelSignature_;  // the same, as the dock model was last refreshed with it
  std::vector<r1ui::widgets::PieTrigger*> pieTriggers_;  // the live triggers of every window (see ViewportPie)

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
  std::string status_ = "Ready";
  std::string pendingText_;
  uint64_t savedOverridesVersion_ = 0;
  uint64_t undoListener_ = 0;
};

}  // namespace preview::editor

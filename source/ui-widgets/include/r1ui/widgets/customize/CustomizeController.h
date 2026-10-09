// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CustomizeController, the object that ties the customization model (ui-commands) to the widgets
//   of one window: the edit-mode flag and its edit session, the drag hub, the registered Customize
//   commands, the confirmation of "reset all", the refusal message of the last rejected edit and the
//   change notifications the bound widgets listen to.
// Why: spec 06 and decisions D1 to D6: a toggle command "Customize" enters edit mode (the bound menu
//   bar, toolbars and free-form panels switch to their edit display), edits apply live, leaving the
//   mode commits the session (the host's storage writes the files), the session can be reverted as
//   a whole, reset-all asks first (decision D6) and a refused edit says why.
// Callers: the host (constructs it after the registry, Customization and CommandUiSync), the
//   customize widgets (CustomizableMenuBar / Toolbar, FreeFormPanel, CustomizeToolStrip, the palette),
//   the gallery. Calls: Customization, CommandServices, DragHub, openDialog.
// Commands registered (registerCommands): customize.toggle (Toggle, checked while editing),
//   customize.revert (revert the session), customize.resetAll (opens the confirmation), customize.newMenu.
//   They are ordinary registry commands: they appear in the keybinding editor, menus and palette.
// Command set tracking: the controller wires the Customization to the registry (command labels, which
//   commands exist) and tells it when the SET of commands changed (not on every touch), so entries of
//   removed commands show as missing without a rebuild on each command execution.
// Lifetime: the controller must outlive the widgets built with it and be destroyed before the
//   Customization, the registry and the UiContext. It removes its commands and subscriptions.
// Threading: UI thread only.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "r1ui/commands/customize/Customization.h"
#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/commands/CommandUiSync.h"
#include "r1ui/widgets/customize/DragHub.h"
#include "r1ui/widgets/dialog/Dialog.h"

namespace r1ui::widgets {

inline constexpr const char* kCmdCustomizeToggle = "customize.toggle";
inline constexpr const char* kCmdCustomizeRevert = "customize.revert";
inline constexpr const char* kCmdCustomizeResetAll = "customize.resetAll";
inline constexpr const char* kCmdCustomizeNewMenu = "customize.newMenu";

class CustomizeController {
 public:
  using Listener = std::function<void()>;
  using ListenerId = uint32_t;

  CustomizeController(UiContext& ui, CommandServices services, CommandUiSync& sync, commands::customize::Customization& customization);
  ~CustomizeController();
  CustomizeController(const CustomizeController&) = delete;
  CustomizeController& operator=(const CustomizeController&) = delete;

  UiContext& ui() const { return ui_; }
  const CommandServices& services() const { return services_; }
  CommandUiSync& sync() const { return sync_; }
  commands::customize::Customization& model() const { return model_; }
  DragHub& drag() { return drag_; }

  // ---- edit mode ------------------------------------------------------------------------------
  bool editMode() const { return editMode_; }
  // Entering begins an edit session; leaving commits it (the storage attached to the model saves).
  void setEditMode(bool on);
  void toggleEditMode() { setEditMode(!editMode_); }
  // Back to the state at the start of the session (decision D6); false outside edit mode.
  bool revertSession();
  // Opens the confirmation (default answer: Cancel); the reset runs only on "Reset all".
  void requestResetAll();
  bool resetAllDialogOpen() const;
  // Starts a user menu named "New menu" (made unique) and returns its id; the edit display puts the
  // title into in-place rename. Empty when the menu bar refuses (locked).
  std::string createUserMenu();
  // The id of a node the edit display should put into in-place rename now (set by createUserMenu);
  // returns it once, then empty.
  std::string takePendingRename();

  // ---- messages -------------------------------------------------------------------------------
  // The reason of the last edit the model refused ("" after a successful one); the edit widgets set it
  // and show it as a tooltip or status text.
  const std::string& lastRefusal() const { return refusal_; }
  void noteResult(const commands::customize::EditResult& result);
  // Called whenever noteResult changed the message (set or cleared); the tool strip shows it.
  ListenerId subscribeMessage(Listener listener);
  void unsubscribeMessage(ListenerId id);

  // The menu the menu editor shows now ("Reset menu" of the tool strip acts on it); empty when none.
  const std::string& currentMenu() const { return currentMenu_; }
  void setCurrentMenu(const std::string& id) { currentMenu_ = id; }

  // ---- notifications --------------------------------------------------------------------------
  // Called when edit mode switches or the model changed; bound widgets rebuild from it.
  ListenerId subscribe(Listener listener);
  void unsubscribe(ListenerId id);
  uint64_t revision() const { return revision_; }

 private:
  void registerCommands();
  void notify();
  uint64_t commandSignature() const;

  UiContext& ui_;
  CommandServices services_;
  CommandUiSync& sync_;
  commands::customize::Customization& model_;
  DragHub drag_;
  bool editMode_ = false;
  std::string refusal_;
  std::string pendingRename_;
  uint64_t revision_ = 0;
  uint64_t seenSignature_ = 0;
  std::vector<std::pair<ListenerId, Listener>> listeners_;
  std::vector<std::pair<ListenerId, Listener>> messageListeners_;
  std::string currentMenu_;
  ListenerId nextListener_ = 1;
  commands::customize::Customization::ListenerId modelListener_ = 0;
  commands::CommandRegistry::ListenerId registryListener_ = 0;
  std::vector<std::string> ownCommands_;
  DialogHandle dialog_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  bool notifying_ = false;
  bool renotify_ = false;
};

}  // namespace r1ui::widgets

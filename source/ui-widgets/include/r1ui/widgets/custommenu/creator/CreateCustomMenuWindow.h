// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CreateCustomMenuWindow, the content of the "Create Custom Menu" window (slice 5.17): first the type
//   of the new menu (Dockable panel or Pie menu), then the editor: on the LEFT the target (the pie as an
//   editable radial preview with a 4/6/8 slot selector, or the panel preview with columns, button size and
//   labels), on the RIGHT the searchable action list with a description on every row, below them the name,
//   the messages and the buttons Create/Save, Cancel, Save to file, Load from file.
// Why: owner requirement 2026-10-10 replaced the earlier Customize mode with this window: pick the type,
//   drag actions from the list onto slots or into the panel (or double-click a row), name the menu, create
//   it. The same window edits an existing menu (Custom Menus > Edit...).
// Callers: the host's dock (the window is a panel in a native floating window), the gallery, tests. Calls:
//   CreatorSession (the draft), PiePreviewEditor / PanelPreviewEditor, ActionList, DragHub, the commands.
// Editing model (documented decision): the user edits a private working copy (MenuDraft). Create/Save
//   commits it into the live set in one step (validated and dry-run first); Cancel discards it, so there
//   is nothing to revert. The live set, the Custom Menus menu, the dock panels and the files do not change
//   before that.
// Validation: the name is required and must differ (ASCII case-insensitively) from every other custom
//   menu; at least one action must be placed. The first problem is shown beside the buttons and the
//   Create/Save button stays disabled until there is none.
// Hooks: the host decides what happens after a commit (close the window, open the new panel), how files
//   are chosen (Save to file / Load from file) and what Cancel does with the window. A missing hook makes
//   its button do nothing (the buttons are then hidden).
// Rebuilding: the window rebuilds its content when the session's draft is replaced or the type is chosen,
//   from a timer (never inside the handler that caused it).
// Lifetime: services, session and hooks' targets outlive the widget; it unsubscribes in onDetached and
//   owns its DragHub. UI thread only. Nothing here throws.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/customize/DragHub.h"
#include "r1ui/widgets/custommenu/creator/CreatorSession.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class ActionList;
class PanelPreviewEditor;
class PiePreviewEditor;

struct CreatorHooks {
  // After Create/Save put the menu into the live set. `edited` is true for an existing menu.
  std::function<void(const std::string& menuId, bool edited)> committed;
  // Cancel was pressed; the session's draft is already dropped.
  std::function<void()> cancelled;
  // "Save to file...": the menu as it would be stored; the host asks for a path and writes the .r1mn file.
  std::function<void(const commands::custommenu::CustomMenu& menu)> saveFile;
  // "Load from file...": the host asks for a path and, when the file is good, calls session.beginFromFile.
  std::function<void()> loadFile;
};

class CreateCustomMenuWindow final : public WidgetObject {
 public:
  CreateCustomMenuWindow(CommandServices services, CreatorSession& session, CreatorHooks hooks);

  const char* typeName() const override { return "CreateCustomMenuWindow"; }
  void onAttached() override;
  void onDetached() override;

  // ---- actions of the buttons (the UI paths call these) ----
  bool create();   // Create / Save
  void cancel();
  // Adds the action to the selected empty slot (or the first empty one) of a pie, or to the end of a panel.
  bool addAction(const std::string& commandId);
  // Chooses the type in the chooser page.
  void chooseType(commands::custommenu::MenuKind kind);

  // ---- state ----
  bool showingChooser() const { return !session_.typeChosen(); }
  // The line under the editors (what the last operation did).
  const std::string& status() const { return status_; }
  // The first reason the draft cannot be committed, empty when it can.
  std::string issue() const;
  CreatorSession& session() { return session_; }

  // ---- parts (tests, the host) ----
  ActionList* actions() const;
  PiePreviewEditor* pieEditor() const;
  PanelPreviewEditor* panelEditor() const;
  DragHub* dragHub() const { return hub_.get(); }
  core::tree::WidgetId nameField() const { return nameField_; }
  core::tree::WidgetId labelField() const { return labelField_; }
  core::tree::WidgetId createButton() const { return createButton_; }
  core::tree::WidgetId cancelButton() const { return cancelButton_; }
  core::tree::WidgetId saveFileButton() const { return saveFileButton_; }
  core::tree::WidgetId loadFileButton() const { return loadFileButton_; }
  core::tree::WidgetId slotsSelector() const { return slotsSelector_; }
  core::tree::WidgetId columnsSelector() const { return columnsSelector_; }
  core::tree::WidgetId sizeSelector() const { return sizeSelector_; }
  core::tree::WidgetId labelsCheckbox() const { return labelsCheckbox_; }
  core::tree::WidgetId typeSelector() const { return typeSelector_; }
  core::tree::WidgetId pieCard() const { return pieCard_; }
  core::tree::WidgetId panelCard() const { return panelCard_; }
  core::tree::WidgetId clearButton() const { return clearButton_; }
  // Runs a pending rebuild now (tests; the timer does it in the app).
  void flush();

 private:
  void scheduleRebuild();
  void rebuild();
  void buildChooser(core::tree::WidgetId parent);
  void buildEditor(core::tree::WidgetId parent);
  void buildLeft(core::tree::WidgetId parent);
  void buildRight(core::tree::WidgetId parent);
  void buildFooter(core::tree::WidgetId parent);
  void draftChanged();
  void say(const std::string& text);
  void refreshControls();
  void selectionChanged(int index);
  commands::custommenu::MenuDraft* draft() const { return session_.draft(); }

  CommandServices services_;
  CreatorSession& session_;
  CreatorHooks hooks_;
  std::unique_ptr<DragHub> hub_;
  CreatorSession::ListenerId listener_ = 0;
  uint64_t builtGeneration_ = 0;
  bool rebuildPending_ = false;
  bool refreshing_ = false;
  std::string status_;
  int selected_ = -1;

  core::tree::WidgetId content_, actions_, pie_, panel_;
  core::tree::WidgetId nameField_, labelField_, createButton_, cancelButton_, saveFileButton_, loadFileButton_, clearButton_;
  core::tree::WidgetId slotsSelector_, columnsSelector_, sizeSelector_, labelsCheckbox_, typeSelector_;
  core::tree::WidgetId pieCard_, panelCard_;
  core::tree::WidgetId statusLabel_, issueLabel_, selectionLabel_, titleLabel_;
};

}  // namespace r1ui::widgets

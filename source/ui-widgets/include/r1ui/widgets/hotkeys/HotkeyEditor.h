// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: HotkeyEditor, the keyboard-shortcut editor in the style of a professional hotkey editor: a
//   "Hotkey Set" line on top; on the left a category and a context drop-down above the searchable
//   ActionList (columns Action | Description | Hotkey, grouped by category); on the right two tabs:
//   "Keyboard" (a drawn keyboard lighting the keys that carry a hotkey) and "Runtime Command Editor"
//   (the details of the selected action with Assign, Clear and Reset to default).
// Why: it replaces how the Shortcuts panel looks while keeping the model of the earlier
//   KeybindingEditor: every change goes through the same overrides layer (assignChord and friends), so
//   conflicts, live menus and tooltips, reset and the import/export file keep working unchanged.
// Callers: hosts (the preview's Shortcuts panel), the gallery, tests. Calls: ActionList, KeyboardView,
//   CommandDetailView, ChordRecorder, ui-commands (Conflicts, KeepBoth, OverrideIo), Dialog, Select,
//   Segmented, Button, TextInput.
//
// Selecting: clicking a list row selects the action; its keys light up with a ring on the keyboard and
//   the keyboard switches to the modifier layer of its first chord. Clicking a key on the keyboard
//   with an action selected ASSIGNS that key (with the layer's modifiers) to the chosen slot (Primary
//   or Alternate); without a selection it selects the first action bound to the key (or says the key
//   is free). Hovering a key shows what is bound to it.
// Conflicts: assigning a chord another command holds opens a dialog naming the combination and the
//   other command: Replace (assignChord with override: the other command loses it), Keep both (only
//   offered when every clash is in a parent or child context, see KeepBoth.h) or Cancel. Nothing
//   changes until one is chosen; Escape or closing the dialog is Cancel.
// Modifier layer: held physical modifiers (events that reach the editor, or setPhysicalModifiers) and
//   the on-screen toggles (the Ctrl/Shift/Alt/Meta buttons or the modifier caps) combine; the keyboard
//   shows the keys that have a binding for exactly that combination.
// Hotkey sets: the active set's name is shown in the "Hotkey Set" line. A set is a named snapshot of
//   the overrides (the existing keybinding JSON) kept in memory by the editor; "Save as..." stores the
//   current bindings under a name, choosing a name in the drop-down loads it (a replace, live). The host
//   persists sets through setOnSetsChanged / addSet (name and JSON text); nothing is written by the
//   editor itself. There is no delete or rename.
// Layout: two columns; below 860 logical px the columns stack (list above, tabs below).
// Lifetime: services must outlive the editor; the editor unsubscribes in onDetached and closes its own
//   dialogs when destroyed.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "r1ui/commands/Chord.h"
#include "r1ui/commands/Conflicts.h"
#include "r1ui/widgets/actions/ActionList.h"
#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/dialog/Dialog.h"
#include "r1ui/widgets/hotkeys/CommandDetail.h"
#include "r1ui/widgets/hotkeys/KeyboardView.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

struct HotkeyEditorOptions {
  std::string setName = "Default";  // name of the set the current bindings belong to at start
};

enum class ConflictResolution : uint8_t { Replace, KeepBoth, Cancel };

// An assignment waiting for the user's decision.
struct PendingAssignment {
  std::string commandId;
  int slot = 0;
  commands::ChordSequence chord;
  std::vector<commands::Conflict> conflicts;
  bool canKeepBoth = false;
};

enum class AssignOutcome : uint8_t { Assigned, NeedsResolution, Unchanged, Refused };

inline constexpr size_t kMaxHotkeySets = 64;
inline constexpr size_t kMaxHotkeySetNameBytes = 64;

class HotkeyEditor final : public WidgetObject {
 public:
  enum class Tab : uint8_t { Keyboard, Command };

  explicit HotkeyEditor(CommandServices services, HotkeyEditorOptions options = {});

  const char* typeName() const override { return "HotkeyEditor"; }
  std::string_view accessibleName() const override { return "Hotkey editor"; }
  void onAttached() override;
  void onDetached() override;
  void onLayout() override;
  // Applies a registry change that is still waiting for the next layout pass (changes are coalesced so
  // registering thousands of commands costs one refresh, not thousands).
  void flush();
  void onKeyDown(Event& e) override;
  void onKeyUp(Event& e) override;

  // ---- selection and filters ----
  // Selects an action (clears the search when it hides it); false for an unknown id or one outside the
  // context filter.
  bool selectAction(std::string_view commandId);
  const std::string& selectedAction() const { return selected_; }
  // A category name, or empty for all; unknown names show an empty list.
  void setCategory(std::string category);
  const std::string& category() const { return category_; }
  void setContextFilter(std::string context);
  const std::string& contextFilter() const { return context_; }
  void setFilter(const std::string& text);
  const std::string& filter() const { return list().filter(); }

  // ---- tabs, slot, modifier layer ----
  void setTab(Tab tab);
  Tab tab() const { return tab_; }
  void setSlot(int slot);
  int slot() const { return slot_; }
  void setModifiers(uint8_t toggled) { keyboard().setToggledModifiers(toggled); }
  uint8_t modifiers() const { return keyboard().effectiveModifiers(); }
  void setPhysicalModifiers(uint8_t modifiers) { keyboard().setPhysicalModifiers(modifiers); }
  // The line under the keyboard: "Currently displaying hotkeys for: <category> (<layer>)".
  std::string caption() const;

  // ---- assigning (the UI paths and the API are the same) ----
  // Assigns `chord` to the slot. No conflict: applied. Conflict: stored as pending() and the dialog
  // opens (NeedsResolution). The same chord already in force: Unchanged. Unknown command, bad slot or
  // invalid chord: Refused.
  AssignOutcome assign(std::string_view commandId, int slot, const commands::ChordSequence& chord);
  const PendingAssignment* pending() const { return pending_ ? &*pending_ : nullptr; }
  bool conflictDialogOpen();
  // Applies the user's choice to the pending assignment and closes the dialog; false without a pending
  // assignment or when KeepBoth is not allowed (the dialog stays).
  bool resolveConflict(ConflictResolution resolution);
  bool clearSlot(std::string_view commandId, int slot);
  bool resetAction(std::string_view commandId);
  void requestResetAll();
  bool resetAllDialogOpen();
  const DialogHandle& resetAllDialog() const { return resetDialog_; }

  // ---- hotkey sets ----
  const std::string& setName() const { return setName_; }
  std::vector<std::string> setNames() const;
  // Stores the current bindings under `name` (trimmed, sanitised, at most kMaxHotkeySetNameBytes) and
  // makes it the active set; false for an empty name or a full set table.
  bool saveSet(std::string name);
  // Replaces the bindings with a stored set; false for an unknown name or a set the model rejects.
  bool loadSet(const std::string& name);
  // Registers a set from the host's storage without activating it (false: bad name or JSON refused).
  bool addSet(std::string name, std::string json);
  const std::string* setJson(const std::string& name) const;
  void setOnSetsChanged(std::function<void(const std::string& name, const std::string& json)> callback) { onSetsChanged_ = std::move(callback); }
  void openSaveSetDialog();
  bool saveSetDialogOpen();

  // ---- host hooks (the buttons stay disabled until a callback is set) ----
  void setOnImport(std::function<void()> callback);
  void setOnExport(std::function<void()> callback);
  const std::string& message() const { return message_; }

  // ---- parts ----
  ActionList& list() const;
  KeyboardView& keyboard() const;
  CommandDetailView& detailView() const;
  ChordRecorder& recorder() const;
  core::tree::WidgetId categorySelect() const { return categorySelect_; }
  core::tree::WidgetId contextSelect() const { return contextSelect_; }
  core::tree::WidgetId setSelect() const { return setSelect_; }
  core::tree::WidgetId tabs() const { return tabs_; }
  core::tree::WidgetId assignButton() const { return assign_; }
  core::tree::WidgetId clearButton() const { return clear_; }
  core::tree::WidgetId resetButton() const { return reset_; }
  core::tree::WidgetId modifierButton(uint8_t modifierBit) const;
  const DialogHandle& conflictDialog() const { return conflictDialog_; }
  const DialogHandle& saveSetDialog() const { return saveDialog_; }

 private:
  void buildHeader();
  void buildLeft(core::tree::WidgetId parent);
  void buildRight(core::tree::WidgetId parent);
  void refreshAll();
  void refreshSelects();
  void refreshSetSelect();
  void refreshDetail();
  void refreshButtons();
  void listSelected(const ActionInfo& action);
  void keyClicked(commands::Key key, uint8_t modifiers);
  void recorded(const commands::KeyChord& chord);
  void say(std::string text);
  void openConflictDialog();
  void closeConflictDialog();
  std::string slotName(int slot) const;
  bool setNameValid(const std::string& name) const;

  CommandServices services_;
  HotkeyEditorOptions options_;
  commands::CommandRegistry::ListenerId listener_ = 0;
  bool refreshing_ = false;
  bool dirty_ = false;

  std::string selected_;
  std::string category_;
  std::string context_;
  Tab tab_ = Tab::Keyboard;
  int slot_ = 0;
  bool compact_ = false;
  std::string message_;
  std::optional<PendingAssignment> pending_;
  bool closingDialog_ = false;

  std::string setName_;
  std::map<std::string, std::string> sets_;
  std::string pendingSetName_;
  std::function<void(const std::string&, const std::string&)> onSetsChanged_;
  std::function<void()> onImport_;
  std::function<void()> onExport_;

  core::tree::WidgetId body_;
  core::tree::WidgetId setSelect_;
  core::tree::WidgetId saveAs_;
  core::tree::WidgetId resetAll_;
  core::tree::WidgetId import_;
  core::tree::WidgetId export_;
  core::tree::WidgetId categorySelect_;
  core::tree::WidgetId contextSelect_;
  core::tree::WidgetId list_;
  core::tree::WidgetId tabs_;
  core::tree::WidgetId keyboardTab_;
  core::tree::WidgetId commandTab_;
  core::tree::WidgetId keyboard_;
  core::tree::WidgetId captionLabel_;
  core::tree::WidgetId detail_;
  core::tree::WidgetId recorder_;
  core::tree::WidgetId assign_;
  core::tree::WidgetId clear_;
  core::tree::WidgetId reset_;
  core::tree::WidgetId slotKeyboard_;
  core::tree::WidgetId slotCommand_;
  core::tree::WidgetId modifierButtons_[4];
  core::tree::WidgetId messageLabel_;
  std::vector<std::string> categoryValues_;
  std::vector<std::string> contextValues_;
  DialogHandle conflictDialog_;
  DialogHandle resetDialog_;
  DialogHandle saveDialog_;
};

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: KeybindingEditor, the shortcut editor of spec 07 rules 38 to 61: a table of every command
//   grouped by category with a search box and a context selector, two chord capture boxes per command
//   (primary and alternate), a remove button per slot, reset for one command and for everything (with a
//   confirmation dialog), import and export buttons that call back into the host, and the conflict
//   popup that names the colliding combination and command with an Override button and a Cancel button.
// Why: the keyboard-shortcut page is where the one command declaration meets the user: every change
//   goes through the overrides layer (assignChord / reset), applies live to chord processing, menus and
//   tooltips (decision D15), and is saved by the host from the overrides when it wants to.
// Callers: hosts and the gallery. Calls: ui-commands (overrides, keymap, conflicts), ChordBox, Dialog,
//   Popover, TextInput, Select, Checkbox, Button, IconButton, ScrollArea.
// Capture protocol: clicking a box (or Enter / Space on a focused one) makes it the single editing box
//   and clears any conflict message. While editing, every key belongs to the editor: held modifiers
//   show at once, a real key completes the chord; Escape without modifiers cancels (decision D15; Ctrl,
//   Alt, Shift or Meta with Escape binds it); key releases and typed characters are ignored. A complete
//   chord with no conflict is committed at once and editing ends. With a conflict the box stays in edit
//   mode and a popup below it names the combination and the other command; Override unbinds the other
//   command in every slot where it holds the combination (also in ancestor and descendant contexts, spec
//   07 rule 25) and binds the chord; Cancel, Escape or any click outside ends editing without a change;
//   typing a different chord re-checks (rule 48). The editing box losing focus without a conflict ends
//   editing and keeps the old chord (rule 53).
// Sequences (decision D14): with the two-step option on, a capture takes two presses: the first chord
//   is shown followed by a comma; the second press completes the sequence; Enter without modifiers
//   ends it as a single chord. The conflict rules include prefixes (see Conflicts.h).
// Live: the editor listens to the registry; a changed command set rebuilds the table, anything else
//   (a rebound chord, a reset, an import) only refreshes the box texts. Rows hidden by the filter take
//   no space; a category heading is hidden with its last visible row.
// Keyboard: everything is reachable by Tab (search, context, option, buttons, boxes, reset buttons); the
//   remove buttons are not in the Tab order, Delete or Backspace on a focused idle box does the same.
// Look: Approximate, no reference exists. The spec widths (name 500, box 200) are for a settings page;
//   the defaults here are compact (KeybindingEditorOptions) and can be set to the spec values.
// Lifetime: services must outlive the editor; the editor unsubscribes in onDetached and closes its own
//   popup and dialog when destroyed.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "r1ui/commands/Chord.h"
#include "r1ui/commands/Conflicts.h"
#include "r1ui/widgets/commands/ChordBox.h"
#include "r1ui/widgets/commands/CommandServices.h"
#include "r1ui/widgets/dialog/Dialog.h"
#include "r1ui/widgets/popover/Popover.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

struct KeybindingEditorOptions {
  double nameMinWidth = 240.0;  // spec 07: 500
  double boxWidth = 168.0;      // spec 07: 200
  double contextWidth = 88.0;
  bool twoStepCapture = false;  // initial state of the two-step option
};

class KeybindingEditor final : public WidgetObject {
 public:
  explicit KeybindingEditor(CommandServices services, KeybindingEditorOptions options = {});

  const char* typeName() const override { return "KeybindingEditor"; }
  void onAttached() override;
  void onDetached() override;
  std::string_view accessibleName() const override { return "Keyboard shortcuts"; }

  // ---- host hooks ----
  // The buttons are disabled until a callback is set. The host reads or writes the file (see OverrideIo).
  void setOnImport(std::function<void()> callback);
  void setOnExport(std::function<void()> callback);

  // ---- search and filters ----
  void setFilter(std::string text);
  const std::string& filter() const { return filter_; }
  // A context name, or empty for all contexts.
  void setContextFilter(std::string context);
  void setTwoStepCapture(bool on);
  bool twoStepCapture() const { return twoStep_; }

  // ---- structure (tests, hosts) ----
  size_t rowCount() const { return rows_.size(); }
  size_t visibleRowCount() const;
  bool rowVisible(std::string_view commandId) const;
  core::tree::WidgetId rowOf(std::string_view commandId) const;
  core::tree::WidgetId boxOf(std::string_view commandId, int slot) const;
  ChordBox* box(std::string_view commandId, int slot);
  core::tree::WidgetId resetButtonOf(std::string_view commandId) const;
  size_t headingCount() const { return headings_.size(); }
  size_t visibleHeadingCount() const;
  core::tree::WidgetId searchInput() const { return search_; }
  core::tree::WidgetId contextSelect() const { return contextSelect_; }
  core::tree::WidgetId twoStepCheckbox() const { return twoStepBox_; }
  core::tree::WidgetId resetAllButton() const { return resetAll_; }
  core::tree::WidgetId importButton() const { return import_; }
  core::tree::WidgetId exportButton() const { return export_; }
  core::tree::WidgetId scrollArea() const { return scroll_; }

  // ---- capture state ----
  bool capturing() const { return capture_.active; }
  const std::string& capturingCommand() const { return capture_.commandId; }
  int capturingSlot() const { return capture_.slot; }
  bool conflictPopupOpen() const;
  const std::string& conflictMessage() const { return capture_.message; }
  core::tree::WidgetId conflictHost() const { return popup_.host; }
  core::tree::WidgetId overrideButton() const { return overrideButton_; }
  core::tree::WidgetId cancelButton() const { return cancelButton_; }
  // Ends editing without changing anything and closes the conflict popup.
  void cancelCapture();

  // ---- actions ----
  // Clears the overrides of one command (both slots go back to the defaults).
  bool resetCommand(std::string_view commandId);
  // Opens the confirmation; on confirm all overrides are cleared.
  void requestResetAll();
  bool resetAllDialogOpen();
  const DialogHandle& resetAllDialog() const { return resetDialog_; }

  // ---- called by ChordBox ----
  void boxClicked(ChordBox& box);
  void boxCaptureKey(ChordBox& box, Event& e);
  void boxFocusLost(ChordBox& box);
  void boxRemove(ChordBox& box);
  void boxNavigate(ChordBox& box, int rows);

 private:
  struct Row {
    std::string commandId;
    std::string label;
    std::string category;
    std::string context;
    core::tree::WidgetId row;
    core::tree::WidgetId boxes[2];
    core::tree::WidgetId reset;
    size_t heading = 0;  // index into headings_
    bool visible = true;
  };
  struct Heading {
    core::tree::WidgetId widget;
    bool visible = true;
  };
  struct Capture {
    bool active = false;
    std::string commandId;
    int slot = 0;
    std::optional<commands::KeyChord> first;  // two-step mode: the first chord already pressed
    bool conflict = false;
    commands::ChordSequence proposed;
    std::string message;
  };

  // structure
  void buildHeader();
  void buildRows();
  void clearRows();
  uint64_t commandSignature() const;
  void onRegistryChanged();
  void refreshTexts();
  void applyFilters();
  Row* find(std::string_view commandId);
  const Row* find(std::string_view commandId) const;
  std::string chordTextOf(const std::string& commandId, int slot) const;
  std::string hintOf(const std::string& commandId, int slot) const;

  // capture (KeybindingCapture.cpp)
  void beginCapture(ChordBox& box);
  void endCapture();
  void evaluate(const commands::ChordSequence& proposed);
  void commit(const commands::ChordSequence& proposed, bool overrideConflicts);
  void showConflict(const commands::ChordSequence& proposed, const std::vector<commands::Conflict>& conflicts);
  void closeConflictPopup();
  void popupDismissed();
  void setBoxPreview(const std::string& text);
  ChordBox* captureBox();

  CommandServices services_;
  KeybindingEditorOptions options_;
  commands::CommandRegistry::ListenerId listener_ = 0;
  uint64_t signature_ = 0;

  std::string filter_;
  std::string contextFilter_;
  bool twoStep_ = false;
  std::function<void()> onImport_;
  std::function<void()> onExport_;

  core::tree::WidgetId search_;
  core::tree::WidgetId contextSelect_;
  core::tree::WidgetId twoStepBox_;
  core::tree::WidgetId resetAll_;
  core::tree::WidgetId import_;
  core::tree::WidgetId export_;
  core::tree::WidgetId scroll_;
  std::vector<Row> rows_;
  std::vector<Heading> headings_;

  Capture capture_;
  PopoverHandle popup_;
  bool closingPopup_ = false;
  core::tree::WidgetId overrideButton_;
  core::tree::WidgetId cancelButton_;
  DialogHandle resetDialog_;
};

}  // namespace r1ui::widgets

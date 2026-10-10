// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of the change half of HotkeyEditor.h: assigning a chord (with conflict
//   detection and the Replace / Keep both / Cancel dialog), clearing, resetting one action or all, and
//   the in-memory hotkey sets (save as, load).
// Invariants: nothing changes in the overrides until the user (or resolveConflict) decides; at most one
//   pending assignment and one conflict dialog exist, and the dialog is open exactly while pending_ is
//   set; every change goes through assignChord / assignKeepingBoth / the overrides, so the registry
//   notifies once per change and all widgets refresh; set names are sanitised and bounded, set JSON is
//   produced and consumed only by exportOverrides / importOverrides (validated there).
// Callers: HotkeyEditor.cpp (key clicks, recorder, buttons), hosts, tests.
#include "r1ui/commands/OverrideIo.h"
#include "r1ui/commands/Text.h"
#include "r1ui/commands/keyboard/KeepBoth.h"
#include "r1ui/widgets/hotkeys/HotkeyEditor.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace r1ui::widgets {

namespace cmd = commands;

namespace {

std::string trimmed(std::string text) {
  const size_t first = text.find_first_not_of(' ');
  if (first == std::string::npos) return {};
  const size_t last = text.find_last_not_of(' ');
  return text.substr(first, last - first + 1);
}

std::string conflictSentence(const cmd::CommandRegistry& registry, const PendingAssignment& p) {
  const cmd::Conflict& first = p.conflicts.front();
  const cmd::CommandDef* other = registry.find(first.commandId);
  const std::string label = other != nullptr ? other->label : first.commandId;
  const std::string chord = cmd::formatSequence(p.chord);
  std::string text;
  switch (first.kind) {
    case cmd::ConflictKind::Equal: text = chord + " is already used by " + label; break;
    case cmd::ConflictKind::ExistingIsPrefix: text = chord + " cannot be used: " + cmd::formatSequence(first.existing) + " already runs " + label; break;
    case cmd::ConflictKind::NewIsPrefix: text = chord + " already starts the sequence " + cmd::formatSequence(first.existing) + " of " + label; break;
  }
  if (first.scope != cmd::ConflictScope::SameContext) text += " (" + first.context + ")";
  if (p.conflicts.size() > 1) text += " and " + std::to_string(p.conflicts.size() - 1) + " more";
  return text + ".";
}

}  // namespace

// ---- assigning ------------------------------------------------------------------------------------------------

AssignOutcome HotkeyEditor::assign(std::string_view commandId, int slot, const cmd::ChordSequence& chord) {
  const cmd::CommandDef* def = services_.registry.find(commandId);
  if (def == nullptr || slot < 0 || slot >= cmd::kSlotCount || chord.empty() || !chord.valid()) {
    say("That shortcut cannot be assigned");
    return AssignOutcome::Refused;
  }
  const std::optional<cmd::ChordSequence> current = services_.keymap.effective(commandId, slot);
  if (current && *current == chord) {
    say(cmd::formatSequence(chord) + " is already the " + slotName(slot) + " shortcut of " + def->label);
    return AssignOutcome::Unchanged;
  }
  const std::vector<cmd::Conflict> conflicts = cmd::findConflicts(services_.registry, services_.keymap, commandId, chord);
  if (conflicts.empty()) {
    const cmd::AssignResult result = cmd::assignChord(services_.overrides, services_.keymap, services_.registry, commandId, slot, chord, false);
    if (!result.ok) {
      say("Could not assign the shortcut: " + result.error);
      return AssignOutcome::Refused;
    }
    say("Assigned " + cmd::formatSequence(chord) + " to " + def->label + " (" + slotName(slot) + ")");
    return AssignOutcome::Assigned;
  }
  if (pending_) closeConflictDialog();
  PendingAssignment pending;
  pending.commandId = std::string(commandId);
  pending.slot = slot;
  pending.chord = chord;
  pending.conflicts = conflicts;
  pending.canKeepBoth = cmd::canKeepBoth(conflicts);
  pending_ = std::move(pending);
  openConflictDialog();
  return AssignOutcome::NeedsResolution;
}

void HotkeyEditor::openConflictDialog() {
  if (!pending_) return;
  const PendingAssignment& p = *pending_;
  DialogSpec spec;
  spec.title = "Shortcut conflict";
  spec.description = conflictSentence(services_.registry, p) + " Replace takes it away from the other command." +
                     (p.canKeepBoth ? " Keep both leaves both bound; the command of the more specific context wins." : "");
  spec.actions.push_back({"cancel", "Cancel", DialogActionKind::Neutral, true, true, true});
  if (p.canKeepBoth) spec.actions.push_back({"keepboth", "Keep both", DialogActionKind::Neutral, false, false, true});
  spec.actions.push_back({"replace", "Replace", DialogActionKind::Primary, false, false, true});
  spec.owner = id();
  spec.maxWidth = 460.0;
  spec.onResult = [context = &ui(), self = id()](const DialogResult& result) {
    HotkeyEditor* e = context->objectAs<HotkeyEditor>(self);
    if (e == nullptr || e->closingDialog_) return;
    e->conflictDialog_ = {};
    ConflictResolution choice = ConflictResolution::Cancel;
    if (!result.dismissed && result.action == "replace") choice = ConflictResolution::Replace;
    if (!result.dismissed && result.action == "keepboth") choice = ConflictResolution::KeepBoth;
    e->resolveConflict(choice);
  };
  conflictDialog_ = openDialog(ui(), std::move(spec));
  if (!conflictDialog_.valid()) {  // no overlay layer: the change cannot be confirmed, so nothing changes
    pending_.reset();
    say("The conflict could not be shown; nothing was changed");
  }
}

void HotkeyEditor::closeConflictDialog() {
  if (!conflictDialog_.valid()) return;
  const DialogHandle handle = conflictDialog_;
  conflictDialog_ = {};
  closingDialog_ = true;
  closeDialog(ui(), handle);
  closingDialog_ = false;
}

bool HotkeyEditor::conflictDialogOpen() { return conflictDialog_.valid() && isDialogOpen(ui(), conflictDialog_); }

bool HotkeyEditor::resolveConflict(ConflictResolution resolution) {
  if (!pending_) return false;
  if (resolution == ConflictResolution::KeepBoth && !pending_->canKeepBoth) return false;
  const PendingAssignment p = *pending_;
  pending_.reset();
  closeConflictDialog();
  const cmd::CommandDef* def = services_.registry.find(p.commandId);
  const std::string label = def != nullptr ? def->label : p.commandId;
  switch (resolution) {
    case ConflictResolution::Replace: {
      const cmd::AssignResult r = cmd::assignChord(services_.overrides, services_.keymap, services_.registry, p.commandId, p.slot, p.chord, true);
      say(r.ok ? "Assigned " + cmd::formatSequence(p.chord) + " to " + label + "; the other command lost it" : "Could not assign the shortcut: " + r.error);
      break;
    }
    case ConflictResolution::KeepBoth: {
      const cmd::AssignResult r = cmd::assignKeepingBoth(services_.overrides, services_.keymap, services_.registry, p.commandId, p.slot, p.chord);
      say(r.ok ? "Assigned " + cmd::formatSequence(p.chord) + " to " + label + "; both commands keep it" : "Could not assign the shortcut: " + r.error);
      break;
    }
    case ConflictResolution::Cancel: say("Assignment cancelled; nothing changed"); break;
  }
  return true;
}

bool HotkeyEditor::clearSlot(std::string_view commandId, int slot) {
  if (services_.registry.find(commandId) == nullptr || slot < 0 || slot >= cmd::kSlotCount) return false;
  const std::optional<cmd::ChordSequence> current = services_.keymap.effective(commandId, slot);
  if (!current || current->empty()) return false;
  const cmd::AssignResult r = cmd::assignChord(services_.overrides, services_.keymap, services_.registry, commandId, slot, std::nullopt, false);
  if (r.ok) say("Cleared the " + slotName(slot) + " shortcut");
  return r.ok;
}

bool HotkeyEditor::resetAction(std::string_view commandId) {
  if (services_.registry.find(commandId) == nullptr) return false;
  const bool changed = services_.overrides.resetCommand(commandId);
  say(changed ? "Shortcuts of the action returned to their defaults" : "The action already uses its default shortcuts");
  return changed;
}

void HotkeyEditor::requestResetAll() {
  if (resetAllDialogOpen()) return;
  DialogSpec spec;
  spec.title = "Reset all shortcuts";
  spec.description = "Every keyboard shortcut returns to its default. Your own shortcuts are removed.";
  spec.actions = {{"cancel", "Cancel", DialogActionKind::Neutral, true, true, true}, {"reset", "Reset all", DialogActionKind::Danger, false, false, true}};
  spec.owner = id();
  spec.maxWidth = 380.0;
  spec.onResult = [context = &ui(), self = id()](const DialogResult& result) {
    HotkeyEditor* e = context->objectAs<HotkeyEditor>(self);
    if (e == nullptr) return;
    e->resetDialog_ = {};
    if (result.action != "reset") return;
    e->services_.overrides.resetAll();
    e->say("All shortcuts returned to their defaults");
  };
  resetDialog_ = openDialog(ui(), std::move(spec));
}

bool HotkeyEditor::resetAllDialogOpen() { return resetDialog_.valid() && isDialogOpen(ui(), resetDialog_); }

// ---- hotkey sets -----------------------------------------------------------------------------------------------

bool HotkeyEditor::setNameValid(const std::string& name) const { return !name.empty() && (sets_.count(name) != 0 || sets_.size() < kMaxHotkeySets); }

std::vector<std::string> HotkeyEditor::setNames() const {
  std::vector<std::string> names;
  names.reserve(sets_.size());
  for (const auto& entry : sets_) names.push_back(entry.first);
  return names;
}

const std::string* HotkeyEditor::setJson(const std::string& name) const {
  const auto it = sets_.find(name);
  return it == sets_.end() ? nullptr : &it->second;
}

bool HotkeyEditor::saveSet(std::string name) {
  name = trimmed(cmd::sanitizeText(name, kMaxHotkeySetNameBytes));
  if (!setNameValid(name)) {
    say("The set needs a name (at most " + std::to_string(kMaxHotkeySets) + " sets)");
    return false;
  }
  std::string json = cmd::exportOverrides(services_.registry, services_.overrides);
  sets_[name] = json;
  setName_ = name;
  refreshSetSelect();
  say("Saved the hotkey set \"" + name + "\"");
  if (onSetsChanged_) {
    auto callback = onSetsChanged_;
    callback(name, json);
  }
  return true;
}

bool HotkeyEditor::addSet(std::string name, std::string json) {
  name = trimmed(cmd::sanitizeText(name, kMaxHotkeySetNameBytes));
  if (!setNameValid(name)) return false;
  if (json.size() > cmd::kMaxImportBytes) return false;  // the importer validates the content when the set is loaded
  sets_[name] = std::move(json);
  refreshSetSelect();
  return true;
}

bool HotkeyEditor::loadSet(const std::string& name) {
  const auto it = sets_.find(name);
  if (it == sets_.end()) {
    say("There is no hotkey set named \"" + name + "\"");
    return false;
  }
  const cmd::ImportReport report = cmd::importOverrides(it->second, services_.registry, services_.overrides, cmd::ImportMode::Replace);
  if (!report.ok) {
    say("The set \"" + name + "\" could not be loaded: " + report.error);
    refreshSetSelect();
    return false;
  }
  if (report.applied == 0 && report.issues.empty()) services_.overrides.resetAll();  // an empty set means the defaults
  setName_ = name;
  refreshSetSelect();
  say("Loaded the hotkey set \"" + name + "\"");
  return true;
}

void HotkeyEditor::openSaveSetDialog() {
  if (saveSetDialogOpen()) return;
  pendingSetName_.clear();
  DialogSpec spec;
  spec.title = "Save hotkey set";
  spec.description = "Name the set. A set with the same name is replaced.";
  spec.actions = {{"cancel", "Cancel", DialogActionKind::Neutral, false, true, true}, {"save", "Save", DialogActionKind::Primary, true, false, true}};
  spec.owner = id();
  spec.maxWidth = 380.0;
  spec.onResult = [context = &ui(), self = id()](const DialogResult& result) {
    HotkeyEditor* e = context->objectAs<HotkeyEditor>(self);
    if (e == nullptr) return;
    e->saveDialog_ = {};
    if (!result.dismissed && result.action == "save") e->saveSet(e->pendingSetName_);
  };
  saveDialog_ = openDialog(ui(), std::move(spec));
  if (!saveDialog_.valid()) return;
  TextInput& input = ui().create<TextInput>(saveDialog_.body);
  input.setPlaceholder("Set name");
  input.setMaxLength(kMaxHotkeySetNameBytes);
  input.setAccessibleName("Set name");
  input.setOnTextChanged([context = &ui(), self = id()](std::string_view text) {
    if (HotkeyEditor* e = context->objectAs<HotkeyEditor>(self)) e->pendingSetName_ = std::string(text);
  });
  ui().focusWidget(input.id(), core::events::FocusReason::Keyboard);
}

bool HotkeyEditor::saveSetDialogOpen() { return saveDialog_.valid() && isDialogOpen(ui(), saveDialog_); }

}  // namespace r1ui::widgets

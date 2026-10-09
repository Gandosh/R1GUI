// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the capture half of KeybindingEditor: the single-editing-box state machine (spec 07 rules
//   42 to 48, 53), live preview of held modifiers, two-step sequences, conflict evaluation and commit,
//   and the conflict popup (message, Override, Cancel).
// Invariants: at most one capture is active; `capture_.active` is cleared before any widget is told
//   to stop editing, so focus and popup callbacks that fire during teardown see an inactive capture;
//   the popup's close callback is ignored while the editor itself closes the popup (closingPopup_).
// Callers: ChordBox (through the box* entry points), KeybindingEditor.cpp.
#include <algorithm>

#include "LayoutBox.h"
#include "r1ui/widgets/button/Button.h"
#include "r1ui/widgets/commands/KeybindingEditor.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/tooltip/TooltipContent.h"
#include "r1ui/commands/Conflicts.h"

namespace r1ui::widgets {

namespace cmd = commands;
namespace layout = core::layout;
using core::events::Key;

namespace {

constexpr double kPopupTextWidth = 200.0;
constexpr size_t kPopupMaxLines = 5;

// "Ctrl+Shift+" for the modifiers currently held, in the order of spec 07 rule 28.
std::string modifierText(uint8_t modifiers) {
  std::string text;
  if ((modifiers & cmd::Mod::kCtrl) != 0) text += "Ctrl+";
  if ((modifiers & cmd::Mod::kMeta) != 0) text += "Cmd+";
  if ((modifiers & cmd::Mod::kAlt) != 0) text += "Alt+";
  if ((modifiers & cmd::Mod::kShift) != 0) text += "Shift+";
  return text;
}

}  // namespace

ChordBox* KeybindingEditor::captureBox() { return capture_.active ? box(capture_.commandId, capture_.slot) : nullptr; }

void KeybindingEditor::setBoxPreview(const std::string& text) {
  if (ChordBox* b = captureBox()) b->setPreview(text);
}

// ---- begin and end ------------------------------------------------------------------------------

void KeybindingEditor::boxClicked(ChordBox& box) { beginCapture(box); }

void KeybindingEditor::beginCapture(ChordBox& box) {
  endCapture();  // any other editing box stops and its conflict message is gone (rule 42)
  services_.router.cancelPending();
  capture_ = Capture{};
  capture_.active = true;
  capture_.commandId = box.commandId();
  capture_.slot = box.slot();
  box.setHint(hintOf(box.commandId(), box.slot()));
  box.setEditing(true);
  ui().focusWidget(box.id());
}

void KeybindingEditor::endCapture() {
  const std::string commandId = capture_.commandId;
  const int slot = capture_.slot;
  const bool wasActive = capture_.active;
  capture_ = Capture{};
  closeConflictPopup();
  if (!wasActive) return;
  if (ChordBox* b = box(commandId, slot)) b->setEditing(false);
}

void KeybindingEditor::cancelCapture() { endCapture(); }

// ---- keys ---------------------------------------------------------------------------------------

// Every key belongs to the capture while a box is editing (rule 44); only Escape without modifiers
// cancels (decision D15), the same key with a modifier is bound like any other.
void KeybindingEditor::boxCaptureKey(ChordBox& box, Event& e) {
  e.markHandled();
  if (!capture_.active || captureBox() != &box) return;
  const uint8_t modifiers = e.modifiers & cmd::kAllModifiers;
  const std::string head = capture_.first ? cmd::formatChord(*capture_.first) + ", " : std::string();
  if (e.key == Key::Unknown) {
    box.setPreview(head + modifierText(modifiers));  // held modifiers show at once
    return;
  }
  if (e.repeat) return;
  if (e.key == Key::Escape && modifiers == 0) {
    endCapture();
    return;
  }
  const cmd::KeyChord chord{e.key, modifiers, false};
  if (!chord.valid()) return;
  if (!twoStep_) {
    evaluate(cmd::ChordSequence::single(chord));
    return;
  }
  if (!capture_.first) {
    capture_.first = chord;
    box.setPreview(cmd::formatChord(chord) + ", ");
    return;
  }
  if (e.key == Key::Enter && modifiers == 0) {
    evaluate(cmd::ChordSequence::single(*capture_.first));  // Enter keeps the first chord alone
    return;
  }
  evaluate(cmd::ChordSequence::pair(*capture_.first, chord));
}

void KeybindingEditor::boxFocusLost(ChordBox& box) {
  // A capture with a conflict stays open while the popup is (rule 47); otherwise it ends and the old
  // chord stays (rule 53).
  if (capture_.active && captureBox() == &box && !capture_.conflict) endCapture();
}

void KeybindingEditor::boxRemove(ChordBox& box) {
  if (capture_.active && captureBox() == &box) endCapture();
  cmd::assignChord(services_.overrides, services_.keymap, services_.registry, box.commandId(), box.slot(), std::nullopt, false);
}

// ---- evaluate and commit ------------------------------------------------------------------------

void KeybindingEditor::evaluate(const cmd::ChordSequence& proposed) {
  setBoxPreview(cmd::formatSequence(proposed));
  const std::vector<cmd::Conflict> conflicts = cmd::findConflicts(services_.registry, services_.keymap, capture_.commandId, proposed);
  if (conflicts.empty()) {
    commit(proposed, false);
    return;
  }
  capture_.first.reset();  // the next press starts a new combination
  showConflict(proposed, conflicts);
}

void KeybindingEditor::commit(const cmd::ChordSequence& proposed, bool overrideConflicts) {
  if (!capture_.active) return;
  const std::string commandId = capture_.commandId;
  const int slot = capture_.slot;
  cmd::assignChord(services_.overrides, services_.keymap, services_.registry, commandId, slot, proposed, overrideConflicts);
  endCapture();
}

// ---- conflict popup -----------------------------------------------------------------------------

namespace {

std::string conflictText(const cmd::CommandRegistry& registry, const cmd::ChordSequence& proposed, const std::vector<cmd::Conflict>& conflicts) {
  const cmd::Conflict& first = conflicts.front();
  const cmd::CommandDef* other = registry.find(first.commandId);
  const std::string label = other != nullptr ? other->label : first.commandId;
  const std::string chord = cmd::formatSequence(proposed);
  std::string text;
  switch (first.kind) {
    case cmd::ConflictKind::Equal: text = chord + " is already used by " + label; break;
    case cmd::ConflictKind::ExistingIsPrefix: text = chord + " cannot be used: " + cmd::formatSequence(first.existing) + " already runs " + label; break;
    case cmd::ConflictKind::NewIsPrefix: text = chord + " already starts the sequence " + cmd::formatSequence(first.existing) + " of " + label; break;
  }
  if (first.scope != cmd::ConflictScope::SameContext) text += " (" + first.context + ")";
  if (conflicts.size() > 1) text += " and " + std::to_string(conflicts.size() - 1) + " more";
  return text + ".";
}

}  // namespace

void KeybindingEditor::showConflict(const cmd::ChordSequence& proposed, const std::vector<cmd::Conflict>& conflicts) {
  ChordBox* anchor = captureBox();
  if (anchor == nullptr) return;
  closeConflictPopup();
  capture_.conflict = true;
  capture_.proposed = proposed;
  capture_.message = conflictText(services_.registry, proposed, conflicts);

  PopoverOptions options;
  options.anchorWidget = anchor->id();
  options.owner = id();
  options.placement = Placement::BelowStart;
  options.gap = 4.0;
  options.padding = 10.0;
  options.width = kPopupTextWidth + 22.0;
  options.focusOnOpen = false;  // the box keeps the keyboard: typing another chord re-checks (rule 48)
  options.restoreFocus = false;
  options.onClosed = [context = &ui(), self = id()](DismissReason) {
    KeybindingEditor* e = context->objectAs<KeybindingEditor>(self);
    if (e != nullptr && !e->closingPopup_) e->popupDismissed();
  };
  popup_ = openPopover(ui(), options);
  if (!popup_.valid()) {
    endCapture();  // no overlay layer: the combination cannot be confirmed, so nothing changes
    return;
  }
  try {
    CommandsLayoutBox& column = layoutColumn(ui(), popup_.host, 8);
    for (const std::string& line : wrapTooltipText(ui(), capture_.message, 12.0, 400, kPopupTextWidth, kPopupMaxLines)) {
      ui().create<Label>(column.id(), line, LabelRole::Body);
    }
    CommandsLayoutBox& buttons = layoutRow(ui(), column.id(), 8);
    buttons.style().justifyContent = layout::Justify::End;
    Button& cancel = ui().create<Button>(buttons.id(), "Cancel", ButtonTone::Neutral, ButtonSize::Sm);
    Button& accept = ui().create<Button>(buttons.id(), "Override", ButtonTone::Accent, ButtonSize::Sm);
    accept.setTooltip("Take the shortcut away from the other command");
    cancel.setOnClick([context = &ui(), self = id()] {
      if (KeybindingEditor* e = context->objectAs<KeybindingEditor>(self)) e->endCapture();
    });
    accept.setOnClick([context = &ui(), self = id()] {
      KeybindingEditor* e = context->objectAs<KeybindingEditor>(self);
      if (e != nullptr && e->capture_.active && e->capture_.conflict) e->commit(e->capture_.proposed, true);
    });
    cancelButton_ = cancel.id();
    overrideButton_ = accept.id();
  } catch (const std::length_error&) {
    endCapture();  // the tree is full: refuse the change rather than show a popup without buttons
  }
}

void KeybindingEditor::closeConflictPopup() {
  if (!popup_.valid()) return;
  const PopoverHandle handle = popup_;
  popup_ = {};
  overrideButton_ = {};
  cancelButton_ = {};
  closingPopup_ = true;
  closePopover(ui(), handle);
  closingPopup_ = false;
}

// Closed by a click outside, by Escape or by the window: editing stops without a change (rule 47).
void KeybindingEditor::popupDismissed() {
  popup_ = {};
  overrideButton_ = {};
  cancelButton_ = {};
  if (capture_.active && capture_.conflict) endCapture();
}

bool KeybindingEditor::conflictPopupOpen() const { return popup_.valid(); }

}  // namespace r1ui::widgets

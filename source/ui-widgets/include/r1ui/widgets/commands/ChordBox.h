// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: ChordBox, the chord capture field of the keybinding editor: shows the chord of one slot of
//   one command, enters edit mode on click, shows greyed hint text or the live preview of what is
//   being pressed, and carries the small remove button.
// Why: spec 07 rules 42 to 54: a click makes the box the single editing box; while editing every key
//   (Escape and Tab included) belongs to the box and updates the displayed chord live; it is not a text
//   box (no typing, no paste); the remove button is hidden while editing or empty but keeps its space
//   and is not reachable by Tab, so a focused idle box also removes its binding with Delete or
//   Backspace; the border highlights on hover and shows the focus style (rule 51).
// Callers: KeybindingEditor creates one per slot and drives it; the box reports every user gesture
//   back to the editor, which owns the capture state machine (conflicts, commit, cancel). Calls:
//   KeybindingEditor (by id), IconButton (remove), FieldChrome (box drawing).
// Keys while idle and focused: Enter or Space starts the capture, Delete or Backspace removes the
//   binding, Up and Down move to the same slot of the neighbouring row. While editing, all keys go to
//   the editor's capture and are marked handled; key releases are swallowed.
// Look: rows chordbox (field look: `input` fill, `border` outline, `accent` outline when editing or
//   focused, `border-strong` on hover) and chordbox.hint (muted); Approximate: no reference exists
//   for this widget, the field look is reused.
#pragma once

#include <span>
#include <string>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class ChordBox final : public WidgetObject {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows();

  ChordBox(core::tree::WidgetId editor, std::string commandId, int slot) : editor_(editor), commandId_(std::move(commandId)), slot_(slot) {}

  const char* typeName() const override { return "ChordBox"; }
  void onAttached() override;
  float paintOpacity() const override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  Cursor cursor() const override { return Cursor::Pointer; }
  std::string_view accessibleName() const override;
  void onClick(Event& e) override;
  void onKeyDown(Event& e) override;
  void onKeyUp(Event& e) override;
  void onFocusOut(Event& e) override;

  const std::string& commandId() const { return commandId_; }
  int slot() const { return slot_; }

  // The chord in force (display text; empty = unbound). Shown while idle.
  void setChordText(std::string text);
  const std::string& chordText() const { return chordText_; }
  // Grey text shown while editing and nothing is pressed yet.
  void setHint(std::string hint);
  // Edit mode. Leaving it clears the preview.
  void setEditing(bool editing);
  bool editing() const { return editing_; }
  // Live text while editing (held modifiers, the chord typed so far); empty shows the hint.
  void setPreview(std::string preview);
  const std::string& preview() const { return preview_; }
  // The text drawn now (tests): the preview or hint while editing, else the chord.
  const std::string& shownText() const;
  bool placeholderShown() const { return editing_ && preview_.empty(); }
  core::tree::WidgetId removeButton() const { return remove_; }

 private:
  void updateRemoveButton();

  core::tree::WidgetId editor_;
  std::string commandId_;
  int slot_;
  std::string chordText_;
  std::string hint_;
  std::string preview_;
  bool editing_ = false;
  core::tree::WidgetId remove_;
};

}  // namespace r1ui::widgets

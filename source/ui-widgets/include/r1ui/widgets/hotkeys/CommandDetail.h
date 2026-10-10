// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: two small widgets of the "Runtime Command Editor" tab: CommandDetailView (a read-only sheet of
//   one command: id, label, description wrapped, category, context, default and current chords,
//   enabled state and the conflict list) and ChordRecorder (a focusable field that records the next
//   chord the user presses).
// Why: the tab shows everything the user needs to decide on a shortcut and lets them record one with
//   the keyboard instead of clicking a drawn key; both are plain widgets the HotkeyEditor composes with
//   a slot selector and the Assign, Clear and Reset buttons.
// Callers: HotkeyEditor, tests. Calls: Chord.h, wrapTooltipText (description lines).
// Recorder protocol (same as the shortcut editor's capture, spec 07 rules 42 to 47): while recording
//   every key belongs to the recorder; held modifiers show live; a real key completes the chord;
//   plain Escape cancels (Escape with a modifier records it); releases and typed characters are
//   swallowed; losing focus cancels. Nothing is changed by the recorder itself: it reports the chord.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "r1ui/commands/Chord.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

struct CommandDetail {
  bool valid = false;  // false: nothing selected
  std::string id;
  std::string label;
  std::string description;
  std::string category;
  std::string context;
  std::string kind;
  std::string defaults;  // "Ctrl+Z, Alt+U" or "none"
  std::string current;
  bool enabled = true;
  std::vector<std::string> conflicts;  // one line each; empty = none
};

class CommandDetailView final : public WidgetObject {
 public:
  const char* typeName() const override { return "CommandDetailView"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;

  void setDetail(CommandDetail detail);
  const CommandDetail& detail() const { return detail_; }

 private:
  CommandDetail detail_;
};

class ChordRecorder final : public WidgetObject {
 public:
  const char* typeName() const override { return "ChordRecorder"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  void onClick(Event& e) override;
  void onKeyDown(Event& e) override;
  void onKeyUp(Event& e) override;
  void onFocusOut(Event& e) override;
  bool wantsTextInput() const override { return recording_; }
  void onTextInput(Event& e) override;

  void begin();
  void cancel();
  bool recording() const { return recording_; }
  // The text shown while idle (the last recorded or current chord).
  void setIdleText(std::string text);
  const std::string& preview() const { return preview_; }
  void setOnChord(std::function<void(const commands::KeyChord&)> callback) { onChord_ = std::move(callback); }
  void setOnCancelled(std::function<void()> callback) { onCancelled_ = std::move(callback); }

 private:
  void end();

  bool recording_ = false;
  std::string preview_;
  std::string idle_;
  std::function<void(const commands::KeyChord&)> onChord_;
  std::function<void()> onCancelled_;
};

// "Ctrl+Alt+" style text of a modifier set (empty for none), the order the rest of the toolkit uses.
std::string modifierPrefix(uint8_t modifiers);

}  // namespace r1ui::widgets

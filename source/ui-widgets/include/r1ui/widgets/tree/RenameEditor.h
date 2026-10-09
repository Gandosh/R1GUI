// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: RenameEditor, the inline rename input of a tree row: a one-line text field (measured: 18 px
//   high inside a 26 px row, 1 px `accent` border, `input` background, 4 px radius, 12 px text, 4 px
//   side padding, all text selected when it opens) driven by the shared ui-text TextEditor.
// Why: renaming happens inside TreeView (a row is not a widget), so the input is a helper object
//   that the tree feeds keys, text and pointer positions and asks to paint; the text editing rules
//   (grapheme-safe caret, selection, undo, clipboard, sanitising of pasted text) are the TextEditor's.
// Callers: TreeView only. Calls: ui-text TextEditor, TextEngine (measurement and drawing).
// Keys (spec 01 rule 25 and spec 08 rules 77-80): Enter commits, Escape cancels, arrows / Home / End /
//   Backspace / Delete edit, Ctrl+A selects all, Ctrl+C / X / V use the host clipboard, Ctrl+Z / Y undo
//   and redo; everything else is ignored so the tree can still see it. The owner decides what a commit
//   means (it may refuse with setError, which keeps the field open and draws the border in `danger`).
// Invariants: the buffer is valid UTF-8 without control characters and at most kMaxBytes (the editor
//   sanitises every insertion); caret and selection stay on grapheme boundaries.
#pragma once

#include <string>
#include <string_view>

#include "r1ui/core/events/Event.h"
#include "r1ui/core/layout/Geometry.h"
#include "r1ui/text/TextEditor.h"
#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/tree/TreeModel.h"

namespace r1ui::widgets {

class UiContext;

class RenameEditor {
 public:
  static constexpr size_t kMaxBytes = 1024;
  static constexpr double kFieldHeight = 18.0;
  static constexpr double kPadX = 4.0;

  RenameEditor() : editor_(text::EditorConfig{kMaxBytes, false}) {}

  enum class Outcome : uint8_t { Ignored, Handled, Commit, Cancel };

  bool active() const { return active_; }
  NodeId node() const { return node_; }
  const std::string& text() const { return editor_.text(); }
  const std::string& error() const { return error_; }
  void setError(std::string message);

  // Opens the editor on `initial` with everything selected (the clipboard comes from the context).
  void begin(UiContext& ui, NodeId node, std::string_view initial);
  void end();

  Outcome keyDown(core::events::Key key, uint8_t modifiers);
  // A typed character; false when the editor is closed or the character was rejected.
  bool textInput(char32_t codePoint);
  // Pointer interaction in window logical coordinates; `field` is the field's rectangle.
  void pointerDown(UiContext& ui, const core::layout::Rect& field, double x, bool shift, uint32_t clickCount);
  void pointerDrag(UiContext& ui, const core::layout::Rect& field, double x);
  bool contains(const core::layout::Rect& field, double x, double y) const;

  void paint(PaintContext& ctx, const core::layout::Rect& field);

 private:
  bool layoutFor(UiContext& ui, float pixelSize);
  size_t offsetAt(UiContext& ui, const core::layout::Rect& field, double x);

  text::TextEditor editor_;
  bool active_ = false;
  NodeId node_ = kTreeRoot;
  std::string error_;
};

}  // namespace r1ui::widgets

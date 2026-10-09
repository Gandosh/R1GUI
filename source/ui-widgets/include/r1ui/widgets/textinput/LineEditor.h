// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: LineEditor, the editing and drawing core shared by every single-line text surface of
//   ui-widgets (TextInput, the edit mode of NumberField, the filter row of Select): a text::TextEditor
//   plus what a widget needs around it: key and character handling with the editing bindings of
//   spec 01 (rules 25-29), clipboard access through the window host, a grapheme limit, hit testing
//   from pointer coordinates, horizontal scrolling, caret blink timing and the painting of selection,
//   text, preedit underline and caret.
// Why: the three widgets differ in chrome and in what Enter and Escape mean, not in how text is
//   edited; one tested core keeps selection, caret, undo and clipboard behaviour identical.
// Callers: TextInput, NumberField, Select. Calls: text::TextEditor (model), TextEngine (shaping and
//   drawing through PaintContext), UiContext::host() (clipboard).
// Units: everything in this class is PHYSICAL pixels (the shaper works at physical sizes); the owning
//   widget converts from logical pixels with the display scale before calling in.
// Invariants: text() is valid single-line UTF-8 (TextEditor sanitises every entry path, this class
//   sanitises first when it must count characters); the layout is current whenever caretX, hitTest
//   or paint are called (ensureLayout is called by them); a key is reported handled when the
//   editor used it even if nothing changed (a Left press at the start of the text), so it never
//   leaks to shortcuts (spec 01 rule 26); Ctrl/Alt chords the editor does not own travel outward.
// Failure behavior: clipboard callbacks that throw or are missing make copy/cut/paste report false and
//   leave text and selection untouched; a text too large to shape draws nothing but stays editable.
// Windowing: text above kWindowBytes is drawn as a window around the visible part so a 1 MiB value
//   costs one shaping per edit instead of a million glyph quads per frame.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "r1ui/core/events/Event.h"
#include "r1ui/render/Painter.h"
#include "r1ui/text/TextEditor.h"
#include "r1ui/widgets/runtime/PaintContext.h"

namespace r1ui::widgets {

class UiContext;
class TextEngine;

// What one editing call did, so the owner can notify and repaint.
struct LineEdit {
  bool handled = false;
  bool textChanged = false;
  bool caretMoved = false;  // caret or selection changed
};

// Where and how LineEditor::paint draws (physical pixels).
struct LineEditorPaint {
  render::Rect content;     // clip rectangle; the text origin is content.x - scrollX()
  float lineTop = 0.0f;     // top of the line box
  float lineHeight = 0.0f;
  float pixelSize = 12.0f;
  int weight = 400;
  render::Color text;
  render::Color selectionBackground;
  render::Color selectionText;
  render::Color caret;
  bool showSelection = true;
  bool showCaret = true;
};

class LineEditor {
 public:
  // Text above this many bytes is drawn as a window.
  static constexpr size_t kWindowBytes = 8192;
  static constexpr uint64_t kBlinkMs = 530;

  explicit LineEditor(const text::EditorConfig& config = {});
  LineEditor(const LineEditor&) = delete;
  LineEditor& operator=(const LineEditor&) = delete;

  // Connects copy/cut/paste to the window clipboard of `ui` (must outlive this object).
  void bind(UiContext& ui);

  text::TextEditor& model() { return editor_; }
  const text::TextEditor& model() const { return editor_; }
  const std::string& text() const { return editor_.text(); }

  // 0 = unlimited. Counted in grapheme clusters; applies to typing, pasting and composition.
  void setMaxChars(size_t chars) { maxChars_ = chars; }
  size_t maxChars() const { return maxChars_; }

  // ---- editing ------------------------------------------------------------------------------
  // Replaces everything (programmatic path: sanitised, cut to the grapheme limit, caret at the end,
  // history cleared, read-only does not block it).
  void setText(std::string_view utf8);
  LineEdit handleKeyDown(const core::events::Event& event);
  LineEdit handleChar(char32_t codePoint, uint8_t modifiers);
  // Inserts text the way typing does (grapheme limit applied); returns the bytes inserted.
  size_t insert(std::string_view utf8, text::InsertSource source = text::InsertSource::Typing);
  bool paste();

  // ---- pointer (x relative to the content left edge, physical) ---------------------------------
  LineEdit pointerPress(float x, int clickCount, bool extend);
  LineEdit pointerDrag(float x);

  // ---- view -------------------------------------------------------------------------------------
  // Shapes the display text at `pixelSize` when the layout is stale. Returns false when shaping failed.
  bool ensureLayout(float pixelSize);
  float pixelSize() const { return pixelSize_; }
  // Scrolls so the caret lies inside a field `contentWidth` wide.
  void scrollCaretIntoView(float contentWidth);
  float scrollX() const { return editor_.scrollX(); }
  // x of the caret relative to the content left edge (scroll applied); nullopt without a layout.
  bool caretOffsetX(float& x) const;

  // ---- caret blink --------------------------------------------------------------------------------
  void noteActivity(uint64_t nowMs) { activityMs_ = nowMs; }
  bool caretPhaseOn(uint64_t nowMs, bool blink) const;

  // ---- painting -----------------------------------------------------------------------------------
  void paint(PaintContext& ctx, const LineEditorPaint& params);

  // Mirrors the TextEditor entry policy (invalid UTF-8 repaired, C0/C1 controls, DEL, U+2028/9 dropped).
  static std::string sanitizeLine(std::string_view utf8);
  // Number of grapheme clusters of valid UTF-8.
  static size_t graphemeCount(std::string_view utf8);

 private:
  struct Window {
    size_t begin = 0;
    size_t end = 0;
    float penX = 0.0f;  // x of `begin` relative to the text origin
  };
  size_t fitToLimit(std::string& clean) const;
  size_t toDisplay(size_t textOffset) const;
  Window windowFor(TextEngine& engine, float contentWidth) const;
  float xOfDisplayOffset(TextEngine& engine, size_t displayOffset) const;

  text::TextEditor editor_;
  UiContext* ui_ = nullptr;
  size_t maxChars_ = 0;
  float pixelSize_ = 0.0f;
  uint64_t activityMs_ = 0;
};

}  // namespace r1ui::widgets

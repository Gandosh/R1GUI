// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the headless model of a single-line text field: UTF-8 buffer, caret/selection, editing
//   operations, clipboard hooks, undo/redo, IME composition state, caret geometry from a shaped
//   layout, and horizontal scrolling.
// Why: widgets (text input, number field, inline rename) share one tested editing core; they
//   translate OS events into these calls and draw from the state it reports.
// Callers: ui-widgets text controls (later), tests. It calls Grapheme, Utf8, Shaper, CaretMap.
// Buffer rules:
//   - text() is always valid UTF-8. Everything entering the buffer (typing, paste, setText,
//     preedit) is sanitized: invalid UTF-8 becomes U+FFFD; C0/C1 control characters (including
//     NUL, TAB, CR, LF), DEL, U+2028 and U+2029 are dropped, since the field is single-line.
//   - The buffer never exceeds maxBytes (default kDefaultMaxBytes = 1 MiB, hard cap
//     kMaxAllowedBytes = 16 MiB). An insertion that would exceed it is cut at the last grapheme
//     boundary that fits; insertText reports how many bytes were taken. setText truncates the
//     same way. Lowering maxBytes below the current size never cuts existing text; it only
//     blocks growth.
// Offsets: caret and anchor are UTF-8 byte offsets in text(), ALWAYS on grapheme boundaries and
//   within [0, size]. After any edit that would leave one inside a cluster (for example inserting
//   a base letter before an existing combining mark) the offset moves forward to the end of that
//   cluster. selection() is normalized (begin <= end) regardless of drag direction.
// Modes: readOnly blocks insertion, deletion, cut, paste, undo/redo and composition, while
//   caret movement, selection and copy keep working. setText is the owner's programmatic path and
//   works in read-only mode (it also resets history and composition).
// Undo: consecutive typing, consecutive Backspace and consecutive Delete each form one step as
//   long as the caret was not moved in between; cut, paste, word deletion, selection deletion and
//   breakUndoGroup() start a new step. History is bounded (1000 steps / 8 MiB).
// Composition (IME): setPreedit shows uncommitted text at the caret without changing text();
//   commitPreedit inserts it as typing; cancelPreedit drops it. displayText() is text() with the
//   preedit spliced in at the caret and is what layout/rendering must use; preeditRange() says
//   where it sits so the renderer can underline it. Any other editing or movement call cancels an
//   active composition first. A selection present when composition starts is deleted (undoable).
// Layout: relayout() shapes displayText(); caretX(), selectionX(), hitTest() and scroll queries
//   are available while the layout is current (text, preedit and size unchanged since).
// Threading: UI thread only. Callbacks run synchronously, never while internal state is half
//   updated.
// Not covered: bidirectional caret movement (logical arrow keys on RTL text), word-wise drag
//   extension after a double click, and spell-check/autocomplete.
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "r1ui/text/CaretMap.h"
#include "r1ui/text/Font.h"
#include "r1ui/text/Result.h"

namespace r1ui::text {

inline constexpr std::size_t kDefaultMaxBytes = std::size_t{1} * 1024 * 1024;
inline constexpr std::size_t kMaxAllowedBytes = std::size_t{16} * 1024 * 1024;

struct EditorConfig {
  std::size_t maxBytes = kDefaultMaxBytes;  // clamped to 1..kMaxAllowedBytes
  bool readOnly = false;
};

struct TextRange {
  std::size_t begin = 0;
  std::size_t end = 0;
  bool empty() const { return begin == end; }
  friend bool operator==(const TextRange&, const TextRange&) = default;
};

enum class Motion { Left, Right, WordLeft, WordRight, LineStart, LineEnd };

enum class InsertSource { Typing, Paste };

// The owner connects these to the OS clipboard; the editor has no OS dependency. Either may be
// empty (then copy/cut/paste report false).
struct ClipboardCallbacks {
  std::function<void(std::string_view utf8)> write;
  std::function<std::optional<std::string>()> read;
};

class TextEditor {
 public:
  explicit TextEditor(const EditorConfig& config = {});
  ~TextEditor();
  TextEditor(TextEditor&&) noexcept;
  TextEditor& operator=(TextEditor&&) noexcept;
  TextEditor(const TextEditor&) = delete;
  TextEditor& operator=(const TextEditor&) = delete;

  // ---- State ----------------------------------------------------------------------------
  const std::string& text() const;
  std::size_t caret() const;
  std::size_t anchor() const;
  TextRange selection() const;
  bool hasSelection() const;
  std::size_t revision() const;  // increments on every change of text()
  bool readOnly() const;
  void setReadOnly(bool value);
  std::size_t maxBytes() const;
  void setMaxBytes(std::size_t bytes);

  // ---- Programmatic content -------------------------------------------------------------
  // Replaces everything, puts the caret at the end, clears history and composition.
  void setText(std::string_view utf8);

  // ---- Editing --------------------------------------------------------------------------
  // Replaces the selection (if any) with the sanitized text. Returns the bytes inserted, 0 when
  // blocked (read-only, full, or nothing left after sanitizing).
  std::size_t insertText(std::string_view utf8, InsertSource source = InsertSource::Typing);
  bool deleteBackward();
  bool deleteForward();
  bool deleteWordBackward();
  bool deleteWordForward();

  // ---- Caret and selection --------------------------------------------------------------
  // Without `extend` a non-empty selection collapses to its edge for Left/Right and to the
  // target for the other motions; with `extend` the anchor stays.
  void move(Motion motion, bool extend);
  void selectAll();
  // Selects the word, punctuation run or whitespace run at `offset`.
  void selectWordAt(std::size_t offset);
  // Pointer press at a text offset (from hitTest): 1 click places the caret (extend = Shift),
  // 2 selects the word, 3 or more select all. Dragging calls this with clickCount 1, extend true.
  void pointerPress(std::size_t offset, int clickCount, bool extend);
  // Offsets inside a cluster are moved to the nearest boundary (ties go forward).
  void setSelection(std::size_t anchor, std::size_t caret);

  // ---- Clipboard ------------------------------------------------------------------------
  void setClipboard(ClipboardCallbacks callbacks);
  bool copy();
  bool cut();
  bool paste();

  // ---- Undo -----------------------------------------------------------------------------
  bool undo();
  bool redo();
  bool canUndo() const;
  bool canRedo() const;
  void breakUndoGroup();

  // ---- Composition (IME) ----------------------------------------------------------------
  // `cursorInPreedit` is a byte offset into the sanitized preedit (clamped to a boundary).
  // An empty preedit cancels the composition. Returns false when blocked (read-only).
  bool setPreedit(std::string_view utf8, std::size_t cursorInPreedit);
  bool commitPreedit();
  void cancelPreedit();
  bool composing() const;
  // text() with the preedit spliced in at the caret.
  const std::string& displayText() const;
  // Where the preedit sits inside displayText(); nullopt when not composing.
  std::optional<TextRange> preeditRange() const;
  // The caret position inside displayText() (inside the preedit while composing).
  std::size_t displayCaret() const;

  // ---- Layout, caret geometry and scrolling ---------------------------------------------
  // Shapes displayText() at the given size. Errors are those of shapeText; on error the previous
  // layout is dropped (layoutCurrent() false).
  Status relayout(const Font& font, float pixelSize, const ShapeOptions& options = {});
  bool layoutCurrent() const;
  float textWidth() const;  // 0 when no current layout

  // x of the caret / selection edges in pixels from the start of the text (before scrolling).
  std::optional<float> caretX() const;
  std::optional<std::pair<float, float>> selectionX() const;

  // Text offset (a grapheme boundary of text()) closest to `x`, where `x` is in text
  // coordinates (field-relative x plus scrollX()). While composing, a hit inside the preedit
  // maps to the caret offset. Without a current layout returns caret().
  std::size_t hitTest(float x) const;

  float scrollX() const;
  // Adjusts and returns the horizontal scroll so the caret lies within a field `fieldWidth`
  // pixels wide, with `margin` pixels of slack at each side; the scroll stays within
  // [0, max(0, textWidth + margin - fieldWidth)]. Invalid widths leave the scroll unchanged.
  float ensureCaretVisible(float fieldWidth, float margin = 1.0f);

 private:
  struct State;
  std::unique_ptr<State> s_;
};

}  // namespace r1ui::text

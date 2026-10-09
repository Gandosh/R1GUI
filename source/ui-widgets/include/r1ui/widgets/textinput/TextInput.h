// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: TextInput, the single-line text input (docs/spec/widgets.md 2.3): tones Default (bg `input`,
//   1 px `border`, focus border `accent`) and Panel (properties-panel field), sizes Sm (11 px) and Md
//   (12 px), states idle, hover, focus, filled, mixed, bound, invalid, disabled and read-only, with
//   placeholder, grapheme limit, optional clear button, horizontal scrolling, caret blink, mouse and
//   keyboard selection, clipboard, undo, IME preedit hooks and the commit / cancel protocol.
// Why: every dialog, search row and form needs a text field; the editing core is LineEditor
//   (shared with NumberField and Select), this class is the widget around it: chrome, focus and
//   pointer behaviour, callbacks.
// Callers: application code, tests, GalleryFields. Calls: LineEditor, FieldChrome (rows, box),
//   UiContext (focus, capture, frames).
// Commit protocol: onTextChanged fires on every edit that changes the text (typing, paste, cut,
//   undo, clear, Escape revert), never for setText(). onCommitted fires on Enter (always) and on blur
//   (only when the text differs from the value at the last commit or focus), with the final text.
//   Escape follows spec 01 rule 29: it first ends a composition, then clears a selection, then restores
//   the text the field had when it got focus; when none applies the key travels outward (closing a
//   popup). Callbacks may destroy the widget; the code re-checks liveness after each one.
// Focus: arrives by Tab or program -> all text selected; by pointer -> caret at the press. The focus
//   indication is the field border (kFocus), not a ring. Disabling a focused field blurs it, which
//   commits a changed text.
// Caret blink: while focused the widget keeps frames coming (wantsContinuousFrames) when the host
//   declared a running frame loop; tests and offscreen renders show a steady caret.
// IME: the platform layer has no composition events yet; setPreedit / commitPreedit / cancelPreedit
//   and caretRect() are the hooks a host connects to its input method.
// Units: logical pixels in the API; the editor works in physical pixels internally.
#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>

#include "r1ui/widgets/runtime/WidgetObject.h"
#include "r1ui/widgets/textinput/LineEditor.h"

namespace r1ui::widgets {

enum class TextInputTone : uint8_t { Default, Panel };
enum class TextInputSize : uint8_t { Sm, Md };

class TextInput : public WidgetObject {
 public:
  using TextCallback = std::function<void(std::string_view)>;

  explicit TextInput(TextInputTone tone = TextInputTone::Default, TextInputSize size = TextInputSize::Md);

  static std::span<const theme::StyleRuleEntry> styleRows();
  // The style row this tone and size resolve (for tests).
  static const char* styleKeyFor(TextInputTone tone, TextInputSize size);

  const char* typeName() const override { return "TextInput"; }

  // ---- content ----
  const std::string& text() const { return lineEditor_.text(); }
  // Programmatic value: sanitised, cut to the limit, becomes the committed value; no callbacks.
  void setText(std::string text);
  void setPlaceholder(std::string placeholder);
  const std::string& placeholder() const { return placeholder_; }
  // Maximum number of characters (grapheme clusters); 0 = unlimited. Existing longer text is kept.
  void setMaxLength(size_t characters);
  size_t maxLength() const { return lineEditor_.maxChars(); }
  void setReadOnly(bool readOnly);
  bool readOnly() const { return readOnly_; }
  // Shows a clear glyph at the right while the field has text; clicking it empties the field.
  void setClearable(bool clearable);

  // ---- selection ----
  void selectAll();
  void setSelection(size_t anchor, size_t caret);
  text::TextRange selection() const { return lineEditor_.model().selection(); }
  size_t caretOffset() const { return lineEditor_.model().caret(); }

  // ---- input method hooks (spec: IME preedit display) ----
  bool setPreedit(std::string_view utf8, size_t cursorInPreedit);
  bool commitPreedit();
  void cancelPreedit();
  bool composing() const { return lineEditor_.model().composing(); }
  // Caret rectangle in window logical coordinates (for the candidate window); empty before layout.
  core::layout::Rect caretRect();

  // ---- callbacks ----
  void setOnTextChanged(TextCallback callback) { onTextChanged_ = std::move(callback); }
  void setOnCommitted(TextCallback callback) { onCommitted_ = std::move(callback); }

  // ---- WidgetObject ----
  void onAttached() override;
  float paintOpacity() const override;
  bool wantsContinuousFrames() const override { return focused(); }
  void paint(PaintContext& ctx) override;
  Cursor cursor() const override;
  std::string_view accessibleName() const override;
  void onStateChanged(uint16_t previous) override;
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;
  void onTextInput(Event& e) override;
  void onFocusIn(Event& e) override;
  void onFocusOut(Event& e) override;

 private:
  struct Geometry {
    double border = 0.0;
    double contentLeft = 0.0;   // logical window coordinates
    double contentRight = 0.0;  // text area right edge (before the clear glyph)
    double lineTop = 0.0;
    double lineHeight = 0.0;
    core::layout::Rect clear;   // clear glyph hit area; empty when not shown
  };

  const char* styleKey() const { return styleKeyFor(tone_, size_); }
  Geometry geometry() const;
  bool clearVisible() const { return clearable_ && !text().empty() && enabled(); }
  // Applies the outcome of an editing call: notifications, caret visibility, repaint.
  void apply(const LineEdit& edit);
  bool notify(const TextCallback& callback);
  void commitIfChanged();
  void clearText();
  void showCaretNow();
  // Scrolls the text so the caret is visible, when an edit moved it since the last time.
  void settleScroll();

  TextInputTone tone_;
  TextInputSize size_;
  LineEditor lineEditor_;
  std::string placeholder_;
  std::string committed_;
  bool readOnly_ = false;
  bool clearable_ = false;
  bool dragging_ = false;
  bool clearPressed_ = false;
  bool clearHover_ = false;
  bool scrollPending_ = false;
  TextCallback onTextChanged_;
  TextCallback onCommitted_;
};

}  // namespace r1ui::widgets

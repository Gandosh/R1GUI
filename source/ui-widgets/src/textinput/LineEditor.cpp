// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of LineEditor.h (key bindings, clipboard glue, limit, layout, painting).
// Invariants: every public entry that can change the model reports it through LineEdit; paint never
//   mutates the model (it may re-shape the layout, which is derived state).
// Callers: TextInput, NumberField, Select, tests.
#include "r1ui/widgets/textinput/LineEditor.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

#include "r1ui/text/Grapheme.h"
#include "r1ui/text/Utf8.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

using core::events::Key;
namespace Mod = core::events::Mod;

// The state that tells whether an editing call changed anything.
struct Snapshot {
  size_t revision;
  size_t caret;
  size_t anchor;
};

Snapshot snapshotOf(const text::TextEditor& e) { return {e.revision(), e.caret(), e.anchor()}; }

LineEdit diff(const text::TextEditor& e, const Snapshot& before, bool handled) {
  LineEdit r;
  r.handled = handled;
  r.textChanged = e.revision() != before.revision;
  r.caretMoved = r.textChanged || e.caret() != before.caret || e.anchor() != before.anchor;
  return r;
}

bool isLetter(Key k) { return k >= Key::A && static_cast<uint16_t>(k) <= static_cast<uint16_t>('Z'); }
bool isDigit(Key k) { return k >= Key::Digit0 && static_cast<uint16_t>(k) <= static_cast<uint16_t>('9'); }

}  // namespace

LineEditor::LineEditor(const text::EditorConfig& config) : editor_(config) {}

void LineEditor::bind(UiContext& ui) {
  ui_ = &ui;
  text::ClipboardCallbacks callbacks;
  callbacks.write = [u = &ui](std::string_view utf8) {
    if (!u->host().writeClipboard) return false;
    try {
      u->host().writeClipboard(utf8);
      return true;
    } catch (const std::exception&) {
      return false;
    }
  };
  callbacks.read = [u = &ui]() -> std::optional<std::string> {
    if (!u->host().readClipboard) return std::nullopt;
    try {
      return u->host().readClipboard();
    } catch (const std::exception&) {
      return std::nullopt;
    }
  };
  editor_.setClipboard(std::move(callbacks));
}

// ---- text policy ------------------------------------------------------------------------------

std::string LineEditor::sanitizeLine(std::string_view utf8) {
  const std::string valid = text::sanitizeUtf8(utf8);
  std::string out;
  out.reserve(valid.size());
  for (size_t pos = 0; pos < valid.size();) {
    const text::DecodedCodePoint cp = text::decodeUtf8(valid, pos);
    const bool control = cp.codePoint < 0x20 || (cp.codePoint >= 0x7F && cp.codePoint <= 0x9F) || cp.codePoint == 0x2028 || cp.codePoint == 0x2029;
    if (!control) out.append(valid, pos, cp.length);
    pos += cp.length;
  }
  return out;
}

size_t LineEditor::graphemeCount(std::string_view utf8) {
  size_t count = 0;
  for (size_t pos = 0; pos < utf8.size(); pos = text::nextGraphemeBoundary(utf8, pos)) ++count;
  return count;
}

namespace {

// Keeps the first `count` grapheme clusters of `s`.
void keepGraphemes(std::string& s, size_t count) {
  size_t pos = 0;
  while (count > 0 && pos < s.size()) {
    pos = text::nextGraphemeBoundary(s, pos);
    --count;
  }
  s.resize(pos);
}

}  // namespace

size_t LineEditor::fitToLimit(std::string& clean) const {
  if (maxChars_ == 0) return clean.size();
  const std::string& current = editor_.text();
  const text::TextRange sel = editor_.selection();
  const size_t total = graphemeCount(current);
  const size_t selected = graphemeCount(std::string_view(current).substr(sel.begin, sel.end - sel.begin));
  const size_t kept = total - std::min(total, selected);
  keepGraphemes(clean, kept >= maxChars_ ? 0 : maxChars_ - kept);
  return clean.size();
}

void LineEditor::setText(std::string_view utf8) {
  std::string clean = sanitizeLine(utf8);
  if (maxChars_ != 0) keepGraphemes(clean, maxChars_);
  editor_.setText(clean);
}

size_t LineEditor::insert(std::string_view utf8, text::InsertSource source) {
  std::string clean = sanitizeLine(utf8);
  fitToLimit(clean);
  if (clean.empty()) return 0;
  return editor_.insertText(clean, source);
}

bool LineEditor::paste() {
  if (editor_.readOnly() || ui_ == nullptr || !ui_->host().readClipboard) return false;
  std::optional<std::string> clip;
  try {
    clip = ui_->host().readClipboard();
  } catch (const std::exception&) {
    return false;
  }
  if (!clip) return false;
  return insert(*clip, text::InsertSource::Paste) > 0;
}

// ---- keyboard -----------------------------------------------------------------------------------

LineEdit LineEditor::handleKeyDown(const core::events::Event& event) {
  const Snapshot before = snapshotOf(editor_);
  const uint8_t mods = event.modifiers;
  const bool ctrl = (mods & Mod::kCtrl) != 0;
  const bool shift = (mods & Mod::kShift) != 0;
  const bool alt = (mods & Mod::kAlt) != 0;
  const bool meta = (mods & Mod::kMeta) != 0;
  bool handled = true;
  if (meta) return diff(editor_, before, false);
  switch (event.key) {
    case Key::Left: editor_.move(ctrl ? text::Motion::WordLeft : text::Motion::Left, shift); break;
    case Key::Right: editor_.move(ctrl ? text::Motion::WordRight : text::Motion::Right, shift); break;
    case Key::Home: editor_.move(text::Motion::LineStart, shift); break;
    case Key::End: editor_.move(text::Motion::LineEnd, shift); break;
    case Key::Backspace: ctrl ? editor_.deleteWordBackward() : editor_.deleteBackward(); break;
    case Key::Delete:
      if (shift && !ctrl) editor_.cut();
      else if (ctrl) editor_.deleteWordForward();
      else editor_.deleteForward();
      break;
    case Key::Insert:
      if (ctrl) editor_.copy();
      else if (shift) paste();
      else handled = false;
      break;
    default:
      if (ctrl && !alt && isLetter(event.key)) {
        switch (static_cast<char>(static_cast<uint16_t>(event.key))) {  // letters use their ASCII codes
          case 'A': editor_.selectAll(); break;
          case 'C': editor_.copy(); break;
          case 'X': editor_.cut(); break;
          case 'V': paste(); break;
          case 'Z': shift ? editor_.redo() : editor_.undo(); break;
          case 'Y': editor_.redo(); break;
          default: handled = false; break;  // Ctrl+S and friends belong to the application
        }
      } else if ((!ctrl || alt) && (isLetter(event.key) || isDigit(event.key) || event.key == Key::Space)) {
        handled = true;  // the character arrives as text input; the key must not reach shortcuts (rules 26, 28)
      } else {
        handled = false;
      }
      break;
  }
  return diff(editor_, before, handled);
}

LineEdit LineEditor::handleChar(char32_t codePoint, uint8_t modifiers) {
  const Snapshot before = snapshotOf(editor_);
  const bool ctrl = (modifiers & Mod::kCtrl) != 0;
  const bool alt = (modifiers & Mod::kAlt) != 0;
  const bool meta = (modifiers & Mod::kMeta) != 0;
  if (meta || (ctrl && !alt)) return diff(editor_, before, false);  // a chord, not text (AltGr arrives as Ctrl+Alt)
  std::string utf8;
  text::appendUtf8(utf8, codePoint);
  insert(utf8, text::InsertSource::Typing);
  return diff(editor_, before, true);
}

// ---- pointer ------------------------------------------------------------------------------------

LineEdit LineEditor::pointerPress(float x, int clickCount, bool extend) {
  const Snapshot before = snapshotOf(editor_);
  if (!std::isfinite(x)) return diff(editor_, before, false);
  ensureLayout(pixelSize_);
  editor_.pointerPress(editor_.hitTest(x + editor_.scrollX()), clickCount, extend);
  return diff(editor_, before, true);
}

LineEdit LineEditor::pointerDrag(float x) {
  const Snapshot before = snapshotOf(editor_);
  if (!std::isfinite(x)) return diff(editor_, before, false);
  ensureLayout(pixelSize_);
  editor_.pointerPress(editor_.hitTest(x + editor_.scrollX()), 1, true);
  return diff(editor_, before, true);
}

// ---- layout and view ----------------------------------------------------------------------------

bool LineEditor::ensureLayout(float pixelSize) {
  if (ui_ == nullptr || !text::isValidPixelSize(pixelSize)) return false;
  if (pixelSize == pixelSize_ && editor_.layoutCurrent()) return true;
  pixelSize_ = pixelSize;
  text::ShapeOptions shape;
  shape.tabularNumbers = true;
  return editor_.relayout(ui_->text().regular(), pixelSize, shape).ok();
}

void LineEditor::scrollCaretIntoView(float contentWidth) {
  if (!ensureLayout(pixelSize_)) return;
  editor_.ensureCaretVisible(contentWidth, 1.0f);
}

bool LineEditor::caretOffsetX(float& x) const {
  const std::optional<float> cx = editor_.caretX();
  if (!cx) return false;
  x = *cx - editor_.scrollX();
  return true;
}

bool LineEditor::caretPhaseOn(uint64_t nowMs, bool blink) const {
  if (!blink || nowMs <= activityMs_) return true;
  return ((nowMs - activityMs_) / kBlinkMs) % 2 == 0;
}

// ---- painting -----------------------------------------------------------------------------------

size_t LineEditor::toDisplay(size_t textOffset) const {
  const std::optional<text::TextRange> pre = editor_.preeditRange();
  if (pre && textOffset > editor_.caret()) return textOffset + (pre->end - pre->begin);
  return textOffset;
}

float LineEditor::xOfDisplayOffset(TextEngine& engine, size_t displayOffset) const {
  if (displayOffset == 0) return 0.0f;
  const std::string& disp = editor_.displayText();
  return engine.measure(std::string_view(disp).substr(0, std::min(displayOffset, disp.size())), pixelSize_, 400, true);
}

LineEditor::Window LineEditor::windowFor(TextEngine& engine, float contentWidth) const {
  const std::string& disp = editor_.displayText();
  if (disp.size() <= kWindowBytes) return {0, disp.size(), 0.0f};
  const float scroll = editor_.scrollX();
  const std::string& plain = editor_.text();
  size_t a = text::prevGraphemeBoundary(plain, editor_.hitTest(scroll));
  size_t b = text::nextGraphemeBoundary(plain, editor_.hitTest(scroll + contentWidth));
  size_t da = toDisplay(a);
  size_t db = toDisplay(b);
  if (const std::optional<text::TextRange> pre = editor_.preeditRange()) {
    da = std::min(da, pre->begin);
    db = std::max(db, pre->end);
  }
  db = std::min(db, disp.size());
  da = std::min(da, db);
  return {da, db, xOfDisplayOffset(engine, da)};
}

void LineEditor::paint(PaintContext& ctx, const LineEditorPaint& p) {
  if (!ensureLayout(p.pixelSize)) return;
  TextEngine& engine = ctx.ui().text();
  render::Painter& painter = ctx.painter();
  const float originX = p.content.x - editor_.scrollX();
  const float baseline = p.lineTop + engine.baselineInBox(p.pixelSize, p.lineHeight);
  painter.pushClip(p.content);

  const std::optional<std::pair<float, float>> sel = editor_.selectionX();
  const bool hasSelection = p.showSelection && sel && sel->second > sel->first;
  float selLeft = 0.0f;
  float selRight = 0.0f;
  if (hasSelection) {
    selLeft = originX + sel->first;
    selRight = originX + sel->second;
    painter.fillRect({selLeft, p.lineTop, selRight - selLeft, p.lineHeight}, p.selectionBackground);
  }

  const std::string& display = editor_.displayText();
  const Window window = windowFor(engine, p.content.w);
  const std::string_view shown = std::string_view(display).substr(window.begin, window.end - window.begin);
  const float penX = originX + window.penX;
  const float contentRight = p.content.x + p.content.w;
  const auto drawBetween = [&](const render::Color& color, float left, float right) {
    painter.pushClip({left, p.content.y, std::max(0.0f, right - left), p.content.h});
    engine.draw(painter, shown, p.pixelSize, p.weight, penX, baseline, color, true);
    painter.popClip();
  };
  if (hasSelection) {
    drawBetween(p.text, p.content.x, selLeft);
    drawBetween(p.selectionText, selLeft, selRight);
    drawBetween(p.text, selRight, contentRight);
  } else {
    engine.draw(painter, shown, p.pixelSize, p.weight, penX, baseline, p.text, true);
  }

  if (const std::optional<text::TextRange> pre = editor_.preeditRange()) {
    const float x0 = originX + xOfDisplayOffset(engine, pre->begin);
    const float x1 = originX + xOfDisplayOffset(engine, pre->end);
    const float thickness = ctx.hairline();
    painter.fillRect({x0, p.lineTop + p.lineHeight - thickness - 1.0f, std::max(0.0f, x1 - x0), thickness}, p.text);
  }

  if (p.showCaret && !editor_.hasSelection()) {
    if (const std::optional<float> cx = editor_.caretX()) {
      const float x = std::floor(originX + *cx + 0.5f);
      painter.fillRect({x, p.lineTop, ctx.hairline(), p.lineHeight}, p.caret);
    }
  }
  painter.popClip();
}

}  // namespace r1ui::widgets

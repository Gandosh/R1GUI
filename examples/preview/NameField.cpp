// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of NameField.h (key bindings, pointer selection, blink, drawing).
// Key bindings: arrows/Home/End move (Shift extends, Ctrl = by word), Backspace/Delete delete
//   (Ctrl = by word), Ctrl+A select all, Ctrl+C/X/V clipboard, Ctrl+Z undo, Ctrl+Y or Ctrl+Shift+Z
//   redo, Enter ends editing. Plain printable keys are reported as used so application shortcuts
//   (theme, mode) do not fire while typing; the characters arrive separately as text input.
// Callers: Scene.cpp.
#include "NameField.h"

#include <algorithm>
#include <cmath>

#include "r1ui/text/Utf8.h"

namespace preview {

namespace {

using r1ui::core::events::Key;
namespace Mod = r1ui::core::events::Mod;
using r1ui::text::Motion;

// Letter keys carry their ASCII code (events::Key only names the first letter); the switch below
// works on the numeric code so those keys can be matched.
constexpr uint16_t code(Key key) { return static_cast<uint16_t>(key); }
constexpr uint16_t kKeyA = 'A';
constexpr uint16_t kKeyC = 'C';
constexpr uint16_t kKeyV = 'V';
constexpr uint16_t kKeyX = 'X';
constexpr uint16_t kKeyY = 'Y';
constexpr uint16_t kKeyZ = 'Z';

}  // namespace

NameField::NameField(TextEngine& text, r1ui::text::ClipboardCallbacks clipboard, std::string initial)
    : text_(text) {
  editor_.setClipboard(std::move(clipboard));
  editor_.setText(initial);
}

void NameField::restartBlink(uint64_t nowMs) {
  caretVisible_ = true;
  nextBlinkMs_ = nowMs + kCaretBlinkMs;
}

void NameField::setFocused(bool focused, uint64_t nowMs) {
  if (focused == focused_) return;
  focused_ = focused;
  if (focused) {
    restartBlink(nowMs);
  } else {
    editor_.breakUndoGroup();
    editor_.setSelection(editor_.caret(), editor_.caret());
  }
}

bool NameField::tick(uint64_t nowMs) {
  if (!focused_ || nowMs < nextBlinkMs_) return false;
  caretVisible_ = !caretVisible_;
  nextBlinkMs_ = nowMs + kCaretBlinkMs;
  return true;
}

std::optional<uint64_t> NameField::msUntilTick(uint64_t nowMs) const {
  if (!focused_) return std::nullopt;
  return nextBlinkMs_ > nowMs ? nextBlinkMs_ - nowMs : 0;
}

void NameField::ensureLayout(float pixelSize) {
  if (editor_.layoutCurrent() && layoutPixelSize_ == pixelSize) return;
  if (editor_.relayout(text_.regular(), pixelSize).ok()) layoutPixelSize_ = pixelSize;
}

bool NameField::onKey(Key key, uint8_t modifiers, uint64_t nowMs) {
  const bool ctrl = (modifiers & Mod::kCtrl) != 0;
  const bool shift = (modifiers & Mod::kShift) != 0;
  const bool other = (modifiers & (Mod::kAlt | Mod::kMeta)) != 0;
  if (other) return false;
  bool used = true;
  switch (code(key)) {
    case code(Key::Left): editor_.move(ctrl ? Motion::WordLeft : Motion::Left, shift); break;
    case code(Key::Right): editor_.move(ctrl ? Motion::WordRight : Motion::Right, shift); break;
    case code(Key::Home): editor_.move(Motion::LineStart, shift); break;
    case code(Key::End): editor_.move(Motion::LineEnd, shift); break;
    case code(Key::Backspace): ctrl ? editor_.deleteWordBackward() : editor_.deleteBackward(); break;
    case code(Key::Delete): ctrl ? editor_.deleteWordForward() : editor_.deleteForward(); break;
    default:
      if (ctrl) {
        switch (code(key)) {
          case kKeyA: editor_.selectAll(); break;
          case kKeyC: editor_.copy(); break;
          case kKeyX: editor_.cut(); break;
          case kKeyV: editor_.paste(); break;
          case kKeyZ: shift ? editor_.redo() : editor_.undo(); break;
          case kKeyY: editor_.redo(); break;
          default: used = false; break;
        }
      } else if (key == Key::Tab || (key >= Key::F1 && static_cast<uint16_t>(key) <= static_cast<uint16_t>(Key::F1) + 11)) {
        used = false;
      }
      break;
  }
  if (used) restartBlink(nowMs);
  return used;
}

void NameField::onText(char32_t codePoint, uint64_t nowMs) {
  std::string utf8;
  r1ui::text::appendUtf8(utf8, codePoint);
  editor_.insertText(utf8);
  restartBlink(nowMs);
}

void NameField::onPointerDown(float x, uint32_t clickCount, bool shift, uint64_t nowMs) {
  ensureLayout(pixelSize_);
  const size_t offset = editor_.hitTest(x - valueLeft_ + editor_.scrollX());
  editor_.pointerPress(offset, static_cast<int>(clickCount), shift);
  restartBlink(nowMs);
}

void NameField::onPointerDrag(float x, uint64_t nowMs) {
  ensureLayout(pixelSize_);
  editor_.pointerPress(editor_.hitTest(x - valueLeft_ + editor_.scrollX()), 1, true);
  restartBlink(nowMs);
}

void NameField::paint(r1ui::render::Painter& painter, const r1ui::render::Rect& rect, float padding, float gap,
                      float pixelSize, float lineHeight, std::string_view prefix, const NameFieldColors& colors) {
  pixelSize_ = pixelSize;
  ensureLayout(pixelSize);
  const float prefixWidth = prefix.empty() ? 0.0f : text_.measure(prefix, pixelSize, 400);
  valueLeft_ = padding + (prefix.empty() ? 0.0f : prefixWidth + gap);
  valueWidth_ = std::max(0.0f, rect.w - valueLeft_ - padding);
  const float baseline = rect.y + text_.baselineInBox(pixelSize, rect.h);
  if (!prefix.empty()) text_.draw(painter, prefix, pixelSize, 400, rect.x + padding, baseline, colors.prefix);

  const float scroll = editor_.ensureCaretVisible(valueWidth_, 1.0f);
  painter.pushClip({rect.x + valueLeft_, rect.y, valueWidth_, rect.h});
  const float originX = rect.x + valueLeft_ - scroll;
  const float top = rect.y + (rect.h - lineHeight) * 0.5f;
  if (focused_) {
    if (const auto selection = editor_.selectionX()) {
      painter.fillRect({originX + selection->first, top, selection->second - selection->first, lineHeight}, colors.selection);
    }
  }
  text_.draw(painter, editor_.displayText(), pixelSize, 400, originX, baseline, colors.text);
  if (focused_ && caretVisible_) {
    if (const auto caretX = editor_.caretX()) {
      const float width = std::max(1.0f, std::round(pixelSize / 12.0f));
      painter.fillRect({std::round(originX + *caretX), top, width, lineHeight}, colors.caret);
    }
  }
  painter.popClip();
}

}  // namespace preview

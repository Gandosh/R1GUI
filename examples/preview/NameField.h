// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the behaviour and drawing of the panel's one real text input (the "Name" field): it wires
//   a ui-text TextEditor to keyboard and pointer events, the window clipboard and caret blinking,
//   and draws the value, selection and caret through the Painter.
// Why: the editing model lives in ui-text (tested headless); the preview only translates events
//   into editor calls and editor geometry into rectangles.
// Callers: Scene (event handler and paint walk). Calls: TextEditor, TextEngine, Painter.
// Units: pointer x values passed in are field-local physical pixels; layout of the text area uses
//   physical pixels derived from the logical paddings and the display scale.
// Invariants: the editor layout is rebuilt before any geometry query when the text or the pixel size
//   changed; blink state is reset by every edit, caret move and focus change so the caret is
//   visible while the user acts.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "Toolkit.h"
#include "r1ui/core/events/Event.h"
#include "r1ui/render/Painter.h"
#include "r1ui/text/TextEditor.h"

namespace preview {

inline constexpr uint64_t kCaretBlinkMs = 530;

struct NameFieldColors {
  r1ui::render::Color text;
  r1ui::render::Color prefix;
  r1ui::render::Color selection;
  r1ui::render::Color caret;
};

class NameField {
 public:
  NameField(TextEngine& text, r1ui::text::ClipboardCallbacks clipboard, std::string initial);

  const std::string& value() const { return editor_.text(); }

  // Focus changes restart the blink cycle and end the current undo group when focus leaves.
  void setFocused(bool focused, uint64_t nowMs);
  bool focused() const { return focused_; }

  // Returns true when the key was used (the router then stops delivery).
  bool onKey(r1ui::core::events::Key key, uint8_t modifiers, uint64_t nowMs);
  void onText(char32_t codePoint, uint64_t nowMs);
  // `x` is relative to the field's left edge, physical pixels. clickCount follows the router.
  void onPointerDown(float x, uint32_t clickCount, bool shift, uint64_t nowMs);
  void onPointerDrag(float x, uint64_t nowMs);

  // Advances the blink timer; true when the visible caret state changed (repaint needed).
  bool tick(uint64_t nowMs);
  // Milliseconds until tick() would change something, nullopt when the caret is not blinking.
  std::optional<uint64_t> msUntilTick(uint64_t nowMs) const;

  // Draws prefix label, value, selection and caret inside `rect` (physical pixels). `padding` and
  // `gap` are the logical paddings scaled by the caller; `pixelSize` the physical font size.
  void paint(r1ui::render::Painter& painter, const r1ui::render::Rect& rect, float padding, float gap,
             float pixelSize, float lineHeight, std::string_view prefix, const NameFieldColors& colors);

 private:
  void restartBlink(uint64_t nowMs);
  void ensureLayout(float pixelSize);
  // Left edge of the value area relative to the field, set by the last paint (physical pixels).
  float valueLeft_ = 0.0f;
  float valueWidth_ = 0.0f;
  float pixelSize_ = 12.0f;

  TextEngine& text_;
  r1ui::text::TextEditor editor_;
  bool focused_ = false;
  bool caretVisible_ = true;
  uint64_t nextBlinkMs_ = 0;
  float layoutPixelSize_ = 0.0f;
};

}  // namespace preview

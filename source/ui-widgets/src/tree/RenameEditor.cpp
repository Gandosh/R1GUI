// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of RenameEditor.h.
// Invariants: relayout() is called with the pixel size the text is drawn at, so caret and selection
//   x positions equal what is on screen; the field clips its text; the selection colour is the
//   measured browser highlight of the reference (it is not a design token) chosen by theme.
// Callers: TreeView.
#include "r1ui/widgets/tree/RenameEditor.h"

#include <algorithm>
#include <cmath>

#include "r1ui/text/Utf8.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

using core::events::Key;
namespace Mod = core::events::Mod;

constexpr float kTextSize = 12.0f;  // the field uses the 12 px body text
// Measured text selection colours of the reference (Chrome's highlight in each theme).
const render::Color kSelectionDark = render::Color::fromRgba8(0x06, 0x3a, 0xa4);
const render::Color kSelectionLight = render::Color::fromRgba8(0x33, 0x67, 0xd1);

bool isLetter(Key key, char letter) { return static_cast<uint16_t>(key) == static_cast<uint16_t>(letter); }

}  // namespace

void RenameEditor::setError(std::string message) { error_ = std::move(message); }

void RenameEditor::begin(UiContext& ui, NodeId node, std::string_view initial) {
  node_ = node;
  active_ = true;
  error_.clear();
  editor_.setText(initial);
  editor_.selectAll();
  UiContext* context = &ui;
  text::ClipboardCallbacks clipboard;
  clipboard.write = [context](std::string_view utf8) {
    if (!context->host().writeClipboard) return false;
    context->host().writeClipboard(utf8);
    return true;
  };
  clipboard.read = [context]() -> std::optional<std::string> {
    if (!context->host().readClipboard) return std::nullopt;
    return context->host().readClipboard();
  };
  editor_.setClipboard(std::move(clipboard));
}

void RenameEditor::end() {
  active_ = false;
  node_ = kTreeRoot;
  error_.clear();
  editor_.setText({});
}

RenameEditor::Outcome RenameEditor::keyDown(Key key, uint8_t modifiers) {
  if (!active_) return Outcome::Ignored;
  const bool ctrl = (modifiers & Mod::kCtrl) != 0;
  const bool shift = (modifiers & Mod::kShift) != 0;
  if (modifiers & (Mod::kAlt | Mod::kMeta)) return Outcome::Ignored;
  if (key == Key::Enter) return Outcome::Commit;
  if (key == Key::Escape) return Outcome::Cancel;
  const auto edited = [&](bool changed) {
    if (changed) error_.clear();
    return Outcome::Handled;
  };
  switch (key) {
    case Key::Left: editor_.move(ctrl ? text::Motion::WordLeft : text::Motion::Left, shift); return Outcome::Handled;
    case Key::Right: editor_.move(ctrl ? text::Motion::WordRight : text::Motion::Right, shift); return Outcome::Handled;
    case Key::Home: editor_.move(text::Motion::LineStart, shift); return Outcome::Handled;
    case Key::End: editor_.move(text::Motion::LineEnd, shift); return Outcome::Handled;
    case Key::Backspace: return edited(ctrl ? editor_.deleteWordBackward() : editor_.deleteBackward());
    case Key::Delete: return edited(ctrl ? editor_.deleteWordForward() : editor_.deleteForward());
    case Key::Up:
    case Key::Down:
    case Key::PageUp:
    case Key::PageDown: return Outcome::Handled;  // no meaning in a one-line field; the tree must not navigate meanwhile
    default: break;
  }
  if (ctrl) {
    if (isLetter(key, 'A')) {
      editor_.selectAll();
      return Outcome::Handled;
    }
    if (isLetter(key, 'C')) {
      editor_.copy();
      return Outcome::Handled;
    }
    if (isLetter(key, 'X')) return edited(editor_.cut());
    if (isLetter(key, 'V')) return edited(editor_.paste());
    if (isLetter(key, 'Z')) return edited(shift ? editor_.redo() : editor_.undo());
    if (isLetter(key, 'Y')) return edited(editor_.redo());
  }
  return Outcome::Ignored;
}

bool RenameEditor::textInput(char32_t codePoint) {
  if (!active_ || codePoint < 0x20) return false;
  std::string utf8;
  text::appendUtf8(utf8, codePoint);
  const bool inserted = editor_.insertText(utf8) > 0;
  if (inserted) error_.clear();
  return inserted;
}

bool RenameEditor::layoutFor(UiContext& ui, float pixelSize) {
  return editor_.relayout(ui.text().regular(), pixelSize).ok();
}

size_t RenameEditor::offsetAt(UiContext& ui, const core::layout::Rect& field, double x) {
  const float size = kTextSize * ui.scale();
  if (!layoutFor(ui, size)) return editor_.caret();
  const double local = (x - field.x - 1.0 - kPadX) * ui.scale() + static_cast<double>(editor_.scrollX());
  return editor_.hitTest(static_cast<float>(local));
}

bool RenameEditor::contains(const core::layout::Rect& field, double x, double y) const {
  return active_ && x >= field.x && y >= field.y && x < static_cast<double>(field.x) + field.w && y < static_cast<double>(field.y) + field.h;
}

void RenameEditor::pointerDown(UiContext& ui, const core::layout::Rect& field, double x, bool shift, uint32_t clickCount) {
  if (!active_) return;
  editor_.pointerPress(offsetAt(ui, field, x), static_cast<int>(std::min<uint32_t>(clickCount, 3)), shift);
}

void RenameEditor::pointerDrag(UiContext& ui, const core::layout::Rect& field, double x) {
  if (!active_) return;
  editor_.pointerPress(offsetAt(ui, field, x), 1, true);
}

void RenameEditor::paint(PaintContext& ctx, const core::layout::Rect& field) {
  if (!active_) return;
  UiContext& ui = ctx.ui();
  const float scale = ctx.scale();
  const float size = kTextSize * scale;
  render::Painter& painter = ctx.painter();
  const render::Rect box = ctx.toPhysical(field.x, field.y, field.w, field.h);
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(4.0));
  painter.fillRoundedRect(box, radii, ctx.color("input"));
  painter.border(box, radii, ctx.hairline(), ctx.color(error_.empty() ? "accent" : "danger"));
  if (!layoutFor(ui, size)) return;
  const double innerLeft = field.x + 1.0 + kPadX;
  const double innerWidth = std::max(0.0, static_cast<double>(field.w) - 2.0 - 2.0 * kPadX);
  const render::Rect inner = ctx.toPhysical(innerLeft, field.y + 1.0, innerWidth, field.h - 2.0);
  const float scroll = editor_.ensureCaretVisible(inner.w, 1.0f);
  painter.pushClip(inner);
  if (const auto sel = editor_.selectionX()) {
    const bool light = ui.theme().id() == theme::ThemeId::Light;
    painter.fillRect({inner.x + sel->first - scroll, inner.y, sel->second - sel->first, inner.h}, light ? kSelectionLight : kSelectionDark);
  }
  const theme::ResolvedStyle& rs = ctx.resolve("tree.rename", 0);
  const render::Color tint = ctx.color(rs.text.color);
  const float baseline = inner.y + ui.text().baselineInBox(size, inner.h);
  ui.text().draw(painter, editor_.text(), size, rs.text.weight, inner.x - scroll, baseline, tint);
  if (!editor_.hasSelection()) {
    if (const auto caret = editor_.caretX()) painter.fillRect({inner.x + *caret - scroll, inner.y, ctx.hairline(), inner.h}, tint);
  }
  painter.popClip();
}

}  // namespace r1ui::widgets

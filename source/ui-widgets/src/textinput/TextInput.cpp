// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of TextInput.h.
// Invariants: after every callback the widget re-checks that it is still alive before touching its
//   node; the editor layout is rebuilt before any pointer or caret query; capture is released on every
//   exit path (the Router drops it when the button goes up or the widget is disabled or destroyed).
// Callers: UiContext (event and paint dispatch), tests.
#include "r1ui/widgets/textinput/TextInput.h"

#include <algorithm>
#include <cmath>

#include "r1ui/text/Shaper.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/textinput/FieldChrome.h"

namespace r1ui::widgets {

namespace {

using core::events::Button;
using core::events::FocusReason;
using core::events::Key;
namespace Mod = core::events::Mod;

constexpr double kClearBox = 16.0;
constexpr double kClearGlyph = 16.0;  // the browser's search clear glyph is a bold 8 px cross; Lucide at 16 px is the nearest
constexpr double kClearGap = 4.0;

}  // namespace

TextInput::TextInput(TextInputTone tone, TextInputSize size)
    : tone_(tone), size_(size), lineEditor_(text::EditorConfig{text::kMaxShapeBytes - 4096, false}) {}

std::span<const theme::StyleRuleEntry> TextInput::styleRows() { return fieldStyleRows(); }

const char* TextInput::styleKeyFor(TextInputTone tone, TextInputSize size) {
  if (tone == TextInputTone::Panel) return size == TextInputSize::Sm ? "input.panel.sm" : "input.panel";
  return size == TextInputSize::Sm ? "input.default.sm" : "input.default";
}

void TextInput::onAttached() {
  lineEditor_.bind(ui());
  setFocusable(true);
  const theme::ResolvedStyle& rs = ui().services().resolve(styleKey(), 0);
  style().height = core::layout::Length::px(rs.height);
  style().minHeight = core::layout::Length::px(rs.height);
}

// ---- content ------------------------------------------------------------------------------------

void TextInput::setText(std::string text) {
  lineEditor_.setText(text);
  committed_ = lineEditor_.text();
  if (focused()) showCaretNow();
  requestPaint();
}

void TextInput::setPlaceholder(std::string placeholder) {
  if (placeholder == placeholder_) return;
  placeholder_ = std::move(placeholder);
  requestPaint();
}

void TextInput::setMaxLength(size_t characters) { lineEditor_.setMaxChars(characters); }

void TextInput::setReadOnly(bool readOnly) {
  if (readOnly == readOnly_) return;
  readOnly_ = readOnly;
  lineEditor_.model().setReadOnly(readOnly);
  requestPaint();
}

void TextInput::setClearable(bool clearable) {
  if (clearable == clearable_) return;
  clearable_ = clearable;
  requestPaint();
}

void TextInput::selectAll() {
  lineEditor_.model().selectAll();
  showCaretNow();
  requestPaint();
}

void TextInput::setSelection(size_t anchor, size_t caret) {
  lineEditor_.model().setSelection(anchor, caret);
  showCaretNow();
  requestPaint();
}

bool TextInput::setPreedit(std::string_view utf8, size_t cursorInPreedit) {
  const bool ok = lineEditor_.model().setPreedit(utf8, cursorInPreedit);
  scrollPending_ = true;
  showCaretNow();
  requestPaint();
  return ok;
}

bool TextInput::commitPreedit() {
  const size_t revision = lineEditor_.model().revision();
  const bool ok = lineEditor_.model().commitPreedit();
  if (lineEditor_.model().revision() != revision) {
    LineEdit edit;
    edit.handled = edit.textChanged = edit.caretMoved = true;
    apply(edit);
  }
  return ok;
}

void TextInput::cancelPreedit() {
  lineEditor_.model().cancelPreedit();
  requestPaint();
}

// ---- geometry -----------------------------------------------------------------------------------

TextInput::Geometry TextInput::geometry() const {
  const theme::ResolvedStyle& rs = ui().services().resolve(styleKey(), styleState());
  const core::layout::Rect r = ui().absRect(id());
  Geometry g;
  g.border = rs.border.width;
  g.contentLeft = r.x + g.border + rs.paddingX;
  g.contentRight = static_cast<double>(r.right()) - g.border - rs.paddingX;
  g.lineTop = r.y + g.border + rs.paddingY;
  g.lineHeight = rs.text.lineHeight;
  // The glyph needs its box, a gap and a little text room; a narrower field shows none.
  const double minWidth = 2.0 * (g.border + rs.paddingX) + kClearBox + kClearGap;
  if (clearVisible() && static_cast<double>(r.w) >= minWidth) {
    const double right = static_cast<double>(r.right()) - g.border - rs.paddingX;
    const double x = right - kClearBox;
    const double y = r.y + (static_cast<double>(r.h) - kClearBox) / 2.0;
    g.clear = {static_cast<int32_t>(std::lround(x)), static_cast<int32_t>(std::lround(y)), static_cast<int32_t>(kClearBox), static_cast<int32_t>(kClearBox)};
    g.contentRight = x - kClearGap;
  }
  return g;
}

core::layout::Rect TextInput::caretRect() {
  settleScroll();
  const theme::ResolvedStyle& rs = ui().services().resolve(styleKey(), styleState());
  if (!lineEditor_.ensureLayout(static_cast<float>(rs.text.fontSize * ui().scale()))) return {};
  float x = 0.0f;
  if (!lineEditor_.caretOffsetX(x)) return {};
  const Geometry g = geometry();
  const double logicalX = g.contentLeft + static_cast<double>(x) / ui().scale();
  return {static_cast<int32_t>(std::lround(logicalX)), static_cast<int32_t>(std::lround(g.lineTop)), 1, static_cast<int32_t>(std::lround(g.lineHeight))};
}

// ---- notifications --------------------------------------------------------------------------------

bool TextInput::notify(const TextCallback& callback) {
  if (!callback) return true;
  const TextCallback copy = callback;
  const std::string snapshot = text();
  copy(snapshot);
  return ui().alive(id());
}

void TextInput::showCaretNow() { lineEditor_.noteActivity(ui().now()); }

void TextInput::apply(const LineEdit& edit) {
  if (edit.textChanged) {
    if (hasState(StateFlag::kMixed)) setMixed(false);
    if (!notify(onTextChanged_)) return;
  }
  if (edit.caretMoved) {
    showCaretNow();
    scrollPending_ = true;  // settled at the next paint or caret query, so a burst of typing shapes once
    requestPaint();
  }
}

void TextInput::settleScroll() {
  if (!scrollPending_) return;
  scrollPending_ = false;
  const theme::ResolvedStyle& rs = ui().services().resolve(styleKey(), styleState());
  if (!lineEditor_.ensureLayout(static_cast<float>(rs.text.fontSize * ui().scale()))) return;
  const Geometry g = geometry();
  lineEditor_.scrollCaretIntoView(static_cast<float>((g.contentRight - g.contentLeft) * ui().scale()));
}

void TextInput::commitIfChanged() {
  if (text() == committed_) return;
  committed_ = text();
  notify(onCommitted_);
}

void TextInput::clearText() {
  if (text().empty()) return;
  lineEditor_.model().setSelection(0, lineEditor_.model().text().size());
  const size_t before = lineEditor_.model().revision();
  lineEditor_.model().deleteBackward();
  if (lineEditor_.model().revision() == before) return;  // read-only
  LineEdit edit;
  edit.handled = edit.textChanged = edit.caretMoved = true;
  apply(edit);
}

// ---- WidgetObject -----------------------------------------------------------------------------------

float TextInput::paintOpacity() const { return static_cast<float>(ui().services().resolve(styleKey(), styleState()).opacity); }

Cursor TextInput::cursor() const { return clearHover_ ? Cursor::Pointer : Cursor::Text; }

std::string_view TextInput::accessibleName() const {
  return WidgetObject::accessibleName().empty() ? std::string_view(placeholder_) : WidgetObject::accessibleName();
}

void TextInput::onStateChanged(uint16_t previous) {
  if (hasState(StateFlag::kDisabled) && (previous & StateFlag::kDisabled) == 0) {
    dragging_ = false;
    clearPressed_ = false;
    clearHover_ = false;
  }
}

void TextInput::paint(PaintContext& ctx) {
  settleScroll();
  const theme::ResolvedStyle& rs = ctx.style(styleKey());
  const render::Rect box = ctx.box();
  paintFieldBox(ctx, rs, animatedFieldColors(ctx, rs, 0), box);
  const Geometry g = geometry();
  const float scale = ctx.scale();
  const float borderPx = ctx.px(g.border);
  const render::Rect content{static_cast<float>(g.contentLeft) * scale, box.y + borderPx, static_cast<float>(g.contentRight - g.contentLeft) * scale,
                             box.h - 2.0f * borderPx};
  const float lineTop = static_cast<float>(g.lineTop) * scale;
  const float lineHeight = ctx.px(rs.text.lineHeight);
  const bool animate = ctx.ui().animationsActive();
  if (focused() && animate) ui().invalidator().requestAnimation(id());

  if (text().empty() && !composing()) {
    const bool mixed = hasState(StateFlag::kMixed);
    const std::string_view hint = mixed ? std::string_view("Mixed") : std::string_view(placeholder_);
    if (!hint.empty() && content.w > 0.0f) {
      TextOptions options;
      options.tabular = true;
      // The reference browser draws a placeholder in the text colour at 50% (measured in both themes).
      options.color = mixed ? ctx.color(rs.text.color) : ctx.color("surface", 0.5);
      ctx.drawText(hint, rs.text, {content.x, lineTop, content.w, lineHeight}, options);
    }
  }
  if (focused() || !text().empty() || composing()) {
    LineEditorPaint lp;
    lp.content = content;
    lp.lineTop = lineTop;
    lp.lineHeight = lineHeight;
    lp.pixelSize = ctx.px(rs.text.fontSize);
    lp.weight = rs.text.weight;
    lp.text = ctx.color(rs.text.color);
    lp.selectionBackground = fieldSelectionBackground(ctx);
    lp.selectionText = fieldSelectionText(ctx);
    lp.caret = ctx.color("surface");
    lp.showSelection = focused();
    lp.showCaret = focused() && !readOnly_ && lineEditor_.caretPhaseOn(ui().now(), animate);
    lineEditor_.paint(ctx, lp);
  }

  if (!g.clear.empty()) {
    const theme::ResolvedStyle& glyph = ctx.resolve("input.glyph", theme::State::kNone);
    ctx.drawIcon("x", kClearGlyph, ctx.toPhysical(g.clear.x, g.clear.y, g.clear.w, g.clear.h), ctx.color(glyph.text.color));
  }
}

// ---- pointer ------------------------------------------------------------------------------------------

void TextInput::onPointerDown(Event& e) {
  if (e.button != Button::Left || !enabled()) return;
  e.markHandled();
  ui().router().focus(id(), FocusReason::Pointer);
  if (!ui().alive(id())) return;
  ui().router().capturePointer(id());
  const Geometry g = geometry();
  if (!g.clear.empty() && core::layout::containsPoint(g.clear, e.x, e.y)) {
    clearPressed_ = true;
    requestPaint();
    return;
  }
  const theme::ResolvedStyle& rs = ui().services().resolve(styleKey(), styleState());
  lineEditor_.ensureLayout(static_cast<float>(rs.text.fontSize * ui().scale()));
  const float x = static_cast<float>((e.x - g.contentLeft) * ui().scale());
  const int clicks = static_cast<int>(std::clamp<uint32_t>(e.clickCount, 1, 3));
  dragging_ = true;
  apply(lineEditor_.pointerPress(x, clicks, (e.modifiers & Mod::kShift) != 0));
}

void TextInput::onPointerMove(Event& e) {
  const Geometry g = geometry();
  const bool overClear = !g.clear.empty() && core::layout::containsPoint(g.clear, e.x, e.y);
  if (overClear != clearHover_) {
    clearHover_ = overClear;
    requestPaint();
  }
  if (dragging_ && (e.buttons & core::events::buttonBit(Button::Left)) != 0) {
    apply(lineEditor_.pointerDrag(static_cast<float>((e.x - g.contentLeft) * ui().scale())));
  }
}

void TextInput::onPointerUp(Event& e) {
  if (e.button != Button::Left) return;
  const bool wasClear = clearPressed_;
  dragging_ = false;
  clearPressed_ = false;
  if (!wasClear) return;
  const Geometry g = geometry();
  if (!g.clear.empty() && core::layout::containsPoint(g.clear, e.x, e.y)) clearText();
  requestPaint();
}

void TextInput::onPointerLeave(Event&) {
  if (clearHover_) {
    clearHover_ = false;
    requestPaint();
  }
}

void TextInput::onCaptureLost(Event&) {
  dragging_ = false;
  clearPressed_ = false;
}

// ---- keyboard and focus -----------------------------------------------------------------------------

void TextInput::onKeyDown(Event& e) {
  if (!enabled()) return;
  if (e.key == Key::Enter) {
    e.markHandled();
    commitPreedit();
    if (!ui().alive(id())) return;
    committed_ = text();
    notify(onCommitted_);
    return;
  }
  if (e.key == Key::Escape) {
    if (lineEditor_.model().composing()) {
      cancelPreedit();
      e.markHandled();
    } else if (lineEditor_.model().hasSelection()) {
      const size_t caret = lineEditor_.model().caret();
      lineEditor_.model().setSelection(caret, caret);
      showCaretNow();
      requestPaint();
      e.markHandled();
    } else if (!readOnly_ && text() != committed_) {
      e.markHandled();
      lineEditor_.setText(committed_);
      LineEdit edit;
      edit.handled = edit.textChanged = edit.caretMoved = true;
      apply(edit);
    }
    return;
  }
  const LineEdit edit = lineEditor_.handleKeyDown(e);
  if (edit.handled) e.markHandled();
  apply(edit);
}

void TextInput::onTextInput(Event& e) {
  if (!enabled()) return;
  const LineEdit edit = lineEditor_.handleChar(e.codePoint, e.modifiers);
  if (edit.handled) e.markHandled();
  apply(edit);
}

void TextInput::onFocusIn(Event& e) {
  committed_ = text();
  showCaretNow();
  if (e.focusReason == FocusReason::Keyboard && !text().empty()) lineEditor_.model().selectAll();
  if (ui().animationsActive()) ui().invalidator().requestAnimation(id());
}

void TextInput::onFocusOut(Event&) {
  dragging_ = false;
  clearPressed_ = false;
  ui().invalidator().cancelAnimation(id());
  commitPreedit();
  if (!ui().alive(id())) return;
  commitIfChanged();
  if (!ui().alive(id())) return;
  lineEditor_.model().setSelection(0, 0);
  scrollPending_ = true;  // the unfocused field shows the start of its text
  requestPaint();
}

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of PickerEntry.h.
// Invariants: the editor layout is rebuilt (ensureLayout) before any caret query or paint, at the
//   display scale, so caret and selection geometry equal what is drawn; the editor only ever holds
//   sanitised single-line UTF-8 (TextEditor's rule); a rejected commit leaves the last accepted text.
// Callers: the colour picker, gradient editor and curve editor widgets; tests.
#include "r1ui/widgets/colorpicker/PickerEntry.h"

#include <algorithm>
#include <cmath>

#include "r1ui/text/Utf8.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

using theme::State::kFocus;
using theme::State::kHover;
using theme::State::kDisabled;
using theme::State::kNone;
using theme::StyleProperty;
using theme::StyleRuleEntry;
namespace events = core::events;

constexpr double kSuffixPadRight = 14.0;
constexpr double kSuffixFontPx = 12.0;
constexpr double kMinTextRoom = 4.0;

const StyleRuleEntry kRows[] = {
    {"picker.entry.field", kNone, StyleProperty::Background, "color:panel-field"},
    {"picker.entry.field", kHover, StyleProperty::Background, "color:panel-field-hover"},
    {"picker.entry.field", kNone, StyleProperty::Foreground, "color:surface"},
    {"picker.entry.field", kNone, StyleProperty::BorderColor, "transparent"},
    {"picker.entry.field", kFocus, StyleProperty::BorderColor, "color:panel-focus"},
    {"picker.entry.field", kNone, StyleProperty::BorderWidth, "number:1"},
    {"picker.entry.field", kNone, StyleProperty::Radius, "radius:panel"},
    {"picker.entry.field", kNone, StyleProperty::PaddingX, "number:8"},
    {"picker.entry.field", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"picker.entry.field", kNone, StyleProperty::LineHeight, "lineHeight:xs"},
    {"picker.entry.field", kNone, StyleProperty::FontWeight, "weight:normal"},
    {"picker.entry.field", kDisabled, StyleProperty::Opacity, "number:0.6"},

    {"picker.entry.input", kNone, StyleProperty::Background, "color:input"},
    {"picker.entry.input", kNone, StyleProperty::Foreground, "color:surface"},
    {"picker.entry.input", kNone, StyleProperty::BorderColor, "color:border"},
    {"picker.entry.input", kFocus, StyleProperty::BorderColor, "color:panel-focus"},
    {"picker.entry.input", kNone, StyleProperty::BorderWidth, "number:1"},
    {"picker.entry.input", kNone, StyleProperty::Radius, "radius:panel"},
    {"picker.entry.input", kNone, StyleProperty::PaddingX, "number:5"},
    {"picker.entry.input", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"picker.entry.input", kNone, StyleProperty::LineHeight, "lineHeight:xs"},
    {"picker.entry.input", kNone, StyleProperty::FontWeight, "weight:normal"},
    {"picker.entry.input", kDisabled, StyleProperty::Opacity, "number:0.6"},

    {"picker.entry.bare", kNone, StyleProperty::Background, "transparent"},
    {"picker.entry.bare", kNone, StyleProperty::Foreground, "color:surface"},
    {"picker.entry.bare", kNone, StyleProperty::BorderColor, "transparent"},
    {"picker.entry.bare", kFocus, StyleProperty::BorderColor, "color:panel-focus"},
    {"picker.entry.bare", kNone, StyleProperty::BorderWidth, "number:1"},
    {"picker.entry.bare", kNone, StyleProperty::Radius, "radius:panel"},
    {"picker.entry.bare", kNone, StyleProperty::PaddingX, "number:7"},
    {"picker.entry.bare", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"picker.entry.bare", kNone, StyleProperty::LineHeight, "lineHeight:xs"},
    {"picker.entry.bare", kNone, StyleProperty::FontWeight, "weight:medium"},
    {"picker.entry.bare", kDisabled, StyleProperty::Opacity, "number:0.6"},
};

bool hasCtrl(uint8_t m) { return (m & (events::Mod::kCtrl | events::Mod::kMeta)) != 0; }

}  // namespace

std::span<const StyleRuleEntry> PickerEntry::styleRows() { return kRows; }

PickerEntry::PickerEntry(Look look) : look_(look), editor_(std::make_unique<r1ui::text::TextEditor>()) {}
PickerEntry::~PickerEntry() = default;

const char* PickerEntry::rowKey() const {
  switch (look_) {
    case Look::Field: return "picker.entry.field";
    case Look::Input: return "picker.entry.input";
    case Look::Bare: return "picker.entry.bare";
  }
  return "picker.entry.field";
}

void PickerEntry::onAttached() {
  core::layout::Style& s = style();
  s.height = core::layout::Length::px(ui().services().tokens().space("control").value_or(26.0));
  s.flexShrink = 1.0;
  setFocusable(true);
  r1ui::text::ClipboardCallbacks clip;
  clip.write = [this](std::string_view t) {
    if (!ui().host().writeClipboard) return false;
    ui().host().writeClipboard(t);
    return true;
  };
  clip.read = [this]() -> std::optional<std::string> {
    if (!ui().host().readClipboard) return std::nullopt;
    return ui().host().readClipboard();
  };
  editor_->setClipboard(std::move(clip));
}

uint8_t PickerEntry::styleState() const {
  uint8_t s = WidgetObject::styleState();
  s &= static_cast<uint8_t>(~theme::State::kActive);  // an entry has no pressed look
  return s;
}

std::string_view PickerEntry::accessibleName() const {
  return WidgetObject::accessibleName().empty() ? std::string_view(editor_->text()) : WidgetObject::accessibleName();
}

const std::string& PickerEntry::text() const { return editor_->text(); }

void PickerEntry::setText(std::string_view text) {
  if (dirty_) {
    pending_.assign(text);
    hasPending_ = true;
    return;
  }
  if (baseline_ == text && editor_->text() == text) return;
  baseline_.assign(text);
  editor_->setText(text);
  baseline_ = editor_->text();  // the editor sanitises (control characters, size limit)
  requestPaint();
}

void PickerEntry::setSuffix(std::string suffix) {
  if (suffix == suffix_) return;
  suffix_ = std::move(suffix);
  requestPaint();
}

void PickerEntry::setFontSize(double logicalPx) {
  fontSize_ = std::isfinite(logicalPx) && logicalPx > 0.0 ? logicalPx : 0.0;
  requestPaint();
}

void PickerEntry::setMaxBytes(size_t bytes) { editor_->setMaxBytes(bytes); }

double PickerEntry::fontPx() const {
  return fontSize_ > 0.0 ? fontSize_ : ui().services().resolve(rowKey(), 0).text.fontSize;
}

double PickerEntry::padLeft() const { return padLeft_ >= 0.0 ? padLeft_ : ui().services().resolve(rowKey(), 0).paddingX; }

float PickerEntry::textLeft() const { return static_cast<float>(padLeft() * ui().scale()); }

void PickerEntry::setPadLeft(double logicalPx) {
  padLeft_ = std::isfinite(logicalPx) ? logicalPx : -1.0;
  requestPaint();
}

float PickerEntry::textRoom() const {
  const core::layout::Rect r = ui().absRect(id());
  const double scale = ui().scale();
  double room = r.w - padLeft() - ui().services().resolve(rowKey(), 0).paddingX;
  if (!suffix_.empty()) room -= kSuffixPadRight + 2.0;
  return static_cast<float>(std::max(kMinTextRoom, room) * scale);
}

void PickerEntry::ensureLayout() {
  const float size = static_cast<float>(fontPx() * ui().scale());
  if (editor_->layoutCurrent() && shapedSize_ == size && shapedRevision_ == editor_->revision()) return;
  editor_->relayout(ui().text().regular(), size);
  shapedSize_ = size;
  shapedRevision_ = editor_->revision();
}

size_t PickerEntry::offsetAt(double localX) {
  ensureLayout();
  const float x = static_cast<float>(localX * ui().scale()) - textLeft() + editor_->scrollX();
  return editor_->hitTest(x);
}

// ---- painting ---------------------------------------------------------------------------------

void PickerEntry::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.style(rowKey());
  ctx.fillBox(rs);
  ensureLayout();
  const render::Rect box = ctx.box();
  const float size = static_cast<float>(fontPx() * ctx.scale());
  const float room = textRoom();
  if (focused()) editor_->ensureCaretVisible(room, 1.0f);
  const float scroll = editor_->scrollX();
  const render::Rect clip{box.x + textLeft(), box.y, room, box.h};
  render::Painter& painter = ctx.painter();
  TextEngine& engine = ctx.ui().text();
  const float baseline = box.y + engine.baselineInBox(size, box.h);

  painter.pushClip(clip);
  if (focused()) {
    if (const auto sel = editor_->selectionX(); sel && sel->first != sel->second) {
      render::Color c = ctx.color("accent", 0.4);
      painter.fillRect({clip.x + sel->first - scroll, box.y + ctx.px(4), sel->second - sel->first, box.h - ctx.px(8)}, c);
    }
  }
  const render::Color tint = ctx.color(rs.text.color);
  engine.draw(painter, editor_->displayText(), size, rs.text.weight, clip.x - scroll, baseline, tint);
  if (focused()) {
    if (const auto cx = editor_->caretX()) {
      const float w = std::max(1.0f, std::round(ctx.scale()));
      painter.fillRect({clip.x + *cx - scroll, box.y + ctx.px(5), w, box.h - ctx.px(10)}, tint);
    }
  }
  painter.popClip();

  if (!suffix_.empty()) {
    const float suffixSize = ctx.px(kSuffixFontPx);
    const float sw = engine.measure(suffix_, suffixSize);
    engine.draw(painter, suffix_, suffixSize, 400, box.x + box.w - ctx.px(kSuffixPadRight) - sw + ctx.px(2), box.y + engine.baselineInBox(suffixSize, box.h), ctx.color("muted"));
  }
}

// ---- editing ----------------------------------------------------------------------------------

void PickerEntry::afterEdit() {
  dirty_ = editor_->text() != baseline_;
  if (!dirty_ && hasPending_) {
    hasPending_ = false;
    setText(pending_);
  }
  requestPaint();
}

void PickerEntry::commit() {
  if (!dirty_) return;
  const std::string typed = editor_->text();
  const std::string before = baseline_;
  baseline_ = typed;
  dirty_ = false;
  const core::tree::WidgetId self = id();
  bool accepted = true;
  if (onCommit) accepted = onCommit(typed);
  if (!ui().alive(self)) return;
  if (!accepted) {
    baseline_ = before;
    editor_->setText(before);
  }
  if (hasPending_) {
    hasPending_ = false;
    baseline_.clear();
    setText(pending_);
  }
  requestPaint();
}

void PickerEntry::revert() {
  editor_->setText(baseline_);
  dirty_ = false;
  if (hasPending_) {
    hasPending_ = false;
    setText(pending_);
  }
  editor_->selectAll();
  requestPaint();
}

void PickerEntry::onFocusIn(Event& e) {
  if (e.focusReason == events::FocusReason::Keyboard) editor_->selectAll();
  requestPaint();
  if (onFocused) onFocused();
}

void PickerEntry::onFocusOut(Event&) {
  selecting_ = false;
  editor_->cancelPreedit();
  const core::tree::WidgetId self = id();
  commit();
  if (!ui().alive(self)) return;
  editor_->setSelection(0, 0);
  requestPaint();
  if (onBlurred) onBlurred();
}

void PickerEntry::onPointerDown(Event& e) {
  if (e.button != events::Button::Left) return;
  if (!focused()) ui().router().focus(id(), events::FocusReason::Pointer);
  const bool extend = (e.modifiers & events::Mod::kShift) != 0;
  editor_->pointerPress(offsetAt(e.localX), static_cast<int>(std::min<uint32_t>(e.clickCount, 3)), extend);
  selecting_ = true;
  ui().router().capturePointer(id());
  e.markHandled();
  requestPaint();
}

void PickerEntry::onPointerMove(Event& e) {
  if (!selecting_ || !pressed()) return;
  editor_->pointerPress(offsetAt(e.localX), 1, true);
  requestPaint();
}

void PickerEntry::onTextInput(Event& e) {
  if (e.codePoint < 0x20 || (e.modifiers & events::Mod::kCtrl) != 0) return;
  std::string utf8;
  r1ui::text::appendUtf8(utf8, e.codePoint);
  if (editor_->insertText(utf8) > 0) {
    afterEdit();
    e.markHandled();
  }
}

void PickerEntry::onKeyDown(Event& e) {
  using events::Key;
  const bool shift = (e.modifiers & events::Mod::kShift) != 0;
  const bool ctrl = hasCtrl(e.modifiers);
  bool used = true;
  switch (static_cast<int>(e.key)) {
    case static_cast<int>(Key::Enter): {
      const core::tree::WidgetId self = id();
      if (dirty_) commit();
      else editor_->selectAll();
      if (!ui().alive(self)) {
        e.markHandled();
        return;
      }
      if (onSubmit) onSubmit();
      if (!ui().alive(self)) {
        e.markHandled();
        return;
      }
      break;
    }
    case static_cast<int>(Key::Escape):
      if (onCancel) {
        if (dirty_) revert();
        const core::tree::WidgetId self = id();
        onCancel();
        if (!ui().alive(self)) {
          e.markHandled();
          return;
        }
      } else if (dirty_) {
        revert();
      } else {
        used = false;
      }
      break;
    case static_cast<int>(Key::Left): editor_->move(ctrl ? r1ui::text::Motion::WordLeft : r1ui::text::Motion::Left, shift); break;
    case static_cast<int>(Key::Right): editor_->move(ctrl ? r1ui::text::Motion::WordRight : r1ui::text::Motion::Right, shift); break;
    case static_cast<int>(Key::Home): editor_->move(r1ui::text::Motion::LineStart, shift); break;
    case static_cast<int>(Key::End): editor_->move(r1ui::text::Motion::LineEnd, shift); break;
    case static_cast<int>(Key::Backspace):
      if (editor_->hasSelection()) editor_->deleteBackward();
      else if (ctrl) editor_->deleteWordBackward();
      else editor_->deleteBackward();
      afterEdit();
      break;
    case static_cast<int>(Key::Delete):
      if (editor_->hasSelection()) editor_->deleteForward();
      else if (ctrl) editor_->deleteWordForward();
      else editor_->deleteForward();
      afterEdit();
      break;
    case static_cast<int>(Key::Up):
    case static_cast<int>(Key::Down):
      if (onStep) {
        const core::tree::WidgetId self = id();
        if (dirty_) commit();
        if (ui().alive(self) && onStep) onStep((e.key == Key::Up ? 1 : -1) * (shift ? 10 : 1));
      } else {
        used = false;
      }
      break;
    case 'A':
      if (ctrl) editor_->selectAll();
      else used = false;
      break;
    case 'C':
      if (ctrl) editor_->copy();
      else used = false;
      break;
    case 'X':
      if (ctrl) {
        editor_->cut();
        afterEdit();
      } else {
        used = false;
      }
      break;
    case 'V':
      if (ctrl) {
        editor_->paste();
        afterEdit();
      } else {
        used = false;
      }
      break;
    case 'Z':
      if (ctrl) {
        if (shift) editor_->redo();
        else editor_->undo();
        afterEdit();
      } else {
        used = false;
      }
      break;
    case 'Y':
      if (ctrl) {
        editor_->redo();
        afterEdit();
      } else {
        used = false;
      }
      break;
    default: used = false; break;
  }
  if (used) {
    e.markHandled();
    requestPaint();
  }
}

}  // namespace r1ui::widgets

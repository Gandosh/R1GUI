// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of CommandDetail.h.
// Invariants: the recorder changes nothing but its own state and calls back once per recording; it is
//   cancelled by Escape, focus loss and destruction; the detail view draws only what setDetail gave it
//   (description lines are wrapped at paint time, bounded to kMaxDescriptionLines).
// Callers: HotkeyEditor, tests.
#include "r1ui/widgets/hotkeys/CommandDetail.h"

#include <algorithm>

#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/tooltip/TooltipContent.h"

namespace r1ui::widgets {

namespace layout = core::layout;
namespace cmd = commands;
using core::events::Key;
namespace Mod = core::events::Mod;

namespace {

constexpr double kCaptionWidth = 84.0;
constexpr double kRowGap = 6.0;
constexpr double kLineHeight = 18.0;
constexpr size_t kMaxDescriptionLines = 6;
constexpr size_t kMaxConflictLines = 6;
constexpr double kRecorderHeight = 30.0;
constexpr double kSheetHeight = 330.0;  // eight one-line rows, a description of up to six lines and the conflicts

}  // namespace

std::string modifierPrefix(uint8_t modifiers) {
  std::string text;
  if ((modifiers & Mod::kCtrl) != 0) text += "Ctrl+";
  if ((modifiers & Mod::kMeta) != 0) text += "Cmd+";
  if ((modifiers & Mod::kAlt) != 0) text += "Alt+";
  if ((modifiers & Mod::kShift) != 0) text += "Shift+";
  return text;
}

// ---- detail view ----------------------------------------------------------------------------------------

void CommandDetailView::onAttached() {
  style().flexGrow = 0.0;
  style().flexShrink = 1.0;
  style().height = layout::Length::px(kSheetHeight);
  style().minHeight = layout::Length::px(60);
  style().overflow = layout::Overflow::Hidden;
}

void CommandDetailView::setDetail(CommandDetail detail) {
  detail_ = std::move(detail);
  requestPaint();
}

void CommandDetailView::paint(PaintContext& ctx) {
  const layout::Rect area = ctx.rect();
  const theme::TextStyle body = ctx.style("label.body").text;
  const theme::TextStyle muted = ctx.style("label.muted").text;
  ctx.painter().pushClip(ctx.box());
  if (!detail_.valid) {
    TextOptions o;
    o.align = TextAlign::Center;
    ctx.drawText("Select an action in the list to see its details", muted, ctx.box(), o);
    ctx.painter().popClip();
    return;
  }
  double y = area.y + 4.0;
  const double valueX = area.x + kCaptionWidth;
  const double valueW = std::max(40.0, static_cast<double>(area.w) - kCaptionWidth - 4.0);
  const auto line = [&](std::string_view caption, std::string_view value, bool strong = false, const theme::TextStyle* valueStyle = nullptr) {
    TextOptions c;
    ctx.drawText(caption, muted, ctx.toPhysical(area.x + 4.0, y, kCaptionWidth - 6.0, kLineHeight), c);
    TextOptions v;
    if (strong) v.weight = 600;
    ctx.drawText(value, valueStyle != nullptr ? *valueStyle : body, ctx.toPhysical(valueX, y, valueW, kLineHeight), v);
    y += kLineHeight + kRowGap;
  };
  line("Action", detail_.label, true);
  line("Id", detail_.id);
  // Description: wrapped, several lines.
  {
    TextOptions c;
    ctx.drawText("Description", muted, ctx.toPhysical(area.x + 4.0, y, kCaptionWidth - 6.0, kLineHeight), c);
    const std::vector<std::string> lines = wrapTooltipText(ctx.ui(), detail_.description.empty() ? std::string("(none)") : detail_.description, body.fontSize, body.weight, valueW, kMaxDescriptionLines);
    for (const std::string& l : lines) {
      TextOptions v;
      ctx.drawText(l, body, ctx.toPhysical(valueX, y, valueW, kLineHeight), v);
      y += kLineHeight;
    }
    y += kRowGap;
  }
  line("Category", detail_.category);
  line("Context", detail_.context);
  line("Kind", detail_.kind);
  line("Default", detail_.defaults.empty() ? "none" : detail_.defaults);
  line("Current", detail_.current.empty() ? "unassigned" : detail_.current, true);
  line("Enabled", detail_.enabled ? "Yes" : "No");
  {
    TextOptions c;
    ctx.drawText("Conflicts", muted, ctx.toPhysical(area.x + 4.0, y, kCaptionWidth - 6.0, kLineHeight), c);
    if (detail_.conflicts.empty()) {
      TextOptions v;
      ctx.drawText("None", body, ctx.toPhysical(valueX, y, valueW, kLineHeight), v);
      y += kLineHeight;
    }
    for (size_t i = 0; i < detail_.conflicts.size() && i < kMaxConflictLines; ++i) {
      TextOptions v;
      v.color = ctx.color("warning-text");
      ctx.drawText(detail_.conflicts[i], body, ctx.toPhysical(valueX, y, valueW, kLineHeight), v);
      y += kLineHeight;
    }
    if (detail_.conflicts.size() > kMaxConflictLines) {
      TextOptions v;
      ctx.drawText("and " + std::to_string(detail_.conflicts.size() - kMaxConflictLines) + " more", muted, ctx.toPhysical(valueX, y, valueW, kLineHeight), v);
    }
  }
  ctx.painter().popClip();
}

// ---- recorder -----------------------------------------------------------------------------------------------

void ChordRecorder::onAttached() {
  setFocusable(true);
  style().height = layout::Length::px(kRecorderHeight);
  style().flexShrink = 0.0;
  style().flexGrow = 1.0;
}

void ChordRecorder::setIdleText(std::string text) {
  if (text == idle_) return;
  idle_ = std::move(text);
  requestPaint();
}

void ChordRecorder::begin() {
  if (recording_) return;
  recording_ = true;
  preview_.clear();
  ui().focusWidget(id(), core::events::FocusReason::Keyboard);
  requestPaint();
}

void ChordRecorder::end() {
  recording_ = false;
  preview_.clear();
  requestPaint();
}

void ChordRecorder::cancel() {
  if (!recording_) return;
  end();
  if (onCancelled_) {
    auto callback = onCancelled_;
    callback();
  }
}

void ChordRecorder::onClick(Event& e) {
  if (e.button != core::events::Button::Left) return;
  e.markHandled();
  begin();
}

void ChordRecorder::onKeyDown(Event& e) {
  if (!recording_) {
    if ((e.key == Key::Enter || e.key == Key::Space) && e.modifiers == 0) {
      begin();
      e.markHandled();
    }
    return;
  }
  e.markHandled();
  const uint8_t mods = static_cast<uint8_t>(e.modifiers & cmd::kAllModifiers);
  if (!cmd::isRealKey(e.key)) {  // a modifier press: show it live
    preview_ = modifierPrefix(mods);
    requestPaint();
    return;
  }
  if (e.key == Key::Escape && mods == 0) {
    cancel();
    return;
  }
  const cmd::KeyChord chord{e.key, mods, false};
  end();
  if (chord.valid() && onChord_) {
    auto callback = onChord_;
    callback(chord);
  }
}

void ChordRecorder::onKeyUp(Event& e) {
  if (recording_) e.markHandled();
}

void ChordRecorder::onTextInput(Event& e) {
  if (recording_) e.markHandled();  // typed characters never become part of a chord
}

void ChordRecorder::onFocusOut(Event&) { cancel(); }

void ChordRecorder::paint(PaintContext& ctx) {
  const float radius = ctx.px(6.0);
  const render::Rect box = ctx.box();
  ctx.painter().fillRoundedRect(box, render::CornerRadii::uniform(radius), ctx.color("input"));
  ctx.painter().border(box, render::CornerRadii::uniform(radius), ctx.hairline(), ctx.color(recording_ ? "accent" : "border"));
  const theme::TextStyle body = ctx.style("label.body").text;
  const theme::TextStyle muted = ctx.style("label.muted").text;
  TextOptions o;
  o.padLeft = 10.0;
  if (recording_) {
    const bool empty = preview_.empty();
    o.color = ctx.color(empty ? muted.color : body.color);
    ctx.drawText(empty ? std::string("Press the keys... (Esc cancels)") : preview_, empty ? muted : body, box, o);
  } else {
    o.color = ctx.color(idle_.empty() ? muted.color : body.color);
    ctx.drawText(idle_.empty() ? std::string("Click, then press a shortcut") : idle_, idle_.empty() ? muted : body, box, o);
  }
}

void ChordRecorder::paintOver(PaintContext& ctx) {
  if (focusVisible() && !recording_) ctx.focusRing(ctx.px(6.0));
}

}  // namespace r1ui::widgets

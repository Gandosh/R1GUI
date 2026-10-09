// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of ChordBox.h: style rows, drawing and the gestures it reports to the editor.
// Invariants: the remove button exists from attachment on and is visible (not display-none) only to
//   take space: it is hidden through the visible flag while editing or empty, so the box width never
//   changes; the box keeps no capture logic, it forwards.
// Callers: KeybindingEditor, UiContext (events, paint).
#include "r1ui/widgets/commands/ChordBox.h"

#include "r1ui/widgets/commands/KeybindingEditor.h"
#include "r1ui/widgets/iconbutton/IconButton.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/textinput/FieldChrome.h"

namespace r1ui::widgets {

namespace {

using theme::State::kDisabled;
using theme::State::kFocus;
using theme::State::kHover;
using theme::State::kNone;
using theme::State::kSelected;
using theme::StyleProperty;

constexpr theme::StyleRuleEntry kRows[] = {
    {"chordbox", kNone, StyleProperty::Background, "color:input"},
    {"chordbox", kNone, StyleProperty::Foreground, "color:surface"},
    {"chordbox", kNone, StyleProperty::BorderColor, "color:border"},
    {"chordbox", kNone, StyleProperty::BorderWidth, "number:1"},
    {"chordbox", kNone, StyleProperty::Radius, "metric:field.radius"},
    {"chordbox", kNone, StyleProperty::Height, "metric:field.height"},
    {"chordbox", kNone, StyleProperty::PaddingX, "number:8"},
    {"chordbox", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"chordbox", kNone, StyleProperty::LineHeight, "lineHeight:xs"},
    {"chordbox", kHover, StyleProperty::BorderColor, "color:border-strong"},
    {"chordbox", kFocus, StyleProperty::BorderColor, "color:accent"},
    {"chordbox", kSelected, StyleProperty::BorderColor, "color:accent"},
    {"chordbox", kDisabled, StyleProperty::Opacity, "number:0.5"},
    {"chordbox.hint", kNone, StyleProperty::Foreground, "color:muted"},
};

constexpr double kRemoveButtonWidth = 20.0;
constexpr double kFallbackHeight = 28.0;

}  // namespace

std::span<const theme::StyleRuleEntry> ChordBox::styleRows() { return kRows; }

void ChordBox::onAttached() {
  core::layout::Style& s = style();
  const double height = ui().services().resolve("chordbox", 0).height;
  s.height = core::layout::Length::px(height > 0.0 ? height : kFallbackHeight);
  s.direction = core::layout::FlexDirection::Row;
  s.alignItems = core::layout::Align::Center;
  s.justifyContent = core::layout::Justify::End;
  s.flexShrink = 0.0;
  s.padding[core::layout::kRight] = 3.0;
  setFocusable(true);
  IconButton& remove = ui().create<IconButton>(id(), "x", IconButtonSize::Sm);
  remove.setTooltip("Remove binding");
  remove.setAccessibleName("Remove binding");
  remove.setFocusable(false);  // spec 07 rule 50: not reachable by keyboard
  remove.setOnClick([ui = &ui(), editor = editor_, self = id()] {
    KeybindingEditor* owner = ui->objectAs<KeybindingEditor>(editor);
    ChordBox* box = ui->objectAs<ChordBox>(self);
    if (owner != nullptr && box != nullptr) owner->boxRemove(*box);
  });
  remove_ = remove.id();
  updateRemoveButton();
}

std::string_view ChordBox::accessibleName() const {
  return WidgetObject::accessibleName().empty() ? std::string_view(commandId_) : WidgetObject::accessibleName();
}

float ChordBox::paintOpacity() const { return static_cast<float>(ui().services().resolve("chordbox", styleState()).opacity); }

const std::string& ChordBox::shownText() const {
  if (!editing_) return chordText_;
  return preview_.empty() ? hint_ : preview_;
}

void ChordBox::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.style("chordbox");
  paintFieldBox(ctx, rs, animatedFieldColors(ctx, rs, 0), ctx.box());
  const std::string& text = shownText();
  if (text.empty()) return;
  TextOptions options;
  options.padLeft = rs.paddingX;
  options.padRight = rs.paddingX + (editing_ || chordText_.empty() ? 0.0 : kRemoveButtonWidth);
  if (placeholderShown()) options.color = ctx.color(ctx.resolve("chordbox.hint", 0).text.color);
  ctx.drawText(text, rs.text, ctx.box(), options);
}

void ChordBox::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.px(ctx.style("chordbox").radius));
}

void ChordBox::updateRemoveButton() {
  WidgetObject* remove = ui().object(remove_);
  if (remove == nullptr) return;
  const bool visible = !editing_ && !chordText_.empty();
  if (remove->node().flags.visible == visible) return;
  remove->node().flags.visible = visible;  // still takes its space when hidden (spec 07 rule 49)
  remove->requestPaint();
}

void ChordBox::setChordText(std::string text) {
  if (text == chordText_) return;
  chordText_ = std::move(text);
  updateRemoveButton();
  requestPaint();
}

void ChordBox::setHint(std::string hint) {
  if (hint == hint_) return;
  hint_ = std::move(hint);
  requestPaint();
}

void ChordBox::setEditing(bool editing) {
  if (editing == editing_) return;
  editing_ = editing;
  preview_.clear();
  setSelected(editing);
  updateRemoveButton();
  requestPaint();
}

void ChordBox::setPreview(std::string preview) {
  if (preview == preview_) return;
  preview_ = std::move(preview);
  requestPaint();
}

// ---- gestures -----------------------------------------------------------------------------------

void ChordBox::onClick(Event& e) {
  if (e.button != core::events::Button::Left) return;
  e.markHandled();
  if (editing_) return;
  if (KeybindingEditor* owner = ui().objectAs<KeybindingEditor>(editor_)) owner->boxClicked(*this);
}

void ChordBox::onKeyDown(Event& e) {
  KeybindingEditor* owner = ui().objectAs<KeybindingEditor>(editor_);
  if (owner == nullptr) return;
  if (editing_) {
    owner->boxCaptureKey(*this, e);
    return;
  }
  const bool plain = (e.modifiers & (core::events::Mod::kCtrl | core::events::Mod::kAlt | core::events::Mod::kMeta | core::events::Mod::kShift)) == 0;
  if (!plain) return;
  using core::events::Key;
  switch (e.key) {
    case Key::Enter:
    case Key::Space:
      e.markHandled();
      if (!e.repeat) owner->boxClicked(*this);
      return;
    case Key::Delete:
    case Key::Backspace:
      e.markHandled();
      owner->boxRemove(*this);
      return;
    case Key::Up:
    case Key::Down:
      e.markHandled();
      owner->boxNavigate(*this, e.key == Key::Up ? -1 : 1);
      return;
    default: return;
  }
}

void ChordBox::onKeyUp(Event& e) {
  if (editing_) e.markHandled();  // the release of a captured key is ignored (spec 07 rule 44)
}

void ChordBox::onFocusOut(Event&) {
  if (!editing_) return;
  if (KeybindingEditor* owner = ui().objectAs<KeybindingEditor>(editor_)) owner->boxFocusLost(*this);
}

}  // namespace r1ui::widgets

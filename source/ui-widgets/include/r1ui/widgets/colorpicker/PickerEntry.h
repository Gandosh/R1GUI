// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: PickerEntry, the minimal single-line text entry the colour picker, the gradient editor and
//   the curve editor use for their numeric and hex fields (hue, alpha %, RGB triple, stop position,
//   key time / value): a TextEditor shaped with the shared text engine, caret and selection
//   painting, pointer placement, keyboard editing, clipboard, commit / revert rules.
// Why: the shared TextInput / NumberField widgets are being built by another group at the same
//   time; the pickers must not depend on them. After the merge an integrator may replace this
//   private helper with the shared widget (the only API the pickers use is setText, text,
//   setSuffix, onCommit and onStep).
// Callers: ColorSliderRow, ChannelFields, GradientStopRow, CurveKeyFields. Calls: ui-text
//   TextEditor, TextEngine (through UiContext), the host clipboard hooks.
// Edit protocol (spec 08 rules 78-80 and the focus spec): while the user has changed the text it is
//   "dirty"; Enter commits (onCommit returns whether the text was accepted, a rejected text reverts
//   to the last accepted one), Escape reverts a dirty entry and is otherwise left to the enclosing
//   popup, losing focus commits a dirty entry the same way as Enter. setText is the owner's
//   programmatic path: it is applied at once unless the entry is dirty, in which case the newest
//   value is kept and shown after the commit or revert.
// Focus: a press focuses the entry with the caret at the pointer; arriving by Tab selects all. The
//   focused look is the focus border of the style row; no ring is drawn.
// Not covered: IME composition, blinking caret, bidirectional caret movement (the TextEditor's).
#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "r1ui/text/TextEditor.h"
#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class PickerEntry : public WidgetObject {
 public:
  // Field: panel field fill (hue / alpha value boxes); Input: input fill with a 1 px border (hex and
  // stop fields); Bare: no chrome of its own (the cells of the channel triple).
  enum class Look : uint8_t { Field, Input, Bare };

  explicit PickerEntry(Look look = Look::Field);
  ~PickerEntry() override;
  static std::span<const theme::StyleRuleEntry> styleRows();

  const char* typeName() const override { return "PickerEntry"; }
  void onAttached() override;
  Cursor cursor() const override { return Cursor::Text; }
  void paint(PaintContext& ctx) override;
  std::string_view accessibleName() const override;

  // ---- content ----
  const std::string& text() const;
  void setText(std::string_view text);
  void setSuffix(std::string suffix);
  void setFontSize(double logicalPx);
  // Left inset of the text in logical px; negative restores the style row's padding.
  void setPadLeft(double logicalPx);
  void setMaxBytes(size_t bytes);
  bool dirty() const { return dirty_; }

  // ---- behaviour ----
  // Accepts the text typed by the user; false rejects it (the entry reverts).
  std::function<bool(std::string_view)> onCommit;
  // Up / Down arrows: +-1, or +-10 with Shift. Absent: the keys are ignored.
  std::function<void(int steps)> onStep;
  // The entry took keyboard focus (a row that contains it can select itself).
  std::function<void()> onFocused;

  // ---- input ----
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onKeyDown(Event& e) override;
  void onTextInput(Event& e) override;
  void onFocusIn(Event& e) override;
  void onFocusOut(Event& e) override;
  void onCaptureLost(Event&) override { selecting_ = false; }
  uint8_t styleState() const override;

  // ---- test hooks ----
  const r1ui::text::TextEditor& editor() const { return *editor_; }

 private:
  const char* rowKey() const;
  double fontPx() const;
  void ensureLayout();
  double padLeft() const;      // logical inset of the text origin
  float textLeft() const;      // physical offset of the text origin inside the box
  float textRoom() const;      // physical width available to the text
  void commit();
  void revert();
  void afterEdit();
  size_t offsetAt(double localX);

  Look look_;
  std::unique_ptr<r1ui::text::TextEditor> editor_;
  std::string baseline_;      // last accepted / programmatic text
  std::string pending_;       // newest programmatic text while dirty
  bool hasPending_ = false;
  bool dirty_ = false;
  bool selecting_ = false;
  bool selectAllOnNextPaint_ = false;
  std::string suffix_;
  double fontSize_ = 0.0;     // 0 = the row's size
  double padLeft_ = -1.0;     // < 0 = the row's padding
  size_t shapedRevision_ = static_cast<size_t>(-1);
  float shapedSize_ = 0.0f;
};

}  // namespace r1ui::widgets

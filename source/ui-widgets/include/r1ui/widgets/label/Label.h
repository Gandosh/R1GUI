// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the Label widget: one line of static text, drawn from the theme (role -> style row
//   label.*), shortened with an ellipsis when its box is narrower than the text.
// Why: every other widget (buttons, fields, rows, menus) needs the same "measure text, place it in
//   a line box, truncate" behaviour; Label is the smallest complete widget and the TEMPLATE new
//   widgets copy: it shows the file layout, style rows, measure hook, paint contract and tests.
// Callers: application code and other widgets (as a child), TooltipManager. Calls: TextEngine via
//   PaintContext / UiContext.
// Behaviour: the label measures to its natural text width and the style's line height; as a flex
//   item it may shrink (flexShrink 1, no minimum), then paint() truncates with U+2026 on a
//   grapheme boundary. Pointer events pass through it (the parent is hovered and pressed, the
//   parent's tooltip applies) unless setInteractive(true).
// Style: role selects the row (label.body, label.muted, label.caption, label.heading, label.title,
//   label.danger in ui-theme's builtin table). A colour override takes a token name. Disabled
//   labels use the row's disabled opacity.
// Units: logical pixels; the measured width is computed at the display scale so it equals what is
//   drawn (shaping is size dependent at fractional scales).
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

enum class LabelRole : uint8_t { Body, Muted, Caption, Heading, Title, Danger };

class Label : public WidgetObject {
 public:
  explicit Label(std::string text = {}, LabelRole role = LabelRole::Body) : text_(std::move(text)), role_(role) {}

  const char* typeName() const override { return "Label"; }

  // ---- content and look (each change requests the layout or paint it needs) ----
  const std::string& text() const { return text_; }
  void setText(std::string text);
  LabelRole role() const { return role_; }
  void setRole(LabelRole role);
  // Token name of the theme colour that replaces the role's colour; empty restores the role's.
  void setColorToken(std::string token);
  void setAlign(TextAlign align);
  void setEllipsis(bool ellipsis);
  void setInteractive(bool interactive);
  // The style row the label resolves (for tests).
  static const char* styleKeyFor(LabelRole role);

  // ---- WidgetObject ----
  void onAttached() override;
  core::layout::MeasureResult measure(const core::layout::MeasureInput& input) override;
  float paintOpacity() const override;
  void paint(PaintContext& ctx) override;
  std::string_view accessibleName() const override;

 private:
  theme::ResolvedStyle resolved() const;

  std::string text_;
  LabelRole role_;
  std::string colorToken_;
  TextAlign align_ = TextAlign::Start;
  bool ellipsis_ = true;
};

}  // namespace r1ui::widgets

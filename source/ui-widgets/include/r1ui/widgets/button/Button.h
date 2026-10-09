// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the Button widget: a text, icon or icon + text push button in the tones ghost, accent,
//   panel, panelAccent and neutral and the sizes sm (28), md (32), icon (32 x 32), iconSm (28 x 28)
//   of docs/spec/widgets.md 2.1.
// Why: every dialog, popover and bar needs the same push button; this is the one implementation
//   (colours from style rows btn.<tone>, sizes from btn.size.<size>, never hard-coded).
// Callers: application code, dialogs, popovers. Calls: Pressable (input), PaintContext (drawing).
// Look: content (leading icon, text, trailing icon, gap 6) is centred in the box; when the box is
//   too narrow the text is shortened with an ellipsis before the icons are touched. The fill and the
//   text colour change over 150 ms (the neutral tone changes instantly, as measured). Disabled draws
//   at 50% opacity and takes no input. Keyboard focus draws the 1 px focus ring over the bounds.
//   The panel tones draw the translucent fill without the reference's 24 px backdrop blur
//   (Approximate).
// Layout: natural width = 2 x padding + content; a style width overrides it. Height from the size.
// Behaviour: see Pressable (click on release inside, Space/Enter on key-up). The click callback may
//   destroy the button.
// Failure behavior: an icon name that is not a plain file name is rejected (the previous icon stays);
//   a well formed name with no SVG throws std::runtime_error from paint (IconCache contract).
#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>

#include "r1ui/widgets/button/Pressable.h"

namespace r1ui::widgets {

enum class ButtonTone : uint8_t { Ghost, Accent, Panel, PanelAccent, Neutral };
enum class ButtonSize : uint8_t { Sm, Md, Icon, IconSm };

// True for names made of letters, digits, '-' and '_' (an icon file name without extension). Shared
// by Button and IconButton.
bool isValidIconName(std::string_view name);

class Button : public Pressable {
 public:
  explicit Button(std::string text = {}, ButtonTone tone = ButtonTone::Ghost, ButtonSize size = ButtonSize::Sm)
      : text_(std::move(text)), tone_(tone), size_(size) {}

  static std::span<const theme::StyleRuleEntry> styleRows();
  const char* typeName() const override { return "Button"; }

  // ---- content and look ----
  const std::string& text() const { return text_; }
  void setText(std::string text);
  // Leading / trailing icon names (Lucide or toolkit icons, no extension); empty removes it.
  bool setIcon(std::string name);
  bool setTrailingIcon(std::string name);
  const std::string& icon() const { return icon_; }
  // Logical icon size in the content row; non-finite or outside 4..64 is rejected. Default 14 for
  // text buttons, 16 (icon) and 14 (iconSm) for icon-only sizes.
  bool setIconSize(double size);
  void setTone(ButtonTone tone);
  void setSize(ButtonSize size);
  ButtonTone tone() const { return tone_; }
  ButtonSize size() const { return size_; }
  // Overrides of the tone's corner radius, side padding and weight; rejected when non-finite or < 0.
  bool setRadius(double radius);
  bool setPaddingX(double padding);
  void setWeight(int weight);
  void setOnClick(std::function<void()> callback) { onClick_ = std::move(callback); }
  // Programmatic activation (what a click does); false while disabled.
  bool click();

  // ---- WidgetObject ----
  void onAttached() override;
  core::layout::MeasureResult measure(const core::layout::MeasureInput& input) override;
  float paintOpacity() const override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  std::string_view accessibleName() const override;

  static const char* toneKey(ButtonTone tone);
  static const char* sizeKey(ButtonSize size);

 protected:
  void activate() override;

 private:
  double sizeHeight() const;
  double iconSize() const;
  double paddingX() const;
  double radius() const;

  std::string text_;
  std::string icon_;
  std::string trailingIcon_;
  ButtonTone tone_;
  ButtonSize size_;
  double iconSize_ = 0.0;     // 0 = the size's default
  double radius_ = -1.0;      // < 0 = the tone's
  double paddingX_ = -1.0;
  int weight_ = -1;
  std::function<void()> onClick_;
};

}  // namespace r1ui::widgets

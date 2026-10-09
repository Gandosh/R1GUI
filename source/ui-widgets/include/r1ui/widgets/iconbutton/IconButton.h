// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the IconButton widget: a square, transparent button with one icon (Lucide or toolkit icon
//   through the IconCache): sm (20 x 20) and md (26 x 26, panel) of docs/spec/widgets.md 2.2, plus a
//   custom box and icon size for the other measured variants (24 dialog close, 16 add-stop).
// Why: every panel header, field adornment and dialog needs the same glyph button; one widget owns
//   its colours (rows iconbtn.*), states and behaviour.
// Callers: application code, panels, dialogs. Calls: Pressable (input), PaintContext (drawing).
// Look: transparent, `muted` glyph; hover turns the glyph `surface` and (md) fills `hover`, over
//   150 ms; active (setActive) draws a 1 px `accent` border and glyph; keyboard focus draws the 1 px
//   `panel-focus` ring; disabled is 50% opacity and takes no input. Sm does not fill on hover
//   (measured: only the glyph changes); setHoverFill overrides.
// Behaviour: see Pressable. The click callback may destroy the button. Tooltip text is the base
//   class' setTooltip; the accessible name falls back to the icon name.
// Failure behavior: an icon name that is not a plain file name is rejected (the previous icon stays);
//   a well formed name with no SVG throws std::runtime_error from paint (IconCache contract).
#pragma once

#include <functional>
#include <span>
#include <string>

#include "r1ui/widgets/button/Pressable.h"

namespace r1ui::widgets {

enum class IconButtonSize : uint8_t { Sm, Md };

class IconButton : public Pressable {
 public:
  explicit IconButton(std::string icon = {}, IconButtonSize size = IconButtonSize::Md);

  static std::span<const theme::StyleRuleEntry> styleRows();
  const char* typeName() const override { return "IconButton"; }

  bool setIcon(std::string name);
  const std::string& icon() const { return icon_; }
  void setSizeClass(IconButtonSize size);
  // Box edge and glyph size in logical px; non-finite or outside 8..128 (box) / 4..64 (glyph) is rejected.
  bool setBoxSize(double size);
  bool setIconSize(double size);
  bool setRadius(double radius);
  void setHoverFill(bool fill);
  void setActive(bool active) { setSelected(active); }
  bool active() const { return hasState(StateFlag::kSelected); }
  void setOnClick(std::function<void()> callback) { onClick_ = std::move(callback); }
  bool click();

  void onAttached() override;
  float paintOpacity() const override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  std::string_view accessibleName() const override;

 protected:
  void activate() override;

 private:
  void applySize();

  std::string icon_;
  IconButtonSize sizeClass_;
  double box_ = 26.0;
  double iconSize_ = 14.0;
  double radius_ = -1.0;
  bool hoverFill_ = true;
  std::function<void()> onClick_;
};

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: ActionButton, the small icon-only push button used inside section headers, panel headers,
//   tab bars, toolbars and tree rows: a box with a tinted icon, hover / pressed / focus / disabled /
//   active looks taken from a style key, activation by click (release inside) and by Space / Enter,
//   150 ms colour transitions, tooltip and accessible name support.
// Why: five widgets of this group need the same pressable icon with different sizes and colours; the
//   behaviour (capture, release-inside activation, keyboard press on key-down and fire on key-up,
//   disabled and destroy-in-handler safety) is written once and the look is data (a style key).
//   The general-purpose IconButton of the toolkit is a separate widget built elsewhere; this one is
//   private to the section / tab bar / toolbar / tree widgets and carries no public contract
//   beyond them.
// Callers: PropertySection, PanelHeader, TabBar, Toolbar, TreeView (as a drawing helper only).
// Style key: the key names rows (registered by the owning widget) for Background, Foreground,
//   BorderColor, BorderWidth, Radius and Opacity with the usual state bits: hover, active (pressed),
//   focus (drawn as the border colour row), selected (the "active tool" look), disabled.
// Invariants: the callback runs after the pressed state has been cleared, may destroy the button
//   (the object outlives the dispatch), and is never called while disabled.
#pragma once

#include <functional>
#include <string>

#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class ActionButton : public WidgetObject {
 public:
  // `iconSize` is the glyph size in logical px; the box size is set on the style by the owner.
  ActionButton(std::string icon, std::string styleKey, double iconSize = 14.0)
      : icon_(std::move(icon)), styleKey_(std::move(styleKey)), iconSize_(iconSize) {}

  const char* typeName() const override { return "ActionButton"; }
  void onAttached() override;
  Cursor cursor() const override { return enabled() ? Cursor::Pointer : Cursor::Default; }
  float paintOpacity() const override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  void onClick(Event& e) override;
  void onKeyDown(Event& e) override;
  void onKeyUp(Event& e) override;
  void onFocusOut(Event& e) override;
  void onStateChanged(uint16_t previous) override;
  uint8_t styleState() const override;
  std::string_view accessibleName() const override;

  const std::string& icon() const { return icon_; }
  void setIcon(std::string icon);
  void setSize(double width, double height);
  void setOnActivate(std::function<void(ActionButton&)> callback) { onActivate_ = std::move(callback); }
  // The "active" (selected tool) look; toggles kSelected.
  void setActive(bool active) { setSelected(active); }
  bool active() const { return hasState(StateFlag::kSelected); }
  // Runs the callback as a click would (when enabled). Returns whether it ran.
  bool activate();

 private:
  std::string icon_;
  std::string styleKey_;
  double iconSize_;
  bool keyPressed_ = false;
  std::function<void(ActionButton&)> onActivate_;
};

}  // namespace r1ui::widgets

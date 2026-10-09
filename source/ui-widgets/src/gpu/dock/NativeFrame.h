// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: NativeFrame and NativeHolder, the two widgets in the root of every native floating window's
//   UiContext: the frame draws the window background, the toolkit title bar (title text, maximize and
//   close buttons) and the 1 px border; the holder is the content area the dock mounts its area view
//   in. Also the description of the window's chrome regions (caption, buttons) for the platform.
// Why: the OS window is borderless, so everything the user sees of the "frame" is toolkit-drawn. The
//   frame does not handle presses: the platform answers the OS hit test from the chrome description
//   (caption drag, edge resize, snap layouts, double-click maximize, button presses), and reports
//   pointer moves over those regions as ordinary moves, which is all the hover look needs.
// Callers: NativeWindow (creates both, pushes the chrome description to the platform window after
//   every layout pass).
// Units: logical pixels in window coordinates; chromeLayout() converts to physical pixels.
#pragma once

#include <span>
#include <string>

#include "r1ui/platform/ChromeHitTest.h"
#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/dock/native/NativeCoords.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets::native {

// The content area; the dock mounts its area view as a child. Clips its children like a window does.
class NativeHolder : public WidgetObject {
 public:
  const char* typeName() const override { return "NativeHolder"; }
  void onAttached() override;
};

class NativeFrame : public WidgetObject {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows();

  explicit NativeFrame(FrameMetrics metrics) : metrics_(metrics) {}

  const char* typeName() const override { return "NativeFrame"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  void onPointerMove(Event& e) override;
  void onPointerLeave(Event& e) override;

  void setTitle(std::string title);
  void setMaximized(bool maximized);
  void setResizable(bool resizable);
  bool maximized() const { return maximized_; }

  // The regions the platform needs, in physical pixels for the current client width and scale. The
  // maximize button exists only for a resizable window.
  platform::ChromeLayout chromeLayout(double clientWidthLogical, float scale) const;

 private:
  enum class Part : uint8_t { None, Maximize, Close };
  dock::Rect closeRect(double clientWidth) const;
  dock::Rect maximizeRect(double clientWidth) const;
  Part partAt(double x, double y) const;
  void setHover(Part part);

  FrameMetrics metrics_;
  std::string title_;
  bool maximized_ = false;
  bool resizable_ = true;
  Part hover_ = Part::None;
};

}  // namespace r1ui::widgets::native

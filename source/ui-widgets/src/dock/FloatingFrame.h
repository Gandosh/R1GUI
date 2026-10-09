// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: FloatingFrame, the widget InWindowFloatingBackend uses for one floating window: shadow,
//   rounded body, title bar with title text, maximize and close buttons, the move and resize gestures
//   and the cursors for them; and FloatingHolder, the clipped child that holds a window's content.
// Why: the frame draws the chrome and translates gestures into "wanted content rectangle" requests;
//   the backend owns the rectangle rules (limits, keeping the title reachable), so the same rules
//   apply to gestures and to API calls.
// Callers: InWindowFloatingBackend only (private header).
// Invariants: the frame never changes its own geometry; it asks the backend and the backend writes
//   the style. A gesture ends on release, capture loss or Escape-free cancel (capture lost).
#pragma once

#include <span>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/dock/InWindowFloatingBackend.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class FloatingHolder : public WidgetObject {
 public:
  const char* typeName() const override { return "FloatingHolder"; }
  void onAttached() override;
};

class FloatingFrame : public WidgetObject {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows();

  FloatingFrame(InWindowFloatingBackend& backend, FloatId window, double titleHeight, double border, double radius, double band)
      : backend_(&backend), window_(window), titleHeight_(titleHeight), border_(border), radius_(radius), band_(band) {}

  const char* typeName() const override { return "FloatingFrame"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  uint8_t phases() const override;
  Cursor cursor() const override;
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onClick(Event& e) override;
  void onDoubleClick(Event& e) override;
  void onCaptureLost(Event& e) override;

  void setTitle(std::string title) { title_ = std::move(title); requestPaint(); }
  void setMaximizable(bool on) { maximizable_ = on; requestPaint(); }
  void setResizable(bool on) { resizable_ = on; }
  void setMaximized(bool on) { maximized_ = on; requestPaint(); }
  // A hidden frame is not drawn but stays in the tree and keeps its children (and any pointer capture inside them) alive: the window of a tab being dragged hides this way.
  void setHidden(bool hidden) { hidden_ = hidden; requestPaint(); }
  float paintOpacity() const override { return hidden_ ? 0.0f : 1.0f; }
  dock::Rect closeButtonRect() const;
  dock::Rect maximizeButtonRect() const;
  dock::Rect titleRect() const;

 private:
  enum Edge : unsigned { kNone = 0, kLeft = 1, kRight = 2, kTop = 4, kBottom = 8 };
  enum class Part : uint8_t { None, Title, Close, Maximize, Edge };
  struct Hit {
    Part part = Part::None;
    unsigned edges = kNone;
    friend bool operator==(const Hit&, const Hit&) = default;
  };
  Hit hitTest(double x, double y) const;
  void setHover(Hit hit);

  InWindowFloatingBackend* backend_;
  FloatId window_;
  double titleHeight_;
  double border_;
  double radius_;
  double band_;
  std::string title_;
  bool maximizable_ = true;
  bool resizable_ = true;
  bool maximized_ = false;
  bool hidden_ = false;
  Hit hover_;
  Hit pressed_;
  bool gesture_ = false;
  dock::Rect startRect_;
  double startX_ = 0.0, startY_ = 0.0;
};

}  // namespace r1ui::widgets

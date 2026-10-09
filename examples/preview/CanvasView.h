// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CanvasView, the document canvas of the composed screen: the `canvas` colour, the document's
//   rectangle drawn from AppDocument (fill with alpha and opacity, corner radius), the selection
//   outline with eight handles when the rectangle is selected, selection by click, moving the
//   rectangle by dragging it (live X / Y in the panel through the change callback) and the right
//   click that asks for a context menu.
// Why: it is the proof that panel fields, layer tree and canvas are one document: every edit in
//   the panel repaints here and a drag here updates the panel.
// Callers: ComposedApp. Calls: PaintContext. Rotation is stored and edited but not drawn (the
//   Painter has no rotated rectangles); the canvas origin maps document (0,0) to the canvas
//   top-left shifted by kOriginX / kOriginY so the default rectangle sits in the view.
// Units: logical pixels; document units equal logical pixels at zoom 1 (there is no zoom).
#pragma once

#include <functional>

#include "AppDocument.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace preview {

class CanvasView final : public r1ui::widgets::WidgetObject {
 public:
  static constexpr double kOriginX = -21.0;
  static constexpr double kOriginY = -80.0;

  explicit CanvasView(AppDocument& document) : doc_(document) {}
  const char* typeName() const override { return "CanvasView"; }
  void onAttached() override;
  void paint(r1ui::widgets::PaintContext& ctx) override;
  void paintOver(r1ui::widgets::PaintContext& ctx) override;
  r1ui::widgets::Cursor cursor() const override;
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onCaptureLost(Event& e) override;

  // The user selected or deselected the rectangle by clicking.
  void setOnSelect(std::function<void(bool)> callback) { onSelect_ = std::move(callback); }
  // The user moved the rectangle (document coordinates changed in the model).
  void setOnMoved(std::function<void()> callback) { onMoved_ = std::move(callback); }
  // Right click at window logical coordinates.
  void setOnContextMenu(std::function<void(double, double)> callback) { onContext_ = std::move(callback); }

  // The rectangle in window logical coordinates.
  r1ui::core::layout::Rect rectangle() const;

 private:
  AppDocument& doc_;
  bool dragging_ = false;
  double grabX_ = 0.0;
  double grabY_ = 0.0;
  std::function<void(bool)> onSelect_;
  std::function<void()> onMoved_;
  std::function<void(double, double)> onContext_;
};

}  // namespace preview

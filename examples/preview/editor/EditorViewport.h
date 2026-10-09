// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: ViewportCanvas, the Editor screen's placeholder viewport: a grid, the scene objects as flat
//   shapes (squares for meshes, rings for lights) in their own colours, the selection outline, the
//   active tool in a corner; click selects (Ctrl toggles), a drag with the Move tool moves the selected
//   objects through the property context, so the move is one undo step and the inspector follows live.
// Why: the dock needs a recognisable main panel and the property/undo/selection plumbing needs a second
//   producer of edits besides the generated panel.
// Callers: the panel factory in EditorPanels.cpp. Calls: EditorModel.
// Lifetime: the model outlives the widget; the widget removes its listeners in onDetached. A drag that is
//   interrupted (capture lost, Escape) restores the values it changed.
#pragma once

#include <cstdint>

#include "EditorModel.h"
#include "r1ui/props/ChangeNotifier.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace preview::editor {

class ViewportCanvas final : public r1ui::widgets::WidgetObject {
 public:
  explicit ViewportCanvas(EditorModel& model) : model_(model) {}
  const char* typeName() const override { return "ViewportCanvas"; }
  void onAttached() override;
  void onDetached() override;
  void paint(r1ui::widgets::PaintContext& ctx) override;
  r1ui::widgets::Cursor cursor() const override;
  void onPointerDown(r1ui::core::events::Event& e) override;
  void onPointerMove(r1ui::core::events::Event& e) override;
  void onPointerUp(r1ui::core::events::Event& e) override;
  void onCaptureLost(r1ui::core::events::Event& e) override;
  void onKeyDown(r1ui::core::events::Event& e) override;

  // World units per logical pixel are fixed: 20 px per unit, the origin at the centre of the widget.
  static constexpr double kPixelsPerUnit = 20.0;

 private:
  struct Point {
    double x = 0.0, y = 0.0;
  };
  Point toWorld(double windowX, double windowY) const;
  Point toLocal(double worldX, double worldY) const;
  uint64_t hit(double windowX, double windowY) const;
  void finishDrag(bool commit);

  EditorModel& model_;
  EditorModel::ListenerId selectionListener_ = 0;
  EditorModel::ListenerId stateListener_ = 0;
  r1ui::props::ChangeNotifier::Token notifierToken_ = 0;
  bool pressed_ = false;
  bool dragging_ = false;
  uint64_t pressedItem_ = 0;
  bool toggleOnRelease_ = false;
  Point last_;
  Point press_;
  Point total_;
};

}  // namespace preview::editor

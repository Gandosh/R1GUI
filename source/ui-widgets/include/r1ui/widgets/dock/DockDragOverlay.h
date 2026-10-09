// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: DockDragOverlay, the overlay-layer widget that draws everything a tab drag shows on top of
//   the dock: the ghost (a 45 percent translucent copy of the dragged region), the drop-zone
//   preview (the half of the target region the drop would create, tinted, with an outline), the
//   cross overlay on a hovered region body (outer and inner rectangles joined by the four diagonals,
//   spec 02 rule 26), the thin edge targets along an area border (rule 30) and the centre target of
//   an empty area (rule 31).
// Why: these are transient feedback, not part of any region, so they live on the overlay layer above
//   every window content and clipping container; the DockHost computes WHAT to show (from the
//   model's drop zones) and this widget only paints it.
// Callers: DockHost (creates one per UiContext for the duration of a drag, feeds DragVisual).
//   Calls: PaintContext. It is hit-test transparent: it never takes input.
// Units: rectangles are logical px in the overlay's window coordinates.
#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "r1ui/dock/DockTypes.h"
#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

struct DragVisual {
  // The translucent ghost: shown while the pointer is over no target.
  std::optional<dock::Rect> ghost;
  std::string ghostTitle;
  // The tinted preview of the zone under the pointer.
  std::optional<dock::Rect> preview;
  // The region body under the pointer, for the cross overlay, and its inner rectangle.
  std::optional<dock::Rect> crossOuter;
  dock::Rect crossInner;
  // Edge targets of the area under the pointer; `hotEdge` is the one the pointer is on (-1 none).
  std::vector<dock::Rect> edges;
  int hotEdge = -1;
  // The single centre target of an empty area.
  std::optional<dock::Rect> emptyTarget;
};

class DockDragOverlay : public WidgetObject {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows();
  const char* typeName() const override { return "DockDragOverlay"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;

  void setVisual(DragVisual visual);
  const DragVisual& visual() const { return visual_; }

 private:
  DragVisual visual_;
};

}  // namespace r1ui::widgets

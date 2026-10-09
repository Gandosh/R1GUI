// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the data stored per widget node (type, style, flags, layout results, dirty bits, user
//   slot). The tree links (parent/children) are private to WidgetTree.
// Why: a retained-mode toolkit keeps one node per widget; the node is plain data so layout,
//   event routing and invalidation can each read the part they own without virtual dispatch.
// Callers: WidgetTree hands out Widget* (null for a stale id). A Widget* is valid until that
//   widget is destroyed (nodes never move) and must not be kept across frames - keep the
//   WidgetId instead.
// Invariants: layout output fields (rect, absRect, exact*) are written only by layout::;
//   dirty bits only by invalidation::; the link fields only by WidgetTree.
// Event handlers: `handler` is a non-owning pointer; the owner must keep the handler alive
//   while the widget lives or clear the pointer first.
#pragma once

#include <cstdint>
#include <string>

#include "r1ui/core/layout/Geometry.h"
#include "r1ui/core/layout/LayoutState.h"
#include "r1ui/core/layout/Style.h"
#include "r1ui/core/tree/WidgetId.h"

namespace r1ui::core::events {
class EventHandler;
}

namespace r1ui::core::tree {

class WidgetTree;

struct WidgetFlags {
  bool visible = true;         // false: not painted, not hit-testable, still occupies layout space
  bool enabled = true;         // false: subtree receives no pointer hits and no focus
  bool focusable = false;      // may take keyboard focus
  bool hitTestTransparent = false;  // pointer passes through this widget (children still hit)
  bool clipsChildren = false;  // children are clipped to this widget's rect (see also overflow)
};

struct Widget {
  // ---- identity and behaviour ----
  uint32_t typeTag = 0;   // host-defined numeric widget type
  std::string name;       // host-defined type/debug name
  layout::Style style;
  WidgetFlags flags;
  int32_t layer = 0;      // stacking layer: higher paints and hits first; ties use sibling order
  int32_t tabIndex = 0;   // 0 = document order, >0 = visited first in ascending order, <0 = skipped by Tab
  uint64_t userData = 0;  // opaque slot for the owner of the widget
  events::EventHandler* handler = nullptr;

  // ---- layout results (written by layout::) ----
  layout::RectD exact;    // unrounded rectangle in the parent's coordinate space
  double exactAbsX = 0.0; // unrounded absolute origin
  double exactAbsY = 0.0;
  layout::Rect rect;      // rounded, parent coordinate space
  layout::Rect absRect;   // rounded, root coordinate space
  layout::NodeLayoutState layoutState;

  // ---- invalidation bits (written by invalidation::) ----
  bool layoutDirty = false;
  bool paintDirty = false;
  bool subtreePaintDirty = false;  // some descendant is paint dirty
  bool animating = false;          // listed by the Invalidator as wanting continuous frames

  // True when children are clipped to the widget rect (flag or overflow:hidden).
  bool clips() const { return flags.clipsChildren || style.overflow == layout::Overflow::Hidden; }
  // True when the widget takes part in painting and hit testing (own flags only).
  bool shown() const { return flags.visible && style.display != layout::Display::None; }

 private:
  friend class WidgetTree;
  static constexpr uint32_t kNone = 0xFFFFFFFFu;
  uint32_t parent_ = kNone;
  uint32_t firstChild_ = kNone;
  uint32_t lastChild_ = kNone;
  uint32_t prevSibling_ = kNone;
  uint32_t nextSibling_ = kNone;
  uint32_t childCount_ = 0;
  uint32_t depth_ = 0;
};

}  // namespace r1ui::core::tree

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the dock model's public surface: DockLayout (main area + floating areas, each a tree of
//   splits and tab stacks), geometry computation, drop-zone hit testing, the editing operations
//   and JSON persistence, plus the result types those calls exchange.
// Why: gives the preview sandbox (and later the real shell) one tested, headless authority for
//   what the docking specs 02, 04 and 05 describe, so rendering code never edits the tree.
// Callers: examples/preview (sandbox), tests/ui-dock. Calls: r1ui::core (JSON, checked casts).
// Failure behavior: every mutating operation works on a copy, normalises it, validates it and
//   only then commits; on any error the layout is left exactly as it was. Nothing throws on bad
//   input values (NaN, negative or huge numbers are rejected or sanitised as documented).
// Normalisation (spec 02 rules 43-46): empty stacks and empty floating areas are removed,
//   single-child splits collapse into the child, a child split on the parent's axis is spliced
//   into the parent with weights scaled so visible proportions stay. Applying it twice changes
//   nothing. Panels are identified by uint32 id; each id is docked at most once; a registered
//   panel that is not docked is "closed" and can be docked again.
// Not modelled yet (see the slice evidence): history entries/reopen at old slot, hidden regions,
//   fixed-content regions, sidebars, panel kinds/locking, native windows, DPI scaling.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/dock/DockTypes.h"

namespace r1ui::dock {

// ---- Geometry results ------------------------------------------------------------------

using Path = std::vector<uint32_t>;  // child indices from an area root down to a node

struct TabLayout {
  PanelId panel = 0;
  Rect rect;
  bool active = false;
};

struct StackLayout {
  uint32_t area = 0;
  Rect bounds;  // strip + body
  Rect strip;
  Rect body;
  std::vector<TabLayout> tabs;  // in tab order
};

struct SplitLayout {
  uint32_t area = 0;
  Path path;
  Axis axis = Axis::Row;
  Rect bounds;
};

// Identifies the gap between child `index` and child `index + 1` of the split at `path`. Valid
// only until the tree's structure changes (paths are positions, not stable ids).
struct SplitterHandle {
  uint32_t area = 0;
  Path path;
  uint32_t index = 0;
  Axis axis = Axis::Row;
  friend bool operator==(const SplitterHandle&, const SplitterHandle&) = default;
};

struct HandleLayout {
  SplitterHandle handle;
  Rect rect;  // the visible bar; also the hit band (same size, spec 05 rule 2)
};

struct AreaLayout {
  uint32_t id = 0;
  bool floating = false;
  bool empty = false;
  Rect bounds;
};

struct LayoutResult {
  std::vector<AreaLayout> areas;  // main first, then floating areas bottom-to-top
  std::vector<SplitLayout> splits;
  std::vector<StackLayout> stacks;
  std::vector<HandleLayout> handles;
};

// ---- Dragging --------------------------------------------------------------------------

enum class DropKind {
  Float,          // new floating area; `preview` is its rectangle
  SplitStack,     // new stack beside the stack holding `stackPanel`, on `side`
  JoinStack,      // join the stack holding `stackPanel` at tab slot `index`
  SplitAreaEdge,  // new full-length stack at `side` of area `area`
  FillEmptyArea   // first stack of the empty area `area`
};

struct DropZone {
  DropKind kind = DropKind::Float;
  uint32_t area = kMainAreaId;
  PanelId stackPanel = 0;
  Side side = Side::Left;
  size_t index = 0;
  Rect preview;  // outline to draw for this zone (spec 02 rule 19)
};

struct DragQuery {
  Point pointer;
  PanelId panel = 0;      // the dragged panel (docked or closed)
  Point grabOffset;       // pointer minus the ghost's top-left, keeps the grab point (rule 17)
};

// True once the pointer has moved strictly farther than the drag threshold from the press point.
bool dragExceedsThreshold(Point press, Point now, const DockConfig& config);

// Handle whose band contains `point`, if any. Later (topmost) areas win.
std::optional<HandleLayout> hitTestHandle(const LayoutResult& layout, Point point);

// ---- The layout ------------------------------------------------------------------------

class DockLayout;
struct DockLayoutResult;
struct LoadResult;

class DockLayout {
 public:
  // Builds a layout for the host's panels. `mainRoot` (optional) is validated and normalised;
  // an empty main area is legal. Fails on duplicate/too many panels, a bad config, or a tree
  // that references unknown or duplicate panels, bad weights, or exceeds the depth/node limits.
  static DockLayoutResult create(std::vector<PanelInfo> panels, const DockConfig& config = {},
                                 std::optional<Node> mainRoot = std::nullopt);

  // Parses and validates layout JSON (spec 04). Panels the host lacks are dropped and counted;
  // anything malformed is rejected with a message. The caller's layout is never touched.
  static LoadResult fromJson(std::string_view text, std::vector<PanelInfo> panels,
                             const DockConfig& config = {});
  std::string toJson() const;

  const DockConfig& config() const { return config_; }
  const std::vector<PanelInfo>& panels() const { return panels_; }
  const PanelInfo* panel(PanelId id) const;
  const std::vector<Area>& areas() const { return areas_; }  // main first
  bool isDocked(PanelId id) const;

  // Rects for every area, stack, tab, split and handle. `mainRect` is sanitised (NaN/negative
  // sizes become 0). Space is shared by weight (spec 05 rules 18-25), so this is also the
  // proportional resize of the main window.
  LayoutResult computeLayout(const Rect& mainRect) const;

  // Which drop the pointer would perform for `query` (spec 02 rules 19-30, spec 08 rule 34).
  // `layout` must come from computeLayout on this layout's current state.
  DropZone hitTestDropZone(const LayoutResult& layout, const DragQuery& query) const;

  // The "no target" drop for `query`: a floating area the size of the ghost (spec 02 rules 21
  // and 33). This is where a drop on no target lands. Same `layout` precondition.
  DropZone floatZone(const LayoutResult& layout, const DragQuery& query) const;

  // Moves `panel` (docked or closed) to `zone`. A stale zone (unknown area or stack, own sole
  // stack, bad index or floating rectangle) is rejected without changes.
  Status dock(PanelId panel, const DropZone& zone);
  // Removes a docked panel; the right neighbour (else left) becomes front when it was front.
  Status closePanel(PanelId panel);
  Status activateTab(PanelId panel);
  // Moves the handle by `delta` along its axis. Only the two children beside the handle change
  // (spec 05 rules 8-11); the pair is clamped to the minimum size. `mainRect` as computeLayout.
  Status moveSplitter(const SplitterHandle& handle, double delta, const Rect& mainRect);

  // Re-checks every invariant; for tests and as the commit gate of each operation.
  Status validate() const;

  friend bool operator==(const DockLayout& a, const DockLayout& b);

 private:
  DockLayout() = default;
  // Normalises and validates `candidate`; commits it and returns success, else leaves *this.
  Status commit(std::vector<Area> candidate, uint32_t nextAreaId);

  DockConfig config_;
  std::vector<PanelInfo> panels_;
  std::vector<Area> areas_;
  uint32_t nextAreaId_ = 1;
};

// ---- Results ---------------------------------------------------------------------------

struct DockLayoutResult {
  std::optional<DockLayout> layout;
  std::string error;  // empty on success
  bool ok() const { return layout.has_value(); }
};

struct LoadResult {
  std::optional<DockLayout> layout;
  std::string error;           // empty on success
  size_t droppedPanels = 0;    // stored panels the host no longer has (spec 04 rule 39)
  bool ok() const { return layout.has_value(); }
};

}  // namespace r1ui::dock

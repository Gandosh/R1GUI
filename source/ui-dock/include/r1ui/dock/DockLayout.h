// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the dock model's public surface: DockLayout (main area + floating areas, each a tree of
//   splits and tab stacks), geometry computation, drop-zone hit testing, the editing operations
//   and JSON persistence, plus the result types those calls exchange.
// Why: gives the DockHost widget, the layout manager and the preview one tested, headless
//   authority for what the docking specs 02, 03, 04 and 05 describe, so rendering code never edits
//   the tree.
// Callers: ui-widgets/dock (DockHost), ui-dock's LayoutManager, examples/preview, tests/ui-dock.
//   Calls: r1ui::core (JSON, checked casts).
// Failure behavior: every mutating operation works on a copy, normalises it, validates it and
//   only then commits; on any error the layout is left exactly as it was. Nothing throws on bad
//   input values (NaN, negative or huge numbers are rejected or sanitised as documented).
// Normalisation (spec 02 rules 43-46): empty stacks and empty floating areas are removed,
//   single-child splits collapse into the child, a child split on the parent's axis is spliced
//   into the parent with weights scaled so visible proportions stay. Applying it twice changes
//   nothing. Panels are identified by uint32 id; each id is docked at most once; a registered
//   panel that is not docked is "closed" and can be docked again, at its remembered slot (see
//   closedSlots()).
// Coordinates: all rectangles share one logical space (see DockTypes.h): the main rectangle given
//   to computeLayout and every floating area's rectangle.
// Not modelled (see docs/dev/docking.md): sidebars and drawers, hidden tab strips, regions that
//   hide when they only hold remembered panels (they are removed; the closed-panel memory restores
//   the slot), fixed-content regions.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/dock/DockMonitors.h"
#include "r1ui/dock/DockTypes.h"

namespace r1ui::dock {

// ---- Geometry results ------------------------------------------------------------------

using Path = std::vector<uint32_t>;  // child indices from an area root down to a node

struct TabLayout {
  PanelId panel = 0;
  Rect rect;  // unscrolled; may extend past the strip when the strip overflows
  bool active = false;
};

struct StackLayout {
  uint32_t area = 0;
  Path path;  // to the stack node inside its area
  Rect bounds;  // strip + body
  Rect strip;
  Rect body;
  std::vector<TabLayout> tabs;  // in tab order
  bool overflow = false;        // the tabs are wider than the strip (decision D12: scroll arrows)
  double tabsWidth = 0.0;       // total width of all tabs
  bool collapsed = false;       // zero-sized by an explicit collapse command
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

// Handle whose band contains `point`, if any. Later (topmost) areas win. `extraBand` widens the hit
// band symmetrically (decision D22: 9 px under touch means extraBand 4).
std::optional<HandleLayout> hitTestHandle(const LayoutResult& layout, Point point, double extraBand = 0.0);

// ---- The layout ------------------------------------------------------------------------

class DockLayout;
struct DockLayoutResult;
struct LoadResult;

// Where a docked panel lives, for hosts that need to act on its stack.
struct PanelSlot {
  uint32_t area = kMainAreaId;
  Path path;          // to the stack node
  size_t tab = 0;     // index inside the stack
  size_t tabCount = 0;
  bool front = false;
};

// Extra inputs of a layout load (all optional).
struct LoadOptions {
  // When set, every window rectangle that is not visible enough is brought back (decision D13).
  const MonitorSet* monitors = nullptr;
  // Place panels that are new to the stored layout at their suggested position (spec 04 rule 42).
  bool placeNewPanels = true;
};

// Inputs and result of DockLayout::openPanel.
struct OpenOptions {
  Rect floatBounds;  // where a floating area is centred (normally the main window rectangle)
  bool floatWhenUnplaced = false;
};
struct OpenOutcome {
  Status status;
  bool alreadyOpen = false;
};

class DockLayout {
 public:
  // Builds a layout for the host's panels. `mainRoot` (optional) is validated and normalised;
  // an empty main area is legal. Fails on duplicate/too many panels, a bad config, or a tree
  // that references unknown or duplicate panels, bad weights, or exceeds the depth/node limits.
  static DockLayoutResult create(std::vector<PanelInfo> panels, const DockConfig& config = {},
                                 std::optional<Node> mainRoot = std::nullopt);

  // Parses and validates layout JSON (spec 04; schema versions 1 and 2). Panels the host lacks are
  // dropped and counted; anything malformed is rejected with a message; version 2 additionally
  // repairs bad weights and duplicate ids and reports each repair in LoadResult::warnings. A
  // version newer than this build reads is rejected. The caller's layout is never touched.
  static LoadResult fromJson(std::string_view text, std::vector<PanelInfo> panels,
                             const DockConfig& config = {}, const LoadOptions& options = {});
  // Schema version 2 (see docs/dev/docking.md for the format).
  std::string toJson() const;

  const DockConfig& config() const { return config_; }
  const std::vector<PanelInfo>& panels() const { return panels_; }
  const PanelInfo* panel(PanelId id) const;
  const std::vector<Area>& areas() const { return areas_; }  // main first, floating bottom-to-top
  bool isDocked(PanelId id) const;
  std::optional<PanelSlot> locate(PanelId id) const;
  const LayoutMeta& meta() const { return meta_; }
  const std::vector<ClosedSlot>& closedSlots() const { return closed_; }
  const ClosedSlot* closedSlot(PanelId id) const;

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

  // ---- editing ----
  // Moves `panel` (docked or closed) to `zone`. A stale zone (unknown area or stack, own sole
  // stack, bad index or floating rectangle) is rejected without changes, and so is moving a locked
  // docked panel or floating a panel that cannot float. A docked panel dropped back where it was
  // is a valid no-op reorder. Docking forgets the panel's closed slot.
  Status dock(PanelId panel, const DropZone& zone);
  // Removes a docked panel; the right neighbour (else left) becomes front when it was front. A
  // locked panel or one with canClose false is refused. Non-document panels leave a ClosedSlot.
  Status closePanel(PanelId panel);
  Status activateTab(PanelId panel);
  // Spec 02 rule 55 in order: already docked -> made front; remembered slot; the suggested
  // placement; then the default region (a tab of the first stack of the main area, else the empty
  // main area). With `floatWhenUnplaced` (the reference editor's behaviour) the last step instead
  // opens a floating area of the panel's default size centred in `floatBounds`.
  OpenOutcome openPanel(PanelId panel, const OpenOptions& options = {});
  // Moves the handle by `delta` along its axis. Only the two children beside the handle change
  // (spec 05 rules 8-11); the pair is clamped to the minimum size. A collapsed neighbour is
  // expanded instead of resized. `mainRect` as computeLayout.
  Status moveSplitter(const SplitterHandle& handle, double delta, const Rect& mainRect);
  // Decision D10: a pinned child keeps its pixel size when the window resizes. Pinning records the
  // current size. `path` names the child (a stack or split) inside area `area`.
  Status setPinned(uint32_t area, const Path& path, bool pinned, const Rect& mainRect);
  // Decision D11: the explicit collapse command. Refused when the node is the only visible child of
  // its parent or the root of an area. Dragging a handle never collapses.
  Status setCollapsed(uint32_t area, const Path& path, bool collapsed);
  // Decision D13: per-tab lock flags (persisted).
  Status setPanelLocked(PanelId panel, bool locked);
  // Floating area rectangle (validated, clamped to the minimum size); the main area is refused.
  Status setAreaRect(uint32_t area, const Rect& rect);
  // Brings a floating area to the top of the stacking order.
  Status raiseArea(uint32_t area);
  Status setWindowState(uint32_t area, const WindowState& state);
  Status setMeta(LayoutMeta meta);
  // Brings every window whose rectangle is not visible enough back onto a monitor (the main
  // window's stored rectangle included). Returns how many windows changed.
  size_t fitWindows(const MonitorSet& monitors);

  // Re-checks every invariant; for tests and as the commit gate of each operation.
  Status validate() const;

  friend bool operator==(const DockLayout& a, const DockLayout& b);

 private:
  DockLayout() = default;
  // Normalises and validates `candidate`; commits it and returns success, else leaves *this.
  Status commit(std::vector<Area> candidate, uint32_t nextAreaId);
  Status commitWith(std::vector<Area> candidate, uint32_t nextAreaId, std::vector<ClosedSlot> closed);

  DockConfig config_;
  std::vector<PanelInfo> panels_;
  std::vector<Area> areas_;
  std::vector<ClosedSlot> closed_;
  LayoutMeta meta_;
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
  int sourceVersion = 0;       // schema version of the file (0 when it could not be read)
  size_t droppedPanels = 0;    // stored panels the host no longer has (spec 04 rule 39)
  size_t duplicatePanels = 0;  // repeated ids dropped (version 2)
  size_t repairedValues = 0;   // weights, active indexes and sizes repaired (version 2)
  size_t windowsMoved = 0;     // windows brought back onto a monitor
  size_t newPanelsPlaced = 0;  // panels new to the layout placed at their suggested position
  std::vector<std::string> warnings;  // one line per repair or drop, bounded
  bool ok() const { return layout.has_value(); }
};

}  // namespace r1ui::dock

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: Splitter, a resizable split container: panes side by side (Row) or stacked (Column) with a
//   5 px draggable handle between every pair of visible panes, live resize, the minimum pane size
//   floor (20 px), explicit collapse / expand commands, ratio persistence and optional keyboard
//   resize.
// Why: panels and the document area need one tested splitter that follows the resize rules of
//   spec 05 (handle band, hover highlight and cursor, capture for the whole drag, only the two
//   panes beside the handle change, clamping at the minimum, proportions kept on window resize)
//   and the owner decisions D10 / D11 (floor instead of dragging to 0, collapse only through an
//   explicit command, keyboard resize through a focusable handle; a double click does nothing).
// Callers: application shells, dock rendering; ui-dock's DockLayout is the headless authority for
//   whole-window layouts, this widget is the on-screen resizer for one split. Calls: UiContext.
// Structure: Splitter (flex Row / Column, gap = handle thickness) -> SplitterPane children. Panes
//   share the space by flex weight (grow = weight, basis 0, min = floor), fixed panes keep their
//   pixel size and cannot be resized. Handles are not widgets: the Splitter draws and hit-tests
//   them in the gaps (and, when the hit band is wider than the gap, in the capture phase).
// Resize rule: moving a handle changes only the nearest resizable visible pane before it and the
//   nearest one after it (their sum is kept); afterwards every resizable pane's weight is set to
//   its pixel size, so later window resizes keep the proportions. Handles with no resizable pane on
//   one side are not hovered and show the default cursor.
// Collapse: collapse(i) gives the pane's space to its nearest resizable neighbour and shrinks it
//   to 0 px (its content is clipped, not removed); expand(i) takes the space back. Dragging a
//   handle next to a collapsed pane expands it to at least the floor.
// Visual (no reference exists, the reference app has fixed panels): idle = 1 px line of `border`,
//   hover = full bar of `border-strong`, dragging = full bar of `accent`.
// Invariants: weights and sizes are finite and positive, every public setter validates its input and
//   keeps the previous state on rejection.
#pragma once

#include <functional>
#include <span>
#include <vector>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

// A region of a Splitter; add the content as children.
class SplitterPane : public WidgetObject {
 public:
  const char* typeName() const override { return "SplitterPane"; }
  void onAttached() override;
};

enum class SplitOrientation : uint8_t { Row, Column };  // Row: panes side by side, vertical handles

struct PaneOptions {
  double weight = 1.0;       // share of the free space among resizable panes (> 0)
  double minSize = -1.0;     // px along the split; < 0 uses the splitter's floor
  double fixedSize = -1.0;   // >= 0: a fixed pane of that size, not resizable
  bool collapsible = false;  // whether collapse() is allowed
};

// What a layout needs to remember: pane proportions and collapse flags (pane count must match).
struct SplitterState {
  std::vector<double> ratios;   // per pane, sums to 1 over the resizable panes (0 for fixed panes)
  std::vector<bool> collapsed;
};

class Splitter : public WidgetObject {
 public:
  static constexpr double kDefaultHandle = 5.0;
  static constexpr double kDefaultFloor = 20.0;
  static constexpr size_t kMaxPanes = 256;

  explicit Splitter(SplitOrientation orientation = SplitOrientation::Row) : orientation_(orientation) {}
  static std::span<const theme::StyleRuleEntry> styleRows();

  const char* typeName() const override { return "Splitter"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  Cursor cursor() const override;
  uint8_t phases() const override;
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onDoubleClick(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onStateChanged(uint16_t previous) override;
  void onKeyDown(Event& e) override;

  // ---- panes ----
  // Adds a pane at the end; invalid options (non-finite or non-positive weight, NaN sizes) are
  // replaced by the defaults. Returns the pane widget, or an invalid id past kMaxPanes.
  core::tree::WidgetId addPane(PaneOptions options = {});
  size_t paneCount() const { return panes_.size(); }
  core::tree::WidgetId pane(size_t index) const;
  // Laid-out size of the pane along the split (logical px; 0 for a hidden or stale pane).
  double paneSize(size_t index) const;
  bool setPaneVisible(size_t index, bool visible);
  bool paneVisible(size_t index) const;
  bool isCollapsed(size_t index) const;

  // ---- configuration ----
  SplitOrientation orientation() const { return orientation_; }
  // Minimum size of a resizable pane (default 20); existing panes below it are not changed.
  bool setMinPaneSize(double px);
  double minPaneSize() const { return floor_; }
  // Handle thickness in px (1..64, default 5); hit band in px (at least the thickness, default
  // thickness; the product widens it to 9 under touch).
  bool setHandleThickness(double px);
  bool setHitBand(double px);
  double handleThickness() const { return thickness_; }
  // A focusable handle: arrows resize the active handle by 10 px (Shift 50), PageUp / PageDown pick
  // the handle, Home / End move it to the limits (decision D10).
  void setKeyboardResize(bool enabled);

  // ---- resizing (all return false when nothing changed or the request is invalid) ----
  // Handles are numbered 0.. between consecutive visible panes.
  size_t handleCount() const;
  bool handleResizable(size_t handle) const;
  // Absolute logical rectangle of a handle's visible bar.
  core::layout::Rect handleRect(size_t handle) const;
  // Moves the handle by `delta` px along the split (positive = towards the end), clamped at the
  // minimum sizes. Returns true when a pane size changed.
  bool moveHandle(size_t handle, double delta);
  bool collapse(size_t index);
  bool expand(size_t index);
  bool toggleCollapse(size_t index);
  // Ends a pointer drag started on a handle and puts the panes back as they were when it began.
  bool cancelDrag();
  bool dragging() const { return dragHandle_ >= 0; }
  int hoveredHandle() const { return hover_; }

  // ---- persistence ----
  SplitterState state() const;
  // False (state unchanged) when the sizes differ from the pane count or a ratio is not finite.
  bool restoreState(const SplitterState& state);
  // After every user or API change of sizes (not after window resizes).
  void setOnChanged(std::function<void(Splitter&)> callback) { onChanged_ = std::move(callback); }

 private:
  struct Pane {
    core::tree::WidgetId id;
    PaneOptions options;
    double weight = 1.0;
    bool collapsed = false;
    bool visible = true;
    double restoreSize = 0.0;
    size_t restoreTo = 0;  // pane that received the space when collapsed
  };
  bool horizontal() const { return orientation_ == SplitOrientation::Row; }
  bool resizable(size_t i) const { return panes_[i].visible && panes_[i].options.fixedSize < 0.0; }
  double effectiveMin(size_t i) const;
  double sizeOf(size_t i) const;
  void applyStyle(size_t i);
  void snapshotWeights();
  // Pane indices around a handle (-1 when no resizable pane exists on that side).
  bool neighbours(size_t handle, size_t& before, size_t& after) const;
  std::vector<size_t> visiblePanes() const;
  int handleAt(double x, double y) const;
  void changed();
  // Resizes the pair from the given start sizes by `delta` (clamped at the minimums); true when the
  // weights or collapse flags changed.
  bool resizePair(size_t a, size_t b, double sizeA, double sizeB, double delta);

  SplitOrientation orientation_;
  std::vector<Pane> panes_;
  double floor_ = kDefaultFloor;
  double thickness_ = kDefaultHandle;
  double band_ = 0.0;  // 0 = the thickness
  int hover_ = -1;
  int dragHandle_ = -1;
  double dragStart_ = 0.0;
  size_t dragBefore_ = 0, dragAfter_ = 0;
  double dragSizeBefore_ = 0.0, dragSizeAfter_ = 0.0;
  std::vector<double> dragWeights_;
  std::vector<bool> dragCollapsed_;
  size_t activeHandle_ = 0;
  bool keyboard_ = false;
  std::function<void(Splitter&)> onChanged_;
};

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: TreeView, the layer tree / flat list of the editor as ONE widget with virtualized rows: 24 px
//   rows with a disclosure chevron, node icon, ellipsized label and hover actions (visibility,
//   lock); single / multi selection by pointer and keyboard; expand / collapse; inline rename (F2
//   and slow click); drag reordering with above / below / onto drop feedback and edge auto-scroll;
//   type-to-search; a thin scrollbar; selection and expansion that survive model refreshes.
// Why: a layer tree can hold 100k nodes, so rows are never widgets: the view flattens the expanded
//   part of the model into an array of (node, depth) rows (one pass, no allocation per node) and
//   paints, hit-tests and scrolls only the rows that are on screen. Measured look (docs/spec/
//   widgets.md 4.5): row 24 px (padding 4, line 16), left padding 16 px per level, disclosure 16 px,
//   icon 12 px, gap 4, radius 4, hover `hover`, selected `panel-selected` when the tree has focus and
//   `panel-selected-muted` otherwise, hidden rows at 50% opacity, actions 16 px with a white 15% hover
//   fill, drop indicators (2 px accent line above / below, accent outline onto) and a dragged row at
//   30% opacity (source-derived: the reference's drag states could not be captured).
// Callers: application panels (layers, pages, assets lists), the gallery. Calls: TreeModel (read
//   only), ScrollBar, RenameEditor, FlyoutList is not used.
// Selection rules (spec 08 rules 45-65): press on an unselected row selects it (Ctrl toggles, Shift adds
//   the range from the anchor, the anchor stays); press on a selected row keeps the selection so a
//   group can be dragged and collapses to that row on release without a drag (multi mode); press on
//   empty space clears; keyboard Up / Down move the cursor and select it (Shift: range replaces, Ctrl+
//   Shift: range adds, Ctrl: add without anchor move), Home / End, Page Up / Down by whole visible
//   rows, Space selects (Ctrl toggles), Ctrl+A selects all in multi mode, Left / Right collapse /
//   expand / go to the parent or first child, F2 renames, Enter activates, Alt disables all of it.
// Rename: F2 or a second press on the selected label after 0.5 s (cancelled by a double click or a
//   drag); Enter commits through the rename handler (a refusal keeps the field open with a `danger`
//   border), Escape cancels, losing focus commits a valid text. The renaming row is 26 px high and
//   the rows below it move down 2 px, as measured.
// Drag: after the Router's drag threshold the selected, draggable rows form the payload; the pointer
//   picks a zone (top and bottom quarter of the row, 3..10 px, else onto); the drop handler decides
//   whether the zone is acceptable and performs the change on the model; dropping onto a row expands
//   it. Near the top or bottom edge (30 px) the list scrolls by itself, faster the closer the pointer.
// Time: slow-click rename and edge scrolling are driven by ui().now() from paint() (the widget asks
//   for frames while a timer or the auto-scroll is active); advance(nowMs) is public for tests.
// Invariants: rows_ and rowIndex_ always describe the same flattening; selection / cursor / anchor
//   hold NodeIds only, never row indices; every public setter validates ids against the model; model
//   callbacks never run while the view's own containers are being modified.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/runtime/WidgetObject.h"
#include "r1ui/widgets/scroll/ScrollArea.h"
#include "r1ui/widgets/scroll/ScrollBar.h"
#include "r1ui/widgets/tree/RenameEditor.h"
#include "r1ui/widgets/tree/TreeModel.h"

namespace r1ui::widgets {

enum class TreeSelectionMode : uint8_t { None, Single, Multi };
enum class TreeAppearance : uint8_t { Tree, List };  // List: no disclosure column, 8 px left padding
enum class DropZone : uint8_t { Above, Below, Onto };
enum class RowAction : uint8_t { ToggleVisibility, ToggleLock };

struct DropRequest {
  std::vector<NodeId> nodes;  // the dragged nodes in row order, descendants of other dragged nodes removed
  NodeId target = kTreeRoot;
  DropZone zone = DropZone::Onto;
};

struct RenameResult {
  bool ok = true;
  std::string error;  // shown by the field (tooltip) when ok is false
};

class TreeView : public WidgetObject {
 public:
  static constexpr double kRowHeight = 24.0;
  static constexpr double kRenameRowHeight = 26.0;
  static constexpr double kIndent = 16.0;
  static constexpr double kDisclosure = 16.0;
  static constexpr double kIconSize = 12.0;
  static constexpr double kGap = 4.0;
  static constexpr double kActionSize = 16.0;
  static constexpr double kRightPad = 4.0;
  static constexpr double kEdgeScrollZone = 30.0;
  static constexpr double kEdgeScrollMax = 600.0;  // px per second at the edge
  static constexpr uint64_t kSlowClickMs = 500;
  static constexpr uint64_t kSearchResetMs = 2000;

  static std::span<const theme::StyleRuleEntry> styleRows();
  TreeView() : vbar_(ScrollAxis::Vertical) {}

  // ---- WidgetObject ----
  const char* typeName() const override { return "TreeView"; }
  void onAttached() override;
  core::layout::MeasureResult measure(const core::layout::MeasureInput& input) override;
  float paintOpacity() const override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  Cursor cursor() const override;
  std::string_view tooltipText() const override;
  std::string_view accessibleName() const override;
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onPointerWheel(Event& e) override;
  void onClick(Event& e) override;
  void onDoubleClick(Event& e) override;
  void onDragStart(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onKeyDown(Event& e) override;
  void onTextInput(Event& e) override;
  void onFocusOut(Event& e) override;
  void onStateChanged(uint16_t previous) override;
  void onLayout() override;

  // ---- model ----
  void setModel(std::shared_ptr<TreeModel> model);
  const std::shared_ptr<TreeModel>& model() const { return model_; }
  // Rebuilds the rows now (also done automatically when the model's revision moved). Selection,
  // cursor and anchor that no longer exist in the model are dropped (one selection callback).
  void refresh();
  void setAppearance(TreeAppearance appearance);
  void setSelectionMode(TreeSelectionMode mode);
  TreeSelectionMode selectionMode() const { return mode_; }
  // The height follows the rows (a tree inside a scrolling panel) instead of the style height.
  void setAutoHeight(bool on);
  // Auto (default) shows the thin scrollbar when the rows overflow the view; Never hides it (a tree inside a
  // scrolling panel, or a view that only ever shows a window of rows).
  void setScrollbarPolicy(ScrollbarPolicy policy);

  // ---- expansion ----
  bool setExpanded(NodeId id, bool expanded, bool recursive = false);
  bool isExpanded(NodeId id) const { return expanded_.count(id) != 0; }
  void expandAll();
  void collapseAll();
  std::vector<NodeId> expandedNodes() const;
  void setExpandedNodes(const std::vector<NodeId>& nodes);

  // ---- rows ----
  size_t rowCount();
  // The node of a visible row (kTreeRoot for a bad index) and the row of a node (nullopt when it is
  // not visible, for example inside a collapsed branch).
  NodeId nodeAtRow(size_t row);
  std::optional<size_t> rowOfNode(NodeId id);
  size_t depthOfRow(size_t row);

  // ---- selection ----
  // Visible selected nodes in row order, then selected nodes inside collapsed branches.
  std::vector<NodeId> selection();
  size_t selectionCount() const { return selected_.size(); }
  bool isSelected(NodeId id) const { return selected_.count(id) != 0; }
  // Replaces the selection (unknown ids are ignored; Single keeps the first); cursor and anchor move
  // to the last / first node. Returns whether the selection changed.
  bool setSelection(const std::vector<NodeId>& nodes);
  bool select(NodeId id) { return setSelection({id}); }
  bool clearSelection();
  bool selectAll();
  NodeId cursorNode() const { return cursor_; }
  NodeId anchorNode() const { return anchor_; }
  // True when focus is inside the tree (selected rows use the focused colour).
  bool treeFocused() const { return focused(); }

  // ---- scrolling ----
  double scrollOffset() const { return scroll_; }
  double maxScroll();
  bool scrollTo(double offset);
  // Brings the node's row fully into view with the least movement (centred when `centre` is set and
  // the row was not visible). Only visible rows can be revealed: a node inside a collapsed branch gives false.
  bool scrollToNode(NodeId id, bool centre = false);
  size_t wholeRowsVisible() const;
  // ---- rename ----
  bool beginRename(NodeId id);
  bool renaming() const { return rename_.active(); }
  bool commitRename();
  void cancelRename();
  const RenameEditor& renameEditor() const { return rename_; }
  // ---- drag ----
  bool dragging() const { return drag_.active; }
  struct DropPreview {
    bool valid = false;
    NodeId target = kTreeRoot;
    DropZone zone = DropZone::Onto;
  };
  DropPreview dropPreview() const { return drag_.preview; }
  void cancelDrag();

  // ---- callbacks ----
  void setOnSelectionChanged(std::function<void(TreeView&)> cb) { onSelection_ = std::move(cb); }
  void setOnActivate(std::function<void(NodeId)> cb) { onActivate_ = std::move(cb); }
  void setOnRename(std::function<RenameResult(NodeId, std::string_view)> cb) { onRename_ = std::move(cb); }
  void setOnAction(std::function<void(NodeId, RowAction)> cb) { onAction_ = std::move(cb); }
  void setOnContextMenu(std::function<void(NodeId, double, double)> cb) { onContext_ = std::move(cb); }
  void setOnExpansionChanged(std::function<void(NodeId, bool)> cb) { onExpansion_ = std::move(cb); }
  // `accept` may veto a zone (and return false for "not a valid target"); `drop` performs the change.
  void setDropHandlers(std::function<bool(const DropRequest&)> accept, std::function<void(const DropRequest&)> drop) {
    acceptDrop_ = std::move(accept);
    onDrop_ = std::move(drop);
  }

  // ---- time (called from paint; public for tests) ----
  void advance(uint64_t nowMs);

  // ---- geometry for tests (window logical px) ----
  core::layout::Rect rowRect(size_t row);
  core::layout::Rect disclosureRect(size_t row);
  core::layout::Rect actionRect(size_t row, RowAction action);
  core::layout::Rect renameFieldRect();
  bool verticalBarVisible() const { return vbar_.scrollable(); }

 private:
  struct Row {
    NodeId id = kTreeRoot;
    uint32_t depth = 0;
    uint32_t parent = 0xFFFFFFFFu;  // row index of the parent, none for top level
    bool hasChildren = false;
  };
  enum class Part : uint8_t { None, Row, Disclosure, Visibility, Lock };
  struct Hit {
    Part part = Part::None;
    size_t row = 0;
  };
  struct Drag {
    bool armed = false;   // a left press on a row (candidate)
    bool active = false;
    NodeId pressed = kTreeRoot;
    bool collapseOnRelease = false;  // press on an already selected row of a multi selection
    std::vector<NodeId> payload;
    DropPreview preview;
    double pointerX = 0.0;
    double pointerY = 0.0;
  };

  // model / rows
  void ensureRows();
  void rebuildRows();
  void pruneSelection();
  bool nodeSelectable(NodeId id) const;
  // geometry
  double contentWidth() const;
  double viewportHeight() const;
  double contentHeight();
  double rowTopLocal(size_t row);
  double rowHeightOf(size_t row) const;
  std::optional<size_t> rowAtLocalY(double y);
  Hit hitTest(double x, double y);
  core::layout::Rect absOrigin() const;
  void placeBar();
  void applyScroll(double offset);
  void clampScroll();
  // selection helpers
  void notifySelection();
  bool replaceSelection(const std::vector<NodeId>& nodes, NodeId cursor, NodeId anchor);
  void selectRange(size_t from, size_t to, bool additive);
  void moveCursor(size_t row, bool shift, bool ctrl);
  void toggleExpanded(size_t row, bool recursive);
  void setExpandedInternal(NodeId id, bool expanded, bool recursive);
  // input
  void pressRow(Event& e, size_t row);
  void keyNavigate(Event& e);
  void typeAhead(char32_t cp);
  void updateDrop(double x, double y);
  void finishDrag(bool commit);
  DropRequest buildRequest(NodeId target, DropZone zone) const;
  bool dropAllowed(NodeId target, DropZone zone);
  std::vector<NodeId> dragPayload();
  void armRename(NodeId id);
  void wantFrames(bool on);
  std::string_view labelOf(NodeId id) const;

  std::shared_ptr<TreeModel> model_;
  uint64_t seenRevision_ = 0;
  bool rebuilding_ = false;
  std::vector<Row> rows_;
  std::unordered_map<NodeId, size_t> rowIndex_;
  std::unordered_set<NodeId> expanded_;
  std::unordered_set<NodeId> selected_;
  NodeId cursor_ = kTreeRoot;
  NodeId anchor_ = kTreeRoot;
  TreeSelectionMode mode_ = TreeSelectionMode::Multi;
  TreeAppearance appearance_ = TreeAppearance::Tree;
  bool autoHeight_ = false;
  ScrollbarPolicy scrollbarPolicy_ = ScrollbarPolicy::Auto;
  double scroll_ = 0.0;
  ScrollBar vbar_;
  bool barDragging_ = false;
  Hit hover_;
  Hit press_;
  Drag drag_;
  RenameEditor rename_;
  std::optional<size_t> renameRow_;  // row index of the renaming node while the editor is open
  bool renameDrag_ = false;
  struct SlowClick {
    bool armed = false;
    NodeId node = kTreeRoot;
    uint64_t dueMs = 0;
  } slow_;
  bool pressedWasSelected_ = false;
  uint64_t lastAdvanceMs_ = 0;
  bool framesWanted_ = false;
  std::string searchPrefix_;
  uint64_t searchMs_ = 0;
  mutable std::string tooltipScratch_;

  std::function<void(TreeView&)> onSelection_;
  std::function<void(NodeId)> onActivate_;
  std::function<RenameResult(NodeId, std::string_view)> onRename_;
  std::function<void(NodeId, RowAction)> onAction_;
  std::function<void(NodeId, double, double)> onContext_;
  std::function<void(NodeId, bool)> onExpansion_;
  std::function<bool(const DropRequest&)> acceptDrop_;
  std::function<void(const DropRequest&)> onDrop_;
};

}  // namespace r1ui::widgets

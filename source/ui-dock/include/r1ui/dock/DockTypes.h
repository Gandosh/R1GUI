// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the plain value types of the dock model: geometry, panel descriptions, tunables, the
//   dock tree node, the area record, window state, closed-panel memory and layout names.
// Why: the tree is pure data so every operation can copy it, mutate the copy, validate and then
//   commit; nothing here knows about windows, rendering or the OS.
// Callers: ui-dock internals, ui-widgets/dock (DockHost), the preview sandbox and tests. Units:
//   logical pixels (doubles). Model coordinates are one shared space ("screen logical"): the main
//   area's rectangle and every floating area's rectangle live in it, which is what lets a drop
//   target in another window be found with the same hit test.
// Invariants (enforced by DockLayout, not by these structs): a Split has >= 2 children and
//   alternates axis with its parent; a Stack has >= 1 tab and `active` indexes into `tabs`;
//   every `weight` is finite and > 0; every panel id occurs at most once in the whole layout; a
//   pinned node has a finite `pinnedSize` >= 0.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace r1ui::dock {

using PanelId = uint32_t;

struct Point {
  double x = 0.0;
  double y = 0.0;
  friend bool operator==(const Point&, const Point&) = default;
};

struct Rect {
  double x = 0.0;
  double y = 0.0;
  double w = 0.0;
  double h = 0.0;
  // Half-open: the left/top edge is inside, the right/bottom edge is not.
  bool contains(Point p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
  friend bool operator==(const Rect&, const Rect&) = default;
};

enum class Side { Left, Right, Top, Bottom };

// Panel kinds of spec 02 ("tab kinds"): they decide the docking rules, the tab strip size and
// whether a closed panel is remembered. Workspace and shared panels differ only in who may host
// them, which a single-instance toolkit cannot express, so both are ordinary here.
enum class PanelKind : uint8_t {
  ApplicationPage,  // top-level mode page: joins strips only, tall strip
  SharedPanel,      // may live in any window
  Panel,            // ordinary (workspace) panel
  Document          // opened per document: forgotten when closed
};

// Where a module suggests a panel first appears (spec 04 rules 42-43): beside or in the stack of an
// existing panel. `relativeTo` 0 means no suggestion.
struct PanelPlacement {
  PanelId relativeTo = 0;
  bool asTab = true;        // join the stack holding `relativeTo`
  Side side = Side::Right;  // used when !asTab: split beside that stack
};

// What the host tells the model about a panel. The host maps `id` to colour or content.
struct PanelInfo {
  PanelId id = 0;
  std::string title;  // UTF-8, for host UI text only
  bool canClose = true;
  PanelKind kind = PanelKind::Panel;
  bool canFloat = true;
  bool locked = false;       // per-tab lock (spec 02 rule 53): cannot be dragged or closed
  PanelPlacement suggested;  // first position when the panel is new to a stored layout
  double floatWidth = 0.0;   // registered default floating size; 0 = DockConfig default
  double floatHeight = 0.0;
};

// Row: children side by side (vertical handles). Column: children stacked (horizontal handles).
enum class Axis { Row, Column };

// Tunables. Defaults come from the interaction specs unless marked "our decision".
struct DockConfig {
  double handleThickness = 5.0;      // spec 05 rule 1; the hit band has the same size
  // Spec 05 rule 14 (decision D11): minimum region length, default 20 px, so a region cannot be
  // dragged to nothing by accident. Collapsing needs an explicit command (DockHost), never a drag.
  double minPanelSize = 20.0;
  double tabStripHeight = 25.0;      // spec 02 rule 9
  double maxTabWidth = 160.0;        // spec 02 rule 9
  double dragThreshold = 5.0;        // spec 02 rule 14 / spec 08 rule 1 (strictly greater)
  double crossInsetFraction = 0.3;   // spec 02 rule 25
  double crossInsetMin = 5.0;
  double crossInsetMax = 150.0;
  double edgeTargetThickness = 6.0;  // spec 02 rule 29
  double ghostMaxLongSide = 800.0;   // spec 02 rule 17
  double ghostMinScale = 0.1;
  double defaultFloatWidth = 1000.0;   // spec 02 rule 54 (panel never shown before)
  double defaultFloatHeight = 600.0;
  double minFloatSize = 64.0;        // our decision: floor for a floating area's width and height
  double minTabWidth = 60.0;         // decision D12: tabs shrink to this, then the strip scrolls
  double appPageStripHeight = 50.0;  // spec 02 rule 9: application pages get taller, wider tabs
  double appPageMaxTabWidth = 210.0;
  double touchHandleBand = 9.0;      // decision D22: hit band of splitter handles under touch
  double visibleMargin = 100.0;      // decision D13 / spec 03 rule 64: saved windows keep this much on screen
  double hoverActivateSeconds = 0.75;  // spec 02 rule 7: a drag over a tab header activates it
  double keyboardResizeStep = 10.0;  // decision D10; Shift multiplies by 5
};

// Whole-layout limits that keep hostile or runaway input bounded.
inline constexpr size_t kMaxTreeDepth = 32;
inline constexpr size_t kMaxTreeNodes = 4096;
inline constexpr size_t kMaxPanels = 4096;
inline constexpr size_t kMaxFloatingAreas = 64;
inline constexpr size_t kMaxNameBytes = 256;
inline constexpr size_t kMaxDescriptionBytes = 4096;
inline constexpr double kMaxWeight = 1.0e9;
inline constexpr double kMinWeight = 1.0e-9;
inline constexpr double kMaxCoordinate = 1.0e7;  // floating-area rectangles stay within this

// Outcome of an operation that can fail; `error` is empty exactly when `ok`.
struct Status {
  bool ok = true;
  std::string error;
  explicit operator bool() const { return ok; }
  static Status success() { return {}; }
  static Status failure(std::string message) { return {false, std::move(message)}; }
};

// Per-window state that is persisted with a layout (spec 04 rules 1, 4, 7, 49). Rectangles are in
// logical units; `monitor` and `dpiScale` record where the window was when it was saved so a later
// run can tell that the display changed. The main window's rectangle lives here (`hasRect`); a
// floating area's rectangle is Area::rect.
struct WindowState {
  bool hasRect = false;
  Rect rect;
  bool maximized = false;
  std::string monitor;    // monitor name at save time; empty = unknown
  int monitorIndex = -1;  // monitor index at save time; -1 = unknown
  double dpiScale = 1.0;
  friend bool operator==(const WindowState&, const WindowState&) = default;
};

// Where a closed panel reopens (spec 02 rule 55, spec 04 closed-panel memory). Stack identity is
// not stable, so the slot is described by what surrounded the panel: the tabs of its stack, else a
// panel of the neighbouring region and the side it was on, else its floating rectangle.
struct ClosedSlot {
  PanelId panel = 0;
  std::vector<PanelId> neighbours;  // other tabs of its stack when it closed, in order
  size_t index = 0;                 // its tab index
  PanelId anchor = 0;               // a panel of the adjacent region when the stack vanished
  Side anchorSide = Side::Left;     // reopen on this side of the anchor's stack
  bool floating = false;
  Rect floatRect;
  friend bool operator==(const ClosedSlot&, const ClosedSlot&) = default;
};

// Names shown in layout menus (spec 04 rule 18).
struct LayoutMeta {
  std::string name;
  std::string description;
  friend bool operator==(const LayoutMeta&, const LayoutMeta&) = default;
};

// A node of the dock tree. Row/Column splits hold ordered children; stacks hold ordered tabs.
// `weight` is this node's size share among its siblings (spec 02 "size weight").
struct Node {
  enum class Kind { Split, Stack };
  Kind kind = Kind::Stack;
  double weight = 1.0;
  Axis axis = Axis::Row;           // Split only
  std::vector<Node> children;      // Split only
  std::vector<PanelId> tabs;       // Stack only
  size_t active = 0;               // Stack only: index of the front tab
  bool pinned = false;             // decision D10: keeps `pinnedSize` px along its parent when it resizes
  double pinnedSize = 0.0;         // meaningful only when pinned
  bool collapsed = false;          // decision D11: 0 px along its parent, set only by an explicit command

  static Node stack(std::vector<PanelId> tabs, size_t active = 0, double weight = 1.0);
  static Node split(Axis axis, std::vector<Node> children, double weight = 1.0);
  friend bool operator==(const Node&, const Node&) = default;
};

// An area is the top container of a window: the main area (id 0, never removed, may be empty) or
// a floating area (removed when empty). Floating areas are plain rectangles in the same
// coordinate space as the main rect; the floating backend gives each one a window.
struct Area {
  uint32_t id = 0;
  Rect rect;                 // floating areas only; the main area's rect comes from the host
  std::optional<Node> root;  // empty only for the main area
  WindowState window;
  friend bool operator==(const Area&, const Area&) = default;
};

inline constexpr uint32_t kMainAreaId = 0;

}  // namespace r1ui::dock

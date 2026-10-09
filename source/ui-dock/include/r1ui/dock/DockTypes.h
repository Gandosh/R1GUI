// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the plain value types of the dock model: geometry, panel descriptions, tunables, the
//   dock tree node and the area record.
// Why: the tree is pure data so every operation can copy it, mutate the copy, validate and then
//   commit; nothing here knows about windows, rendering or the OS.
// Callers: ui-dock internals, the preview sandbox and tests. Units: logical pixels (doubles).
// Invariants (enforced by DockLayout, not by these structs): a Split has >= 2 children and
//   alternates axis with its parent; a Stack has >= 1 tab and `active` indexes into `tabs`;
//   every `weight` is finite and > 0; every panel id occurs at most once in the whole layout.
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

// What the host tells the model about a panel. The host maps `id` to colour or content.
struct PanelInfo {
  PanelId id = 0;
  std::string title;  // UTF-8, for host UI text only
  bool canClose = true;
};

// Row: children side by side (vertical handles). Column: children stacked (horizontal handles).
enum class Axis { Row, Column };

enum class Side { Left, Right, Top, Bottom };

// Tunables. Defaults come from the interaction specs unless marked "our decision".
struct DockConfig {
  double handleThickness = 5.0;      // spec 05 rule 1; the hit band has the same size
  // Spec 05 rule 14 (decision D11): configurable minimum region length, default 20 px, so a
  // region cannot be dragged to nothing by accident. Collapsing to 0 would need an explicit
  // gesture for regions marked collapsible; no such gesture or marking exists yet, so a
  // host that wants the reference's behavior sets this to 0.
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
};

// Whole-layout limits that keep hostile or runaway input bounded.
inline constexpr size_t kMaxTreeDepth = 32;
inline constexpr size_t kMaxTreeNodes = 4096;
inline constexpr size_t kMaxPanels = 4096;
inline constexpr size_t kMaxFloatingAreas = 64;
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

  static Node stack(std::vector<PanelId> tabs, size_t active = 0, double weight = 1.0);
  static Node split(Axis axis, std::vector<Node> children, double weight = 1.0);
  friend bool operator==(const Node&, const Node&) = default;
};

// An area is the top container of a window: the main area (id 0, never removed, may be empty) or
// a floating area (removed when empty). Floating areas are plain rectangles in the same
// coordinate space as the main rect for now; native windows are a later slice.
struct Area {
  uint32_t id = 0;
  Rect rect;                 // floating areas only; the main area's rect comes from the host
  std::optional<Node> root;  // empty only for the main area
  friend bool operator==(const Area&, const Area&) = default;
};

inline constexpr uint32_t kMainAreaId = 0;

}  // namespace r1ui::dock

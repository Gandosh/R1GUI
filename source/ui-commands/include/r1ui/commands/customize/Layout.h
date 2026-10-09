// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the declarative layout tree of menus, toolbars and free-form panels that the customization
//   layer edits: Node (one stable-id entry), MenuLayout (menu bar -> menus -> sections -> entries),
//   ToolbarLayout (items, size step, gap, orientation), FreeFormPanelLayout (buttons at pixel
//   positions), LayoutSet (the three together) and the structural rules every layout obeys.
// Why: spec 06 describes menus and toolbars as named parts a user can hide, move, rename and extend;
//   one plain data model (no widgets, no registry) lets the pure effectiveLayout function, the
//   editing operations, the persistence and the widget binders all agree on what a legal layout is.
//   The host supplies the BUILT-IN layouts; the user's changes are a Delta (Delta.h) applied over them.
// Callers: Delta.h / Effective.cpp, Customization, CustomizationIo, the widget layer (customize
//   folder of ui-widgets), tests. Calls: nothing (standard library only).
// Ids: every node has a stable id, unique inside one LayoutSet together with the container ids (menu
//   bar, toolbars, panels). Hosts choose ids for built-in nodes ("menu.edit", "menu.edit.undo");
//   user-created nodes get generated ids ("u12"). Labels are UTF-8 and bounded (kMaxLabelBytes).
// Hierarchy (what may hold what, see canContain): MenuBar > Menu > Section > {Command, Separator,
//   Heading, Submenu}; Submenu > Section. Toolbar > {Command, Separator, Group, Spacer}; Group >
//   Command. Panel > FreeButton. A Section is the unit of drag inside a menu; adjacent sections are
//   separated by a separator line when the menu is built, a section heading text is a Heading row.
// Flags: `locked` is set only by the owner of the built-in layout (decision D5): a locked node and its
//   whole subtree refuse every user change. `visible = false` is the user's "hidden" (decision D3);
//   `user` marks nodes the user created; `missing` marks a command reference whose command is not
//   registered (kept only when asked for).
// Bounds: kMaxNodes entries per layout set, kMaxDepth levels, kMaxLabelBytes / kMaxIdBytes text.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace r1ui::commands::customize {

inline constexpr size_t kMaxNodes = 100000;
inline constexpr size_t kMaxDepth = 12;       // levels below a container (Menu > Section > Submenu > Section > ...)
inline constexpr size_t kMaxIdBytes = 128;
inline constexpr size_t kMaxLabelBytes = 256;
inline constexpr size_t kMaxContainers = 256;  // toolbars + panels in one set

enum class Kind : uint8_t {
  // containers (not stored as nodes, they hold nodes)
  MenuBar,
  Toolbar,
  Panel,
  // nodes
  Menu,
  Section,
  Command,
  Separator,
  Heading,
  Submenu,
  Group,
  Spacer,
  FreeButton
};

const char* kindName(Kind kind);
// Parses a name produced by kindName for a node kind; false for anything else (containers included).
bool parseNodeKind(std::string_view name, Kind& kind);
// True when a parent of kind `parent` may hold a child of kind `child`.
bool canContain(Kind parent, Kind child);

struct Rect {
  double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
  friend bool operator==(const Rect&, const Rect&) = default;
};

struct Node {
  std::string id;
  Kind kind = Kind::Command;
  std::string commandId;  // Command and FreeButton: the stable command id
  std::string label;      // default text: Menu title, Section heading ("" = none), Heading, Submenu title
  std::string userLabel;  // the user's own text ("" = the default is shown)
  bool visible = true;
  bool locked = false;
  bool user = false;
  bool missing = false;
  Rect rect;              // FreeButton: logical px inside the panel
  std::vector<Node> children;

  // The text to show for a node that has one of its own: userLabel when set, else label.
  const std::string& shownLabel() const { return userLabel.empty() ? label : userLabel; }

  static Node menu(std::string id, std::string title, std::vector<Node> sections = {});
  static Node section(std::string id, std::string heading, std::vector<Node> entries = {});
  static Node command(std::string id, std::string commandId);
  static Node separator(std::string id);
  static Node heading(std::string id, std::string text);
  static Node submenu(std::string id, std::string title, std::vector<Node> sections = {});
  static Node group(std::string id, std::vector<Node> commands);
  static Node spacer(std::string id);
  static Node freeButton(std::string id, std::string commandId, Rect rect);
  friend bool operator==(const Node&, const Node&) = default;
};

struct MenuLayout {
  std::string id = "menubar";
  bool locked = false;
  std::vector<Node> menus;  // Kind::Menu
};

enum class SizeStep : uint8_t { Small, Medium, Large };
enum class Orientation : uint8_t { Horizontal, Vertical };

inline constexpr double kMinToolbarGap = 0.0;
inline constexpr double kMaxToolbarGap = 32.0;
inline constexpr double kDefaultToolbarGap = 2.0;

struct ToolbarLayout {
  std::string id;
  std::string title;  // for lists ("Main tools")
  bool user = false;
  bool locked = false;
  Orientation orientation = Orientation::Horizontal;
  SizeStep sizeStep = SizeStep::Medium;
  double gap = kDefaultToolbarGap;
  std::vector<Node> items;
  friend bool operator==(const ToolbarLayout&, const ToolbarLayout&) = default;
};

inline constexpr double kMinButtonSize = 16.0;
inline constexpr double kDefaultGridSize = 8.0;
inline constexpr double kMaxPanelSize = 8192.0;

struct FreeFormPanelLayout {
  std::string id;
  std::string title;
  bool user = false;
  bool locked = false;
  double width = 0.0;   // logical px, the panel's own size; free buttons live inside it
  double height = 0.0;
  bool snap = false;    // snap-to-grid toggle, default off
  double grid = kDefaultGridSize;
  std::vector<Node> buttons;  // FreeButton; the order is the z-order (last = on top)
  friend bool operator==(const FreeFormPanelLayout&, const FreeFormPanelLayout&) = default;
};

struct LayoutSet {
  MenuLayout menuBar;
  std::vector<ToolbarLayout> toolbars;
  std::vector<FreeFormPanelLayout> panels;
};

// ---- queries ------------------------------------------------------------------------------------

const char* sizeStepName(SizeStep step);
bool parseSizeStep(std::string_view name, SizeStep& step);
// Button edge in logical px for a size step: small 26, medium 32 (the toolbar's own size), large 40.
double sizeStepPixels(SizeStep step);

// Counts every node (containers excluded).
size_t countNodes(const LayoutSet& set);
// The node with `id` anywhere in the set, or nullptr.
const Node* findNode(const LayoutSet& set, std::string_view id);
const ToolbarLayout* findToolbar(const LayoutSet& set, std::string_view id);
const FreeFormPanelLayout* findPanel(const LayoutSet& set, std::string_view id);

// Structural problems of a layout set: duplicate ids, illegal parent/child kinds, too deep, too many
// nodes, over-long text, non-finite or too small free-form rectangles. Empty = the set is legal.
// Used by tests and by the persistence layer; effectiveLayout always returns a legal set.
std::vector<std::string> validateLayout(const LayoutSet& set);

// A valid generated or host id: 1..kMaxIdBytes of [A-Za-z0-9._:/-].
bool isValidNodeId(std::string_view id);

}  // namespace r1ui::commands::customize

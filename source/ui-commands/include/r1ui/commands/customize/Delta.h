// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: Delta, the user's customization stored as a difference over the built-in layouts (hide,
//   rename, move, user-created nodes and containers, toolbar and panel settings, free-form
//   rectangles), the Report of what could not be applied, and effectiveLayout, the pure function
//   that applies a Delta to a LayoutSet.
// Why: spec 06 rules 33 to 42 and decisions D1 to D6: customizations are keyed by stable ids, so a
//   product update that adds, removes or reorders built-in entries still lets the user's changes
//   apply sensibly. New built-in entries appear at their default position; a removed command's
//   entry is dropped (and reported); an anchor that no longer exists falls back to the end of the same
//   parent; user menus and other user nodes survive; changes inside a menu the owner has since locked
//   are ignored (the owner wins).
// Callers: Customization (editing, persistence), the widget binders (through Customization), tests.
//   Calls: Layout.h.
// effectiveLayout contract: deterministic and total. Whatever the delta contains (unknown ids, cycles,
//   illegal parents, huge counts, duplicates) it never throws and never returns an illegal layout
//   (validateLayout(result.layout) is empty); every problem becomes a ReportEntry and the offending
//   operation is skipped, leaving the node at its default position. Hidden and missing-command nodes
//   are removed unless EffectiveOptions asks to keep them (the edit display keeps both).
// Order of application: containers (built-in, then user toolbars and panels), added nodes in
//   creation order, moves in their stored order (a later move may anchor on an earlier one), then
//   per-node edits (hidden, label, rectangle), then toolbar and panel settings.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "r1ui/commands/customize/Layout.h"

namespace r1ui::commands::customize {

// Where a node goes inside a parent: first, last, or next to a sibling (the anchor).
enum class Side : uint8_t { End, Start, Before, After };

const char* sideName(Side side);
bool parseSide(std::string_view name, Side& side);

struct Placement {
  std::string parent;  // container or node id
  std::string anchor;  // sibling id for Before / After
  Side side = Side::End;
  friend bool operator==(const Placement&, const Placement&) = default;
};

struct NodeEdit {
  std::optional<bool> hidden;
  std::optional<std::string> label;  // the user's text; absent = default
  std::optional<Rect> rect;          // FreeButton position and size
  bool empty() const { return !hidden && !label && !rect; }
  friend bool operator==(const NodeEdit&, const NodeEdit&) = default;
};

struct MoveEdit {
  std::string node;
  Placement to;
  friend bool operator==(const MoveEdit&, const MoveEdit&) = default;
};

struct AddedNode {
  Node node;     // children stay empty: a nested user node is its own AddedNode with the parent set
  Placement at;
  friend bool operator==(const AddedNode&, const AddedNode&) = default;
};

struct ToolbarEdit {
  std::optional<SizeStep> sizeStep;
  std::optional<double> gap;
  friend bool operator==(const ToolbarEdit&, const ToolbarEdit&) = default;
};

struct PanelEdit {
  std::optional<bool> snap;
  std::optional<double> grid;
  friend bool operator==(const PanelEdit&, const PanelEdit&) = default;
};

struct Delta {
  std::map<std::string, NodeEdit> edits;
  std::vector<MoveEdit> moves;
  std::vector<AddedNode> added;
  std::vector<ToolbarLayout> userToolbars;       // items stay empty (they are AddedNodes)
  std::vector<FreeFormPanelLayout> userPanels;   // buttons stay empty
  std::map<std::string, ToolbarEdit> toolbarEdits;
  std::map<std::string, PanelEdit> panelEdits;
  uint32_t serial = 0;  // counter of generated ids; never goes down so deleted ids are not reused

  bool empty() const {
    return edits.empty() && moves.empty() && added.empty() && userToolbars.empty() && userPanels.empty() && toolbarEdits.empty() && panelEdits.empty();
  }
  // Entries that count against kMaxNodes when loading.
  size_t entryCount() const {
    return edits.size() + moves.size() + added.size() + userToolbars.size() + userPanels.size() + toolbarEdits.size() + panelEdits.size();
  }
  friend bool operator==(const Delta&, const Delta&) = default;
};

// `over` wins: node edits merge field by field, moves of the same node are replaced by the later one,
// added nodes and containers with the same id are replaced, settings merge field by field.
Delta mergeDeltas(const Delta& base, const Delta& over);

struct ReportEntry {
  enum class Code : uint8_t {
    MissingCommand,  // a command reference whose command is not registered (dropped or marked missing)
    MissingNode,     // an edit or move names a node that does not exist (any more)
    MissingParent,   // the parent of an added or moved node does not exist
    MissingAnchor,   // the anchor sibling does not exist: the node went to the end of the parent
    IllegalParent,   // the parent cannot hold that kind of node, or the node is in another container kind
    Cycle,           // a move into the node's own subtree
    Locked,          // the change touches a locked node and was ignored
    DuplicateId,     // a second node with an id in use (skipped)
    Invalid,         // a bad id, rectangle or setting (skipped or clamped)
    Limit            // depth or node count limit reached
  };
  Code code = Code::MissingNode;
  std::string id;      // the node concerned
  std::string detail;
};

const char* reportCodeName(ReportEntry::Code code);

struct Report {
  std::vector<ReportEntry> entries;
  size_t count(ReportEntry::Code code) const;
  bool has(ReportEntry::Code code, std::string_view id) const;
  bool clean() const { return entries.empty(); }
};

using CommandExists = std::function<bool(const std::string& commandId)>;

struct EffectiveOptions {
  bool keepHidden = false;   // hidden nodes stay in the tree with visible = false (edit display)
  bool keepMissing = false;  // nodes of unregistered commands stay with missing = true (edit display)
  CommandExists exists;      // empty = every command exists
};

struct EffectiveResult {
  LayoutSet layout;
  Report report;
};

EffectiveResult effectiveLayout(const LayoutSet& builtin, const Delta& delta, const EffectiveOptions& options = {});

}  // namespace r1ui::commands::customize

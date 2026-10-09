// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: internal tree utilities shared by the dock translation units: normalisation, invariant
//   validation, path navigation and panel lookup.
// Why: DockOps, DockGeometry and DockSerialize all need the same definition of "a valid tree";
//   keeping it in one file means an operation cannot commit a tree another file would reject.
// Callers: ui-dock sources only (not a public header). Recursion is bounded because every
//   entry point first runs withinLimits() (iterative) which caps depth at kMaxTreeDepth.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "r1ui/dock/DockLayout.h"

namespace r1ui::dock::detail {

// Where a docked panel lives.
struct PanelLocation {
  size_t area = 0;  // index into the areas vector
  Path path;        // to the stack
  size_t tab = 0;   // index within the stack's tabs
};

// Iterative check of depth (<= kMaxTreeDepth) and total node count (<= kMaxTreeNodes) over all
// areas. Returns an error message, or an empty string when within limits. Safe on any shape.
std::string withinLimits(const std::vector<Area>& areas);

// Applies the collapse rules to one subtree; returns false when the subtree vanishes entirely.
bool normaliseNode(Node& node);
// Normalises every area: removes empty floating areas, empties the main area's root if needed.
void normaliseAreas(std::vector<Area>& areas);

// Checks every invariant listed in DockTypes.h plus panel registration and uniqueness.
// Returns an error message, or an empty string when the layout is valid.
std::string validateAreas(const std::vector<Area>& areas, const std::vector<PanelInfo>& panels,
                          const DockConfig& config);

// Checks the closed-panel memory against the (valid) areas: every slot names a known panel that is
// not docked, ids are unique, referenced panels are known and rectangles are finite.
std::string validateClosed(const std::vector<ClosedSlot>& closed, const std::vector<Area>& areas,
                           const std::vector<PanelInfo>& panels);

Node* nodeAt(Area& area, const Path& path);
const Node* nodeAt(const Area& area, const Path& path);
std::optional<size_t> areaIndex(const std::vector<Area>& areas, uint32_t id);
std::optional<PanelLocation> findPanel(const std::vector<Area>& areas, PanelId panel);
// Number of panels in a subtree.
size_t countPanels(const Node& node);
// The first tab of the first stack of a subtree (0 for an empty one).
PanelId firstPanelOf(const Node& node);

// Pixel size of each child of `split` laid out in `bounds`: weights share the length left after
// the handles (spec 05 rules 18-19), honouring config.minPanelSize when the space allows it.
// Never negative; all zero when no space is left.
std::vector<double> splitSizes(const Node& split, const Rect& bounds, const DockConfig& config);

// Rounds half up; used so adjacent rectangles share edges exactly.
double roundEdge(double value);

}  // namespace r1ui::dock::detail

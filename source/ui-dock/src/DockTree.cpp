// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: normalisation (spec 02 rules 43-46), invariant validation and path/lookup helpers.
// Invariants: normaliseNode is idempotent; validateAreas accepts exactly the trees that
//   normalisation produces (plus legal weights/limits), so commit-after-normalise cannot fail
//   except for real limit or weight violations.
#include "DockTree.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "r1ui/core/CheckedCast.h"

namespace r1ui::dock {

Node Node::stack(std::vector<PanelId> tabs, size_t active, double weight) {
  Node node;
  node.kind = Kind::Stack;
  node.tabs = std::move(tabs);
  node.active = active;
  node.weight = weight;
  return node;
}

Node Node::split(Axis axis, std::vector<Node> children, double weight) {
  Node node;
  node.kind = Kind::Split;
  node.axis = axis;
  node.children = std::move(children);
  node.weight = weight;
  return node;
}

namespace detail {

namespace {

bool legalWeight(double w) { return std::isfinite(w) && w >= kMinWeight && w <= kMaxWeight; }

// Keeps weights from drifting toward the legal limits after many splices: rescales a split's
// children so their sum is back near the child count when it leaves [1e-3, 1e6].
void rescaleIfDrifted(std::vector<Node>& children) {
  double sum = 0.0;
  for (const Node& c : children) sum += c.weight;
  if (sum >= 1.0e-3 && sum <= 1.0e6) return;
  const double factor = static_cast<double>(children.size()) / sum;
  for (Node& c : children) c.weight *= factor;
}

}  // namespace

std::string withinLimits(const std::vector<Area>& areas) {
  struct Frame {
    const Node* node;
    size_t depth;
  };
  size_t nodes = 0;
  std::vector<Frame> pending;
  for (const Area& area : areas) {
    if (!area.root) continue;
    pending.push_back({&*area.root, 1});
    while (!pending.empty()) {
      const Frame frame = pending.back();
      pending.pop_back();
      if (frame.depth > kMaxTreeDepth) return "dock tree is nested deeper than the limit";
      if (++nodes > kMaxTreeNodes) return "dock tree has more nodes than the limit";
      for (const Node& child : frame.node->children) pending.push_back({&child, frame.depth + 1});
    }
  }
  return {};
}

bool normaliseNode(Node& node) {
  if (node.kind == Node::Kind::Stack) {
    if (node.tabs.empty()) return false;
    node.active = std::min(node.active, node.tabs.size() - 1);
    return true;
  }
  std::vector<Node> kept;
  kept.reserve(node.children.size());
  for (Node& child : node.children) {
    if (!normaliseNode(child)) continue;
    if (child.kind == Node::Kind::Split && child.axis == node.axis) {
      // Spec 02 rule 45: dissolve a same-direction child, preserving visible proportions.
      double total = 0.0;
      for (const Node& grand : child.children) total += grand.weight;
      const double factor = total > 0.0 ? child.weight / total : 1.0;
      for (Node& grand : child.children) {
        grand.weight *= factor;
        kept.push_back(std::move(grand));
      }
    } else {
      kept.push_back(std::move(child));
    }
  }
  if (kept.empty()) return false;
  if (kept.size() == 1) {
    const double weight = node.weight;
    node = std::move(kept.front());
    node.weight = weight;
    return true;
  }
  rescaleIfDrifted(kept);
  node.children = std::move(kept);
  return true;
}

void normaliseAreas(std::vector<Area>& areas) {
  for (Area& area : areas) {
    if (area.root && !normaliseNode(*area.root)) area.root.reset();
  }
  std::erase_if(areas, [](const Area& a) { return a.id != kMainAreaId && !a.root; });
}

namespace {

// Recursive structural checks; safe because withinLimits() already bounded the depth.
std::string checkNode(const Node& node, const Axis* parentAxis, const std::unordered_set<PanelId>& known,
                      std::unordered_set<PanelId>& seen) {
  if (!legalWeight(node.weight)) return "node weight is not a finite positive number in range";
  if (node.kind == Node::Kind::Stack) {
    if (!node.children.empty()) return "stack node has children";
    if (node.tabs.empty()) return "stack has no tabs";
    if (node.active >= node.tabs.size()) return "stack active tab index is out of range";
    for (PanelId id : node.tabs) {
      if (known.count(id) == 0) return "layout references unregistered panel " + std::to_string(id);
      if (!seen.insert(id).second) return "panel " + std::to_string(id) + " is docked twice";
    }
    return {};
  }
  if (!node.tabs.empty()) return "split node has tabs";
  if (node.children.size() < 2) return "split has fewer than two children";
  if (parentAxis != nullptr && *parentAxis == node.axis) return "split has the same axis as its parent";
  for (const Node& child : node.children) {
    std::string error = checkNode(child, &node.axis, known, seen);
    if (!error.empty()) return error;
  }
  return {};
}

}  // namespace

std::string validateAreas(const std::vector<Area>& areas, const std::vector<PanelInfo>& panels,
                          const DockConfig& config) {
  if (areas.empty() || areas.front().id != kMainAreaId) return "the main area is missing";
  if (areas.size() > kMaxFloatingAreas + 1) return "too many floating areas";
  if (std::string error = withinLimits(areas); !error.empty()) return error;

  std::unordered_set<PanelId> known;
  for (const PanelInfo& p : panels) known.insert(p.id);
  std::unordered_set<PanelId> seen;
  std::unordered_set<uint32_t> areaIds;
  for (size_t i = 0; i < areas.size(); ++i) {
    const Area& area = areas[i];
    if (!areaIds.insert(area.id).second) return "duplicate area id";
    if ((area.id == kMainAreaId) != (i == 0)) return "the main area must be first and unique";
    if (area.id != kMainAreaId) {
      const Rect& r = area.rect;
      const bool finite = std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.w) && std::isfinite(r.h);
      if (!finite || std::abs(r.x) > kMaxCoordinate || std::abs(r.y) > kMaxCoordinate ||
          r.w < config.minFloatSize || r.h < config.minFloatSize || r.w > kMaxCoordinate || r.h > kMaxCoordinate) {
        return "floating area rectangle is not finite or is out of range";
      }
      if (!area.root) return "floating area is empty";
    }
    if (area.root) {
      std::string error = checkNode(*area.root, nullptr, known, seen);
      if (!error.empty()) return error;
    }
  }
  return {};
}

Node* nodeAt(Area& area, const Path& path) {
  return const_cast<Node*>(nodeAt(static_cast<const Area&>(area), path));
}

const Node* nodeAt(const Area& area, const Path& path) {
  if (!area.root) return nullptr;
  const Node* node = &*area.root;
  for (uint32_t index : path) {
    if (node->kind != Node::Kind::Split || index >= node->children.size()) return nullptr;
    node = &node->children[index];
  }
  return node;
}

std::optional<size_t> areaIndex(const std::vector<Area>& areas, uint32_t id) {
  for (size_t i = 0; i < areas.size(); ++i) {
    if (areas[i].id == id) return i;
  }
  return std::nullopt;
}

namespace {

bool searchPanel(const Node& node, PanelId panel, Path& path, size_t& tab) {
  if (node.kind == Node::Kind::Stack) {
    const auto it = std::find(node.tabs.begin(), node.tabs.end(), panel);
    if (it == node.tabs.end()) return false;
    tab = static_cast<size_t>(it - node.tabs.begin());
    return true;
  }
  for (size_t i = 0; i < node.children.size(); ++i) {
    path.push_back(core::checkedCast<uint32_t>(i));
    if (searchPanel(node.children[i], panel, path, tab)) return true;
    path.pop_back();
  }
  return false;
}

}  // namespace

std::optional<PanelLocation> findPanel(const std::vector<Area>& areas, PanelId panel) {
  for (size_t a = 0; a < areas.size(); ++a) {
    if (!areas[a].root) continue;
    PanelLocation loc;
    loc.area = a;
    if (searchPanel(*areas[a].root, panel, loc.path, loc.tab)) return loc;
  }
  return std::nullopt;
}

size_t countPanels(const Node& node) {
  size_t total = node.tabs.size();
  for (const Node& child : node.children) total += countPanels(child);
  return total;
}

double roundEdge(double value) { return std::floor(value + 0.5); }

}  // namespace detail
}  // namespace r1ui::dock

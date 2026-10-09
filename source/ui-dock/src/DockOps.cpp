// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: construction and the editing operations of DockLayout: create, dock, closePanel,
//   activateTab, moveSplitter, validate and equality.
// Why: all mutation funnels through commit(), which normalises and validates a copy before
//   replacing the live state, so no operation can leave the tree invalid (guard register:
//   "keep the previous valid state on rejection").
// Callers: the sandbox and tests through DockLayout.h. Calls: DockTree/DockGeometry internals.
// Invariants: operations resolve their target by panel id / path BEFORE removing the dragged tab
//   and insert before normalising, so an emptied source stack is still a valid anchor.
#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "DockTree.h"
#include "r1ui/core/CheckedCast.h"
#include "r1ui/dock/DockLayout.h"

namespace r1ui::dock {

namespace {

bool positive(double v) { return std::isfinite(v) && v > 0.0; }
bool nonNegative(double v) { return std::isfinite(v) && v >= 0.0; }

std::string configError(const DockConfig& c) {
  const bool ok = nonNegative(c.handleThickness) && nonNegative(c.minPanelSize) && positive(c.tabStripHeight) &&
                  positive(c.maxTabWidth) && nonNegative(c.dragThreshold) && positive(c.crossInsetFraction) &&
                  c.crossInsetFraction < 0.5 && positive(c.crossInsetMin) && positive(c.crossInsetMax) &&
                  c.crossInsetMin <= c.crossInsetMax && nonNegative(c.edgeTargetThickness) &&
                  positive(c.ghostMaxLongSide) && positive(c.ghostMinScale) && c.ghostMinScale <= 1.0 &&
                  positive(c.defaultFloatWidth) && positive(c.defaultFloatHeight) && positive(c.minFloatSize) &&
                  c.minFloatSize <= kMaxCoordinate && c.handleThickness <= kMaxCoordinate && positive(c.minTabWidth) &&
                  positive(c.appPageStripHeight) && positive(c.appPageMaxTabWidth) && nonNegative(c.touchHandleBand) &&
                  nonNegative(c.visibleMargin) && nonNegative(c.hoverActivateSeconds) && positive(c.keyboardResizeStep);
  return ok ? std::string() : "dock configuration has a non-finite, negative or inconsistent value";
}

Axis axisOf(Side side) { return (side == Side::Left || side == Side::Right) ? Axis::Row : Axis::Column; }
bool comesFirst(Side side) { return side == Side::Left || side == Side::Top; }

// Orders (existing, added) by side: the added node goes first for Left/Top.
std::vector<Node> ordered(Node existing, Node added, Side side) {
  std::vector<Node> pair;
  pair.reserve(2);
  if (comesFirst(side)) {
    pair.push_back(std::move(added));
    pair.push_back(std::move(existing));
  } else {
    pair.push_back(std::move(existing));
    pair.push_back(std::move(added));
  }
  return pair;
}

// Removes the tab without normalising: the (possibly empty) stack stays as an anchor.
void detachTab(Node& stack, size_t tab) {
  stack.tabs.erase(stack.tabs.begin() + core::checkedCast<std::ptrdiff_t>(tab));
  if (tab < stack.active) --stack.active;
}

// Describes where `panel` (about to close) sits so it can reopen there (spec 02 rule 41): the other
// tabs of its stack; when it is alone, a panel of the neighbouring region and the side it was on;
// when it is alone in a floating window, the window rectangle.
ClosedSlot rememberSlot(const std::vector<Area>& areas, const detail::PanelLocation& loc, PanelId panel) {
  ClosedSlot slot;
  slot.panel = panel;
  slot.index = loc.tab;
  const Area& area = areas[loc.area];
  const Node& stack = *detail::nodeAt(area, loc.path);
  for (PanelId other : stack.tabs) {
    if (other != panel) slot.neighbours.push_back(other);
  }
  if (area.id != kMainAreaId) {
    slot.floating = true;
    slot.floatRect = area.rect;
  }
  if (slot.neighbours.empty() && !loc.path.empty()) {
    Path parentPath = loc.path;
    const uint32_t at = parentPath.back();
    parentPath.pop_back();
    const Node& parent = *detail::nodeAt(area, parentPath);
    const bool after = static_cast<size_t>(at) + 1 < parent.children.size();
    const Node& sibling = parent.children[after ? at + 1 : at - 1];
    slot.anchor = detail::firstPanelOf(sibling);
    if (parent.axis == Axis::Row) {
      slot.anchorSide = after ? Side::Left : Side::Right;
    } else {
      slot.anchorSide = after ? Side::Top : Side::Bottom;
    }
  }
  return slot;
}

}  // namespace

// ---- Construction and accessors ---------------------------------------------------------

DockLayoutResult DockLayout::create(std::vector<PanelInfo> panels, const DockConfig& config,
                                    std::optional<Node> mainRoot) {
  DockLayoutResult result;
  if (std::string error = configError(config); !error.empty()) {
    result.error = std::move(error);
    return result;
  }
  if (panels.size() > kMaxPanels) {
    result.error = "too many panels registered";
    return result;
  }
  std::unordered_set<PanelId> ids;
  for (const PanelInfo& p : panels) {
    if (!ids.insert(p.id).second) {
      result.error = "duplicate panel id " + std::to_string(p.id);
      return result;
    }
  }
  DockLayout layout;
  layout.config_ = config;
  layout.panels_ = std::move(panels);
  Area main;
  main.id = kMainAreaId;
  main.root = std::move(mainRoot);
  Status status = layout.commit({std::move(main)}, 1);
  if (!status) {
    result.error = std::move(status.error);
    return result;
  }
  result.layout = std::move(layout);
  return result;
}

const PanelInfo* DockLayout::panel(PanelId id) const {
  for (const PanelInfo& p : panels_) {
    if (p.id == id) return &p;
  }
  return nullptr;
}

bool DockLayout::isDocked(PanelId id) const { return detail::findPanel(areas_, id).has_value(); }

bool operator==(const DockLayout& a, const DockLayout& b) {
  if (a.areas_.size() != b.areas_.size() || a.panels_.size() != b.panels_.size()) return false;
  for (size_t i = 0; i < a.panels_.size(); ++i) {
    if (a.panels_[i].id != b.panels_[i].id || a.panels_[i].locked != b.panels_[i].locked) return false;
  }
  // Floating area ids are bookkeeping handed out in creation order; equality is about content.
  for (size_t i = 0; i < a.areas_.size(); ++i) {
    if (a.areas_[i].rect != b.areas_[i].rect || a.areas_[i].root != b.areas_[i].root ||
        a.areas_[i].window != b.areas_[i].window) {
      return false;
    }
  }
  return a.closed_ == b.closed_ && a.meta_ == b.meta_;
}

Status DockLayout::validate() const {
  std::string error = detail::validateAreas(areas_, panels_, config_);
  if (error.empty()) error = detail::validateClosed(closed_, areas_, panels_);
  return error.empty() ? Status::success() : Status::failure(std::move(error));
}

Status DockLayout::commit(std::vector<Area> candidate, uint32_t nextAreaId) {
  return commitWith(std::move(candidate), nextAreaId, closed_);
}

// Panels that are docked in the candidate lose their closed slot (docking forgets it); the rest
// of the memory must still describe known, closed panels.
Status DockLayout::commitWith(std::vector<Area> candidate, uint32_t nextAreaId, std::vector<ClosedSlot> closed) {
  if (std::string error = detail::withinLimits(candidate); !error.empty()) return Status::failure(std::move(error));
  detail::normaliseAreas(candidate);
  if (std::string error = detail::validateAreas(candidate, panels_, config_); !error.empty()) {
    return Status::failure(std::move(error));
  }
  std::erase_if(closed, [&](const ClosedSlot& s) { return detail::findPanel(candidate, s.panel).has_value(); });
  if (std::string error = detail::validateClosed(closed, candidate, panels_); !error.empty()) {
    return Status::failure(std::move(error));
  }
  areas_ = std::move(candidate);
  closed_ = std::move(closed);
  nextAreaId_ = nextAreaId;
  return Status::success();
}

// ---- dock -------------------------------------------------------------------------------

Status DockLayout::dock(PanelId panelId, const DropZone& zone) {
  if (panel(panelId) == nullptr) return Status::failure("unknown panel " + std::to_string(panelId));
  std::vector<Area> candidate = areas_;
  uint32_t nextId = nextAreaId_;
  const std::optional<detail::PanelLocation> source = detail::findPanel(candidate, panelId);
  const PanelInfo& info = *panel(panelId);
  if (source && info.locked) return Status::failure("panel " + std::to_string(panelId) + " is locked");
  if (zone.kind == DropKind::Float && !info.canFloat) {
    return Status::failure("panel " + std::to_string(panelId) + " cannot float");
  }

  // Resolve the target before touching the tree; paths stay valid until normalisation.
  std::optional<size_t> targetArea = detail::areaIndex(candidate, zone.area);
  std::optional<detail::PanelLocation> targetStack;
  if (zone.kind == DropKind::SplitStack || zone.kind == DropKind::JoinStack) {
    targetStack = detail::findPanel(candidate, zone.stackPanel);
    if (!targetStack || candidate[targetStack->area].id != zone.area) return Status::failure("drop target stack no longer exists");
    const Node* stack = detail::nodeAt(candidate[targetStack->area], targetStack->path);
    const bool ownSole = source && source->area == targetStack->area && source->path == targetStack->path && stack->tabs.size() == 1;
    if (zone.kind == DropKind::SplitStack && ownSole) return Status::failure("a tab cannot be split off its own single-tab stack");
  } else if (zone.kind != DropKind::Float) {
    if (!targetArea) return Status::failure("drop target area no longer exists");
    const Area& area = candidate[*targetArea];
    if (zone.kind == DropKind::FillEmptyArea && area.root) return Status::failure("drop target area is not empty");
    if (zone.kind == DropKind::SplitAreaEdge) {
      if (!area.root) return Status::failure("edge drop needs a non-empty area");
      if (source && source->area == *targetArea && detail::countPanels(*area.root) == 1) {
        return Status::failure("an area holding only the dragged tab cannot take it again");
      }
    }
  }

  if (source) detachTab(*detail::nodeAt(candidate[source->area], source->path), source->tab);

  switch (zone.kind) {
    case DropKind::JoinStack: {
      Node& stack = *detail::nodeAt(candidate[targetStack->area], targetStack->path);
      const size_t at = std::min(zone.index, stack.tabs.size());
      stack.tabs.insert(stack.tabs.begin() + core::checkedCast<std::ptrdiff_t>(at), panelId);
      stack.active = at;
      break;
    }
    case DropKind::SplitStack: {
      Area& area = candidate[targetStack->area];
      Path path = targetStack->path;
      Node fresh = Node::stack({panelId});
      if (path.empty()) {
        Node existing = std::move(*area.root);
        existing.weight = 1.0;
        area.root = Node::split(axisOf(zone.side), ordered(std::move(existing), std::move(fresh), zone.side));
        break;
      }
      const uint32_t at = path.back();
      path.pop_back();
      Node& parent = *detail::nodeAt(area, path);
      if (parent.axis == axisOf(zone.side)) {
        fresh.weight = parent.children[at].weight;  // spec 02 rule 27: same weight as the target
        parent.children.insert(parent.children.begin() + core::checkedCast<std::ptrdiff_t>(comesFirst(zone.side) ? at : at + 1),
                               std::move(fresh));
      } else {
        const double weight = parent.children[at].weight;
        Node target = std::move(parent.children[at]);
        target.weight = 1.0;
        parent.children[at] = Node::split(axisOf(zone.side), ordered(std::move(target), std::move(fresh), zone.side), weight);
      }
      break;
    }
    case DropKind::SplitAreaEdge: {
      Area& area = candidate[*targetArea];
      Node existing = std::move(*area.root);
      existing.weight = 1.0;
      area.root = Node::split(axisOf(zone.side), ordered(std::move(existing), Node::stack({panelId}), zone.side));
      break;
    }
    case DropKind::FillEmptyArea:
      candidate[*targetArea].root = Node::stack({panelId});
      break;
    case DropKind::Float: {
      if (nextId == UINT32_MAX) return Status::failure("area ids exhausted");
      Area area;
      area.id = nextId++;
      area.rect = {zone.preview.x, zone.preview.y, std::max(zone.preview.w, config_.minFloatSize),
                   std::max(zone.preview.h, config_.minFloatSize)};
      area.root = Node::stack({panelId});
      candidate.push_back(std::move(area));
      break;
    }
  }
  return commit(std::move(candidate), nextId);
}

// ---- close / activate -------------------------------------------------------------------

Status DockLayout::closePanel(PanelId panelId) {
  const PanelInfo* info = panel(panelId);
  if (info == nullptr) return Status::failure("unknown panel " + std::to_string(panelId));
  if (!info->canClose) return Status::failure("panel " + std::to_string(panelId) + " cannot be closed");
  if (info->locked) return Status::failure("panel " + std::to_string(panelId) + " is locked");
  std::vector<Area> candidate = areas_;
  const std::optional<detail::PanelLocation> loc = detail::findPanel(candidate, panelId);
  if (!loc) return Status::failure("panel " + std::to_string(panelId) + " is not docked");
  std::vector<ClosedSlot> closed = closed_;
  if (info->kind != PanelKind::Document) {  // spec 02 rule 41: documents are forgotten
    std::erase_if(closed, [&](const ClosedSlot& s) { return s.panel == panelId; });
    closed.push_back(rememberSlot(candidate, *loc, panelId));
  }
  // Spec 02 rule 5: when the front tab closes the right neighbour (else left) takes over; the
  // index stays put and normalisation clamps it to the last tab.
  detachTab(*detail::nodeAt(candidate[loc->area], loc->path), loc->tab);
  return commitWith(std::move(candidate), nextAreaId_, std::move(closed));
}

Status DockLayout::activateTab(PanelId panelId) {
  std::vector<Area> candidate = areas_;
  const std::optional<detail::PanelLocation> loc = detail::findPanel(candidate, panelId);
  if (!loc) return Status::failure("panel " + std::to_string(panelId) + " is not docked");
  detail::nodeAt(candidate[loc->area], loc->path)->active = loc->tab;
  return commit(std::move(candidate), nextAreaId_);
}

// ---- splitter ---------------------------------------------------------------------------

Status DockLayout::moveSplitter(const SplitterHandle& handle, double delta, const Rect& mainRect) {
  if (!std::isfinite(delta)) return Status::failure("splitter delta is not finite");
  std::vector<Area> candidate = areas_;
  const std::optional<size_t> areaIdx = detail::areaIndex(candidate, handle.area);
  if (!areaIdx) return Status::failure("splitter area no longer exists");
  Node* split = detail::nodeAt(candidate[*areaIdx], handle.path);
  if (split == nullptr || split->kind != Node::Kind::Split || split->axis != handle.axis ||
      static_cast<size_t>(handle.index) + 1 >= split->children.size()) {
    return Status::failure("splitter handle no longer exists");
  }

  const LayoutResult layout = computeLayout(mainRect);
  const auto found = std::find_if(layout.splits.begin(), layout.splits.end(), [&](const SplitLayout& s) {
    return s.area == handle.area && s.path == handle.path;
  });
  if (found == layout.splits.end()) return Status::failure("splitter has no layout");
  const std::vector<double> sizes = detail::splitSizes(*split, found->bounds, config_);

  // Decision D11: a collapsed neighbour is brought back by the handle (its old weight restores its
  // old size); the drag itself never reduces anything below the floor or to nothing.
  const size_t i = handle.index;
  Node& left = split->children[i];
  Node& right = split->children[i + 1];
  if (left.collapsed || right.collapsed) {
    left.collapsed = false;
    right.collapsed = false;
    return commit(std::move(candidate), nextAreaId_);
  }

  // Spec 05 rules 8-11: only the two neighbours change, clamped so neither drops below the
  // floor (or below where it already is), and their combined weight is preserved.
  const double before = sizes[i];
  const double total = before + sizes[i + 1];
  if (total <= 0.0 || delta == 0.0) return Status::success();
  const double lo = std::min(config_.minPanelSize, before);
  const double hi = total - std::min(config_.minPanelSize, sizes[i + 1]);
  const double after = std::clamp(before + delta, lo, hi);
  const double combined = left.weight + right.weight;
  const double first = std::clamp(combined * after / total, combined * 1.0e-6, combined * (1.0 - 1.0e-6));
  left.weight = first;
  right.weight = combined - first;
  const bool anyPinned = std::any_of(split->children.begin(), split->children.end(), [](const Node& c) { return c.pinned; });
  if (anyPinned) {
    // With pins in play the flexible children share what the pins leave, so the pair's new sizes
    // are written back as pixels: pinned neighbours store them, every flexible child's weight
    // becomes its pixel size so the siblings keep exactly the size they have.
    std::vector<double> px = sizes;
    px[i] = after;
    px[i + 1] = total - after;
    double flexibleTotal = 0.0;
    double weightTotal = 0.0;
    for (size_t k = 0; k < px.size(); ++k) {
      Node& c = split->children[k];
      if (c.collapsed) continue;
      if (c.pinned) {
        c.pinnedSize = px[k];
      } else {
        flexibleTotal += px[k];
        weightTotal += c.weight;
      }
    }
    if (flexibleTotal > 0.0) {
      for (size_t k = 0; k < px.size(); ++k) {
        Node& c = split->children[k];
        if (!c.collapsed && !c.pinned) c.weight = std::max(weightTotal * px[k] / flexibleTotal, kMinWeight);
      }
    }
  }
  return commit(std::move(candidate), nextAreaId_);
}

}  // namespace r1ui::dock

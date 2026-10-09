// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the DockLayout operations that are not tab or splitter editing: opening a panel at its
//   remembered or suggested slot, pinning and collapsing regions, lock flags, window rectangles and
//   state, layout names and fitting windows onto the connected monitors, plus slot queries.
// Why: DockOps.cpp keeps the tab/handle editing core; these operations share its commit gate
//   (work on a copy, normalise, validate, commit) but have their own rules (spec 02 rule 55, spec 03
//   rules 62-67, decisions D10, D11, D13).
// Callers: DockHost, LayoutManager, tests through DockLayout.h.
// Invariants: every operation leaves the layout unchanged on failure; positions are only ever
//   resolved through panel ids (stack paths are positions and are looked up fresh each time).
#include <algorithm>
#include <cmath>

#include "DockTree.h"
#include "r1ui/core/CheckedCast.h"
#include "r1ui/dock/DockLayout.h"

namespace r1ui::dock {

namespace {

// The bounds of the node at `path` in a freshly computed layout, or nullopt when it has none.
std::optional<Rect> boundsOfNode(const LayoutResult& layout, uint32_t area, const Path& path) {
  for (const SplitLayout& s : layout.splits) {
    if (s.area == area && s.path == path) return s.bounds;
  }
  for (const StackLayout& s : layout.stacks) {
    if (s.area == area && s.path == path) return s.bounds;
  }
  return std::nullopt;
}

}  // namespace

// ---- queries ----------------------------------------------------------------------------

std::optional<PanelSlot> DockLayout::locate(PanelId id) const {
  const std::optional<detail::PanelLocation> loc = detail::findPanel(areas_, id);
  if (!loc) return std::nullopt;
  const Node* stack = detail::nodeAt(areas_[loc->area], loc->path);
  PanelSlot slot;
  slot.area = areas_[loc->area].id;
  slot.path = loc->path;
  slot.tab = loc->tab;
  slot.tabCount = stack->tabs.size();
  slot.front = stack->active == loc->tab;
  return slot;
}

const ClosedSlot* DockLayout::closedSlot(PanelId id) const {
  for (const ClosedSlot& s : closed_) {
    if (s.panel == id) return &s;
  }
  return nullptr;
}

// ---- opening ----------------------------------------------------------------------------

OpenOutcome DockLayout::openPanel(PanelId id, const OpenOptions& options) {
  OpenOutcome out;
  const PanelInfo* info = panel(id);
  if (info == nullptr) {
    out.status = Status::failure("unknown panel " + std::to_string(id));
    return out;
  }
  if (isDocked(id)) {
    out.alreadyOpen = true;
    out.status = activateTab(id);  // rule 8: a repeated request only brings it forward
    return out;
  }

  const auto joinTabsOf = [&](PanelId neighbour, size_t index) -> std::optional<DropZone> {
    const std::optional<PanelSlot> slot = locate(neighbour);
    if (!slot) return std::nullopt;
    DropZone zone;
    zone.kind = DropKind::JoinStack;
    zone.area = slot->area;
    zone.stackPanel = neighbour;
    zone.index = std::min(index, slot->tabCount);
    return zone;
  };
  const auto splitBeside = [&](PanelId anchor, Side side) -> std::optional<DropZone> {
    const std::optional<PanelSlot> slot = locate(anchor);
    if (!slot) return std::nullopt;
    DropZone zone;
    zone.kind = DropKind::SplitStack;
    zone.area = slot->area;
    zone.stackPanel = anchor;
    zone.side = side;
    return zone;
  };
  const auto floatRectFor = [&](const Rect& bounds) {
    double w = info->floatWidth > 0.0 ? info->floatWidth : config_.defaultFloatWidth;
    double h = info->floatHeight > 0.0 ? info->floatHeight : config_.defaultFloatHeight;
    const bool fits = std::isfinite(bounds.w) && std::isfinite(bounds.h) && bounds.w > 0.0 && bounds.h > 0.0;
    if (fits) {
      w = std::min(w, bounds.w);
      h = std::min(h, bounds.h);
    }
    w = std::max(w, config_.minFloatSize);
    h = std::max(h, config_.minFloatSize);
    const double ox = fits && std::isfinite(bounds.x) ? bounds.x + (bounds.w - w) / 2.0 : 0.0;
    const double oy = fits && std::isfinite(bounds.y) ? bounds.y + (bounds.h - h) / 2.0 : 0.0;
    return Rect{ox, oy, w, h};
  };

  std::vector<DropZone> candidates;
  if (const ClosedSlot* slot = closedSlot(id)) {  // rule 55.2: the slot it was closed from
    for (PanelId n : slot->neighbours) {
      if (auto zone = joinTabsOf(n, slot->index)) {
        candidates.push_back(*zone);
        break;
      }
    }
    if (slot->anchor != 0) {
      if (auto zone = splitBeside(slot->anchor, slot->anchorSide)) candidates.push_back(*zone);
    }
    if (slot->floating && info->canFloat) {
      DropZone zone;
      zone.kind = DropKind::Float;
      zone.preview = slot->floatRect.w >= config_.minFloatSize && slot->floatRect.h >= config_.minFloatSize
                         ? slot->floatRect
                         : floatRectFor(options.floatBounds);
      candidates.push_back(zone);
    }
  }
  if (info->suggested.relativeTo != 0) {  // the module's suggested first position
    const auto zone = info->suggested.asTab ? joinTabsOf(info->suggested.relativeTo, kMaxPanels)
                                            : splitBeside(info->suggested.relativeTo, info->suggested.side);
    if (zone) candidates.push_back(*zone);
  }
  if (options.floatWhenUnplaced && info->canFloat) {
    DropZone zone;
    zone.kind = DropKind::Float;
    zone.preview = floatRectFor(options.floatBounds);
    candidates.push_back(zone);
  }
  if (const Area& main = areas_.front(); main.root) {  // the default region
    if (auto zone = joinTabsOf(detail::firstPanelOf(*main.root), kMaxPanels)) candidates.push_back(*zone);
  } else {
    DropZone zone;
    zone.kind = DropKind::FillEmptyArea;
    zone.area = kMainAreaId;
    candidates.push_back(zone);
  }
  if (info->canFloat) {
    DropZone zone;
    zone.kind = DropKind::Float;
    zone.preview = floatRectFor(options.floatBounds);
    candidates.push_back(zone);
  }

  Status last = Status::failure("panel " + std::to_string(id) + " has nowhere to open");
  for (const DropZone& zone : candidates) {
    last = dock(id, zone);
    if (last) break;
  }
  out.status = std::move(last);
  return out;
}

// ---- pinning, collapsing --------------------------------------------------------------------

Status DockLayout::setPinned(uint32_t area, const Path& path, bool pinned, const Rect& mainRect) {
  if (path.empty()) return Status::failure("an area root cannot be pinned");
  std::vector<Area> candidate = areas_;
  const std::optional<size_t> areaIdx = detail::areaIndex(candidate, area);
  if (!areaIdx) return Status::failure("area no longer exists");
  Node* node = detail::nodeAt(candidate[*areaIdx], path);
  Path parentPath = path;
  parentPath.pop_back();
  Node* parent = detail::nodeAt(candidate[*areaIdx], parentPath);
  if (node == nullptr || parent == nullptr || parent->kind != Node::Kind::Split) return Status::failure("region no longer exists");
  if (pinned) {
    const std::optional<Rect> bounds = boundsOfNode(computeLayout(mainRect), area, path);
    if (!bounds) return Status::failure("region has no layout");
    node->pinned = true;
    node->pinnedSize = parent->axis == Axis::Row ? bounds->w : bounds->h;
    const bool flexibleLeft = std::any_of(parent->children.begin(), parent->children.end(),
                                          [](const Node& c) { return !c.pinned && !c.collapsed; });
    if (!flexibleLeft) return Status::failure("at least one region of a split must stay flexible");
  } else {
    node->pinned = false;
    node->pinnedSize = 0.0;
  }
  return commit(std::move(candidate), nextAreaId_);
}

Status DockLayout::setCollapsed(uint32_t area, const Path& path, bool collapsed) {
  if (path.empty()) return Status::failure("an area root cannot be collapsed");
  std::vector<Area> candidate = areas_;
  const std::optional<size_t> areaIdx = detail::areaIndex(candidate, area);
  if (!areaIdx) return Status::failure("area no longer exists");
  Node* node = detail::nodeAt(candidate[*areaIdx], path);
  Path parentPath = path;
  parentPath.pop_back();
  Node* parent = detail::nodeAt(candidate[*areaIdx], parentPath);
  if (node == nullptr || parent == nullptr || parent->kind != Node::Kind::Split) return Status::failure("region no longer exists");
  node->collapsed = collapsed;
  if (collapsed) {
    node->pinned = false;
    node->pinnedSize = 0.0;
    const bool flexibleLeft = std::any_of(parent->children.begin(), parent->children.end(),
                                          [](const Node& c) { return !c.pinned && !c.collapsed; });
    if (!flexibleLeft) return Status::failure("the last visible region cannot be collapsed");
  }
  return commit(std::move(candidate), nextAreaId_);
}

// ---- locks, windows, names -------------------------------------------------------------------

Status DockLayout::setPanelLocked(PanelId id, bool locked) {
  for (PanelInfo& p : panels_) {
    if (p.id == id) {
      p.locked = locked;
      return Status::success();
    }
  }
  return Status::failure("unknown panel " + std::to_string(id));
}

Status DockLayout::setAreaRect(uint32_t area, const Rect& rect) {
  if (area == kMainAreaId) return Status::failure("the main area has no rectangle of its own");
  std::vector<Area> candidate = areas_;
  const std::optional<size_t> idx = detail::areaIndex(candidate, area);
  if (!idx) return Status::failure("area no longer exists");
  if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.w) || !std::isfinite(rect.h)) {
    return Status::failure("window rectangle is not finite");
  }
  candidate[*idx].rect = {rect.x, rect.y, std::max(rect.w, config_.minFloatSize), std::max(rect.h, config_.minFloatSize)};
  return commit(std::move(candidate), nextAreaId_);
}

Status DockLayout::raiseArea(uint32_t area) {
  if (area == kMainAreaId) return Status::failure("the main area is always at the bottom");
  std::vector<Area> candidate = areas_;
  const std::optional<size_t> idx = detail::areaIndex(candidate, area);
  if (!idx) return Status::failure("area no longer exists");
  if (*idx + 1 == candidate.size()) return Status::success();
  std::rotate(candidate.begin() + core::checkedCast<std::ptrdiff_t>(*idx), candidate.begin() + core::checkedCast<std::ptrdiff_t>(*idx) + 1,
              candidate.end());
  return commit(std::move(candidate), nextAreaId_);
}

Status DockLayout::setWindowState(uint32_t area, const WindowState& state) {
  std::vector<Area> candidate = areas_;
  const std::optional<size_t> idx = detail::areaIndex(candidate, area);
  if (!idx) return Status::failure("area no longer exists");
  candidate[*idx].window = state;
  return commit(std::move(candidate), nextAreaId_);
}

Status DockLayout::setMeta(LayoutMeta meta) {
  if (meta.name.size() > kMaxNameBytes) return Status::failure("layout name is too long");
  if (meta.description.size() > kMaxDescriptionBytes) return Status::failure("layout description is too long");
  meta_ = std::move(meta);
  return Status::success();
}

size_t DockLayout::fitWindows(const MonitorSet& monitors) {
  std::vector<Area> candidate = areas_;
  size_t changed = 0;
  for (Area& area : candidate) {
    Rect* target = area.id == kMainAreaId ? (area.window.hasRect ? &area.window.rect : nullptr) : &area.rect;
    if (target == nullptr) continue;
    const FitResult fit = fitWindowRect(*target, monitors, config_.visibleMargin, config_.minFloatSize);
    if (fit.moved || fit.resized) {
      *target = fit.rect;
      ++changed;
    }
  }
  if (changed == 0) return 0;
  return commit(std::move(candidate), nextAreaId_) ? changed : 0;
}

}  // namespace r1ui::dock

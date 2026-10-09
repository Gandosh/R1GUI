// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: geometry of the dock model: weight-based space sharing (spec 05 rules 18-25), the
//   rectangles of stacks, tab strips, tabs and splitter handles, handle hit testing, the drag
//   threshold, and drop-zone hit testing (spec 02 rules 17-30, spec 08 rule 34).
// Callers: DockLayout members declared in DockLayout.h and DockOps (splitSizes).
// Failure behavior: no function throws or returns an error; non-finite or negative inputs are
//   sanitised to zero-sized rectangles / the origin, so a hostile pointer or window size cannot
//   produce NaN geometry. Sizes are never negative; edges are rounded so neighbours share edges.
#include <algorithm>
#include <cmath>

#include "DockTree.h"
#include "r1ui/core/CheckedCast.h"
#include "r1ui/dock/DockLayout.h"

namespace r1ui::dock {

namespace {

using detail::roundEdge;

double finiteOr(double value, double fallback) { return std::isfinite(value) ? value : fallback; }

Rect sanitiseRect(const Rect& r) {
  return {std::clamp(finiteOr(r.x, 0.0), -kMaxCoordinate, kMaxCoordinate),
          std::clamp(finiteOr(r.y, 0.0), -kMaxCoordinate, kMaxCoordinate),
          std::clamp(finiteOr(r.w, 0.0), 0.0, kMaxCoordinate),
          std::clamp(finiteOr(r.h, 0.0), 0.0, kMaxCoordinate)};
}

Point sanitisePoint(Point p) {
  return {std::clamp(finiteOr(p.x, 0.0), -kMaxCoordinate, kMaxCoordinate),
          std::clamp(finiteOr(p.y, 0.0), -kMaxCoordinate, kMaxCoordinate)};
}

// ---- Space sharing ----------------------------------------------------------------------

// Weighted shares of `usable`; if every child can have `minSize`, children below it are raised and
// the shortfall is taken from earlier siblings first, then later ones (spec 05 rule 19).
std::vector<double> distribute(double usable, const std::vector<double>& weights, double minSize) {
  const size_t n = weights.size();
  std::vector<double> sizes(n, 0.0);
  if (!(usable > 0.0) || n == 0) return sizes;
  double total = 0.0;
  for (double w : weights) total += w;
  if (!(total > 0.0)) return sizes;
  for (size_t i = 0; i < n; ++i) sizes[i] = usable * weights[i] / total;
  if (minSize <= 0.0 || usable < minSize * static_cast<double>(n)) return sizes;

  for (size_t i = 0; i < n; ++i) {
    double deficit = minSize - sizes[i];
    if (deficit <= 0.0) continue;
    sizes[i] = minSize;
    const auto take = [&](size_t j) {
      const double available = std::max(0.0, sizes[j] - minSize);
      const double taken = std::min(available, deficit);
      sizes[j] -= taken;
      deficit -= taken;
    };
    for (size_t j = 0; j < i && deficit > 0.0; ++j) take(j);
    for (size_t j = i + 1; j < n && deficit > 0.0; ++j) take(j);
  }
  return sizes;
}

// ---- Layout pass ------------------------------------------------------------------------

struct LayoutPass {
  const DockConfig& config;
  const std::vector<PanelInfo>& panels;
  LayoutResult& out;
  uint32_t area;
};

bool isApplicationPage(const LayoutPass& pass, PanelId id) {
  for (const PanelInfo& p : pass.panels) {
    if (p.id == id) return p.kind == PanelKind::ApplicationPage;
  }
  return false;
}

// Spec 02 rule 9 and decision D12: equal tab widths capped at 160 x 25 (the first tab decides
// whether the application-page cap of 210 x 50 applies); tabs shrink to the minimum width, beyond
// which the strip overflows and the host scrolls it.
void layoutStack(LayoutPass& pass, const Node& node, const Rect& bounds, const Path& path, bool collapsed) {
  StackLayout stack;
  stack.area = pass.area;
  stack.path = path;
  stack.bounds = bounds;
  stack.collapsed = collapsed;
  const bool appPage = isApplicationPage(pass, node.tabs.front());
  const double stripHeight = std::min(appPage ? pass.config.appPageStripHeight : pass.config.tabStripHeight, bounds.h);
  const double maxWidth = appPage ? pass.config.appPageMaxTabWidth : pass.config.maxTabWidth;
  stack.strip = {bounds.x, bounds.y, bounds.w, stripHeight};
  stack.body = {bounds.x, bounds.y + stripHeight, bounds.w, bounds.h - stripHeight};
  const double count = static_cast<double>(node.tabs.size());
  const double fitted = stack.strip.w / count;
  const double tabWidth = fitted >= pass.config.minTabWidth ? std::min(maxWidth, fitted)
                                                              : std::min(maxWidth, pass.config.minTabWidth);
  stack.tabsWidth = tabWidth * count;
  stack.overflow = stack.strip.w > 0.0 && stack.tabsWidth > stack.strip.w + 0.5;
  for (size_t i = 0; i < node.tabs.size(); ++i) {
    stack.tabs.push_back({node.tabs[i],
                          {stack.strip.x + tabWidth * static_cast<double>(i), stack.strip.y, tabWidth, stripHeight},
                          i == node.active});
  }
  pass.out.stacks.push_back(std::move(stack));
}

void layoutNode(LayoutPass& pass, const Node& node, const Rect& bounds, Path& path, bool collapsed = false) {
  if (node.kind == Node::Kind::Stack) {
    layoutStack(pass, node, bounds, path, collapsed);
    return;
  }
  pass.out.splits.push_back({pass.area, path, node.axis, bounds});
  const std::vector<double> sizes = detail::splitSizes(node, bounds, pass.config);
  const bool row = node.axis == Axis::Row;
  const double origin = row ? bounds.x : bounds.y;
  const double thickness = pass.config.handleThickness;
  double cursor = origin;
  for (size_t i = 0; i < node.children.size(); ++i) {
    const double start = roundEdge(cursor);
    const double end = roundEdge(cursor + sizes[i]);
    const Rect childRect = row ? Rect{start, bounds.y, end - start, bounds.h}
                               : Rect{bounds.x, start, bounds.w, end - start};
    path.push_back(core::checkedCast<uint32_t>(i));
    layoutNode(pass, node.children[i], childRect, path, node.children[i].collapsed);
    path.pop_back();
    if (i + 1 < node.children.size()) {
      const Rect bar = row ? Rect{end, bounds.y, thickness, bounds.h} : Rect{bounds.x, end, bounds.w, thickness};
      pass.out.handles.push_back({{pass.area, path, core::checkedCast<uint32_t>(i), node.axis}, bar});
    }
    cursor += sizes[i] + thickness;
  }
}

// ---- Drop zones -------------------------------------------------------------------------

Rect halfOf(const Rect& r, Side side) {
  switch (side) {
    case Side::Left: return {r.x, r.y, r.w / 2.0, r.h};
    case Side::Right: return {r.x + r.w / 2.0, r.y, r.w / 2.0, r.h};
    case Side::Top: return {r.x, r.y, r.w, r.h / 2.0};
    case Side::Bottom: return {r.x, r.y + r.h / 2.0, r.w, r.h / 2.0};
  }
  return r;
}

// Spec 02 rule 25: the cross overlay's inner rectangle is inset by a fraction of each dimension.
double crossInset(double length, const DockConfig& c) {
  return std::clamp(length * c.crossInsetFraction, c.crossInsetMin, c.crossInsetMax);
}

// Spec 02 rule 26: band between the outer and inner rectangle is cut by the corner diagonals; the
// side whose normalised depth is smallest owns the point. nullopt = inside the inner rectangle.
std::optional<Side> crossSide(const Rect& body, Point p, const DockConfig& c) {
  const double ix = crossInset(body.w, c);
  const double iy = crossInset(body.h, c);
  const double depth[4] = {(p.x - body.x) / ix, (body.x + body.w - p.x) / ix, (p.y - body.y) / iy,
                           (body.y + body.h - p.y) / iy};
  constexpr Side sides[4] = {Side::Left, Side::Right, Side::Top, Side::Bottom};
  size_t best = 0;
  for (size_t i = 1; i < 4; ++i) {
    if (depth[i] < depth[best]) best = i;
  }
  if (depth[best] >= 1.0) return std::nullopt;
  return sides[best];
}

const StackLayout* stackHolding(const LayoutResult& layout, PanelId panel) {
  for (const StackLayout& stack : layout.stacks) {
    for (const TabLayout& tab : stack.tabs) {
      if (tab.panel == panel) return &stack;
    }
  }
  return nullptr;
}

// Spec 02 rule 17: ghost = origin stack size scaled so its long side is at most the cap; a
// panel with no origin (closed) gets the default floating size (rule 54).
Rect floatZoneRect(const LayoutResult& layout, const DragQuery& q, const DockConfig& c) {
  double w = c.defaultFloatWidth;
  double h = c.defaultFloatHeight;
  if (const StackLayout* origin = stackHolding(layout, q.panel)) {
    w = origin->bounds.w;
    h = origin->bounds.h;
  }
  const double longSide = std::max(w, h);
  if (longSide > c.ghostMaxLongSide) {
    const double scale = std::max(c.ghostMinScale, c.ghostMaxLongSide / longSide);
    w *= scale;
    h *= scale;
  }
  const Point grab = sanitisePoint(q.grabOffset);
  return {q.pointer.x - grab.x, q.pointer.y - grab.y, std::max(w, c.minFloatSize), std::max(h, c.minFloatSize)};
}

}  // namespace

namespace detail {

// Collapsed children take no space (decision D11) but keep their handle, so dragging it brings them
// back. Pinned children keep their stored pixel size (decision D10) unless that would leave the
// flexible children below their minimum, in which case the pins are ignored for this pass.
std::vector<double> splitSizes(const Node& split, const Rect& bounds, const DockConfig& config) {
  const size_t n = split.children.size();
  const double length = split.axis == Axis::Row ? bounds.w : bounds.h;
  const double usable = length - config.handleThickness * static_cast<double>(n - 1);
  std::vector<double> sizes(n, 0.0);
  std::vector<size_t> flexible;
  double pinnedTotal = 0.0;
  size_t pinnedCount = 0;
  for (size_t i = 0; i < n; ++i) {
    const Node& child = split.children[i];
    if (child.collapsed) continue;
    if (child.pinned) {
      pinnedTotal += child.pinnedSize;
      ++pinnedCount;
    } else {
      flexible.push_back(i);
    }
  }
  const auto share = [&](const std::vector<size_t>& indices, double space) {
    std::vector<double> weights;
    weights.reserve(indices.size());
    for (size_t i : indices) weights.push_back(split.children[i].weight);
    const std::vector<double> part = distribute(space, weights, config.minPanelSize);
    for (size_t k = 0; k < indices.size(); ++k) sizes[indices[k]] = part[k];
  };
  const double free = usable - pinnedTotal;
  const bool pinsFit = pinnedCount > 0 && !flexible.empty() &&
                       free >= config.minPanelSize * static_cast<double>(flexible.size());
  if (pinsFit) {
    for (size_t i = 0; i < n; ++i) {
      if (split.children[i].pinned && !split.children[i].collapsed) sizes[i] = split.children[i].pinnedSize;
    }
    share(flexible, free);
    return sizes;
  }
  std::vector<size_t> visible;
  for (size_t i = 0; i < n; ++i) {
    if (!split.children[i].collapsed) visible.push_back(i);
  }
  share(visible, usable);
  return sizes;
}

}  // namespace detail

bool dragExceedsThreshold(Point press, Point now, const DockConfig& config) {
  const double distance = std::hypot(now.x - press.x, now.y - press.y);
  return std::isfinite(distance) && distance > config.dragThreshold;
}

std::optional<HandleLayout> hitTestHandle(const LayoutResult& layout, Point point, double extraBand) {
  const double extra = std::isfinite(extraBand) && extraBand > 0.0 ? extraBand : 0.0;
  for (size_t i = layout.areas.size(); i-- > 0;) {
    const AreaLayout& area = layout.areas[i];
    if (!area.bounds.contains(point)) continue;
    for (const HandleLayout& h : layout.handles) {
      if (h.handle.area != area.id) continue;
      Rect band = h.rect;
      if (h.handle.axis == Axis::Row) {
        band.x -= extra;
        band.w += 2.0 * extra;
      } else {
        band.y -= extra;
        band.h += 2.0 * extra;
      }
      if (band.contains(point)) return h;
    }
    return std::nullopt;  // the topmost area under the pointer owns it
  }
  return std::nullopt;
}

LayoutResult DockLayout::computeLayout(const Rect& mainRect) const {
  LayoutResult result;
  for (const Area& area : areas_) {
    const bool floating = area.id != kMainAreaId;
    const Rect bounds = sanitiseRect(floating ? area.rect : mainRect);
    result.areas.push_back({area.id, floating, !area.root.has_value(), bounds});
    if (!area.root) continue;
    LayoutPass pass{config_, panels_, result, area.id};
    Path path;
    layoutNode(pass, *area.root, bounds, path);
  }
  return result;
}

DropZone DockLayout::floatZone(const LayoutResult& layout, const DragQuery& rawQuery) const {
  DragQuery q = rawQuery;
  q.pointer = sanitisePoint(rawQuery.pointer);
  DropZone zone;
  zone.kind = DropKind::Float;
  zone.preview = floatZoneRect(layout, q, config_);
  return zone;
}

DropZone DockLayout::hitTestDropZone(const LayoutResult& layout, const DragQuery& rawQuery) const {
  DragQuery q = rawQuery;
  q.pointer = sanitisePoint(rawQuery.pointer);
  DropZone zone;
  zone.kind = DropKind::Float;
  zone.preview = floatZoneRect(layout, q, config_);

  for (size_t i = layout.areas.size(); i-- > 0;) {
    const AreaLayout& area = layout.areas[i];
    if (!area.bounds.contains(q.pointer)) continue;
    const std::optional<size_t> index = detail::areaIndex(areas_, area.id);
    if (!index) continue;
    const Area& model = areas_[*index];
    // A node never accepts a drop inside the content being dragged (spec 02 rule 32): an area
    // holding nothing but the dragged panel is skipped, so the pointer falls through to what lies below.
    const std::optional<detail::PanelLocation> origin = detail::findPanel(areas_, q.panel);
    const bool onlyDragged = model.root && origin && origin->area == *index && detail::countPanels(*model.root) == 1;
    if (onlyDragged) continue;

    if (area.empty) {
      zone.kind = DropKind::FillEmptyArea;  // spec 02 rule 30: a single centre target
      zone.area = area.id;
      zone.preview = area.bounds;
      return zone;
    }

    // Spec 02 rule 29: thin edge targets along the area's outer border win over everything else.
    const Rect& b = area.bounds;
    const double t = config_.edgeTargetThickness;
    const double distances[4] = {q.pointer.x - b.x, b.x + b.w - q.pointer.x, q.pointer.y - b.y, b.y + b.h - q.pointer.y};
    constexpr Side sides[4] = {Side::Left, Side::Right, Side::Top, Side::Bottom};
    size_t nearest = 0;
    for (size_t k = 1; k < 4; ++k) {
      if (distances[k] < distances[nearest]) nearest = k;
    }
    if (distances[nearest] < t) {
      zone.kind = DropKind::SplitAreaEdge;
      zone.area = area.id;
      zone.side = sides[nearest];
      zone.preview = halfOf(b, sides[nearest]);
      return zone;
    }

    for (const StackLayout& stack : layout.stacks) {
      if (stack.area != area.id || !stack.bounds.contains(q.pointer) || stack.tabs.empty()) continue;
      zone.area = area.id;
      zone.stackPanel = stack.tabs.front().panel;
      if (stack.strip.contains(q.pointer)) {
        // Spec 02 rules 21-22: join at the slot under the pointer, counting the other tabs only.
        const bool holdsDragged = std::any_of(stack.tabs.begin(), stack.tabs.end(),
                                              [&](const TabLayout& tab) { return tab.panel == q.panel; });
        const size_t others = stack.tabs.size() - (holdsDragged ? 1 : 0);
        const double tabWidth = stack.tabs.front().rect.w;
        double slot = tabWidth > 0.0 ? std::floor((q.pointer.x - stack.strip.x) / tabWidth) : 0.0;
        slot = std::clamp(slot, 0.0, static_cast<double>(others));
        zone.kind = DropKind::JoinStack;
        zone.index = static_cast<size_t>(slot);
        zone.preview = {stack.strip.x + tabWidth * slot, stack.strip.y, tabWidth, stack.strip.h};
        return zone;
      }
      const bool ownSoleStack = stack.tabs.size() == 1 && stack.tabs.front().panel == q.panel;
      if (ownSoleStack) break;  // spec 02 edge case 1: nothing to offer, fall through to floating
      if (const std::optional<Side> side = crossSide(stack.body, q.pointer, config_)) {
        zone.kind = DropKind::SplitStack;
        zone.side = *side;
        zone.preview = halfOf(stack.bounds, *side);
        return zone;
      }
      break;  // inner rectangle: no target (rule 26), which floats (rule 28)
    }
    break;
  }
  return floatZone(layout, q);
}

}  // namespace r1ui::dock

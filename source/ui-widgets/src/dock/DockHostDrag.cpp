// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: DockHost's tab drag (spec 02 rules 14-34, spec 03, decisions D9 and the hover rule) and
//   splitter drag: drag start, the drop zone under the pointer across all windows, the live gap in a
//   strip, the overlay (ghost, preview, cross, edge targets), activation by hovering a tab for
//   0.75 s, Escape, drop, loss of capture, pointer tracking for a drag that leaves a window, and the
//   handle resize with its Escape revert.
// Why: the model answers "where would this land" (hitTestDropZone) and "do it" (dock); everything
//   in between is interaction state, kept here in one place so the rules (the model stays untouched
//   until the drop, Escape restores everything, the source window hides while it would be empty)
//   can be read and tested as one state machine.
// Invariants: the model is not changed while a tab drag runs, except by tab activation through
//   hovering, which Escape undoes; every exit path (drop, cancel, capture loss, detach, new layout)
//   goes through endDragState, which restores window visibility, removes the overlay, cancels the
//   timer, stops pointer tracking and un-lifts the tab. Zones are always recomputed from a fresh
//   LayoutResult, never cached across model changes.
// Callers: DockTabStrip / DockAreaView (through IDockInteraction), the backend's pointer tracking.
#include <algorithm>
#include <cmath>

#include "r1ui/widgets/dock/DockHost.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace layout = core::layout;
using core::tree::WidgetId;

namespace {

bool handleEquals(const dock::SplitterHandle& a, const HandleDrag& b) {
  return a.area == b.area && a.path == b.path && a.index == b.index && a.axis == b.axis;
}

dock::SplitterHandle splitterOf(const HandleDrag& h) { return {h.area, h.path, h.index, h.axis}; }

}  // namespace

// ---- tab drag: begin -------------------------------------------------------------------------

bool DockHost::tabDragBegin(DockTabStrip& strip, dock::PanelId panel, dock::Point pointer, dock::Point grabOffset, dock::Point tabSize) {
  if (drag_.active || resize_.active) return false;
  const dock::PanelInfo* info = layout_->panel(panel);
  if (info == nullptr || info->locked || !layout_->isDocked(panel)) return false;  // rule 16
  drag_ = {};
  drag_.active = true;
  drag_.panel = panel;
  drag_.sourceStrip = strip.id();
  drag_.sourceUi = &strip.ui();
  drag_.sourceWindow = strip.window();
  drag_.sourceArea = strip.area();
  drag_.grabOffset = grabOffset;
  drag_.tabSize = tabSize;
  drag_.screen = backend_.toScreen(strip.window(), pointer);
  // Rule 35: a floating window that held only this tab disappears from view at once.
  if (strip.window() != kMainWindow && panelsInWindow(strip.window()) == 1) {
    drag_.hiddenWindow = strip.window();
    drag_.windowHidden = backend_.setVisible(strip.window(), false);
  }
  if (backend_.describe().pointerTracking) drag_.tracking = backend_.beginPointerTracking(*this);
  relayout();  // lifts the tab: strips hide it, the region shows its neighbour (rule 15)
  notify(DockChange::DragStarted);
  if (drag_.active) updateDrag(drag_.screen);
  return true;
}

size_t DockHost::panelsInWindow(FloatId window) const {
  const AreaRec* rec = areaRecForWindow(window);
  size_t count = 0;
  if (rec == nullptr) return 0;
  for (const dock::Area& a : layout_->areas()) {
    if (a.id != rec->area || !a.root) continue;
    std::vector<const dock::Node*> pending{&*a.root};
    while (!pending.empty()) {
      const dock::Node* n = pending.back();
      pending.pop_back();
      count += n->tabs.size();
      for (const dock::Node& c : n->children) pending.push_back(&c);
    }
  }
  return count;
}

void DockHost::tabDragMove(DockTabStrip& strip, dock::Point pointer) {
  if (!drag_.active) return;
  updateDrag(backend_.toScreen(strip.window(), pointer));
}

void DockHost::tabDragEnd(DockTabStrip& strip, dock::Point pointer, bool commit) {
  if (!drag_.active) return;
  finishDrag(commit, backend_.toScreen(strip.window(), pointer));
}

void DockHost::onTrackedPointer(dock::Point screen, bool leftDown) {
  if (!drag_.active) return;
  if (leftDown) {
    updateDrag(screen);
  } else {
    finishDrag(true, screen);
  }
}

// ---- tab drag: zones ---------------------------------------------------------------------------

bool DockHost::zoneAllowed(const dock::DropZone& zone) const {
  const dock::PanelInfo* info = layout_->panel(drag_.panel);
  if (info == nullptr) return false;
  if (!options_.allowDocking) {  // every tab only reorders inside its own strip
    const std::optional<dock::PanelSlot> own = layout_->locate(drag_.panel);
    const std::optional<dock::PanelSlot> target = layout_->locate(zone.stackPanel);
    return zone.kind == dock::DropKind::JoinStack && own && target && own->area == target->area && own->path == target->path;
  }
  if (info->kind == dock::PanelKind::ApplicationPage) {  // rule 33: strips only, never splits
    return zone.kind == dock::DropKind::JoinStack || zone.kind == dock::DropKind::FillEmptyArea || zone.kind == dock::DropKind::Float;
  }
  return true;
}

std::optional<dock::DropZone> DockHost::zoneAt(dock::Point screen, std::optional<FloatId>& windowUnder) {
  std::vector<FloatId> exclude;
  if (drag_.windowHidden) exclude.push_back(drag_.hiddenWindow);
  windowUnder = backend_.topmostWindowAt(screen, exclude);
  result_ = layout_->computeLayout(mainRect());
  dock::DragQuery query;
  query.pointer = screen;
  query.panel = drag_.panel;
  query.grabOffset = drag_.grabOffset;
  dock::DropZone zone = layout_->hitTestDropZone(result_, query);
  if (zone.kind != dock::DropKind::Float) {
    // The model knows rectangles, not which window is on top: a point on a window's frame (outside
    // its content) or outside every window is a drop on nothing.
    const std::optional<FloatId> owner = windowOfArea(zone.area);
    if (!windowUnder || !owner || *owner != *windowUnder) zone = layout_->floatZone(result_, query);
  }
  if (zone.kind == dock::DropKind::JoinStack) refineJoinSlot(zone, screen);
  if (!zoneAllowed(zone)) zone = layout_->floatZone(result_, query);  // edge case 1: falls through to floating
  const dock::PanelInfo* info = layout_->panel(drag_.panel);
  if (zone.kind == dock::DropKind::Float && (info == nullptr || !info->canFloat || !options_.allowDocking)) return std::nullopt;
  return zone;
}

// The model computes the slot from equal tab widths; the strip knows its scroll offset and the real
// width of its tabs, so it decides the slot and the preview of a drop on a strip.
void DockHost::refineJoinSlot(dock::DropZone& zone, dock::Point screen) const {
  DockAreaView* view = areaView(zone.area);
  const std::optional<FloatId> window = windowOfArea(zone.area);
  if (view == nullptr || !window) return;
  const DockAreaView::Unit* unit = view->unitHolding(zone.stackPanel);
  DockTabStrip* strip = unit != nullptr ? view->stripOf(*unit) : nullptr;
  if (strip == nullptr) return;
  const dock::Point local = backend_.toWindow(*window, screen);
  const double centre = local.x - drag_.grabOffset.x + strip->naturalTabWidth() / 2.0;
  const size_t slot = strip->slotAt(centre);
  const dock::Rect r = strip->slotRect(slot);
  zone.index = slot;
  const dock::Point origin = backend_.toScreen(*window, {r.x, r.y});
  zone.preview = {origin.x, origin.y, r.w, r.h};
}

void DockHost::updateDrag(dock::Point screen) {
  if (!drag_.active) return;
  drag_.screen = screen;
  std::optional<FloatId> under;
  drag_.zone = zoneAt(screen, under);
  refreshStripVisuals();
  showVisual(drag_.zone ? &*drag_.zone : nullptr, screen, under);
  updateHoverActivation(screen, under);
}

void DockHost::refreshStripVisuals() {
  const dock::DropZone* zone = drag_.zone ? &*drag_.zone : nullptr;
  for (const AreaRec& rec : areas_) {
    DockAreaView* view = rec.ui != nullptr ? rec.ui->objectAs<DockAreaView>(rec.view) : nullptr;
    if (view == nullptr) continue;
    for (const DockAreaView::Unit& u : view->units()) {
      DockTabStrip* strip = view->stripOf(u);
      if (strip == nullptr) continue;
      const bool target = zone != nullptr && zone->kind == dock::DropKind::JoinStack && zone->area == rec.area && strip->indexOf(zone->stackPanel);
      strip->setGap(target ? std::optional<size_t>(zone->index) : std::nullopt);
    }
  }
}

// ---- tab drag: overlay ---------------------------------------------------------------------------

DockDragOverlay* DockHost::overlayFor(UiContext& context) {
  if (drag_.overlayUi == &context && context.alive(drag_.overlay)) return context.objectAs<DockDragOverlay>(drag_.overlay);
  if (drag_.overlayUi != nullptr && drag_.overlayUi->alive(drag_.overlay)) drag_.overlayUi->destroy(drag_.overlay);
  drag_.overlayUi = &context;
  drag_.overlay = context.create<DockDragOverlay>(context.overlays().layer()).id();
  return context.objectAs<DockDragOverlay>(drag_.overlay);
}

void DockHost::showVisual(const dock::DropZone* zone, dock::Point screen, std::optional<FloatId> windowUnder) {
  const FloatId window = windowUnder.value_or(kMainWindow);
  const std::optional<FloatContent> content = backend_.content(window);
  if (!content || content->ui == nullptr) return;
  DockDragOverlay* overlay = overlayFor(*content->ui);
  if (overlay == nullptr) return;
  const auto local = [&](const dock::Rect& r) {
    const dock::Point p = backend_.toWindow(window, {r.x, r.y});
    return dock::Rect{p.x, p.y, r.w, r.h};
  };
  DragVisual v;
  const std::string title = [&] {
    const PanelDescriptor* d = registry_.find(drag_.panel);
    return d != nullptr ? d->title : std::string();
  }();
  if (zone == nullptr) {  // nothing accepts it and it cannot float: show the ghost only
    dock::Rect g{screen.x - drag_.grabOffset.x, screen.y - drag_.grabOffset.y, drag_.tabSize.x, drag_.tabSize.y};
    v.ghost = local(g);
    v.ghostTitle = title;
    overlay->setVisual(std::move(v));
    return;
  }
  // Edge targets of the area under the pointer (rule 30) and the centre target of an empty area.
  const dock::DockConfig& cfg = options_.config;
  const AreaRec* underRec = nullptr;
  for (const AreaRec& rec : areas_) {
    if (rec.window == window) underRec = &rec;
  }
  if (underRec != nullptr) {
    for (const dock::AreaLayout& a : result_.areas) {
      if (a.id != underRec->area || !a.bounds.contains(screen)) continue;
      if (a.empty) {
        if (zone->kind == dock::DropKind::FillEmptyArea) v.emptyTarget = local(a.bounds);
        continue;
      }
      const double t = cfg.edgeTargetThickness;
      const dock::Rect& b = a.bounds;
      v.edges = {local({b.x, b.y, t, b.h}), local({b.x + b.w - t, b.y, t, b.h}), local({b.x, b.y, b.w, t}), local({b.x, b.y + b.h - t, b.w, t})};
      if (zone->kind == dock::DropKind::SplitAreaEdge && zone->area == a.id) {
        v.hotEdge = zone->side == dock::Side::Left ? 0 : zone->side == dock::Side::Right ? 1 : zone->side == dock::Side::Top ? 2 : 3;
      }
    }
    // The cross over the region body under the pointer (rule 26).
    for (const dock::StackLayout& s : result_.stacks) {
      if (s.area != underRec->area || !s.body.contains(screen) || s.tabs.empty()) continue;
      const bool ownSole = s.tabs.size() == 1 && s.tabs.front().panel == drag_.panel;
      if (ownSole) continue;
      const double ix = std::clamp(s.body.w * cfg.crossInsetFraction, cfg.crossInsetMin, cfg.crossInsetMax);
      const double iy = std::clamp(s.body.h * cfg.crossInsetFraction, cfg.crossInsetMin, cfg.crossInsetMax);
      v.crossOuter = local(s.body);
      v.crossInner = {v.crossOuter->x + ix, v.crossOuter->y + iy, std::max(0.0, s.body.w - 2.0 * ix), std::max(0.0, s.body.h - 2.0 * iy)};
    }
  }
  switch (zone->kind) {
    case dock::DropKind::Float:
      v.ghost = local(zone->preview);
      v.ghostTitle = title;
      break;
    case dock::DropKind::FillEmptyArea:
      break;  // shown as the empty target
    default:
      v.preview = local(zone->preview);
      break;
  }
  overlay->setVisual(std::move(v));
}

void DockHost::clearVisuals() {
  if (drag_.overlayUi != nullptr && drag_.overlayUi->alive(drag_.overlay)) drag_.overlayUi->destroy(drag_.overlay);
  drag_.overlay = {};
  drag_.overlayUi = nullptr;
  for (const AreaRec& rec : areas_) {
    DockAreaView* view = rec.ui != nullptr ? rec.ui->objectAs<DockAreaView>(rec.view) : nullptr;
    if (view == nullptr) continue;
    for (const DockAreaView::Unit& u : view->units()) {
      if (DockTabStrip* strip = view->stripOf(u)) strip->setGap(std::nullopt);
    }
  }
}

// ---- tab drag: hover activation (rule 7) ---------------------------------------------------------------

void DockHost::cancelHoverTimer() {
  if (drag_.hoverTimer != 0 && drag_.hoverUi != nullptr) drag_.hoverUi->cancelTimer(drag_.hoverTimer);
  drag_.hoverTimer = 0;
  drag_.hoverPanel = 0;
  drag_.hoverUi = nullptr;
}

void DockHost::updateHoverActivation(dock::Point screen, std::optional<FloatId> windowUnder) {
  dock::PanelId hovered = 0;
  UiContext* hoveredUi = nullptr;
  if (windowUnder) {
    for (const AreaRec& rec : areas_) {
      if (rec.window != *windowUnder || rec.ui == nullptr) continue;
      DockAreaView* view = rec.ui->objectAs<DockAreaView>(rec.view);
      if (view == nullptr) continue;
      const dock::Point local = backend_.toWindow(rec.window, screen);
      if (const DockAreaView::Unit* unit = view->unitStripAt(local.x, local.y)) {
        if (DockTabStrip* strip = view->stripOf(*unit)) {
          if (const std::optional<dock::PanelId> tab = strip->tabAt(local.x, local.y)) {
            hovered = *tab;
            hoveredUi = rec.ui;
          }
        }
      }
    }
  }
  if (hovered == drag_.hoverPanel) return;
  cancelHoverTimer();
  const std::optional<dock::PanelSlot> slot = hovered != 0 ? layout_->locate(hovered) : std::nullopt;
  if (!slot || slot->front || hoveredUi == nullptr) return;  // already the front tab: nothing to wait for
  drag_.hoverPanel = hovered;
  drag_.hoverUi = hoveredUi;
  const std::shared_ptr<bool> alive = alive_;
  const uint64_t delay = static_cast<uint64_t>(std::max(0.0, options_.config.hoverActivateSeconds) * 1000.0);
  drag_.hoverTimer = hoveredUi->setTimer(delay, [this, alive, hovered] {
    if (!*alive || !drag_.active || drag_.hoverPanel != hovered) return;
    drag_.hoverTimer = 0;
    const dock::PanelId previousFront = frontOfStackHolding(hovered);
    if (!layout_->activateTab(hovered)) return;
    drag_.activated.push_back(previousFront);
    afterModelChanged(DockChange::Active, false, false);
    drag_.hoverPanel = 0;
    updateDrag(drag_.screen);
  });
}

// ---- tab drag: end ---------------------------------------------------------------------------------

void DockHost::endDragState() {
  const FloatId hidden = drag_.hiddenWindow;
  const bool wasHidden = drag_.windowHidden;
  const bool tracking = drag_.tracking;
  UiContext* sourceUi = drag_.sourceUi;
  const WidgetId sourceStrip = drag_.sourceStrip;
  cancelHoverTimer();
  clearVisuals();
  drag_.active = false;  // un-lifts the tab on the relayout below
  if (tracking) backend_.endPointerTracking();
  if (wasHidden) backend_.setVisible(hidden, true);
  if (sourceUi != nullptr && sourceUi->alive(sourceStrip)) {
    if (DockTabStrip* strip = sourceUi->objectAs<DockTabStrip>(sourceStrip)) strip->clearDrag();
  }
  drag_ = {};
}

void DockHost::finishDrag(bool dropNow, dock::Point screen) {
  if (!drag_.active) return;
  const dock::PanelId panel = drag_.panel;
  std::optional<dock::DropZone> zone;
  if (dropNow) {
    std::optional<FloatId> under;
    drag_.screen = screen;
    zone = zoneAt(screen, under);
  } else {
    // Losing the pointer ends the drag like a drop on nothing (spec 03 rule 29).
    dock::DragQuery query;
    query.pointer = drag_.screen;
    query.panel = panel;
    query.grabOffset = drag_.grabOffset;
    const dock::PanelInfo* info = layout_->panel(panel);
    if (info != nullptr && info->canFloat && options_.allowDocking) zone = layout_->floatZone(layout_->computeLayout(mainRect()), query);
  }
  endDragState();
  notify(DockChange::DragEnded);
  if (!zone) {
    afterModelChanged(DockChange::Active, false, false);
    return;
  }
  dock::DropZone z = *zone;
  if (z.kind == dock::DropKind::Float) z.preview = contentRectForFloat(z.preview);
  const dock::Status status = layout_->dock(panel, z);
  if (!status) {
    lastError_ = status.error;
    afterModelChanged(DockChange::Arrangement, false, false);
    return;
  }
  setActivePanel(panel);
  afterModelChanged(DockChange::Arrangement);
}

bool DockHost::cancelDragRequested() {
  if (!drag_.active) return false;
  // Decision D9: everything returns to where it was. Fronts changed by hovering are put back in
  // reverse order; the tab itself never left the model.
  const std::vector<dock::PanelId> activations = drag_.activated;
  endDragState();
  for (auto it = activations.rbegin(); it != activations.rend(); ++it) {
    if (layout_->isDocked(*it)) layout_->activateTab(*it);
  }
  notify(DockChange::DragEnded);
  afterModelChanged(DockChange::Active, false, !activations.empty());
  return true;
}

// ---- splitter drag ---------------------------------------------------------------------------------

bool DockHost::handleDragBegin(const HandleDrag& handle, dock::Point pointer) {
  if (drag_.active || resize_.active) return false;
  for (const dock::HandleLayout& h : result_.handles) {
    if (!handleEquals(h.handle, handle)) continue;
    resize_.active = true;
    resize_.handle = handle;
    resize_.grab = handle.axis == dock::Axis::Row ? pointer.x - h.rect.x : pointer.y - h.rect.y;
    resize_.snapshot = *layout_;
    return true;
  }
  return false;
}

void DockHost::handleDragMove(dock::Point pointer) {
  if (!resize_.active) return;
  for (const dock::HandleLayout& h : result_.handles) {
    if (!handleEquals(h.handle, resize_.handle)) continue;
    const bool row = resize_.handle.axis == dock::Axis::Row;
    const double desired = (row ? pointer.x : pointer.y) - resize_.grab;
    const double delta = desired - (row ? h.rect.x : h.rect.y);
    if (std::abs(delta) < 0.5) return;
    if (layout_->moveSplitter(splitterOf(resize_.handle), delta, mainRect())) afterModelChanged(DockChange::Arrangement, false, false);
    return;
  }
}

void DockHost::handleDragEnd(bool commit) {
  if (!resize_.active) return;
  std::optional<dock::DockLayout> snapshot = std::move(resize_.snapshot);
  resize_ = {};
  if (!commit && snapshot) {
    layout_ = std::move(*snapshot);  // Escape: the sizes before the drag
    afterModelChanged(DockChange::Arrangement, false, false);
    return;
  }
  notify(DockChange::Arrangement);
}

void DockHost::handleNudge(const HandleDrag& handle, double delta) {
  if (drag_.active || resize_.active) return;
  if (!commitOp(layout_->moveSplitter(splitterOf(handle), delta, mainRect()))) return;
  afterModelChanged(DockChange::Arrangement);
}

}  // namespace r1ui::widgets

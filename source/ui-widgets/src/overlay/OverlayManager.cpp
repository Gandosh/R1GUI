// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of OverlayManager.h: opening and closing overlays, placement after layout,
//   outside-press and Escape dismissal, modal blockers, the Tab focus trap and focus restore.
// Invariants: entries_ is in stacking order (last = topmost); an entry's host and blocker are live
//   widgets or already removed together with the entry; close() removes the entry before it
//   destroys widgets or calls the user callback so re-entrant calls see a consistent stack.
// Callers: UiContext, popup widgets, TooltipManager, tests.
#include "r1ui/widgets/overlay/OverlayManager.h"

#include <algorithm>
#include <cmath>

#include "r1ui/core/events/TreeQueries.h"
#include "r1ui/widgets/overlay/OverlayHost.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

namespace events = core::events;
using core::tree::WidgetId;

constexpr int kMaxPlaceAttempts = 4;

}  // namespace

OverlayManager::OverlayManager(UiContext& ui) : ui_(ui) {}

OverlayManager::~OverlayManager() = default;

void OverlayManager::createLayer() { layer_ = ui_.create<OverlayLayer>(ui_.root()).id(); }

OverlayManager::Entry* OverlayManager::find(OverlayId id) {
  for (Entry& e : entries_) {
    if (e.id == id.value) return &e;
  }
  return nullptr;
}

const OverlayManager::Entry* OverlayManager::find(OverlayId id) const {
  for (const Entry& e : entries_) {
    if (e.id == id.value) return &e;
  }
  return nullptr;
}

size_t OverlayManager::indexOf(uint32_t id) const {
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (entries_[i].id == id) return i;
  }
  return entries_.size();
}

// ---- open / close -------------------------------------------------------------------------

OverlayHandle OverlayManager::open(const OverlayOptions& options) {
  if (!ui_.alive(layer_)) return {};
  Entry entry;
  entry.id = nextId_++;
  entry.options = options;
  entry.savedFocus = ui_.router().saveFocus();
  entry.savedFocusVisible = ui_.router().focusVisible();
  entry.focusPending = options.focusOnOpen;
  const bool tooltip = options.surface == OverlaySurface::Tooltip;
  if (options.modal) entry.blocker = ui_.create<OverlayBlocker>(layer_, options.scrim).id();
  OverlayHost& host = ui_.create<OverlayHost>(layer_, options.surface, options.fadeInMs, options.interactive, options.shadow);
  entry.host = host.id();

  core::layout::Style& s = host.style();
  if (options.matchAnchorWidth && options.anchor.w > 0) s.minWidth = core::layout::Length::px(options.anchor.w);
  if (options.maxHeightFraction > 0.0) {
    const double limit = ui_.viewportHeight() * std::min(options.maxHeightFraction, 1.0) - 2.0 * options.windowMargin;
    if (limit > 0) s.maxHeight = core::layout::Length::px(limit);
  }
  host.requestLayout();
  entries_.push_back(entry);
  if (!tooltip) ui_.tooltips().onModalOpened();  // any popup closes a visible tooltip (spec rule 8)
  return {OverlayId{entry.id}, entry.host};
}

void OverlayManager::restoreFocusFor(const Entry& entry, bool focusWasInside) {
  if (!entry.options.restoreFocus) return;
  const WidgetId focused = ui_.router().focused();
  if (focused.valid() && !focusWasInside) return;  // a command moved focus elsewhere: it wins
  // The focus indication comes back only if it was showing when the overlay opened.
  if (entry.savedFocus.valid() && ui_.alive(entry.savedFocus)) {
    ui_.router().focus(entry.savedFocus, entry.savedFocusVisible ? core::events::FocusReason::Keyboard : core::events::FocusReason::Program);
  }
}

bool OverlayManager::close(OverlayId id, DismissReason reason) {
  const size_t index = indexOf(id.value);
  if (index >= entries_.size()) return false;
  Entry entry = std::move(entries_[index]);
  entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
  const WidgetId focused = ui_.router().focused();
  const bool focusInside = focused.valid() && (focused == entry.host || ui_.tree().isAncestor(entry.host, focused));
  ui_.destroy(entry.host);
  if (entry.blocker.valid()) ui_.destroy(entry.blocker);
  restoreFocusFor(entry, focusInside);
  if (entry.options.onClosed) entry.options.onClosed(reason);
  return true;
}

void OverlayManager::closeAll(DismissReason reason) {
  while (!entries_.empty()) close(OverlayId{entries_.back().id}, reason);
}

void OverlayManager::setPosition(OverlayId id, double x, double y) {
  Entry* e = find(id);
  if (e == nullptr) return;
  e->options.anchor = {static_cast<int32_t>(std::lround(x)), static_cast<int32_t>(std::lround(y)), 0, 0};
  e->options.placement = Placement::Manual;
  e->placed = false;
  e->placeAttempts = 0;
}

void OverlayManager::setAnchor(OverlayId id, const core::layout::Rect& anchor) {
  Entry* e = find(id);
  if (e == nullptr) return;
  e->options.anchor = anchor;
  e->placed = false;
  e->placeAttempts = 0;
}

// ---- queries ------------------------------------------------------------------------------

bool OverlayManager::isOpen(OverlayId id) const { return find(id) != nullptr; }

bool OverlayManager::anyModal() const {
  return std::any_of(entries_.begin(), entries_.end(), [](const Entry& e) { return e.options.modal; });
}

std::vector<OverlayId> OverlayManager::stack() const {
  std::vector<OverlayId> out;
  out.reserve(entries_.size());
  for (const Entry& e : entries_) out.push_back(OverlayId{e.id});
  return out;
}

WidgetId OverlayManager::hostOf(OverlayId id) const {
  const Entry* e = find(id);
  return e != nullptr ? e->host : WidgetId{};
}

int OverlayManager::indexContaining(WidgetId widget) const {
  if (!widget.valid()) return -1;
  for (size_t i = entries_.size(); i-- > 0;) {
    const WidgetId host = entries_[i].host;
    if (host == widget || ui_.tree().isAncestor(host, widget)) return static_cast<int>(i);
  }
  return -1;
}

bool OverlayManager::contains(OverlayId id, WidgetId widget) const {
  const Entry* e = find(id);
  return e != nullptr && widget.valid() && (e->host == widget || ui_.tree().isAncestor(e->host, widget));
}

// ---- dismissal ----------------------------------------------------------------------------

OverlayManager::PressOutcome OverlayManager::pressOutside(WidgetId hit) {
  PressOutcome outcome;
  if (entries_.empty()) return outcome;
  const int inside = indexContaining(hit);
  // Overlays above the one that was hit (all of them when the press landed outside the stack) see
  // an outside press, top first. Ids are collected first because close() edits the stack.
  std::vector<uint32_t> candidates;
  for (size_t i = entries_.size(); i-- > static_cast<size_t>(inside + 1);) candidates.push_back(entries_[i].id);
  for (const uint32_t id : candidates) {
    const size_t index = indexOf(id);
    if (index >= entries_.size()) continue;
    const Entry& e = entries_[index];
    if (!e.options.interactive) continue;  // a tooltip above a menu must not shield it from the press
    if (!e.options.dismissOnOutsidePress) break;
    const WidgetId anchor = e.options.anchorWidget;
    const bool inAnchor = anchor.valid() && hit.valid() && (hit == anchor || ui_.tree().isAncestor(anchor, hit));
    if (inAnchor || !e.options.outsidePressPassesThrough || e.options.modal) outcome.deliver = false;
    close(OverlayId{id}, DismissReason::OutsidePress);
    outcome.dismissed = true;
  }
  return outcome;
}

bool OverlayManager::escape(bool afterWidgets) {
  // Tooltips (non-interactive overlays) never take part in dismissal: look past them, otherwise a
  // visible tooltip would make Escape ignore the menu or dialog under it.
  const auto topmostInteractive = [this]() {
    for (size_t i = entries_.size(); i-- > 0;) {
      if (entries_[i].options.interactive) return i;
    }
    return entries_.size();
  };
  const size_t topIndex = topmostInteractive();
  if (topIndex >= entries_.size()) return false;
  const Entry& top = entries_[topIndex];
  if (!top.options.dismissOnEscape) return false;
  if (!afterWidgets && !top.options.escapeFirst) return false;
  if (!top.options.closeAllOnEscape) {
    close(OverlayId{top.id}, DismissReason::Escape);
    return true;
  }
  // The whole contiguous menu stack closes at once (spec 01 rule 43).
  for (size_t index = topmostInteractive(); index < entries_.size() && entries_[index].options.closeAllOnEscape; index = topmostInteractive()) {
    close(OverlayId{entries_[index].id}, DismissReason::Escape);
  }
  return true;
}

void OverlayManager::windowDeactivated() {
  for (size_t i = entries_.size(); i-- > 0;) {
    if (i < entries_.size() && entries_[i].options.dismissOnWindowDeactivate) close(OverlayId{entries_[i].id}, DismissReason::WindowDeactivated);
  }
}

// ---- focus trap ---------------------------------------------------------------------------

bool OverlayManager::trapTab(bool backwards) {
  for (size_t i = entries_.size(); i-- > 0;) {
    const Entry& e = entries_[i];
    if (!trapping(e)) continue;
    const WidgetId next = events::nextFocusable(ui_.tree(), e.host, ui_.router().focused(), backwards);
    if (next.valid()) ui_.router().focus(next, events::FocusReason::Keyboard);
    return true;  // consumed even when nothing is focusable: Tab never leaves a trapping overlay
  }
  return false;
}

void OverlayManager::enforceFocusTrap() {
  for (size_t i = entries_.size(); i-- > 0;) {
    const Entry& e = entries_[i];
    if (!trapping(e)) continue;
    if (!e.placed) return;
    const WidgetId focused = ui_.router().focused();
    if (focused.valid() && (focused == e.host || ui_.tree().isAncestor(e.host, focused))) return;
    const WidgetId first = events::nextFocusable(ui_.tree(), e.host, WidgetId{}, false);
    if (first.valid()) ui_.router().focus(first, events::FocusReason::Program);
    return;
  }
}

// ---- placement ----------------------------------------------------------------------------

bool OverlayManager::needsPlacement() const {
  return std::any_of(entries_.begin(), entries_.end(), [](const Entry& e) { return !e.placed; });
}

void OverlayManager::place(Entry& entry) {
  const core::layout::Rect size = ui_.absRect(entry.host);
  if (size.empty() && entry.placeAttempts < kMaxPlaceAttempts) {
    ++entry.placeAttempts;  // not laid out yet; the caller lays out again
    return;
  }
  const int margin = static_cast<int>(std::lround(entry.options.windowMargin));
  const core::layout::Rect bounds{margin, margin, static_cast<int32_t>(std::lround(ui_.viewportWidth())) - 2 * margin,
                                  static_cast<int32_t>(std::lround(ui_.viewportHeight())) - 2 * margin};
  PlacementInput in;
  in.width = size.w;
  in.height = size.h;
  in.anchor = entry.options.anchor;
  in.bounds = bounds;
  in.placement = entry.options.placement;
  in.gap = entry.options.gap;
  in.flip = entry.options.flip;
  const PlacementResult result = placePopup(in);
  WidgetObject* host = ui_.object(entry.host);
  if (host == nullptr) return;
  host->style().inset[core::layout::kLeft] = core::layout::Length::px(std::round(result.x));
  host->style().inset[core::layout::kTop] = core::layout::Length::px(std::round(result.y));
  host->requestLayout();
  if (!ui_.tree().get(entry.host)->flags.visible) {
    ui_.invalidator().setVisible(entry.host, true);
    if (auto* h = dynamic_cast<OverlayHost*>(host)) h->markShown(ui_.now());
  }
  entry.placed = true;
}

bool OverlayManager::afterLayout() {
  bool placedAny = false;
  for (size_t i = 0; i < entries_.size(); ++i) {
    Entry& e = entries_[i];
    if (e.placed) continue;
    place(e);
    placedAny = placedAny || e.placed;
    if (e.placed && e.focusPending) {
      e.focusPending = false;
      const WidgetId first = events::nextFocusable(ui_.tree(), e.host, WidgetId{}, false);
      if (first.valid()) ui_.router().focus(first, events::FocusReason::Keyboard);
    }
  }
  return placedAny;
}

}  // namespace r1ui::widgets

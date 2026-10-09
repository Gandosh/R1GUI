// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of TabBar.h: width distribution (natural, shrink, overflow), painting, hit
//   testing, pointer / keyboard handling, drag reordering and the tabbar.* style rows.
// Invariants: layout() is a pure function of (titles, closable / icon flags, bar width, display
//   scale, scroll offset, show-new flag) and is recomputed whenever one of them changes
//   (invalidateLayout); tab edges are rounded to whole logical pixels so neighbours never overlap or
//   leave gaps; the hover and drag indices always refer to existing tabs (reset on every structural
//   change); a user callback may destroy the bar, so state is never touched after a callback.
// Callers: UiContext (events, paint), tests, application shells.
#include "r1ui/widgets/tabbar/TabBar.h"

#include <algorithm>
#include <cmath>

#include "TabBarMetrics.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/toolbar/FlyoutList.h"

namespace r1ui::widgets {

namespace layout = core::layout;
namespace State = theme::State;
using core::events::Button;
using core::events::Key;
using theme::StyleProperty;

namespace {

constexpr theme::StyleRuleEntry kRows[] = {
    {"tabbar.bar", State::kNone, StyleProperty::Background, "color:canvas"},
    {"tabbar.bar", State::kNone, StyleProperty::BorderColor, "color:border"},
    {"tabbar.tab", State::kNone, StyleProperty::Background, "transparent"},
    {"tabbar.tab", State::kNone, StyleProperty::Foreground, "color:muted"},
    {"tabbar.tab", State::kNone, StyleProperty::BorderColor, "color:border"},
    {"tabbar.tab", State::kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"tabbar.tab", State::kNone, StyleProperty::LineHeight, "number:16"},
    {"tabbar.tab", State::kHover, StyleProperty::Foreground, "color:surface"},
    {"tabbar.tab", State::kSelected, StyleProperty::Background, "color:panel"},
    {"tabbar.tab", State::kSelected, StyleProperty::Foreground, "color:surface"},
    {"tabbar.close", State::kNone, StyleProperty::Background, "transparent"},
    {"tabbar.close", State::kNone, StyleProperty::Radius, "radius:sm"},
    {"tabbar.close", State::kHover, StyleProperty::Background, "color:hover"},
    {"tabbar.button", State::kNone, StyleProperty::Foreground, "color:muted"},
    {"tabbar.button", State::kHover, StyleProperty::Foreground, "color:surface"},
    {"tabbar.button", State::kDisabled, StyleProperty::Foreground, "color:muted@0.4"},
};


}  // namespace

std::span<const theme::StyleRuleEntry> TabBar::styleRows() { return kRows; }

void TabBar::onAttached() {
  style().height = layout::Length::px(kHeight);
  style().flexShrink = 0.0;
  setFocusable(true);
}

layout::Rect TabBar::barRect() const { return ui().absRect(id()); }

std::string_view TabBar::accessibleName() const { return WidgetObject::accessibleName().empty() ? std::string_view("Document tabs") : WidgetObject::accessibleName(); }

// ---- tab model ----------------------------------------------------------------------------------

std::optional<size_t> TabBar::indexOf(TabId id) const {
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i].id == id) return i;
  }
  return std::nullopt;
}

void TabBar::invalidateLayout() {
  layoutValid_ = false;
  requestPaint();
}

bool TabBar::addTab(TabId id, std::string title, TabOptions options) { return insertTab(tabs_.size(), id, std::move(title), std::move(options)); }

bool TabBar::insertTab(size_t index, TabId id, std::string title, TabOptions options) {
  if (index > tabs_.size() || tabs_.size() >= kMaxTabs || indexOf(id)) return false;
  TabInfo info;
  info.id = id;
  info.title = std::move(title);
  info.icon = std::move(options.icon);
  info.closable = options.closable;
  info.tooltip = std::move(options.tooltip);
  tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(index), std::move(info));
  hover_ = {};
  if (drag_.active) endDrag(false);
  if (!active_) active_ = id;
  invalidateLayout();
  return true;
}

bool TabBar::removeTab(TabId id) {
  const std::optional<size_t> at = indexOf(id);
  if (!at) return false;
  if (drag_.active || drag_.armed) endDrag(false);
  const bool wasActive = active_ && *active_ == id;
  tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(*at));
  hover_ = {};
  middlePress_.reset();
  invalidateLayout();
  if (tabs_.empty()) {
    active_.reset();
    scroll_ = 0.0;
    return true;
  }
  if (wasActive) {
    const size_t next = std::min(*at, tabs_.size() - 1);  // the right neighbour, else the new last tab
    active_ = tabs_[next].id;
    scrollToTab(*active_);
    if (onActivate_) {
      auto callback = onActivate_;
      callback(*active_);
    }
  }
  return true;
}

bool TabBar::moveTab(TabId id, size_t newIndex) {
  const std::optional<size_t> at = indexOf(id);
  if (!at || newIndex >= tabs_.size()) return false;
  if (*at == newIndex) return false;
  TabInfo info = std::move(tabs_[*at]);
  tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(*at));
  tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(newIndex), std::move(info));
  hover_ = {};
  invalidateLayout();
  return true;
}

bool TabBar::setTabTitle(TabId id, std::string title) {
  const std::optional<size_t> at = indexOf(id);
  if (!at) return false;
  if (tabs_[*at].title == title) return true;
  tabs_[*at].title = std::move(title);
  invalidateLayout();
  return true;
}

bool TabBar::setTabIcon(TabId id, std::string icon) {
  const std::optional<size_t> at = indexOf(id);
  if (!at) return false;
  if (tabs_[*at].icon == icon) return true;
  tabs_[*at].icon = std::move(icon);
  invalidateLayout();
  return true;
}

bool TabBar::setActiveTab(TabId id) {
  if (!indexOf(id)) return false;
  active_ = id;
  scrollToTab(id);
  requestPaint();
  return true;
}

void TabBar::setShowNewButton(bool show) {
  if (show == showNew_) return;
  showNew_ = show;
  invalidateLayout();
}

// ---- pointer ------------------------------------------------------------------------------------

void TabBar::setHover(Hit hit) {
  if (hit.part == hover_.part && hit.index == hover_.index) return;
  hover_ = hit;
  requestPaint();
}

void TabBar::activateByUser(size_t index) {
  if (index >= tabs_.size()) return;
  const TabId tabId = tabs_[index].id;
  if (active_ && *active_ == tabId) return;
  active_ = tabId;
  scrollToTab(tabId);
  requestPaint();
  if (onActivate_) {
    auto callback = onActivate_;
    callback(tabId);
  }
}

void TabBar::requestClose(size_t index) {
  if (index >= tabs_.size() || !tabs_[index].closable) return;
  const TabId tabId = tabs_[index].id;
  if (onClose_) {
    auto callback = onClose_;
    callback(tabId);
  } else {
    removeTab(tabId);
  }
}

void TabBar::onPointerDown(Event& e) {
  const Hit hit = hitTest(e.x, e.y);
  setHover(hit);
  pressHit_ = hit;
  if (e.button == Button::Left) {
    if (hit.part == Part::Tab) {
      activateByUser(hit.index);
      if (!ui().alive(id())) return;
      const Layout& L = layout();
      drag_ = {};
      drag_.armed = true;
      drag_.index = hit.index < tabs_.size() ? hit.index : 0;
      drag_.slot = drag_.index;
      drag_.grab = e.x - ui().absRect(id()).x - L.x[drag_.index];
      drag_.pointerX = e.x;
      ui().router().capturePointer(id());
    }
    if (hit.part != Part::None) {
      ui().router().focus(id(), core::events::FocusReason::Pointer);
      e.markHandled();
    }
  } else if (e.button == Button::Middle) {
    if (hit.part == Part::Tab || hit.part == Part::Close) {
      middlePress_ = hit.index;
      e.markHandled();
    }
  } else if (e.button == Button::Right && (hit.part == Part::Tab || hit.part == Part::Close)) {
    activateByUser(hit.index);
    if (!ui().alive(id())) return;
    e.markHandled();
    if (onContext_ && hit.index < tabs_.size()) {
      auto callback = onContext_;
      callback(tabs_[hit.index].id, e.x, e.y);
    }
  }
}

void TabBar::onPointerUp(Event& e) {
  if (e.button == Button::Left) {
    if (drag_.active) endDrag(true);
    drag_.armed = false;
  } else if (e.button == Button::Middle && middlePress_) {
    const Hit hit = hitTest(e.x, e.y);
    const size_t pressed = *middlePress_;
    middlePress_.reset();
    if ((hit.part == Part::Tab || hit.part == Part::Close) && hit.index == pressed) {
      e.markHandled();
      requestClose(pressed);
    }
  }
}

void TabBar::onClick(Event& e) {
  if (e.button != Button::Left) return;
  const Hit hit = hitTest(e.x, e.y);
  // The release must be on the same control the press began on.
  if (hit.part != pressHit_.part || (hit.part == Part::Close && hit.index != pressHit_.index)) return;
  switch (hit.part) {
    case Part::Close:
      e.markHandled();
      requestClose(hit.index);
      break;
    case Part::New:
      e.markHandled();
      if (onNew_) {
        auto callback = onNew_;
        callback();
      }
      break;
    case Part::Left:
      e.markHandled();
      scroll_ -= kMinTabWidth;
      invalidateLayout();
      clampScroll();
      break;
    case Part::Right:
      e.markHandled();
      scroll_ += kMinTabWidth;
      invalidateLayout();
      clampScroll();
      break;
    case Part::List:
      e.markHandled();
      openTabList();
      break;
    default: break;
  }
}

void TabBar::onPointerMove(Event& e) {
  if (drag_.active) {
    drag_.pointerX = e.x;
    drag_.slot = slotFor(e.x);
    requestPaint();
    e.markHandled();
    return;
  }
  setHover(hitTest(e.x, e.y));
}

void TabBar::onPointerLeave(Event&) {
  if (drag_.active) return;
  setHover({});
}

void TabBar::onPointerWheel(Event& e) {
  const Layout& L = layout();
  if (!L.overflow) return;
  const double delta = -(e.wheelY != 0.0 ? e.wheelY : e.wheelX) * kWheelStep;
  const double before = scroll_;
  scroll_ += delta;
  invalidateLayout();
  clampScroll();
  if (scroll_ != before) {
    e.stopPropagation();
    e.markHandled();
  }
}

void TabBar::onDragStart(Event& e) {
  if (!drag_.armed || drag_.active || tabs_.empty()) return;
  drag_.active = true;
  drag_.pointerX = e.x;
  drag_.slot = slotFor(e.x);  // the pointer may already be far from the press when the drag is reported
  e.markHandled();
  requestPaint();
}

size_t TabBar::slotFor(double pointerX) const {
  const Layout& L = layout();
  const double barX = barRect().x;
  const double centre = pointerX - barX - drag_.grab + L.w[drag_.index] * 0.5;
  size_t slot = 0;
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (i == drag_.index) continue;
    if (L.x[i] + L.w[i] * 0.5 < centre) ++slot;
  }
  return slot;
}

void TabBar::endDrag(bool commit) {
  const bool wasActive = drag_.active;
  const size_t from = drag_.index;
  const size_t to = drag_.slot;
  drag_ = {};
  requestPaint();
  if (!wasActive || !commit || from == to || from >= tabs_.size()) return;
  const TabId moved = tabs_[from].id;
  if (!moveTab(moved, to)) return;
  scrollToTab(moved);
  if (onReorder_) {
    auto callback = onReorder_;
    callback(moved, from, to);
  }
}

void TabBar::onCaptureLost(Event&) {
  if (drag_.active) endDrag(false);
  drag_.armed = false;
}

void TabBar::onStateChanged(uint16_t) {
  if (enabled()) return;
  drag_ = {};
  hover_ = {};
  middlePress_.reset();
}

// ---- keyboard -----------------------------------------------------------------------------------

void TabBar::onKeyDown(Event& e) {
  if (e.key == Key::Escape && drag_.active) {
    endDrag(false);
    ui().router().cancelPointerInteraction();
    e.markHandled();
    return;
  }
  if (tabs_.empty()) return;
  const bool ctrl = (e.modifiers & core::events::Mod::kCtrl) != 0;
  if (e.modifiers & (core::events::Mod::kAlt | core::events::Mod::kMeta)) return;
  const size_t current = active_ ? indexOf(*active_).value_or(0) : 0;
  if (ctrl && e.key == Key{static_cast<uint16_t>('W')}) {
    e.markHandled();
    requestClose(current);
    return;
  }
  if (ctrl) return;
  switch (e.key) {
    case Key::Left:
      if (current > 0) activateByUser(current - 1);
      break;
    case Key::Right:
      if (current + 1 < tabs_.size()) activateByUser(current + 1);
      break;
    case Key::Home: activateByUser(0); break;
    case Key::End: activateByUser(tabs_.size() - 1); break;
    default: return;
  }
  e.markHandled();
}

// ---- all-tabs list ------------------------------------------------------------------------------

bool TabBar::openTabList() {
  if (tabs_.empty() || !layout().overflow) return false;
  if (ui().overlays().isOpen(listOverlay_)) {
    ui().overlays().close(listOverlay_);
    listOverlay_ = {};
    return false;
  }
  std::vector<FlyoutItem> items;
  items.reserve(tabs_.size());
  for (const TabInfo& t : tabs_) {
    FlyoutItem item;
    item.label = t.title.empty() ? std::string("Untitled") : t.title;
    item.checked = active_ && *active_ == t.id;
    items.push_back(std::move(item));
  }
  FlyoutOpenOptions options;
  options.anchor = listButtonRect();
  options.placement = Placement::BelowEnd;
  options.gap = 2.0;
  options.anchorWidget = id();
  options.initialHighlight = active_ ? static_cast<int>(indexOf(*active_).value_or(0)) : -1;
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  const OverlayHandle handle = openFlyout(
      ui(), std::move(items),
      [context, self](size_t index) {
        if (TabBar* bar = context->objectAs<TabBar>(self)) {
          if (index < bar->tabs_.size()) bar->activateByUser(index);
        }
      },
      options);
  listOverlay_ = handle.id;
  return handle.valid();
}

}  // namespace r1ui::widgets

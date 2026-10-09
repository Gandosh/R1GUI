// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: DockTabStrip's model, geometry, hit testing and input (pointer, wheel, keyboard, drag
//   hand-off). Painting is in DockTabStripPaint.cpp.
// Invariants: layout() is a pure function of (tab count, lifted tab, gap, strip width, metrics) and
//   is recomputed when one of them changes; scroll is clamped to the content; every index coming from
//   hit testing refers to an existing tab (hover and press are reset by structural changes); a host
//   callback may rebuild or destroy the strip, so no member is touched after one (the id is re-checked).
// Callers: DockAreaView (feeds), UiContext (events), tests.
#include "r1ui/widgets/dock/DockTabStrip.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/toolbar/FlyoutList.h"

namespace r1ui::widgets {

namespace layout = core::layout;
namespace State = theme::State;
using core::tree::WidgetId;
using core::events::Button;
using core::events::Key;
namespace Mod = core::events::Mod;
using theme::StyleProperty;

namespace {

constexpr theme::StyleRuleEntry kRows[] = {
    {"dock.strip", State::kNone, StyleProperty::Background, "color:canvas"},
    {"dock.strip", State::kNone, StyleProperty::BorderColor, "color:border"},
    {"dock.tab", State::kNone, StyleProperty::Background, "transparent"},
    {"dock.tab", State::kNone, StyleProperty::Foreground, "color:muted"},
    {"dock.tab", State::kNone, StyleProperty::BorderColor, "color:border"},
    {"dock.tab", State::kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"dock.tab", State::kNone, StyleProperty::LineHeight, "number:16"},
    {"dock.tab", State::kHover, StyleProperty::Foreground, "color:surface"},
    {"dock.tab", State::kSelected, StyleProperty::Background, "color:panel"},
    {"dock.tab", State::kSelected, StyleProperty::Foreground, "color:surface"},
    {"dock.close", State::kNone, StyleProperty::Background, "transparent"},
    {"dock.close", State::kNone, StyleProperty::Radius, "radius:sm"},
    {"dock.close", State::kHover, StyleProperty::Background, "color:hover"},
    {"dock.button", State::kNone, StyleProperty::Foreground, "color:muted"},
    {"dock.button", State::kHover, StyleProperty::Foreground, "color:surface"},
    {"dock.button", State::kDisabled, StyleProperty::Foreground, "color:muted@0.4"},
    {"dock.marker", State::kNone, StyleProperty::Background, "color:accent"},
};

}  // namespace

std::span<const theme::StyleRuleEntry> DockTabStrip::styleRows() { return kRows; }

void DockTabStrip::onAttached() {
  style().position = layout::Position::Absolute;
  style().height = layout::Length::px(metrics_.height);
  setFocusable(true);
}

void DockTabStrip::onDetached() {
  if (listOverlay_.valid()) ui().overlays().close(listOverlay_);
}

void DockTabStrip::bind(IDockInteraction* host, uint32_t area, FloatId window) {
  host_ = host;
  area_ = area;
  window_ = window;
}

std::string_view DockTabStrip::accessibleName() const {
  return WidgetObject::accessibleName().empty() ? std::string_view("Panel tabs") : WidgetObject::accessibleName();
}

dock::Rect DockTabStrip::stripRect() const { return toDockRect(ui().absRect(id())); }

// ---- model ----------------------------------------------------------------------------------

void DockTabStrip::invalidate() {
  layoutValid_ = false;
  requestPaint();
}

void DockTabStrip::setTabs(std::vector<DockTabInfo> tabs, size_t active, const StripMetrics& metrics) {
  if (tabs.size() > kMaxTabs) tabs.resize(kMaxTabs);
  const dock::PanelId previousFront = frontPanel();
  tabs_ = std::move(tabs);
  active_ = tabs_.empty() ? 0 : std::min(active, tabs_.size() - 1);
  metrics_ = metrics;
  style().height = layout::Length::px(metrics_.height);
  hover_ = {};
  pressHit_ = {};
  middlePress_.reset();
  if (press_.armed && (press_.index >= tabs_.size())) press_ = {};
  if (lifted_ != 0 && !indexOf(lifted_)) lifted_ = 0;
  invalidate();
  requestLayout();
  if (frontPanel() != previousFront) scrollToTab(frontPanel());
}

std::optional<size_t> DockTabStrip::indexOf(dock::PanelId panel) const {
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i].panel == panel) return i;
  }
  return std::nullopt;
}

void DockTabStrip::setLifted(dock::PanelId panel) {
  if (lifted_ == panel) return;
  lifted_ = panel;
  invalidate();
}

void DockTabStrip::setGap(std::optional<size_t> slot) {
  if (gap_ == slot) return;
  gap_ = slot;
  invalidate();
}

void DockTabStrip::clearDrag() {
  const bool was = drag_.active || press_.armed;
  drag_ = {};
  press_ = {};
  if (was && ui().router().capturer() == id()) ui().router().releaseCapture();
  requestPaint();
}

// ---- layout ---------------------------------------------------------------------------------

DockTabStrip::Layout DockTabStrip::computeLayout(bool withGap) const {
  Layout L;
  const double width = stripRect().w;
  size_t visible = 0;
  for (const DockTabInfo& t : tabs_) {
    if (t.panel != lifted_) ++visible;
  }
  L.visible = visible;
  L.slots = visible + (withGap && gap_ ? 1 : 0);
  const double fitted = L.slots == 0 ? metrics_.maxTabWidth : width / static_cast<double>(L.slots);
  L.tabW = std::min(metrics_.maxTabWidth, std::max(metrics_.minTabWidth, fitted));
  L.contentW = L.tabW * static_cast<double>(L.slots);
  L.overflow = width > 0.0 && L.contentW > width + 0.5;
  L.viewportX = L.overflow ? kArrowWidth : 0.0;
  L.viewportW = L.overflow ? std::max(0.0, width - 2.0 * kArrowWidth - kListWidth) : width;
  return L;
}

const DockTabStrip::Layout& DockTabStrip::layout() const {
  const double width = stripRect().w;
  if (!layoutValid_ || layoutWidth_ != width) {
    layout_ = computeLayout(true);
    layoutValid_ = true;
    layoutWidth_ = width;
  }
  return layout_;
}

void DockTabStrip::clampScroll() {
  const Layout& L = layout();
  const double maxScroll = L.overflow ? std::max(0.0, L.contentW - L.viewportW) : 0.0;
  scroll_ = std::clamp(scroll_, 0.0, maxScroll);
}

bool DockTabStrip::overflowing() const { return layout().overflow; }

double DockTabStrip::naturalTabWidth() const { return layout().tabW; }

size_t DockTabStrip::visiblePosition(size_t index) const {
  size_t j = 0;
  for (size_t i = 0; i < index && i < tabs_.size(); ++i) {
    if (tabs_[i].panel != lifted_) ++j;
  }
  if (gap_ && j >= *gap_) ++j;
  return j;
}

double DockTabStrip::tabLeft(size_t index) const {
  const Layout& L = layout();
  return stripRect().x + L.viewportX + static_cast<double>(visiblePosition(index)) * L.tabW - scroll_;
}

dock::Rect DockTabStrip::tabRect(size_t index) const {
  if (index >= tabs_.size() || isLifted(index)) return {};
  const dock::Rect strip = stripRect();
  return {tabLeft(index), strip.y, layout().tabW, strip.h};
}

dock::Rect DockTabStrip::closeRect(size_t index) const {
  const dock::Rect t = tabRect(index);
  if (t.w <= 0.0) return {};
  return {t.right() - 1.0 - 6.0 - kCloseSize, t.y + (t.h - kCloseSize) / 2.0, kCloseSize, kCloseSize};
}

dock::Rect DockTabStrip::leftArrowRect() const {
  const dock::Rect s = stripRect();
  return layout().overflow ? dock::Rect{s.x, s.y, kArrowWidth, s.h} : dock::Rect{};
}

dock::Rect DockTabStrip::rightArrowRect() const {
  const dock::Rect s = stripRect();
  const Layout& L = layout();
  return L.overflow ? dock::Rect{s.x + L.viewportX + L.viewportW, s.y, kArrowWidth, s.h} : dock::Rect{};
}

dock::Rect DockTabStrip::listButtonRect() const {
  const dock::Rect s = stripRect();
  const Layout& L = layout();
  return L.overflow ? dock::Rect{s.x + L.viewportX + L.viewportW + kArrowWidth, s.y, kListWidth, s.h} : dock::Rect{};
}

size_t DockTabStrip::slotAt(double centreX) const {
  const Layout L = computeLayout(true);
  const size_t others = L.visible;
  if (L.tabW <= 0.0) return 0;
  const double rel = centreX - stripRect().x - L.viewportX + scroll_;
  const double slot = std::floor(rel / L.tabW);
  return static_cast<size_t>(std::clamp(slot, 0.0, static_cast<double>(others)));
}

dock::Rect DockTabStrip::slotRect(size_t slot) const {
  const Layout L = computeLayout(true);
  const dock::Rect s = stripRect();
  return {s.x + L.viewportX + static_cast<double>(slot) * L.tabW - scroll_, s.y, L.tabW, s.h};
}

std::optional<dock::PanelId> DockTabStrip::tabAt(double x, double y) const {
  const Hit h = hitTest(x, y);
  if ((h.part == Part::Tab || h.part == Part::Close) && h.index < tabs_.size()) return tabs_[h.index].panel;
  return std::nullopt;
}

DockTabStrip::Hit DockTabStrip::hitTest(double x, double y) const {
  const dock::Rect s = stripRect();
  if (!(x >= s.x && x < s.right() && y >= s.y && y < s.bottom())) return {};
  const Layout& L = layout();
  if (L.overflow) {
    if (x >= leftArrowRect().x && x < leftArrowRect().right()) return {Part::Left, 0};
    if (x >= rightArrowRect().x && x < rightArrowRect().right()) return {Part::Right, 0};
    if (x >= listButtonRect().x && x < listButtonRect().right()) return {Part::List, 0};
    if (x < s.x + L.viewportX || x >= s.x + L.viewportX + L.viewportW) return {Part::Empty, 0};
  }
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (isLifted(i)) continue;
    const dock::Rect t = tabRect(i);
    if (x < t.x || x >= t.right()) continue;
    const bool showsClose = tabs_[i].closable && !tabs_[i].locked && (i == active_ || hover_.index == i);
    const dock::Rect c = closeRect(i);
    if (showsClose && x >= c.x && x < c.right() && y >= c.y && y < c.bottom()) return {Part::Close, i};
    return {Part::Tab, i};
  }
  return {Part::Empty, 0};
}

void DockTabStrip::setHover(Hit hit) {
  if (hit == hover_) return;
  hover_ = hit;
  requestPaint();
}

bool DockTabStrip::scrollToTab(dock::PanelId panel) {
  const std::optional<size_t> at = indexOf(panel);
  if (!at || isLifted(*at)) return false;
  const Layout& L = layout();
  if (!L.overflow) {
    scroll_ = 0.0;
    return true;
  }
  const double left = static_cast<double>(visiblePosition(*at)) * L.tabW;
  if (left < scroll_) {
    scroll_ = left;
  } else if (left + L.tabW > scroll_ + L.viewportW) {
    scroll_ = left + L.tabW - L.viewportW;
  }
  clampScroll();
  requestPaint();
  return true;
}

bool DockTabStrip::openTabList() {
  if (tabs_.empty() || !layout().overflow) return false;
  if (ui().overlays().isOpen(listOverlay_)) {
    ui().overlays().close(listOverlay_);
    listOverlay_ = {};
    return false;
  }
  std::vector<FlyoutItem> items;
  items.reserve(tabs_.size());
  for (size_t i = 0; i < tabs_.size(); ++i) {
    FlyoutItem item;
    item.label = tabs_[i].title.empty() ? std::string("Untitled") : tabs_[i].title;
    item.checked = i == active_;
    items.push_back(std::move(item));
  }
  FlyoutOpenOptions options;
  const dock::Rect button = listButtonRect();
  options.anchor = {static_cast<int32_t>(std::lround(button.x)), static_cast<int32_t>(std::lround(button.y)),
                    static_cast<int32_t>(std::lround(button.w)), static_cast<int32_t>(std::lround(button.h))};
  options.placement = Placement::BelowEnd;
  options.anchorWidget = id();
  const WidgetId self = id();
  UiContext& context = ui();
  const std::vector<dock::PanelId> panels = [&] {
    std::vector<dock::PanelId> out;
    for (const DockTabInfo& t : tabs_) out.push_back(t.panel);
    return out;
  }();
  const OverlayHandle handle = openFlyout(
      context, std::move(items),
      [&context, self, panels](size_t index) {
        DockTabStrip* strip = context.objectAs<DockTabStrip>(self);
        if (strip == nullptr || index >= panels.size() || strip->host_ == nullptr) return;
        IDockInteraction* host = strip->host_;
        const dock::PanelId panel = panels[index];
        host->tabActivated(panel);
        if (DockTabStrip* again = context.objectAs<DockTabStrip>(self)) again->scrollToTab(panel);
      },
      options);
  listOverlay_ = handle.id;
  return handle.valid();
}

// ---- pointer --------------------------------------------------------------------------------

void DockTabStrip::onPointerDown(Event& e) {
  if (host_ == nullptr) return;
  const Hit hit = hitTest(e.x, e.y);
  setHover(hit);
  pressHit_ = hit;
  if (e.button == Button::Left) {
    if (hit.part == Part::Tab) {
      const dock::PanelId panel = tabs_[hit.index].panel;
      const dock::Rect t = tabRect(hit.index);
      const bool locked = tabs_[hit.index].locked;
      ui().router().focus(id(), core::events::FocusReason::Pointer);
      if (!locked) {
        press_ = {true, hit.index, e.x - t.x, e.y - t.y, e.x, e.y};
        ui().router().capturePointer(id());
      }
      e.markHandled();
      const WidgetId self = id();
      host_->tabActivated(panel);  // rule 1: before any drag
      if (!ui().alive(self)) return;
    } else if (hit.part != Part::None) {
      ui().router().focus(id(), core::events::FocusReason::Pointer);
      e.markHandled();
    }
  } else if (e.button == Button::Middle) {
    if (hit.part == Part::Tab || hit.part == Part::Close) {
      middlePress_ = hit.index;  // rule 37: closes on release over the same tab
      e.markHandled();
    }
  } else if (e.button == Button::Right && hit.part != Part::None) {
    e.markHandled();
    const WidgetId self = id();
    dock::PanelId panel = 0;
    if (hit.part == Part::Tab || hit.part == Part::Close) {
      panel = tabs_[hit.index].panel;
      host_->tabActivated(panel);  // rule 2
      if (!ui().alive(self)) return;
    }
    host_->tabContextMenu(*this, panel, e.x, e.y);
  }
}

void DockTabStrip::onPointerUp(Event& e) {
  if (host_ == nullptr) return;
  if (e.button == Button::Left) {
    const bool wasDragging = drag_.active;
    press_ = {};
    if (wasDragging) {
      drag_ = {};
      requestPaint();
      host_->tabDragEnd(*this, {e.x, e.y}, true);
    }
  } else if (e.button == Button::Middle && middlePress_) {
    const Hit hit = hitTest(e.x, e.y);
    const size_t pressed = *middlePress_;
    middlePress_.reset();
    if ((hit.part == Part::Tab || hit.part == Part::Close) && hit.index == pressed && pressed < tabs_.size()) {
      e.markHandled();
      host_->tabCloseRequested(tabs_[pressed].panel);
    }
  }
}

void DockTabStrip::onClick(Event& e) {
  if (host_ == nullptr || e.button != Button::Left) return;
  const Hit hit = hitTest(e.x, e.y);
  if (hit.part != pressHit_.part || (hit.part == Part::Close && hit.index != pressHit_.index)) return;
  switch (hit.part) {
    case Part::Close:
      e.markHandled();
      host_->tabCloseRequested(tabs_[hit.index].panel);
      break;
    case Part::Left:
      e.markHandled();
      scroll_ -= layout().tabW * 2.0;
      invalidate();
      clampScroll();
      break;
    case Part::Right:
      e.markHandled();
      scroll_ += layout().tabW * 2.0;
      invalidate();
      clampScroll();
      break;
    case Part::List:
      e.markHandled();
      openTabList();
      break;
    default: break;
  }
}

void DockTabStrip::onDoubleClick(Event& e) {
  if (host_ == nullptr || e.button != Button::Left) return;
  const Hit hit = hitTest(e.x, e.y);
  if (hit.part == Part::Tab || hit.part == Part::Close) {
    e.markHandled();  // spec 02 rule 51: a double click on a tab does nothing extra
    return;
  }
  if (hit.part == Part::Empty) {
    e.markHandled();
    host_->stripBackgroundDoubleClick(*this);
  }
}

void DockTabStrip::onDragStart(Event& e) {
  if (host_ == nullptr || !press_.armed || drag_.active || press_.index >= tabs_.size()) return;
  const dock::PanelId panel = tabs_[press_.index].panel;
  const dock::Rect t = tabRect(press_.index);
  const dock::Point grab{press_.grabX, press_.grabY};
  const dock::Point size{t.w, t.h};
  const WidgetId self = id();
  e.markHandled();
  if (!host_->tabDragBegin(*this, panel, {e.x, e.y}, grab, size)) {
    if (ui().alive(self)) clearDrag();
    return;
  }
  if (!ui().alive(self)) return;
  drag_.active = true;
  press_.armed = false;
  requestPaint();
}

void DockTabStrip::onPointerMove(Event& e) {
  if (drag_.active && host_ != nullptr) {
    e.markHandled();
    host_->tabDragMove(*this, {e.x, e.y});
    return;
  }
  setHover(hitTest(e.x, e.y));
}

void DockTabStrip::onPointerLeave(Event&) {
  if (!drag_.active) setHover({});
}

void DockTabStrip::onCaptureLost(Event& e) {
  press_ = {};
  if (!drag_.active || host_ == nullptr) return;
  drag_ = {};
  requestPaint();
  host_->tabDragEnd(*this, {e.x, e.y}, false);
}

void DockTabStrip::onPointerWheel(Event& e) {
  const Layout& L = layout();
  if (!L.overflow) return;
  const double before = scroll_;
  scroll_ -= (e.wheelY != 0.0 ? e.wheelY : e.wheelX) * kWheelStep;
  invalidate();
  clampScroll();
  if (scroll_ != before) {
    e.stopPropagation();
    e.markHandled();
  }
}

// ---- keyboard -------------------------------------------------------------------------------

void DockTabStrip::onKeyDown(Event& e) {
  if (host_ == nullptr) return;
  if (e.key == Key::Escape) {
    if (host_->cancelDragRequested()) {
      e.stopPropagation();
      e.markHandled();
    }
    return;
  }
  if (tabs_.empty()) return;
  const bool ctrl = (e.modifiers & Mod::kCtrl) != 0;
  if (ctrl && e.key == static_cast<Key>('W')) {
    e.markHandled();
    host_->tabCloseRequested(frontPanel());
    return;
  }
  if (ctrl || (e.modifiers & (Mod::kAlt | Mod::kMeta)) != 0) return;
  size_t target = active_;
  switch (e.key) {
    case Key::Left: target = active_ > 0 ? active_ - 1 : active_; break;
    case Key::Right: target = std::min(active_ + 1, tabs_.size() - 1); break;
    case Key::Home: target = 0; break;
    case Key::End: target = tabs_.size() - 1; break;
    case Key::Down:
      e.markHandled();
      host_->focusPanelOfStrip(*this);
      return;
    default: return;
  }
  e.markHandled();
  if (target != active_) host_->tabActivated(tabs_[target].panel);
}

}  // namespace r1ui::widgets

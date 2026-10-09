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

#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/toolbar/FlyoutList.h"

namespace r1ui::widgets {

namespace layout = core::layout;
namespace State = theme::State;
using core::events::Button;
using core::events::Key;
using theme::StyleProperty;

namespace {

constexpr double kPadX = 12.0;        // measured: padding 0 x 12
constexpr double kIconSize = 12.0;    // leading file icon
constexpr double kGap = 6.0;          // measured gap between icon, label and close button
constexpr double kCloseSize = 16.0;
constexpr double kCloseIcon = 12.0;
constexpr double kBorder = 1.0;
constexpr double kTabHeight = 35.0;   // bar 36 with the 1 px bottom border inside
constexpr double kCloseTop = 9.5;     // measured close button y
constexpr double kWheelStep = 48.0;
constexpr double kButtonIcon = 14.0;

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

int slotOf(TabId id, int sub) { return static_cast<int>((id * 4 + static_cast<TabId>(sub)) & 0x3fffffffu); }

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

// ---- layout -------------------------------------------------------------------------------------

double TabBar::naturalWidth(std::string_view title, bool closable, bool hasIcon) const {
  const theme::ResolvedStyle& rs = ui().services().resolve("tabbar.tab", 0);
  const double scale = ui().scale();
  const double text = static_cast<double>(ui().text().measure(title, static_cast<float>(rs.text.fontSize * scale), rs.text.weight)) / scale;
  double w = kPadX + (hasIcon ? kIconSize + kGap : 0.0) + text + (closable ? kGap + kCloseSize : 0.0) + kPadX + kBorder;
  return std::min(std::ceil(w), kMaxTabWidth);
}

const TabBar::Layout& TabBar::layout() const {
  const double width = static_cast<double>(barRect().w);
  const double scale = ui().scale();
  if (layoutValid_ && layout_.barWidth == width && layoutScale_ == scale) return layout_;
  Layout L;
  L.barWidth = width;
  L.hasNew = showNew_;
  const size_t n = tabs_.size();
  std::vector<double> natural(n);
  double sum = 0.0;
  for (size_t i = 0; i < n; ++i) {
    natural[i] = naturalWidth(tabs_[i].title, tabs_[i].closable, !tabs_[i].icon.empty());
    sum += natural[i];
  }
  const double newW = showNew_ ? kNewButtonWidth : 0.0;
  std::vector<double> widths(n);
  if (sum + newW <= width) {
    widths = natural;
    L.stripW = sum;
    L.newX = sum;
  } else if (static_cast<double>(n) * kMinTabWidth <= width - newW) {
    // Shrink evenly: tabs shorter than the common width keep their natural width.
    const double avail = width - newW;
    std::vector<double> sorted = natural;
    std::sort(sorted.begin(), sorted.end());
    double rest = avail;
    double common = avail / static_cast<double>(n);
    for (size_t i = 0; i < n; ++i) {
      const double share = rest / static_cast<double>(n - i);
      if (sorted[i] <= share) {
        rest -= sorted[i];
      } else {
        common = share;
        break;
      }
    }
    for (size_t i = 0; i < n; ++i) widths[i] = std::min(natural[i], common);
    L.stripW = avail;
    L.newX = avail;
  } else {
    L.overflow = true;
    std::fill(widths.begin(), widths.end(), kMinTabWidth);
    L.stripX = kOverflowButtonWidth;
    L.stripW = std::max(0.0, width - newW - 3.0 * kOverflowButtonWidth);
    L.leftX = 0.0;
    L.rightX = L.stripX + L.stripW;
    L.listX = L.rightX + kOverflowButtonWidth;
    L.newX = width - newW;
  }
  double contentW = 0.0;
  for (const double w : widths) contentW += w;
  L.contentW = contentW;
  const double maxScroll = L.overflow ? std::max(0.0, contentW - L.stripW) : 0.0;
  const double scroll = std::clamp(scroll_, 0.0, maxScroll);
  // Whole-pixel edges from the cumulative widths.
  L.x.resize(n);
  L.w.resize(n);
  double cumulative = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const double left = std::round(cumulative);
    cumulative += widths[i];
    const double right = std::round(cumulative);
    L.x[i] = L.stripX + left - scroll;
    L.w[i] = right - left;
  }
  if (!L.overflow) L.newX = std::round(contentW);
  layout_ = std::move(L);
  scroll_ = scroll;
  layoutValid_ = true;
  layoutScale_ = scale;
  return layout_;
}

void TabBar::clampScroll() {
  layoutValid_ = false;
  (void)layout();  // recomputes and clamps scroll_
}

bool TabBar::overflowing() const { return layout().overflow; }

bool TabBar::scrollToTab(TabId id) {
  const std::optional<size_t> at = indexOf(id);
  if (!at) return false;
  const Layout& L = layout();
  if (!L.overflow) return true;
  const double left = L.x[*at] - L.stripX + scroll_;  // content coordinates
  const double right = left + L.w[*at];
  double target = scroll_;
  if (left < scroll_) target = left;
  else if (right > scroll_ + L.stripW) target = right - L.stripW;
  if (target != scroll_) {
    scroll_ = target;
    invalidateLayout();
    clampScroll();
  }
  return true;
}

// ---- geometry for tests and hit testing ---------------------------------------------------------

namespace {
layout::Rect toRect(const layout::Rect& bar, double x, double y, double w, double h) {
  return {static_cast<int32_t>(std::lround(bar.x + x)), static_cast<int32_t>(std::lround(bar.y + y)), static_cast<int32_t>(std::lround(w)), static_cast<int32_t>(std::lround(h))};
}
}  // namespace

layout::Rect TabBar::tabRect(TabId id) const {
  const auto at = indexOf(id);
  if (!at) return {};
  const Layout& L = layout();
  return toRect(barRect(), L.x[*at], 0, L.w[*at], kTabHeight);
}

layout::Rect TabBar::closeRect(TabId id) const {
  const auto at = indexOf(id);
  if (!at || !tabs_[*at].closable) return {};
  const Layout& L = layout();
  return toRect(barRect(), L.x[*at] + L.w[*at] - kBorder - kPadX - kCloseSize, kCloseTop, kCloseSize, kCloseSize);
}

layout::Rect TabBar::newButtonRect() const {
  const Layout& L = layout();
  return L.hasNew ? toRect(barRect(), L.newX, 0, kNewButtonWidth, kHeight) : layout::Rect{};
}

layout::Rect TabBar::leftArrowRect() const {
  const Layout& L = layout();
  return L.overflow ? toRect(barRect(), L.leftX, 0, kOverflowButtonWidth, kTabHeight) : layout::Rect{};
}

layout::Rect TabBar::rightArrowRect() const {
  const Layout& L = layout();
  return L.overflow ? toRect(barRect(), L.rightX, 0, kOverflowButtonWidth, kTabHeight) : layout::Rect{};
}

layout::Rect TabBar::listButtonRect() const {
  const Layout& L = layout();
  return L.overflow ? toRect(barRect(), L.listX, 0, kOverflowButtonWidth, kTabHeight) : layout::Rect{};
}

TabBar::Hit TabBar::hitTest(double x, double y) const {
  const layout::Rect bar = barRect();
  const double lx = x - bar.x;
  const double ly = y - bar.y;
  if (lx < 0 || ly < 0 || lx >= bar.w || ly >= kHeight) return {};
  const Layout& L = layout();
  const auto within = [&](double left, double width) { return lx >= left && lx < left + width; };
  if (L.hasNew && within(L.newX, kNewButtonWidth)) return {Part::New, 0};
  if (L.overflow) {
    if (within(L.leftX, kOverflowButtonWidth)) return {Part::Left, 0};
    if (within(L.rightX, kOverflowButtonWidth)) return {Part::Right, 0};
    if (within(L.listX, kOverflowButtonWidth)) return {Part::List, 0};
    if (lx < L.stripX || lx >= L.stripX + L.stripW) return {};
  }
  if (ly >= kTabHeight) return {};
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (!within(L.x[i], L.w[i])) continue;
    if (tabs_[i].closable && within(L.x[i] + L.w[i] - kBorder - kPadX - kCloseSize, kCloseSize) && ly >= kCloseTop && ly < kCloseTop + kCloseSize) return {Part::Close, i};
    return {Part::Tab, i};
  }
  return {};
}

// ---- painting -----------------------------------------------------------------------------------

void TabBar::paint(PaintContext& ctx) {
  const Layout& L = layout();
  const layout::Rect bar = ctx.rect();
  const theme::ResolvedStyle& barStyle = ctx.resolve("tabbar.bar", 0);
  render::Painter& painter = ctx.painter();
  painter.fillRect(ctx.box(), ctx.color(barStyle.background));
  painter.fillRect(ctx.toPhysical(bar.x, bar.y + kHeight - 1.0, bar.w, 1.0), ctx.color(barStyle.border.color));

  // Positions of the tabs; while dragging the others make room for the dragged one.
  std::vector<double> xs = L.x;
  const size_t n = tabs_.size();
  if (drag_.active && n > 0) {
    std::vector<size_t> order;
    for (size_t i = 0; i < n; ++i) {
      if (i != drag_.index) order.push_back(i);
    }
    order.insert(order.begin() + static_cast<std::ptrdiff_t>(std::min(drag_.slot, order.size())), drag_.index);
    double cursor = L.x[0];
    for (const size_t i : order) {
      xs[i] = cursor;
      cursor += L.w[i];
    }
    const double lo = L.stripX;
    const double hi = L.stripX + std::max(L.overflow ? L.stripW : L.contentW, L.w[drag_.index]) - L.w[drag_.index];
    xs[drag_.index] = std::clamp(drag_.pointerX - bar.x - drag_.grab, lo, hi);
  }

  if (L.overflow) painter.pushClip(ctx.toPhysical(bar.x + L.stripX, bar.y, L.stripW, kHeight));
  const auto paintTab = [&](size_t i) {
    const TabInfo& t = tabs_[i];
    const bool activeTab = active_ && *active_ == t.id;
    const bool hot = (hover_.part == Part::Tab || hover_.part == Part::Close) && hover_.index == i;
    const uint8_t bits = (hot ? State::kHover : State::kNone) | (activeTab ? State::kSelected : State::kNone);
    const theme::ResolvedStyle& rs = ctx.resolve("tabbar.tab", bits);
    const double x = bar.x + xs[i];
    const render::Rect box = ctx.toPhysical(x, bar.y, L.w[i], kTabHeight);
    if (rs.background.a > 0) painter.fillRect(box, ctx.color(rs.background));
    painter.fillRect(ctx.toPhysical(x + L.w[i] - kBorder, bar.y, kBorder, kTabHeight), ctx.color(rs.border.color));
    const render::Color text = ctx.animatedColor(slotOf(t.id, 0), ctx.color(rs.text.color));
    double left = kPadX;
    if (!t.icon.empty()) {
      render::Color iconTint = text;
      iconTint.a *= 0.5f;  // measured: the file icon is drawn at 50% opacity
      ctx.drawIcon(t.icon, kIconSize, ctx.toPhysical(x + left, bar.y, kIconSize, kTabHeight), iconTint);
      left += kIconSize + kGap;
    }
    const double right = t.closable ? kBorder + kPadX + kCloseSize + kGap : kPadX + kBorder;
    TextOptions o;
    o.padLeft = left;
    o.padRight = right;
    o.color = text;
    ctx.drawText(t.title, rs.text, box, o);
    if (t.closable) {
      const float visible = ctx.animatedValue(slotOf(t.id, 1), (activeTab || hot) ? 1.0f : 0.0f);
      if (visible > 0.001f) {
        const bool overClose = hover_.part == Part::Close && hover_.index == i;
        const theme::ResolvedStyle& cs = ctx.resolve("tabbar.close", overClose ? State::kHover : State::kNone);
        const render::Rect closeBox = ctx.toPhysical(x + L.w[i] - kBorder - kPadX - kCloseSize, bar.y + kCloseTop, kCloseSize, kCloseSize);
        if (cs.background.a > 0) {
          render::Color bg = ctx.color(cs.background);
          bg.a *= visible;
          painter.fillRoundedRect(closeBox, render::CornerRadii::uniform(ctx.px(cs.radius)), bg);
        }
        render::Color tint = text;
        tint.a *= visible;
        ctx.drawIcon("x", kCloseIcon, closeBox, tint);
      }
    }
  };
  for (size_t i = 0; i < n; ++i) {
    if (!(drag_.active && i == drag_.index)) paintTab(i);
  }
  if (drag_.active && drag_.index < n) paintTab(drag_.index);
  if (L.overflow) painter.popClip();

  const auto button = [&](Part part, double x, double w, const char* icon, bool enabled) {
    const bool hot = hover_.part == part && enabled;
    const theme::ResolvedStyle& bs = ctx.resolve("tabbar.button", (hot ? State::kHover : State::kNone) | (enabled ? State::kNone : State::kDisabled));
    const render::Color tint = ctx.animatedColor(slotOf(static_cast<TabId>(part) + 1000000, 0), ctx.color(bs.text.color));
    ctx.drawIcon(icon, kButtonIcon, ctx.toPhysical(bar.x + x, bar.y, w, kTabHeight), tint);
  };
  if (L.hasNew) button(Part::New, L.newX, kNewButtonWidth, "plus", true);
  if (L.overflow) {
    button(Part::Left, L.leftX, kOverflowButtonWidth, "chevron-left", scroll_ > 0.0);
    button(Part::Right, L.rightX, kOverflowButtonWidth, "chevron-right", scroll_ < L.contentW - L.stripW - 0.5);
    button(Part::List, L.listX, kOverflowButtonWidth, "chevron-down", true);
  }
}

void TabBar::paintOver(PaintContext& ctx) {
  if (!focusVisible() || !active_) return;
  const auto at = indexOf(*active_);
  if (!at) return;
  const Layout& L = layout();
  const layout::Rect bar = ctx.rect();
  ctx.focusRing(ctx.toPhysical(bar.x + L.x[*at], bar.y, L.w[*at], kTabHeight), 0.0f);
}

// ---- cursor, tooltips ---------------------------------------------------------------------------

Cursor TabBar::cursor() const {
  if (drag_.active) return Cursor::Move;
  return hover_.part == Part::None ? Cursor::Default : Cursor::Pointer;
}

std::string_view TabBar::fullTitle(size_t index) const {
  const TabInfo& t = tabs_[index];
  return t.tooltip.empty() ? std::string_view(t.title) : std::string_view(t.tooltip);
}

std::string_view TabBar::tooltipText() const {
  if (drag_.active) return {};
  switch (hover_.part) {
    case Part::Tab: return hover_.index < tabs_.size() ? fullTitle(hover_.index) : std::string_view();
    case Part::Close:
      if (hover_.index >= tabs_.size()) return {};
      tooltipScratch_ = "Close " + tabs_[hover_.index].title;
      return tooltipScratch_;
    case Part::New: return newTooltip_;
    case Part::Left: return "Scroll tabs left";
    case Part::Right: return "Scroll tabs right";
    case Part::List: return "All tabs";
    case Part::None: break;
  }
  return {};
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
      drag_.pressX = e.x;
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

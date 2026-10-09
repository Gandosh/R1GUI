// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the width distribution of TabBar (natural widths, even shrinking down to the 60 px minimum,
//   overflow with scrolling), the geometry queries for tests and the hit test.
// Invariants: layout() is a pure function of (titles, closable / icon flags, bar width, display scale,
//   scroll offset, show-new flag) cached until one of them changes; tab edges are whole logical pixels.
// Callers: TabBar painting and input, tests.
#include <algorithm>
#include <cmath>

#include "TabBarMetrics.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace layout = core::layout;

// ---- layout -------------------------------------------------------------------------------------

double TabBar::naturalWidth(std::string_view title, bool closable, bool hasIcon) const {
  const theme::ResolvedStyle& rs = ui().services().resolve("tabbar.tab", 0);
  const double scale = ui().scale();
  const double text = static_cast<double>(ui().text().measure(title, static_cast<float>(rs.text.fontSize * scale), rs.text.weight)) / scale;
  double w = kPadX + (hasIcon ? kIconSize + kGap : 0.0) + text + (closable ? kGap + kCloseSize : 0.0) + kPadX + kBorder;
  return std::min(std::round(w), kMaxTabWidth);  // whole pixels like the browser's layout (a tab titled "Untitled" is 109 px)
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
  if (revealPending_ && width > 0.0 && active_) {
    // The active tab was set before the bar had a width: bring it into view now.
    revealPending_ = false;
    if (const auto at = indexOf(*active_); at && layout_.overflow) {
      const double left = layout_.x[*at] - layout_.stripX + scroll_;
      const double right = left + layout_.w[*at];
      double target = scroll_;
      if (left < scroll_) target = left;
      else if (right > scroll_ + layout_.stripW) target = right - layout_.stripW;
      scroll_ = std::clamp(target, 0.0, std::max(0.0, layout_.contentW - layout_.stripW));
      layoutValid_ = false;  // positions depend on the scroll offset: compute them again
      return layout();
    }
  }
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
  if (barRect().w <= 0) {
    revealPending_ = true;
    return true;
  }
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

}  // namespace r1ui::widgets

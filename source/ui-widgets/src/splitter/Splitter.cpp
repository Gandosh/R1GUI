// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Splitter.h (pane styles, handle geometry, drag / keyboard resizing,
//   collapse and expand, state persistence, painting of the handles).
// Invariants: pane weights are finite and >= 1e-6; resizing always goes through applyPair(), which
//   keeps the sum of the two panes and never lets either go below its minimum; sizes are read from
//   the last layout (exact rectangles), and a request made before any layout (total 0) is refused.
// Callers: UiContext (events, paint), tests, application shells.
#include "r1ui/widgets/splitter/Splitter.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace layout = core::layout;
namespace State = theme::State;
using core::events::Button;
using core::events::Key;
using core::events::Phase;
using theme::StyleProperty;

namespace {

constexpr theme::StyleRuleEntry kRows[] = {
    {"splitter.handle", State::kNone, StyleProperty::Background, "color:border"},
    {"splitter.handle", State::kHover, StyleProperty::Background, "color:border-strong"},
    {"splitter.handle", State::kActive, StyleProperty::Background, "color:accent"},
};

constexpr double kMinWeight = 1e-6;
constexpr double kKeyStep = 10.0;
constexpr double kKeyStepLarge = 50.0;

}  // namespace

void SplitterPane::onAttached() {
  layout::Style& s = style();
  s.overflow = layout::Overflow::Hidden;
  s.direction = layout::FlexDirection::Column;
}

// ---- construction -------------------------------------------------------------------------------

void Splitter::onAttached() {
  layout::Style& s = style();
  s.direction = horizontal() ? layout::FlexDirection::Row : layout::FlexDirection::Column;
  s.alignItems = layout::Align::Stretch;
  (horizontal() ? s.gapColumn : s.gapRow) = thickness_;
}

std::span<const theme::StyleRuleEntry> Splitter::styleRows() { return kRows; }

uint8_t Splitter::phases() const { return core::events::kListenCapture | core::events::kListenTarget | core::events::kListenBubble; }

core::tree::WidgetId Splitter::addPane(PaneOptions options) {
  if (panes_.size() >= kMaxPanes) return {};
  if (!std::isfinite(options.weight) || options.weight <= 0.0) options.weight = 1.0;
  if (!std::isfinite(options.minSize)) options.minSize = -1.0;
  if (!std::isfinite(options.fixedSize)) options.fixedSize = -1.0;
  const core::tree::WidgetId id = ui().create<SplitterPane>(this->id()).id();
  Pane p;
  p.id = id;
  p.options = options;
  p.weight = std::max(options.weight, kMinWeight);
  panes_.push_back(p);
  applyStyle(panes_.size() - 1);
  requestLayout();
  return id;
}

core::tree::WidgetId Splitter::pane(size_t index) const { return index < panes_.size() ? panes_[index].id : core::tree::WidgetId{}; }

double Splitter::effectiveMin(size_t i) const {
  const Pane& p = panes_[i];
  if (p.collapsed) return 0.0;
  return p.options.minSize >= 0.0 ? p.options.minSize : floor_;
}

double Splitter::sizeOf(size_t i) const {
  if (i >= panes_.size() || !panes_[i].visible) return 0.0;
  const core::tree::Widget* w = ui().tree().get(panes_[i].id);
  if (w == nullptr) return 0.0;
  return horizontal() ? w->exact.w : w->exact.h;
}

double Splitter::paneSize(size_t index) const { return sizeOf(index); }

void Splitter::applyStyle(size_t i) {
  Pane& p = panes_[i];
  core::tree::Widget* node = ui().tree().get(p.id);
  if (node == nullptr) return;
  layout::Style& s = node->style;
  s.display = p.visible ? layout::Display::Flex : layout::Display::None;
  layout::Length& minMain = horizontal() ? s.minWidth : s.minHeight;
  layout::Length& maxMain = horizontal() ? s.maxWidth : s.maxHeight;
  if (p.options.fixedSize >= 0.0) {
    s.flexGrow = 0.0;
    s.flexShrink = 0.0;
    s.flexBasis = layout::Length::px(p.options.fixedSize);
    minMain = layout::Length::px(p.options.fixedSize);
    maxMain = layout::Length::px(p.options.fixedSize);
  } else {
    s.flexGrow = p.collapsed ? 0.0 : std::max(p.weight, kMinWeight);
    s.flexShrink = p.collapsed ? 0.0 : 1.0;
    s.flexBasis = layout::Length::px(0);
    minMain = layout::Length::px(effectiveMin(i));
    maxMain = p.collapsed ? layout::Length::px(0) : layout::Length::autoValue();
  }
  ui().invalidator().requestLayout(p.id);
  requestLayout();
}

bool Splitter::setPaneVisible(size_t index, bool visible) {
  if (index >= panes_.size() || panes_[index].visible == visible) return false;
  panes_[index].visible = visible;
  applyStyle(index);
  hover_ = -1;
  return true;
}

bool Splitter::paneVisible(size_t index) const { return index < panes_.size() && panes_[index].visible; }
bool Splitter::isCollapsed(size_t index) const { return index < panes_.size() && panes_[index].collapsed; }

bool Splitter::setMinPaneSize(double px) {
  if (!std::isfinite(px) || px < 0.0 || px > 1.0e6) return false;
  floor_ = px;
  for (size_t i = 0; i < panes_.size(); ++i) applyStyle(i);
  return true;
}

bool Splitter::setHandleThickness(double px) {
  if (!std::isfinite(px) || px < 1.0 || px > 64.0) return false;
  thickness_ = px;
  (horizontal() ? style().gapColumn : style().gapRow) = px;
  requestLayout();
  return true;
}

bool Splitter::setHitBand(double px) {
  if (!std::isfinite(px) || px < 0.0 || px > 64.0) return false;
  band_ = px;
  return true;
}

void Splitter::setKeyboardResize(bool enabled) {
  keyboard_ = enabled;
  setFocusable(enabled);
}

// ---- handles ------------------------------------------------------------------------------------

std::vector<size_t> Splitter::visiblePanes() const {
  std::vector<size_t> v;
  for (size_t i = 0; i < panes_.size(); ++i) {
    if (panes_[i].visible) v.push_back(i);
  }
  return v;
}

size_t Splitter::handleCount() const {
  const size_t visible = visiblePanes().size();
  return visible > 0 ? visible - 1 : 0;
}

bool Splitter::neighbours(size_t handle, size_t& before, size_t& after) const {
  const std::vector<size_t> v = visiblePanes();
  if (handle + 1 >= v.size()) return false;
  bool haveBefore = false;
  bool haveAfter = false;
  for (size_t k = handle + 1; k-- > 0;) {
    if (resizable(v[k])) {
      before = v[k];
      haveBefore = true;
      break;
    }
  }
  for (size_t k = handle + 1; k < v.size(); ++k) {
    if (resizable(v[k])) {
      after = v[k];
      haveAfter = true;
      break;
    }
  }
  return haveBefore && haveAfter;
}

bool Splitter::handleResizable(size_t handle) const {
  size_t a = 0, b = 0;
  return neighbours(handle, a, b);
}

layout::Rect Splitter::handleRect(size_t handle) const {
  const std::vector<size_t> v = visiblePanes();
  if (handle + 1 >= v.size()) return {};
  const layout::Rect first = ui().absRect(panes_[v[handle]].id);
  const layout::Rect second = ui().absRect(panes_[v[handle + 1]].id);
  const layout::Rect me = ui().absRect(id());
  const double thick = thickness_;
  if (horizontal()) {
    const double gapStart = static_cast<double>(first.x) + first.w;
    const double gap = static_cast<double>(second.x) - gapStart;
    const double x = std::round(gapStart + (gap - thick) * 0.5);
    return {static_cast<int32_t>(x), me.y, static_cast<int32_t>(thick), me.h};
  }
  const double gapStart = static_cast<double>(first.y) + first.h;
  const double gap = static_cast<double>(second.y) - gapStart;
  const double y = std::round(gapStart + (gap - thick) * 0.5);
  return {me.x, static_cast<int32_t>(y), me.w, static_cast<int32_t>(thick)};
}

int Splitter::handleAt(double x, double y) const {
  const size_t count = handleCount();
  const double band = std::max(band_, thickness_);
  for (size_t h = 0; h < count; ++h) {
    const layout::Rect r = handleRect(h);
    const double centre = horizontal() ? r.x + r.w * 0.5 : r.y + r.h * 0.5;
    const double along = horizontal() ? x : y;
    const double cross = horizontal() ? y : x;
    const double crossStart = horizontal() ? r.y : r.x;
    const double crossLen = horizontal() ? r.h : r.w;
    if (std::abs(along - centre) <= band * 0.5 && cross >= crossStart && cross < crossStart + crossLen && handleResizable(h)) return static_cast<int>(h);
  }
  return -1;
}

// ---- resizing -----------------------------------------------------------------------------------

void Splitter::snapshotWeights() {
  double total = 0.0;
  for (size_t i = 0; i < panes_.size(); ++i) {
    if (resizable(i) && !panes_[i].collapsed) total += sizeOf(i);
  }
  if (total <= 0.0) return;
  for (size_t i = 0; i < panes_.size(); ++i) {
    if (resizable(i) && !panes_[i].collapsed) panes_[i].weight = std::max(sizeOf(i), kMinWeight);
  }
}

void Splitter::changed() {
  for (size_t i = 0; i < panes_.size(); ++i) applyStyle(i);
  requestPaint();
  if (onChanged_) onChanged_(*this);
}

bool Splitter::resizePair(size_t a, size_t b, double sizeA, double sizeB, double delta) {
  const bool wasA = panes_[a].collapsed;
  const bool wasB = panes_[b].collapsed;
  // The pane that grows leaves the collapsed state; its floor then applies.
  if (delta > 0.0) panes_[a].collapsed = false;
  else if (delta < 0.0) panes_[b].collapsed = false;
  const double total = sizeA + sizeB;
  const double lo = effectiveMin(a);
  const double hi = total - effectiveMin(b);
  const bool room = total > 0.0 && lo <= hi;
  const double newA = room ? std::clamp(sizeA + delta, lo, hi) : sizeA;
  const bool flagsChanged = wasA != panes_[a].collapsed || wasB != panes_[b].collapsed;
  if (!room || (std::abs(newA - sizeA) < 1e-9 && !flagsChanged)) {
    panes_[a].collapsed = wasA;
    panes_[b].collapsed = wasB;
    return false;
  }
  snapshotWeights();
  panes_[a].weight = std::max(newA, kMinWeight);
  panes_[b].weight = std::max(total - newA, kMinWeight);
  return true;
}

bool Splitter::moveHandle(size_t handle, double delta) {
  if (!std::isfinite(delta) || delta == 0.0) return false;
  size_t a = 0, b = 0;
  if (!neighbours(handle, a, b)) return false;
  if (!resizePair(a, b, sizeOf(a), sizeOf(b), delta)) return false;
  changed();
  return true;
}

// ---- collapse -----------------------------------------------------------------------------------

bool Splitter::collapse(size_t index) {
  if (index >= panes_.size()) return false;
  Pane& p = panes_[index];
  if (!p.options.collapsible || !resizable(index) || p.collapsed) return false;
  const double size = sizeOf(index);
  if (size <= 0.0) return false;
  // The space goes to the nearest resizable pane that is not collapsed, after the pane first.
  size_t target = panes_.size();
  for (size_t i = index + 1; i < panes_.size() && target == panes_.size(); ++i) {
    if (resizable(i) && !panes_[i].collapsed) target = i;
  }
  for (size_t i = index; i-- > 0 && target == panes_.size();) {
    if (resizable(i) && !panes_[i].collapsed) target = i;
  }
  if (target == panes_.size()) return false;
  double total = 0.0;
  for (size_t i = 0; i < panes_.size(); ++i) {
    if (resizable(i) && !panes_[i].collapsed) total += sizeOf(i);
  }
  snapshotWeights();
  p.restoreSize = total > 0.0 ? size / total : 0.0;
  p.restoreTo = target;
  panes_[target].weight = std::max(panes_[target].weight + size, kMinWeight);
  p.collapsed = true;
  changed();
  return true;
}

bool Splitter::expand(size_t index) {
  if (index >= panes_.size() || !panes_[index].collapsed) return false;
  double total = 0.0;
  for (size_t i = 0; i < panes_.size(); ++i) {
    if (resizable(i) && !panes_[i].collapsed) total += sizeOf(i);
  }
  const double want = std::max(panes_[index].restoreSize * total, floor_);
  // Take the space from the pane that received it, else from the nearest pane that can spare it.
  std::vector<size_t> order;
  order.push_back(panes_[index].restoreTo);
  for (size_t d = 1; d < panes_.size(); ++d) {
    if (index + d < panes_.size()) order.push_back(index + d);
    if (d <= index) order.push_back(index - d);
  }
  for (const size_t donor : order) {
    if (donor >= panes_.size() || donor == index || !resizable(donor) || panes_[donor].collapsed) continue;
    const double spare = sizeOf(donor) - effectiveMin(donor);
    if (spare < want - 1e-9 && spare < floor_) continue;
    const double take = std::min(want, spare);
    if (take <= 0.0) continue;
    snapshotWeights();
    panes_[donor].weight = std::max(panes_[donor].weight - take, kMinWeight);
    panes_[index].weight = std::max(take, kMinWeight);
    panes_[index].collapsed = false;
    changed();
    return true;
  }
  return false;
}

bool Splitter::toggleCollapse(size_t index) { return isCollapsed(index) ? expand(index) : collapse(index); }

// ---- persistence --------------------------------------------------------------------------------

SplitterState Splitter::state() const {
  SplitterState st;
  double total = 0.0;
  for (size_t i = 0; i < panes_.size(); ++i) {
    if (resizable(i) && !panes_[i].collapsed) total += sizeOf(i);
  }
  for (size_t i = 0; i < panes_.size(); ++i) {
    double ratio = 0.0;
    if (resizable(i)) {
      if (panes_[i].collapsed) ratio = panes_[i].restoreSize;
      else if (total > 0.0) ratio = sizeOf(i) / total;
      else ratio = panes_[i].weight;
    }
    st.ratios.push_back(ratio);
    st.collapsed.push_back(panes_[i].collapsed);
  }
  return st;
}

bool Splitter::restoreState(const SplitterState& state) {
  if (state.ratios.size() != panes_.size() || state.collapsed.size() != panes_.size()) return false;
  double sum = 0.0;
  for (size_t i = 0; i < panes_.size(); ++i) {
    const double r = state.ratios[i];
    if (!std::isfinite(r) || r < 0.0) return false;
    if (resizable(i) && !state.collapsed[i]) sum += r;
  }
  if (sum <= 0.0 && std::any_of(state.collapsed.begin(), state.collapsed.end(), [](bool c) { return !c; })) {
    bool anyResizable = false;
    for (size_t i = 0; i < panes_.size(); ++i) anyResizable = anyResizable || (resizable(i) && !state.collapsed[i]);
    if (anyResizable) return false;
  }
  for (size_t i = 0; i < panes_.size(); ++i) {
    Pane& p = panes_[i];
    if (!resizable(i)) continue;
    const bool collapse = state.collapsed[i] && p.options.collapsible;
    p.collapsed = collapse;
    if (collapse) {
      p.restoreSize = state.ratios[i];
      p.restoreTo = i;
    } else {
      p.weight = std::max(state.ratios[i], kMinWeight);
    }
  }
  for (size_t i = 0; i < panes_.size(); ++i) {
    if (!panes_[i].collapsed) continue;
    // A restored collapse needs a receiver for expand(): the nearest resizable, uncollapsed pane.
    size_t target = i;
    for (size_t d = 1; d < panes_.size() && target == i; ++d) {
      if (i + d < panes_.size() && resizable(i + d) && !panes_[i + d].collapsed) target = i + d;
      else if (d <= i && resizable(i - d) && !panes_[i - d].collapsed) target = i - d;
    }
    panes_[i].restoreTo = target;
  }
  changed();
  return true;
}

// ---- painting -----------------------------------------------------------------------------------

void Splitter::paint(PaintContext& ctx) {
  const size_t count = handleCount();
  for (size_t h = 0; h < count; ++h) {
    const bool active = static_cast<int>(h) == dragHandle_;
    const bool hot = static_cast<int>(h) == hover_ && dragHandle_ < 0;
    const uint8_t bits = active ? State::kActive : (hot ? State::kHover : State::kNone);
    const theme::ResolvedStyle& rs = ctx.resolve("splitter.handle", bits);
    const layout::Rect r = handleRect(h);
    render::Rect box = ctx.toPhysical(r.x, r.y, r.w, r.h);
    if (!active && !hot) {
      // Idle: a one pixel line centred in the handle.
      const float line = ctx.hairline();
      if (horizontal()) {
        box.x += (box.w - line) * 0.5f;
        box.w = line;
      } else {
        box.y += (box.h - line) * 0.5f;
        box.h = line;
      }
    }
    ctx.painter().fillRect(box, ctx.color(rs.background));
  }
}

void Splitter::paintOver(PaintContext& ctx) {
  if (!keyboard_ || !focusVisible() || handleCount() == 0) return;
  const layout::Rect r = handleRect(std::min(activeHandle_, handleCount() - 1));
  ctx.focusRing(ctx.toPhysical(r.x, r.y, r.w, r.h), 0.0f);
}

Cursor Splitter::cursor() const {
  if (dragHandle_ < 0 && hover_ < 0) return Cursor::Default;
  return horizontal() ? Cursor::ResizeHorizontal : Cursor::ResizeVertical;
}

// ---- pointer ------------------------------------------------------------------------------------

void Splitter::onPointerDown(Event& e) {
  if (e.phase == Phase::Bubble || e.button != Button::Left) return;
  const int h = handleAt(e.x, e.y);
  if (h < 0) return;
  size_t a = 0, b = 0;
  if (!neighbours(static_cast<size_t>(h), a, b)) return;
  dragHandle_ = h;
  dragStart_ = horizontal() ? e.x : e.y;
  dragBefore_ = a;
  dragAfter_ = b;
  dragSizeBefore_ = sizeOf(a);
  dragSizeAfter_ = sizeOf(b);
  snapshotWeights();  // weights = pixel sizes, so restoring them during the drag restores the panes
  dragWeights_.clear();
  dragCollapsed_.clear();
  for (const Pane& p : panes_) {
    dragWeights_.push_back(p.weight);
    dragCollapsed_.push_back(p.collapsed);
  }
  activeHandle_ = static_cast<size_t>(h);
  ui().router().capturePointer(id());
  if (keyboard_) ui().router().focus(id(), core::events::FocusReason::Pointer);
  e.stopPropagation();
  e.markHandled();
  requestPaint();
}

void Splitter::onPointerMove(Event& e) {
  if (e.phase == Phase::Bubble) return;
  if (dragHandle_ >= 0) {
    const double delta = (horizontal() ? e.x : e.y) - dragStart_;
    // Absolute from the press: the pointer coming back restores the size (spec 05 rule 13).
    for (size_t i = 0; i < panes_.size() && i < dragWeights_.size(); ++i) {
      panes_[i].weight = dragWeights_[i];
      panes_[i].collapsed = dragCollapsed_[i];
    }
    if (delta != 0.0) resizePair(dragBefore_, dragAfter_, dragSizeBefore_, dragSizeAfter_, delta);
    changed();
    e.markHandled();
    return;
  }
  const int h = handleAt(e.x, e.y);
  if (h != hover_) {
    hover_ = h;
    requestPaint();
  }
}

void Splitter::onPointerUp(Event& e) {
  if (dragHandle_ < 0 || e.button != Button::Left) return;
  dragHandle_ = -1;
  hover_ = handleAt(e.x, e.y);
  requestPaint();
}

void Splitter::onCaptureLost(Event&) {
  if (dragHandle_ < 0) return;
  dragHandle_ = -1;
  requestPaint();
}

void Splitter::onPointerLeave(Event&) {
  if (dragHandle_ >= 0 || hover_ < 0) return;
  hover_ = -1;
  requestPaint();
}

// Spec 05 rule 39: a double click on a handle does nothing (collapse is an explicit command).
void Splitter::onDoubleClick(Event&) {}

void Splitter::onStateChanged(uint16_t) {
  if (enabled()) return;
  dragHandle_ = -1;
  hover_ = -1;
}

bool Splitter::cancelDrag() {
  if (dragHandle_ < 0) return false;
  for (size_t i = 0; i < panes_.size() && i < dragWeights_.size(); ++i) {
    panes_[i].weight = dragWeights_[i];
    panes_[i].collapsed = dragCollapsed_[i];
  }
  dragHandle_ = -1;
  ui().router().releaseCapture();
  changed();
  return true;
}

// ---- keyboard -----------------------------------------------------------------------------------

void Splitter::onKeyDown(Event& e) {
  if (!keyboard_ || e.phase == Phase::Capture || handleCount() == 0) return;
  if (e.modifiers & (core::events::Mod::kCtrl | core::events::Mod::kAlt | core::events::Mod::kMeta)) return;
  activeHandle_ = std::min(activeHandle_, handleCount() - 1);
  const double step = (e.modifiers & core::events::Mod::kShift) != 0 ? kKeyStepLarge : kKeyStep;
  const Key back = horizontal() ? Key::Left : Key::Up;
  const Key forward = horizontal() ? Key::Right : Key::Down;
  bool used = false;
  if (e.key == back) {
    moveHandle(activeHandle_, -step);
    used = true;
  } else if (e.key == forward) {
    moveHandle(activeHandle_, step);
    used = true;
  } else if (e.key == Key::Home) {
    moveHandle(activeHandle_, -1.0e7);
    used = true;
  } else if (e.key == Key::End) {
    moveHandle(activeHandle_, 1.0e7);
    used = true;
  } else if (e.key == Key::PageUp && activeHandle_ > 0) {
    --activeHandle_;
    requestPaint();
    used = true;
  } else if (e.key == Key::PageDown && activeHandle_ + 1 < handleCount()) {
    ++activeHandle_;
    requestPaint();
    used = true;
  }
  if (used) e.markHandled();
}

}  // namespace r1ui::widgets

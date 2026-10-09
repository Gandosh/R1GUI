// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: DockBody and DockAreaView (see DockAreaView.h): turning a LayoutResult into strips and
//   bodies, the splitter handles (hit band, hover, capture, keyboard), painting of handles and of
//   the empty-area placeholder.
// Invariants: apply() only writes a style when it changed (so a settled layout stops requesting
//   layout passes); a handle drag is ended on every exit path; indices into handles_ are re-validated
//   after every apply because the model may have changed under a keyboard or hover state.
// Callers: DockHost (apply, queries), UiContext (events, paint).
#include "r1ui/widgets/dock/DockAreaView.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace layout = core::layout;
namespace State = theme::State;
using core::events::Button;
using core::events::Key;
using core::events::Phase;
using core::tree::WidgetId;
using theme::StyleProperty;

namespace {

constexpr theme::StyleRuleEntry kBodyRows[] = {
    {"dock.body", State::kNone, StyleProperty::Background, "color:panel"},
};

constexpr theme::StyleRuleEntry kAreaRows[] = {
    {"dock.area", State::kNone, StyleProperty::Background, "color:canvas"},
    {"dock.handle", State::kNone, StyleProperty::Background, "color:border"},
    {"dock.handle", State::kHover, StyleProperty::Background, "color:border-strong"},
    {"dock.handle", State::kActive, StyleProperty::Background, "color:accent"},
    {"dock.hint", State::kNone, StyleProperty::Foreground, "color:muted"},
    {"dock.hint", State::kNone, StyleProperty::BorderColor, "color:border-strong"},
    {"dock.hint", State::kNone, StyleProperty::FontSize, "fontSize:sm"},
    {"dock.hint", State::kNone, StyleProperty::LineHeight, "number:20"},
    {"dock.hint", State::kNone, StyleProperty::Radius, "radius:lg"},
    {"dock.hint.title", State::kNone, StyleProperty::Foreground, "color:surface"},
    {"dock.hint.title", State::kNone, StyleProperty::FontSize, "fontSize:sm"},
    {"dock.hint.title", State::kNone, StyleProperty::FontWeight, "weight:semibold"},
    {"dock.hint.title", State::kNone, StyleProperty::LineHeight, "number:20"},
};

// Writes an absolute rectangle into a style; returns true when anything changed.
bool placeAbsolute(layout::Style& s, double x, double y, double w, double h) {
  const layout::Style before = s;
  s.position = layout::Position::Absolute;
  s.inset[layout::kLeft] = layout::Length::px(x);
  s.inset[layout::kTop] = layout::Length::px(y);
  s.inset[layout::kRight] = layout::Length::autoValue();
  s.inset[layout::kBottom] = layout::Length::autoValue();
  s.width = layout::Length::px(std::max(0.0, w));
  s.height = layout::Length::px(std::max(0.0, h));
  s.flexShrink = 0.0;
  return !(before.inset[layout::kLeft] == s.inset[layout::kLeft] && before.inset[layout::kTop] == s.inset[layout::kTop] &&
           before.width == s.width && before.height == s.height && before.position == s.position);
}

}  // namespace

// ---- DockBody -------------------------------------------------------------------------------

std::span<const theme::StyleRuleEntry> DockBody::styleRows() { return kBodyRows; }

void DockBody::onAttached() {
  layout::Style& s = style();
  s.position = layout::Position::Absolute;
  s.overflow = layout::Overflow::Hidden;
  s.direction = layout::FlexDirection::Column;
  s.alignItems = layout::Align::Stretch;
}

void DockBody::paint(PaintContext& ctx) { ctx.painter().fillRect(ctx.box(), ctx.color(ctx.resolve("dock.body", 0).background)); }

uint8_t DockBody::phases() const { return core::events::kListenCapture | core::events::kListenTarget | core::events::kListenBubble; }

void DockBody::onPointerDown(Event& e) {
  // Seen in the capture phase so the content still gets the press; only activation happens here.
  if (e.phase == Phase::Capture && host_ != nullptr && front_ != 0) host_->regionPressed(front_);
}

// ---- DockAreaView: lifecycle ----------------------------------------------------------------------

std::span<const theme::StyleRuleEntry> DockAreaView::styleRows() { return kAreaRows; }

void DockAreaView::onAttached() {
  layout::Style& s = style();
  s.position = layout::Position::Absolute;
  for (int e = 0; e < 4; ++e) s.inset[e] = layout::Length::px(0);
  node().flags.clipsChildren = true;
}

void DockAreaView::onDetached() {
  if (dragHandle_ >= 0 && host_ != nullptr) host_->handleDragEnd(false);
  dragHandle_ = -1;
}

uint8_t DockAreaView::phases() const { return core::events::kListenCapture | core::events::kListenTarget | core::events::kListenBubble; }

// ---- apply ----------------------------------------------------------------------------------

void DockAreaView::apply(const dock::LayoutResult& result) {
  const dock::AreaLayout* area = nullptr;
  for (const dock::AreaLayout& a : result.areas) {
    if (a.id == area_) area = &a;
  }
  std::vector<const dock::StackLayout*> mine;
  if (area != nullptr) {
    for (const dock::StackLayout& s : result.stacks) {
      if (s.area == area_) mine.push_back(&s);
    }
  }
  emptyHint_ = area != nullptr && area->empty;
  if (area != nullptr) origin_ = {area->bounds.x, area->bounds.y};
  if (emptyHint_ != wasEmpty_) {
    wasEmpty_ = emptyHint_;
    requestPaint();
  }

  while (units_.size() > mine.size()) {
    host_->releaseBody(ui(), units_.back().body);
    ui().destroy(units_.back().strip);
    ui().destroy(units_.back().body);
    units_.pop_back();
  }
  while (units_.size() < mine.size()) {
    Unit u;
    u.strip = ui().create<DockTabStrip>(id()).id();
    u.body = ui().create<DockBody>(id()).id();
    units_.push_back(std::move(u));
  }

  const dock::DockConfig& cfg = host_->dockConfig();
  const dock::PanelId lifted = host_->liftedPanel();
  for (size_t k = 0; k < mine.size(); ++k) {
    const dock::StackLayout& s = *mine[k];
    Unit& u = units_[k];
    DockTabStrip* strip = ui().objectAs<DockTabStrip>(u.strip);
    DockBody* body = ui().objectAs<DockBody>(u.body);
    if (strip == nullptr || body == nullptr) continue;

    std::vector<DockTabInfo> infos;
    infos.reserve(s.tabs.size());
    size_t front = 0;
    for (size_t i = 0; i < s.tabs.size(); ++i) {
      infos.push_back(host_->tabInfo(s.tabs[i].panel));
      if (s.tabs[i].active) front = i;
    }
    // Rule 15: while the front tab is being dragged the region shows its right neighbour, else left.
    size_t shown = front;
    if (infos[front].panel == lifted && infos.size() > 1) shown = front + 1 < infos.size() ? front + 1 : front - 1;
    const dock::PanelId frontPanel = infos[shown].panel == lifted ? 0 : infos[shown].panel;

    const bool appPage = s.strip.h > cfg.tabStripHeight + 0.5;
    StripMetrics metrics;
    metrics.height = s.strip.h;
    metrics.minTabWidth = cfg.minTabWidth;
    metrics.maxTabWidth = appPage ? cfg.appPageMaxTabWidth : cfg.maxTabWidth;
    strip->bind(host_, area_, window_);
    strip->setTabs(std::move(infos), shown, metrics);
    strip->setLifted(lifted);
    body->bind(host_);
    body->setFront(frontPanel);

    bool changed = placeAbsolute(strip->style(), s.strip.x - origin_.x, s.strip.y - origin_.y, s.strip.w, s.strip.h);
    changed = placeAbsolute(body->style(), s.body.x - origin_.x, s.body.y - origin_.y, s.body.w, s.body.h) || changed;
    if (changed) {
      strip->requestLayout();
      body->requestLayout();
    }
    ui().invalidator().setVisible(u.strip, !s.collapsed);
    ui().invalidator().setVisible(u.body, !s.collapsed);

    u.path = s.path;
    u.front = frontPanel;
    u.bodyRect = s.body;
    u.stripRect = s.strip;
    for (const dock::TabLayout& t : s.tabs) host_->mountContent(t.panel, ui(), u.body, t.panel == frontPanel);
  }

  handles_.clear();
  if (area != nullptr) {
    for (const dock::HandleLayout& h : result.handles) {
      if (h.handle.area == area_) handles_.push_back(h);
    }
  }
  if (keyHandle_ >= handles_.size()) keyHandle_ = 0;
  if (hoverHandle_ >= static_cast<int>(handles_.size())) hoverHandle_ = -1;
  if (dragHandle_ >= static_cast<int>(handles_.size())) dragHandle_ = -1;
  setFocusable(!handles_.empty());
  requestPaint();
}

// ---- lookups --------------------------------------------------------------------------------

DockTabStrip* DockAreaView::stripOf(const Unit& unit) const { return ui().objectAs<DockTabStrip>(unit.strip); }

const DockAreaView::Unit* DockAreaView::unitHolding(dock::PanelId panel) const {
  for (const Unit& u : units_) {
    const DockTabStrip* strip = stripOf(u);
    if (strip != nullptr && strip->indexOf(panel)) return &u;
  }
  return nullptr;
}

const DockAreaView::Unit* DockAreaView::unitFront(dock::PanelId panel) const {
  for (const Unit& u : units_) {
    if (u.front == panel) return &u;
  }
  return nullptr;
}

const DockAreaView::Unit* DockAreaView::unitStripAt(double x, double y) const {
  for (const Unit& u : units_) {
    const dock::Rect r = toDockRect(ui().absRect(u.strip));
    if (x >= r.x && x < r.right() && y >= r.y && y < r.bottom()) return &u;
  }
  return nullptr;
}

// ---- handles --------------------------------------------------------------------------------

dock::Rect DockAreaView::handleLocalRect(const dock::HandleLayout& h) const {
  const dock::Rect me = toDockRect(ui().absRect(id()));
  return {me.x + (h.rect.x - origin_.x), me.y + (h.rect.y - origin_.y), h.rect.w, h.rect.h};
}

DockAreaView::HandleHit DockAreaView::hitHandle(double x, double y) const {
  const double band = host_->handleHitBand();
  for (size_t i = 0; i < handles_.size(); ++i) {
    dock::Rect r = handleLocalRect(handles_[i]);
    const bool vertical = handles_[i].handle.axis == dock::Axis::Row;  // a Row split has vertical bars
    const double extra = std::max(0.0, (band - (vertical ? r.w : r.h)) / 2.0);
    if (vertical) {
      r.x -= extra;
      r.w += 2.0 * extra;
    } else {
      r.y -= extra;
      r.h += 2.0 * extra;
    }
    if (x >= r.x && x < r.right() && y >= r.y && y < r.bottom()) return {static_cast<int>(i)};
  }
  return {};
}

void DockAreaView::setHover(int index) {
  if (hoverHandle_ == index) return;
  hoverHandle_ = index;
  requestPaint();
}

void DockAreaView::onPointerDown(Event& e) {
  if (e.button != Button::Left || e.phase == Phase::Bubble || dragHandle_ >= 0) return;
  const HandleHit hit = hitHandle(e.x, e.y);
  if (hit.index < 0) return;
  const dock::Point screen = host_->screenPoint(window_, {e.x, e.y});
  const WidgetId self = id();
  if (!host_->handleDragBegin(dragOf(handles_[static_cast<size_t>(hit.index)]), screen)) return;
  if (!ui().alive(self)) return;
  dragHandle_ = hit.index;
  keyHandle_ = static_cast<size_t>(hit.index);
  ui().router().capturePointer(self);
  ui().router().focus(self, core::events::FocusReason::Pointer);
  e.stopPropagation();
  e.markHandled();
  requestPaint();
}

void DockAreaView::onPointerMove(Event& e) {
  if (dragHandle_ >= 0) {
    if (e.phase == Phase::Capture) return;
    e.markHandled();
    host_->handleDragMove(host_->screenPoint(window_, {e.x, e.y}));
    return;
  }
  if (e.phase == Phase::Bubble) return;
  setHover(hitHandle(e.x, e.y).index);
}

void DockAreaView::onPointerUp(Event& e) {
  if (dragHandle_ < 0 || e.button != Button::Left || e.phase == Phase::Capture) return;
  dragHandle_ = -1;
  e.markHandled();
  requestPaint();
  host_->handleDragEnd(true);
}

void DockAreaView::onPointerLeave(Event&) {
  if (dragHandle_ < 0) setHover(-1);
}

void DockAreaView::onCaptureLost(Event&) {
  if (dragHandle_ < 0) return;
  dragHandle_ = -1;
  requestPaint();
  host_->handleDragEnd(true);
}

Cursor DockAreaView::cursor() const {
  const int index = dragHandle_ >= 0 ? dragHandle_ : hoverHandle_;
  if (index < 0 || index >= static_cast<int>(handles_.size())) return Cursor::Default;
  return handles_[static_cast<size_t>(index)].handle.axis == dock::Axis::Row ? Cursor::ResizeHorizontal : Cursor::ResizeVertical;
}

void DockAreaView::onKeyDown(Event& e) {
  if (e.target != id() || handles_.empty()) return;
  if (e.key == Key::Escape && dragHandle_ >= 0) {
    dragHandle_ = -1;
    e.markHandled();
    ui().router().releaseCapture();
    requestPaint();
    host_->handleDragEnd(false);
    return;
  }
  if ((e.modifiers & (core::events::Mod::kCtrl | core::events::Mod::kAlt | core::events::Mod::kMeta)) != 0) return;
  keyHandle_ = std::min(keyHandle_, handles_.size() - 1);
  const dock::HandleLayout& h = handles_[keyHandle_];
  const bool vertical = h.handle.axis == dock::Axis::Row;
  const double step = host_->dockConfig().keyboardResizeStep * ((e.modifiers & core::events::Mod::kShift) != 0 ? 5.0 : 1.0);
  double delta = 0.0;
  switch (e.key) {
    case Key::Left: delta = vertical ? -step : 0.0; break;
    case Key::Right: delta = vertical ? step : 0.0; break;
    case Key::Up: delta = vertical ? 0.0 : -step; break;
    case Key::Down: delta = vertical ? 0.0 : step; break;
    case Key::Home: delta = -dock::kMaxCoordinate; break;
    case Key::End: delta = dock::kMaxCoordinate; break;
    case Key::PageUp:
      keyHandle_ = keyHandle_ > 0 ? keyHandle_ - 1 : keyHandle_;
      e.markHandled();
      requestPaint();
      return;
    case Key::PageDown:
      keyHandle_ = std::min(keyHandle_ + 1, handles_.size() - 1);
      e.markHandled();
      requestPaint();
      return;
    default: return;
  }
  if (delta == 0.0) return;
  e.markHandled();
  host_->handleNudge(dragOf(h), delta);
}

// ---- paint ----------------------------------------------------------------------------------

void DockAreaView::paint(PaintContext& ctx) {
  render::Painter& painter = ctx.painter();
  painter.fillRect(ctx.box(), ctx.color(ctx.resolve("dock.area", 0).background));
  for (size_t i = 0; i < handles_.size(); ++i) {
    const bool active = static_cast<int>(i) == dragHandle_;
    const bool hot = static_cast<int>(i) == hoverHandle_ && dragHandle_ < 0;
    const theme::ResolvedStyle& rs = ctx.resolve("dock.handle", active ? State::kActive : (hot ? State::kHover : State::kNone));
    const dock::Rect r = handleLocalRect(handles_[i]);
    render::Rect box = ctx.toPhysical(r.x, r.y, r.w, r.h);
    if (!active && !hot) {  // idle: a one pixel line centred in the handle
      const float line = ctx.hairline();
      if (handles_[i].handle.axis == dock::Axis::Row) {
        box.x += (box.w - line) * 0.5f;
        box.w = line;
      } else {
        box.y += (box.h - line) * 0.5f;
        box.h = line;
      }
    }
    painter.fillRect(box, ctx.color(rs.background));
  }
  if (!emptyHint_) return;

  // The area-empty placeholder (spec 02 rule 48): a framed card with the recovery options.
  const dock::Rect me = toDockRect(ctx.rect());
  const double w = std::min(420.0, std::max(0.0, me.w - 32.0));
  const double h = 96.0;
  if (w < 120.0 || me.h < h + 16.0) return;
  const dock::Rect card{me.x + (me.w - w) / 2.0, me.y + (me.h - h) / 2.0, w, h};
  const theme::ResolvedStyle& hint = ctx.resolve("dock.hint", 0);
  painter.border(ctx.toPhysical(card.x, card.y, card.w, card.h), render::CornerRadii::uniform(ctx.px(hint.radius)), ctx.hairline(),
                 ctx.color(hint.border.color));
  const theme::ResolvedStyle& title = ctx.resolve("dock.hint.title", 0);
  TextOptions center;
  center.align = TextAlign::Center;
  center.padLeft = 12.0;
  center.padRight = 12.0;
  ctx.drawText("No panels are open", title.text, ctx.toPhysical(card.x, card.y + 12.0, card.w, 20.0), center);
  ctx.drawText("Open a panel from the Window menu,", hint.text, ctx.toPhysical(card.x, card.y + 36.0, card.w, 20.0), center);
  ctx.drawText("drag one back from a floating window, or reset the layout.", hint.text, ctx.toPhysical(card.x, card.y + 56.0, card.w, 20.0), center);
}

void DockAreaView::paintOver(PaintContext& ctx) {
  if (!focusVisible() || handles_.empty() || !focused()) return;
  const dock::Rect r = handleLocalRect(handles_[std::min(keyHandle_, handles_.size() - 1)]);
  ctx.focusRing(ctx.toPhysical(r.x, r.y, r.w, r.h), 0.0f);
}

}  // namespace r1ui::widgets

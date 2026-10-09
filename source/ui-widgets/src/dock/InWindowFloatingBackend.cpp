// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: InWindowFloatingBackend (see its header for the contract it implements): the window table,
//   the rectangle rules (minimum size, the title bar stays reachable, maximize), stacking by layer
//   value, and the translation of frame gestures into listener calls.
// Invariants: windows_ is ordered bottom to top and frame layers are 100 + index (below the overlay
//   layer at 10000); a window's content rectangle is always finite, at least its minimum size and
//   positioned so its title bar can be grabbed; listener calls are made after the backend's own state
//   is consistent and never touch a Window reference afterwards (the listener may destroy windows).
// Callers: DockHost (IFloatingBackend), FloatingFrame (gestures), tests.
#include "r1ui/widgets/dock/InWindowFloatingBackend.h"

#include <algorithm>
#include <cmath>

#include "FloatingFrame.h"
#include "r1ui/widgets/dock/DockInteraction.h"

namespace r1ui::widgets {

namespace layout = core::layout;
using core::tree::WidgetId;

namespace {

constexpr int32_t kFirstLayer = 100;

bool finiteRect(const dock::Rect& r) { return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.w) && std::isfinite(r.h); }

}  // namespace

InWindowFloatingBackend::InWindowFloatingBackend(UiContext& ui, WidgetId layer, InWindowFloatingOptions options)
    : ui_(ui), layer_(layer), options_(options) {}

BackendInfo InWindowFloatingBackend::describe() const {
  BackendInfo info;
  info.name = "in-window";
  info.frame = {options_.border, options_.titleHeight, options_.border, options_.border};
  return info;
}

void InWindowFloatingBackend::setMainContent(UiContext&, WidgetId contentWidget) { main_ = contentWidget; }

dock::Rect InWindowFloatingBackend::mainContentRect() const {
  if (!main_.valid() || !ui_.alive(main_)) return {};
  const dock::Rect r = toDockRect(ui_.absRect(main_));
  return {r.x, r.y, r.w, r.h};
}

InWindowFloatingBackend::Window* InWindowFloatingBackend::find(FloatId window) {
  for (Window& w : windows_) {
    if (w.id == window) return &w;
  }
  return nullptr;
}

const InWindowFloatingBackend::Window* InWindowFloatingBackend::find(FloatId window) const {
  for (const Window& w : windows_) {
    if (w.id == window) return &w;
  }
  return nullptr;
}

WidgetId InWindowFloatingBackend::frameWidget(FloatId window) const {
  const Window* w = find(window);
  return w != nullptr ? w->frame : WidgetId{};
}

// ---- rectangle rules ------------------------------------------------------------------------------

dock::Rect InWindowFloatingBackend::maximizedRect() const {
  const double vw = ui_.viewportWidth();
  const double vh = ui_.viewportHeight();
  return {options_.border, options_.titleHeight, std::max(0.0, vw - 2.0 * options_.border), std::max(0.0, vh - options_.titleHeight - options_.border)};
}

dock::Rect InWindowFloatingBackend::clampRect(const Window& w, dock::Rect rect) const {
  if (!finiteRect(rect)) return w.content;
  const double vw = ui_.viewportWidth();
  const double vh = ui_.viewportHeight();
  rect.w = std::clamp(rect.w, w.minSize.x, dock::kMaxCoordinate);
  rect.h = std::clamp(rect.h, w.minSize.y, dock::kMaxCoordinate);
  if (vw > 0.0 && vh > 0.0) {
    // A window never needs to be larger than the host window, and its title bar stays reachable.
    rect.w = std::max(w.minSize.x, std::min(rect.w, vw - 2.0 * options_.border));
    rect.h = std::max(w.minSize.y, std::min(rect.h, vh - options_.titleHeight - options_.border));
    const double keep = std::min(options_.minVisibleTitle, rect.w);
    rect.x = std::clamp(rect.x, keep - rect.w, vw - keep);
    rect.y = std::clamp(rect.y, options_.titleHeight, std::max(options_.titleHeight, vh - keep));
  }
  return rect;
}

void InWindowFloatingBackend::apply(Window& w) {
  const dock::Rect origin = toDockRect(ui_.absRect(layer_));
  const double b = options_.border;
  const auto place = [](layout::Style& s, double x, double y, double width, double height) {
    s.position = layout::Position::Absolute;
    s.inset[layout::kLeft] = layout::Length::px(x);
    s.inset[layout::kTop] = layout::Length::px(y);
    s.inset[layout::kRight] = layout::Length::autoValue();
    s.inset[layout::kBottom] = layout::Length::autoValue();
    s.width = layout::Length::px(std::max(0.0, width));
    s.height = layout::Length::px(std::max(0.0, height));
  };
  if (WidgetObject* frame = ui_.object(w.frame)) {
    place(frame->style(), w.content.x - b - origin.x, w.content.y - options_.titleHeight - origin.y, w.content.w + 2.0 * b,
          w.content.h + options_.titleHeight + b);
    frame->requestLayout();
  }
  if (WidgetObject* holder = ui_.object(w.holder)) {
    place(holder->style(), b, options_.titleHeight, w.content.w, w.content.h);
    holder->requestLayout();
  }
  if (FloatingFrame* frame = ui_.objectAs<FloatingFrame>(w.frame)) {
    frame->setHidden(!w.visible);
    frame->setTitle(w.title);
    frame->setMaximized(w.maximized);
    frame->setResizable(w.resizable);
  }
}

void InWindowFloatingBackend::relayer() {
  for (size_t i = 0; i < windows_.size(); ++i) {
    if (core::tree::Widget* node = ui_.tree().get(windows_[i].frame)) node->layer = kFirstLayer + static_cast<int32_t>(i);
    if (WidgetObject* frame = ui_.object(windows_[i].frame)) frame->requestPaint();
  }
}

// ---- IFloatingBackend -----------------------------------------------------------------------------

FloatCreateResult InWindowFloatingBackend::createWindow(const FloatRequest& request) {
  FloatCreateResult result;
  if (windows_.size() >= options_.maxWindows) {
    result.error = "too many floating windows";
    return result;
  }
  if (!finiteRect(request.contentRect) || !std::isfinite(request.minContentSize.x) || !std::isfinite(request.minContentSize.y)) {
    result.error = "the window rectangle is not finite";
    return result;
  }
  Window w;
  w.id = nextId_++;
  w.title = request.title;
  w.minSize = {std::clamp(request.minContentSize.x, 1.0, dock::kMaxCoordinate), std::clamp(request.minContentSize.y, 1.0, dock::kMaxCoordinate)};
  w.resizable = request.resizable;
  w.content = {request.contentRect.x, request.contentRect.y, std::max(request.contentRect.w, w.minSize.x), std::max(request.contentRect.h, w.minSize.y)};
  w.content = clampRect(w, w.content);
  try {
    w.frame = ui_.create<FloatingFrame>(layer_, *this, w.id, options_.titleHeight, options_.border, options_.radius, options_.resizeBand).id();
    w.holder = ui_.create<FloatingHolder>(w.frame).id();
  } catch (const std::exception& ex) {
    if (ui_.alive(w.frame)) ui_.destroy(w.frame);
    result.error = std::string("cannot create the window: ") + ex.what();
    return result;
  }
  windows_.push_back(std::move(w));
  apply(windows_.back());
  relayer();
  result.ok = true;
  result.id = windows_.back().id;
  return result;
}

bool InWindowFloatingBackend::destroyWindow(FloatId window) {
  for (auto it = windows_.begin(); it != windows_.end(); ++it) {
    if (it->id != window) continue;
    const WidgetId frame = it->frame;
    windows_.erase(it);
    if (ui_.alive(frame)) ui_.destroy(frame);
    relayer();
    return true;
  }
  return false;
}

bool InWindowFloatingBackend::setContentRect(FloatId window, const dock::Rect& contentRect) {
  Window* w = find(window);
  if (w == nullptr) return false;
  w->maximized = false;
  w->content = clampRect(*w, contentRect);
  apply(*w);
  return true;
}

std::optional<dock::Rect> InWindowFloatingBackend::contentRect(FloatId window) const {
  const Window* w = find(window);
  if (w == nullptr) return std::nullopt;
  return w->content;
}

std::optional<FloatContent> InWindowFloatingBackend::content(FloatId window) const {
  if (window == kMainWindow) {
    if (!main_.valid()) return std::nullopt;
    return FloatContent{&ui_, main_, ui_.scale()};
  }
  const Window* w = find(window);
  if (w == nullptr) return std::nullopt;
  return FloatContent{&ui_, w->holder, ui_.scale()};
}

bool InWindowFloatingBackend::bringToFront(FloatId window) {
  for (auto it = windows_.begin(); it != windows_.end(); ++it) {
    if (it->id != window) continue;
    std::rotate(it, it + 1, windows_.end());
    relayer();
    return true;
  }
  return false;
}

std::vector<FloatId> InWindowFloatingBackend::stacking() const {
  std::vector<FloatId> out;
  out.reserve(windows_.size());
  for (const Window& w : windows_) out.push_back(w.id);
  return out;
}

bool InWindowFloatingBackend::setTitle(FloatId window, std::string_view title) {
  Window* w = find(window);
  if (w == nullptr) return false;
  w->title = std::string(title);
  if (FloatingFrame* frame = ui_.objectAs<FloatingFrame>(w->frame)) frame->setTitle(w->title);
  return true;
}

bool InWindowFloatingBackend::setMaximized(FloatId window, bool maximized) {
  Window* w = find(window);
  if (w == nullptr) return false;
  if (w->maximized == maximized) return true;
  if (maximized) {
    w->restore = w->content;
    w->maximized = true;
    w->content = maximizedRect();
  } else {
    w->maximized = false;
    w->content = clampRect(*w, w->restore);
  }
  apply(*w);
  return true;
}

bool InWindowFloatingBackend::isMaximized(FloatId window) const {
  const Window* w = find(window);
  return w != nullptr && w->maximized;
}

bool InWindowFloatingBackend::setVisible(FloatId window, bool visible) {
  Window* w = find(window);
  if (w == nullptr) return false;
  w->visible = visible;
  if (FloatingFrame* frame = ui_.objectAs<FloatingFrame>(w->frame)) frame->setHidden(!visible);
  return true;
}

std::optional<FloatId> InWindowFloatingBackend::topmostWindowAt(dock::Point screen, std::span<const FloatId> exclude) const {
  const auto excluded = [&](FloatId id) { return std::find(exclude.begin(), exclude.end(), id) != exclude.end(); };
  const double b = options_.border;
  for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
    if (!it->visible || excluded(it->id)) continue;
    const dock::Rect outer{it->content.x - b, it->content.y - options_.titleHeight, it->content.w + 2.0 * b, it->content.h + options_.titleHeight + b};
    if (outer.contains(screen)) return it->id;
  }
  if (!excluded(kMainWindow) && mainContentRect().contains(screen)) return kMainWindow;
  return std::nullopt;
}

// ---- gestures ---------------------------------------------------------------------------------------

void InWindowFloatingBackend::userChangedRect(FloatId window, const dock::Rect& wanted) {
  Window* w = find(window);
  if (w == nullptr) return;
  const dock::Rect next = clampRect(*w, wanted);
  if (next == w->content) return;
  w->content = next;
  w->maximized = false;
  apply(*w);
  if (listener_ != nullptr) listener_->onFloatMoved(window, next);
}

void InWindowFloatingBackend::userPressed(FloatId window) {
  if (windows_.empty() || find(window) == nullptr) return;
  const bool wasTop = windows_.back().id == window;
  bringToFront(window);
  if (!wasTop && listener_ != nullptr) listener_->onFloatActivated(window);
}

void InWindowFloatingBackend::userRequestedClose(FloatId window) {
  if (find(window) != nullptr && listener_ != nullptr) listener_->onFloatCloseRequested(window);
}

void InWindowFloatingBackend::userToggledMaximize(FloatId window) {
  Window* w = find(window);
  if (w == nullptr) return;
  const bool now = !w->maximized;
  setMaximized(window, now);
  if (listener_ == nullptr) return;
  const dock::Rect rect = find(window)->content;
  listener_->onFloatMaximizedChanged(window, now);
  if (find(window) != nullptr) listener_->onFloatMoved(window, rect);
}

}  // namespace r1ui::widgets

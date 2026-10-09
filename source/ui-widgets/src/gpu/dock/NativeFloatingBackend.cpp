// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: NativeFloatingBackend's contract surface (see its header): the window table, creation and
//   destruction, rectangle rules (limits, reachability, maximize), stacking, coordinates, the drop
//   target query and pointer tracking.
// Invariants: every window the host can see is in table_; a destroyed or lost window is out of the
//   table at once and its object waits in the parking list; the stored content rectangle of a window
//   is what the OS shows (read back after every change), which is what makes the echo rule cheap: an
//   event that finds the stored state unchanged is the host's own doing and reports nothing. Listener
//   calls happen with no iterator or reference into the table held (the listener may destroy windows).
// Callers: DockHost (IFloatingBackend), AppLoop and tests (the loop side, NativeBackendEvents.cpp).
#include "r1ui/widgets/dock/native/NativeFloatingBackend.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "NativeWin32.h"
#include "NativeWindow.h"
#include "r1ui/platform/Dpi.h"
#include "r1ui/widgets/dock/DockInteraction.h"

namespace r1ui::widgets {

using native::NativeWindow;

namespace {

bool finiteRect(const dock::Rect& r) { return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.w) && std::isfinite(r.h); }

}  // namespace

NativeFloatingBackend::NativeFloatingBackend(platform::Window& mainWindow, render::RenderDevice& device, Services& services, NativeBackendOptions options)
    : main_(mainWindow), device_(device), services_(services), options_(std::move(options)) {
  refreshMonitors();
}

NativeFloatingBackend::~NativeFloatingBackend() {
  track_ = {};
  listener_ = nullptr;
}

BackendInfo NativeFloatingBackend::describe() const {
  BackendInfo info;
  info.name = "native";
  info.nativeWindows = true;
  info.pointerTracking = true;
  info.maximize = true;
  info.frame = options_.frame.insets();
  return info;
}

// ---- lookups ------------------------------------------------------------------------------------

NativeWindow* NativeFloatingBackend::find(FloatId window) { return window == kMainWindow ? nullptr : table_.find(window); }
const NativeWindow* NativeFloatingBackend::find(FloatId window) const { return window == kMainWindow ? nullptr : table_.find(window); }

size_t NativeFloatingBackend::windowCount() const { return table_.size(); }
size_t NativeFloatingBackend::parkedCount() const { return table_.parkedCount(); }
void NativeFloatingBackend::collectGarbage() { table_.sweep(); }

platform::Window* NativeFloatingBackend::nativeWindow(FloatId window) {
  NativeWindow* w = find(window);
  return w != nullptr ? &w->window() : nullptr;
}

uint64_t NativeFloatingBackend::framesPresented(FloatId window) const {
  const NativeWindow* w = find(window);
  return w != nullptr ? w->framesPresented() : 0;
}

void NativeFloatingBackend::refreshMonitors() { space_ = native::ScreenSpace(platform::enumerateMonitors()); }

// ---- geometry helpers -----------------------------------------------------------------------------

dock::Point NativeFloatingBackend::mainOrigin() const {
  const platform::Point o = main_.clientOrigin();
  return space_.toLogical(o.x, o.y);
}

dock::Point NativeFloatingBackend::clientOrigin(const NativeWindow& w) const {
  const platform::Point o = w.window().clientOrigin();
  return space_.toLogical(o.x, o.y);
}

// The content rectangle the OS currently shows. A minimized window reports a 0x0 client area, in
// which case the last known rectangle stays.
dock::Rect NativeFloatingBackend::readContent(const NativeWindow& w) const {
  if (w.window().clientWidth() <= 0 || w.window().clientHeight() <= 0 || !w.window().isAlive()) return w.content;
  const platform::Point o = w.window().clientOrigin();
  const double s = w.scale();
  const native::FrameMetrics& f = w.frameMetrics();
  const dock::Point origin = space_.toLogical(static_cast<double>(o.x) + f.border * s, static_cast<double>(o.y) + f.titleHeight * s);
  const dock::Point client = w.clientLogical();
  return {origin.x, origin.y, client.x - 2.0 * f.border, client.y - f.titleHeight - f.border};
}

// No window needs to be larger than the largest work area; without monitors (no desktop) only the
// coordinate limit applies.
dock::Point NativeFloatingBackend::maxContentSize() const {
  const dock::Point work = space_.largestWorkAreaSize();
  const native::FrameMetrics& f = options_.frame;
  if (work.x <= 0.0 || work.y <= 0.0) return {dock::kMaxCoordinate, dock::kMaxCoordinate};
  return {work.x - 2.0 * f.border, work.y - f.titleHeight - f.border};
}

// ---- main window ------------------------------------------------------------------------------------

void NativeFloatingBackend::setMainContent(UiContext& ui, core::tree::WidgetId contentWidget) {
  mainUi_ = contentWidget.valid() ? &ui : nullptr;
  mainWidget_ = contentWidget;
  if (!contentWidget.valid()) lastMain_ = {};
}

dock::Rect NativeFloatingBackend::mainContentRect() const {
  if (mainUi_ == nullptr || !mainWidget_.valid() || !mainUi_->alive(mainWidget_)) return {};
  // A minimized window has no client area; the last rectangle stays so layouts do not collapse.
  if (main_.isMinimized() || main_.clientWidth() <= 0) return lastMain_;
  const dock::Rect r = toDockRect(mainUi_->absRect(mainWidget_));
  const dock::Point o = mainOrigin();
  lastMain_ = {o.x + r.x, o.y + r.y, r.w, r.h};
  return lastMain_;
}

// ---- creation and destruction -------------------------------------------------------------------------

FloatCreateResult NativeFloatingBackend::createWindow(const FloatRequest& request) {
  FloatCreateResult result;
  if (table_.size() >= options_.maxWindows) {
    result.error = "too many floating windows";
    return result;
  }
  if (!finiteRect(request.contentRect) || !std::isfinite(request.minContentSize.x) || !std::isfinite(request.minContentSize.y)) {
    result.error = "the window rectangle is not finite";
    return result;
  }
  refreshMonitors();
  const native::FrameMetrics& f = options_.frame;
  const dock::Point minimum{std::clamp(request.minContentSize.x, 1.0, dock::kMaxCoordinate), std::clamp(request.minContentSize.y, 1.0, dock::kMaxCoordinate)};
  const dock::Rect content = native::limitContentSize(request.contentRect, minimum, maxContentSize());

  int monitor = -1;
  double s = 1.0;
  const platform::Rect outer = outerPhysical(content, -1, monitor, s);

  native::NativeWindowInit init;
  init.id = table_.nextId();
  init.request = &request;
  init.owner = &main_;
  init.device = &device_;
  init.services = &services_;
  init.frame = f;
  init.outerPosition = {outer.x, outer.y};
  init.contentSize = {static_cast<double>(outer.width) / s - 2.0 * f.border, static_cast<double>(outer.height) / s - f.titleHeight - f.border};
  init.minContent = minimum;
  std::unique_ptr<NativeWindow> window;
  try {
    window = std::make_unique<NativeWindow>(init);
  } catch (const std::exception& ex) {
    result.error = std::string("cannot create the window: ") + ex.what();
    return result;
  }
  window->content = readContent(*window);
  if (live_) window->window().setLiveCallback(live_);
  result.id = table_.add(std::move(window));
  result.ok = true;
  return result;
}

bool NativeFloatingBackend::destroyWindow(FloatId window) {
  NativeWindow* w = find(window);
  if (w == nullptr) return false;
  // Gone from the screen at once; the object waits for collectGarbage(): this call may come from one
  // of the window's own event handlers.
  w->window().setVisible(false);
  table_.retire(window);
  return true;
}

// ---- rectangle rules ---------------------------------------------------------------------------------

// The outer window rectangle (physical px) for a content rectangle: the monitor showing its top-left
// decides the scale and the origin (`hint` wins where logical ranges overlap), the size is the logical
// size times that scale, and the 100 px reachability rule is applied. `monitor` and `scale` report what
// was used.
platform::Rect NativeFloatingBackend::outerPhysical(const dock::Rect& content, int hint, int& monitor, double& scale) const {
  const native::FrameMetrics& f = options_.frame;
  const dock::Point topLeft{content.x, content.y};
  monitor = space_.monitorOfLogical(topLeft, hint);
  scale = space_.scaleOf(monitor);
  const dock::Point origin = space_.toPhysical(topLeft, monitor);
  const auto round = [](double v) { return static_cast<int>(std::lround(std::clamp(v, -1.0e6, 1.0e6))); };
  const platform::Rect outer{round(origin.x - f.border * scale), round(origin.y - f.titleHeight * scale), std::max(1, round((content.w + 2.0 * f.border) * scale)),
                             std::max(1, round((content.h + f.titleHeight + f.border) * scale))};
  return platform::clampToVisible(outer, space_.monitors(), options_.visibleMarginLogical);
}

// Moves and sizes the window to `content` (already finite). The OS may hand over to another DPI while
// SetWindowPos runs (the platform applies the suggested rectangle at once), so the placement repeats
// once with the new scale until a pass changes no DPI: that pass is exact.
void NativeFloatingBackend::place(NativeWindow& w, dock::Rect content) {
  content = native::limitContentSize(content, w.minContent, maxContentSize());
  if (w.maximized) {
    w.maximized = false;
    if (w.window().isMaximized()) w.window().maximizeToggle();
  }
  const platform::Rect now = w.window().windowRect();
  int hint = space_.monitorOfPhysical(now.x + now.width / 2.0, now.y + now.height / 2.0);
  const float dpiBefore = w.window().dpiScale();
  for (int pass = 0; pass < 3; ++pass) {
    int monitor = -1;
    double s = 1.0;
    const platform::Rect outer = outerPhysical(content, hint, monitor, s);
    const float dpiPass = w.window().dpiScale();
    w.window().setWindowRect(outer);
    hint = monitor;
    if (w.window().dpiScale() == dpiPass) break;
  }
  // The DPI event the move produced belongs to the host's call, not to the user.
  w.expectedScale = w.window().dpiScale() != dpiBefore ? static_cast<double>(w.window().dpiScale()) : 0.0;
  w.content = readContent(w);
}

bool NativeFloatingBackend::setContentRect(FloatId window, const dock::Rect& contentRect) {
  NativeWindow* w = find(window);
  if (w == nullptr || !finiteRect(contentRect)) return false;
  place(*w, contentRect);
  return true;
}

std::optional<dock::Rect> NativeFloatingBackend::contentRect(FloatId window) const {
  const NativeWindow* w = find(window);
  if (w == nullptr) return std::nullopt;
  return w->content;
}

std::optional<FloatContent> NativeFloatingBackend::content(FloatId window) const {
  if (window == kMainWindow) {
    if (mainUi_ == nullptr || !mainWidget_.valid()) return std::nullopt;
    return FloatContent{mainUi_, mainWidget_, static_cast<double>(main_.dpiScale())};
  }
  const NativeWindow* w = find(window);
  if (w == nullptr) return std::nullopt;
  return FloatContent{&w->ui(), w->holder(), w->scale()};
}

bool NativeFloatingBackend::setMaximized(FloatId window, bool maximized) {
  NativeWindow* w = find(window);
  if (w == nullptr) return false;
  if (w->maximized == maximized) return true;
  if (!w->resizable) return false;
  if (maximized) {
    w->maximized = true;
    if (!w->window().isMaximized()) w->window().maximizeToggle();
  } else {
    w->maximized = false;
    if (w->window().isMaximized()) w->window().maximizeToggle();
  }
  w->content = readContent(*w);
  return true;
}

bool NativeFloatingBackend::isMaximized(FloatId window) const {
  const NativeWindow* w = find(window);
  return w != nullptr && w->maximized;
}

bool NativeFloatingBackend::setTitle(FloatId window, std::string_view title) {
  NativeWindow* w = find(window);
  if (w == nullptr) return false;
  w->title = std::string(title);
  w->applyTitle();
  return true;
}

bool NativeFloatingBackend::setVisible(FloatId window, bool visible) {
  NativeWindow* w = find(window);
  if (w == nullptr) return false;
  w->shown = visible;
  w->window().setVisible(visible);
  if (visible) w->requestRedraw();
  return true;
}

// ---- stacking ------------------------------------------------------------------------------------------

std::vector<FloatId> NativeFloatingBackend::stacking() const { return table_.order(); }

bool NativeFloatingBackend::bringToFront(FloatId window) {
  bool changed = false;
  if (find(window) == nullptr || !table_.raise(window, &changed)) return false;
  if (changed) raiseOs(window);
  return true;
}

// Brings the OS z-order in step with the table: every other floating window goes directly below
// this one, bottom to top, which keeps their relative order and leaves other applications' windows
// where they are.
void NativeFloatingBackend::raiseOs(FloatId window) {
  NativeWindow* top = find(window);
  if (top == nullptr) return;
  for (const FloatId id : table_.order()) {
    if (id == window) continue;
    if (NativeWindow* other = find(id)) native::os::placeAbove(top->window(), other->window());
  }
}

// ---- coordinates and queries ------------------------------------------------------------------------------

dock::Point NativeFloatingBackend::toScreen(FloatId window, dock::Point local) const {
  const NativeWindow* w = find(window);
  const dock::Point origin = w != nullptr ? clientOrigin(*w) : mainOrigin();
  return {origin.x + local.x, origin.y + local.y};
}

dock::Point NativeFloatingBackend::toWindow(FloatId window, dock::Point screen) const {
  const NativeWindow* w = find(window);
  const dock::Point origin = w != nullptr ? clientOrigin(*w) : mainOrigin();
  return {screen.x - origin.x, screen.y - origin.y};
}

std::optional<FloatId> NativeFloatingBackend::topmostWindowAt(dock::Point screen, std::span<const FloatId> exclude) const {
  std::vector<native::StackedWindow> stack;
  for (const FloatId id : table_.order()) {
    const NativeWindow* w = find(id);
    if (w == nullptr) continue;
    stack.push_back({id, native::outerOfContent(w->content, options_.frame), w->shown && w->window().isVisible()});
  }
  const bool mainVisible = main_.isVisible() && !main_.isMinimized();
  return native::topmostWindow(stack, mainContentRect(), mainVisible, screen, exclude);
}

std::string NativeFloatingBackend::monitorAt(dock::Point screen) const { return space_.nameAt(screen); }

// ---- pointer tracking ---------------------------------------------------------------------------------------

PointerSample NativeFloatingBackend::samplePointer() const {
  return options_.pointerSource ? options_.pointerSource() : native::os::readPointer();
}

bool NativeFloatingBackend::beginPointerTracking(IPointerSink& sink) {
  const PointerSample now = samplePointer();
  track_ = {};
  track_.sink = &sink;
  track_.wasDown = now.leftDown;
  track_.haveLast = true;
  track_.last = now.physical;
  return true;
}

void NativeFloatingBackend::endPointerTracking() { track_ = {}; }

}  // namespace r1ui::widgets

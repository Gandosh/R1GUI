// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: everything one window's UI needs: the WidgetTree, Router and Invalidator, the widget
//   objects attached to tree nodes, the overlay layer and tooltip manager, the animation clock,
//   input dispatch from platform events, and the frame function (layout when dirty, overlay
//   placement, paint, animation scheduling).
// Why: this is the toolkit integration the preview's Scene did ad hoc; a UiContext makes it a
//   reusable object. One UiContext per OS window; they share a Services object (theme, style sheet,
//   text engine, icon cache) and the RenderDevice, so floating panels are just more contexts.
// Callers: the application shell (one per window: feed events, call frame/paint), widgets (through
//   ui()), tests (headless, with NullTextureFactory). Calls: ui-core, ui-theme via Services,
//   ui-render's Painter (CPU), platform::Event for translation only.
// Units: the tree, layout, pointer coordinates in the direct input API and every widget value are
//   logical pixels; setViewport takes the physical client size and the display scale (1.0 = 96 dpi)
//   and platform events are physical; painting multiplies by the scale (PaintContext).
// Frame protocol (host): per loop iteration set the clock (setTime), feed events
//   (handlePlatformEvent), call tick(); if needsFrame(): frame() (layout, overlay placement), then
//   paint(painter) between the target's beginFrame/endFrame, then finishPaint() (uploads the glyph
//   atlas; call before endFrame) and after endFrame check consumeRepaint(). Between frames the
//   host waits for events for at most msUntilTick(). The window target is cleared and fully
//   repainted every frame; damage rectangles are reported by frame() for hosts that can use them.
// Animation: PaintContext::animatedColor drives 150 ms colour transitions with the token easing.
//   They are instant while animations are disabled or no frame loop is running (see
//   setFrameLoopRunning); tests and offscreen renders therefore never wait.
// Destruction: destroy() removes the subtree from the tree at once but frees the C++ objects at
//   the next safe point, so widgets may destroy themselves from their own handlers.
// Failure behavior: exceptions from widget paint/measure propagate to the caller of frame/paint
//   (the shell shows them); an exception thrown by an input handler, a timer callback or a tooltip
//   never leaves the input entry points (handlePlatformEvent, pointerDown, keyDown, tick, ...): it is
//   reported through UiHost::reportFault, counted (inputFaults) and the pointer interaction is
//   released. Tree limits make create() fail (std::length_error is thrown by create<T>).
// Threading: UI thread only.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "r1ui/core/events/EventHandler.h"
#include "r1ui/core/events/Router.h"
#include "r1ui/core/invalidation/Invalidator.h"
#include "r1ui/core/layout/Measure.h"
#include "r1ui/core/tree/WidgetTree.h"
#include "r1ui/platform/Events.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/overlay/OverlayManager.h"
#include "r1ui/widgets/overlay/TooltipManager.h"
#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/Services.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

// Hooks the shell provides (all optional).
struct UiHost {
  std::function<void(std::string_view)> writeClipboard;
  std::function<std::optional<std::string>()> readClipboard;
  // Receives the message of an exception that a widget handler or timer callback threw (see
  // UiContext::inputFaults); without it the message goes to stderr.
  std::function<void(std::string_view)> reportFault;
};

struct UiContextOptions {
  UiHost host;
  core::tree::TreeLimits limits;
  core::events::RouterConfig router;
};

struct FrameInfo {
  bool layoutRan = false;
  std::vector<core::layout::Rect> damage;  // absolute logical rectangles that changed
};

class UiContext final : private core::layout::MeasureProvider, private core::events::GlobalKeyHandler {
 public:
  explicit UiContext(Services& services, UiContextOptions options = {});
  ~UiContext() override;
  UiContext(const UiContext&) = delete;
  UiContext& operator=(const UiContext&) = delete;

  Services& services() { return services_; }
  theme::Theme& theme() { return services_.theme(); }
  TextEngine& text() { return services_.text(); }
  IconCache& icons() { return services_.icons(); }
  const UiHost& host() const { return host_; }

  // ---- tree ----
  core::tree::WidgetTree& tree() { return tree_; }
  const core::tree::WidgetTree& tree() const { return tree_; }
  core::events::Router& router() { return router_; }
  core::invalidation::Invalidator& invalidator() { return invalidator_; }
  core::tree::WidgetId root() const { return root_; }
  // The root node's style: configure the window's top-level flex layout here.
  core::layout::Style& rootStyle();

  // Creates a widget of type T as the last child of `parent` (or before `before`), registers its
  // style rows, binds it and calls onAttached(). Throws std::invalid_argument for a stale parent
  // and std::length_error when the tree limits are reached.
  template <class T, class... Args>
  T& create(core::tree::WidgetId parent, Args&&... args) {
    static_assert(std::is_base_of_v<WidgetObject, T>, "widgets derive from WidgetObject");
    registerRows<T>();
    auto object = std::make_unique<T>(std::forward<Args>(args)...);
    T& ref = *object;
    attach(parent, core::tree::kNoWidget, std::move(object));
    return ref;
  }
  template <class T, class... Args>
  T& createBefore(core::tree::WidgetId parent, core::tree::WidgetId before, Args&&... args) {
    static_assert(std::is_base_of_v<WidgetObject, T>, "widgets derive from WidgetObject");
    registerRows<T>();
    auto object = std::make_unique<T>(std::forward<Args>(args)...);
    T& ref = *object;
    attach(parent, before, std::move(object));
    return ref;
  }

  // Destroys the widget and its subtree (tree nodes at once, objects at the next safe point).
  // Returns false for a stale id.
  bool destroy(core::tree::WidgetId id);
  bool alive(core::tree::WidgetId id) const { return tree_.alive(id); }
  WidgetObject* object(core::tree::WidgetId id);
  const WidgetObject* object(core::tree::WidgetId id) const;
  template <class T>
  T* objectAs(core::tree::WidgetId id) { return dynamic_cast<T*>(object(id)); }
  // Absolute logical rectangle of a widget (empty for a stale id).
  core::layout::Rect absRect(core::tree::WidgetId id) const;
  // Re-parents a widget (e.g. a drag preview) keeping damage bookkeeping.
  bool reparent(core::tree::WidgetId child, core::tree::WidgetId newParent, core::tree::WidgetId before = core::tree::kNoWidget);

  // ---- viewport, time, window state ----
  // Physical client size and display scale (non-finite or non-positive scale -> 1). Zero sizes
  // (minimised) are accepted. A scale change re-lays out the tree.
  void setViewport(int physicalWidth, int physicalHeight, float scale);
  float scale() const { return scale_; }
  double viewportWidth() const;   // logical
  double viewportHeight() const;
  // Monotonic milliseconds; call before feeding events and frames.
  void setTime(uint64_t ms) { nowMs_ = ms; }
  uint64_t now() const { return nowMs_; }
  void setWindowActive(bool active);
  bool windowActive() const { return windowActive_; }

  // ---- overlays and tooltips ----
  OverlayManager& overlays() { return overlays_; }
  TooltipManager& tooltips() { return tooltips_; }

  // ---- timers ----
  // One-shot timers on the context clock (setTime): the callback runs from tick() once `delayMs`
  // has passed. Callbacks run on the UI thread outside any lock and may set or cancel timers; a
  // timer set by a callback fires at the earliest on the next tick. Returns 0 when the limit
  // (kMaxTimers) is reached. Capture WidgetIds, not pointers, in the callback and re-check alive().
  using TimerId = uint32_t;
  static constexpr size_t kMaxTimers = 4096;
  TimerId setTimer(uint64_t delayMs, std::function<void()> callback);
  // True when the timer was still pending.
  bool cancelTimer(TimerId id);

  // ---- animation ----
  void setAnimationsEnabled(bool enabled) { animationsEnabled_ = enabled; }
  // The shell declares that frames are being produced; without it animations are instant.
  void setFrameLoopRunning(bool running) { frameLoopRunning_ = running; }
  bool animationsActive() const { return animationsEnabled_ && frameLoopRunning_; }
  render::Color animatedColor(core::tree::WidgetId widget, int slot, const render::Color& target);
  float animatedValue(core::tree::WidgetId widget, int slot, float target);
  // Drops the tween of one (widget, slot) pair and gives back the frame request it held. For widgets
  // whose slot set changes while they live (a tab bar closing a tab); a destroyed widget loses all
  // its tweens automatically.
  void releaseAnimation(core::tree::WidgetId widget, int slot);

  // ---- input ----
  // Translates one platform event (physical pixels) and routes it. Resize and DPI events are
  // ignored here: the shell calls setViewport.
  void handlePlatformEvent(const platform::Event& event);
  void setGlobalKeyHandler(core::events::GlobalKeyHandler* handler) { appKeys_ = handler; }
  // The window's share of the glyph atlas (see AtlasConsumer). By default the context has its own; a
  // shell that also draws text itself in the same frame (before or after the context paints) passes
  // one consumer per window so the whole frame is tracked together. Must outlive the context's frames.
  void setAtlasConsumer(AtlasConsumer* consumer) { atlas_ = consumer != nullptr ? consumer : &ownAtlas_; }
  // Direct input in logical pixels (tests, synthetic input). Each returns true when handled.
  bool pointerMove(double x, double y, uint8_t modifiers = 0);
  bool pointerDown(double x, double y, core::events::Button button = core::events::Button::Left, uint8_t modifiers = 0);
  bool pointerUp(double x, double y, core::events::Button button = core::events::Button::Left, uint8_t modifiers = 0);
  bool wheel(double x, double y, double deltaX, double deltaY, uint8_t modifiers = 0);
  void pointerLeftWindow();
  bool keyDown(core::events::Key key, uint8_t modifiers = 0, bool repeat = false);
  bool keyUp(core::events::Key key, uint8_t modifiers = 0);
  bool textInput(char32_t codePoint, uint8_t modifiers = 0);
  // Moves keyboard focus to `id` from application code (program focus shows no focus ring; pass
  // FocusReason::Keyboard to show it). Unlike
  // calling router().focus() directly, this runs inside a dispatch frame, so a blur handler that
  // destroys its own widget cannot free it while the router is still using it. False (focus
  // unchanged) when the widget is not focusable.
  bool focusWidget(core::tree::WidgetId id, core::events::FocusReason reason = core::events::FocusReason::Program);
  // Removes keyboard focus (the same guard).
  void clearFocus();
  // Cursor wanted at the current pointer position.
  Cursor cursor() const;
  // Last pointer position in logical pixels (valid once the pointer has been seen).
  bool pointerKnown() const { return pointerKnown_; }
  double pointerX() const { return pointerX_; }
  double pointerY() const { return pointerY_; }

  // ---- frame ----
  bool needsFrame() const;
  // Runs layout when dirty (re-running it while overlays need placement) and returns what changed.
  FrameInfo frame();
  // Paints the whole tree (overlay layer last). Between the target's beginFrame and endFrame.
  void paint(render::Painter& painter);
  // After the last paint of a frame, before endFrame: uploads the glyph atlas.
  void finishPaint();
  // True (once) when an atlas ran out of room or was reset during the frame: paint again.
  bool consumeRepaint();
  // Advances timers (tooltips and setTimer callbacks); true when a frame is needed because of it.
  bool tick();
  // Milliseconds until tick() has something to do; nullopt = nothing scheduled.
  std::optional<uint64_t> msUntilTick() const;
  // Discards all layout caches (benchmarks, after a font change).
  void requestFullLayout() { invalidator_.requestFullLayout(); }
  size_t widgetCount() const { return tree_.nodeCount(); }
  // Bookkeeping sizes for leak tests: widgets that asked for onLayout, and live animation tweens.
  size_t layoutCallbackCount() const { return layoutCallbacks_.size(); }
  size_t animationCount() const { return tweens_.size(); }
  // Exceptions caught at the input boundary since the context was created, and the last message.
  size_t inputFaults() const { return inputFaults_; }
  const std::string& lastInputFault() const { return lastInputFault_; }

 private:
  friend class WidgetObject;
  friend class OverlayManager;

  template <class T>
  void registerRows() {
    if constexpr (requires { T::styleRows(); }) services_.addStyleRows(T::styleRows());
  }
  void attach(core::tree::WidgetId parent, core::tree::WidgetId before, std::unique_ptr<WidgetObject> object);
  void setLayoutCallback(core::tree::WidgetId id, bool wants);
  void forgetDestroyed();

  // MeasureProvider
  core::layout::MeasureResult measure(core::tree::WidgetId widget, const core::layout::MeasureInput& input) override;
  // GlobalKeyHandler: overlays take an unused Escape, then the application's shortcuts.
  bool onGlobalKey(const core::events::Event& event, core::events::Router& router) override;

  // Input plumbing
  bool routePointerDown(const core::events::PointerInput& input);
  core::events::PointerInput makePointer(double x, double y, core::events::Button button, uint8_t modifiers) const;
  // Runs `body` (an input entry point's work); an exception is reported and counted, never propagated.
  template <class F>
  bool guarded(F&& body);
  void noteFault(const char* what);
  // Marks an input/frame entry point: while one is on the stack destroyed objects stay allocated.
  struct DispatchGuard {
    UiContext& ui;
    explicit DispatchGuard(UiContext& u) : ui(u) { ++ui.dispatchDepth_; }
    ~DispatchGuard() {
      if (--ui.dispatchDepth_ == 0) ui.graveyard_.clear();
    }
    DispatchGuard(const DispatchGuard&) = delete;
    DispatchGuard& operator=(const DispatchGuard&) = delete;
  };

  struct Tween {
    float from[4] = {0, 0, 0, 0};
    float to[4] = {0, 0, 0, 0};
    float current[4] = {0, 0, 0, 0};
    uint64_t startMs = 0;
    bool running = false;
    bool scheduled = false;  // requestAnimation was issued for it
  };
  struct TweenKey {
    core::tree::WidgetId widget;
    int slot = 0;
    friend bool operator==(const TweenKey&, const TweenKey&) = default;
  };
  struct TweenKeyHash {
    size_t operator()(const TweenKey& k) const {
      return std::hash<core::tree::WidgetId>{}(k.widget) * 31u + static_cast<size_t>(k.slot);
    }
  };
  void animate(core::tree::WidgetId widget, int slot, const float* target, int count, float* out);
  void endAnimationPass();

  void paintWidget(render::Painter& painter, core::tree::WidgetId id);
  bool runDueTimers();
  std::optional<uint64_t> msUntilTimer() const;

  struct Timer {
    TimerId id = 0;
    uint64_t dueMs = 0;
    std::function<void()> callback;
  };

  Services& services_;
  UiHost host_;
  core::tree::WidgetTree tree_;
  core::events::Router router_;
  core::invalidation::Invalidator invalidator_;
  core::tree::WidgetId root_;
  std::vector<std::unique_ptr<WidgetObject>> objects_;   // index = userData - 1; null = free
  std::vector<uint32_t> freeSlots_;
  std::vector<std::unique_ptr<WidgetObject>> graveyard_;
  std::vector<core::tree::WidgetId> layoutCallbacks_;
  OverlayManager overlays_;
  TooltipManager tooltips_;
  core::events::GlobalKeyHandler* appKeys_ = nullptr;
  AtlasConsumer ownAtlas_;
  AtlasConsumer* atlas_ = &ownAtlas_;
  uint64_t iconEpochAtPaint_ = 0;

  float scale_ = 1.0f;
  int viewportW_ = 0;
  int viewportH_ = 0;
  uint64_t nowMs_ = 0;
  bool windowActive_ = true;
  bool animationsEnabled_ = true;
  bool frameLoopRunning_ = false;
  bool repaintRequested_ = false;
  bool pointerKnown_ = false;
  double pointerX_ = 0.0;
  double pointerY_ = 0.0;
  int dispatchDepth_ = 0;
  size_t inputFaults_ = 0;
  std::string lastInputFault_;
  uint32_t seenThemeRevision_ = 0;
  uint32_t seenSheetRevision_ = 0;
  std::unordered_map<TweenKey, Tween, TweenKeyHash> tweens_;
  std::vector<Timer> timers_;
  TimerId nextTimerId_ = 1;
};

}  // namespace r1ui::widgets

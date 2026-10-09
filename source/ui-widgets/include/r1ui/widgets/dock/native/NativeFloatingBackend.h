// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: NativeFloatingBackend, the IFloatingBackend that shows every floating panel window as a real
//   top-level OS window: borderless, owned by the main window (so it stays above it and minimizes and
//   restores with it), a tool window (no taskbar button), rounded corners from the window manager,
//   its own UiContext and swapchain on the application's shared Services and RenderDevice, and a
//   toolkit-drawn title bar (title, maximize and close buttons; caption drag, edge resize, double-click
//   maximize and snapping come from the platform's chrome hit test).
// Why: slice 5.2. The dock decides what a floating window holds; this backend decides how it is
//   shown. It passes the same conformance suite as the in-window backend
//   (tests/ui-widgets/dock/BackendConformance.h) plus the clauses only real windows can exercise.
// Callers: the application (constructs it with the main window, the device and the services, gives it
//   to DockHost, and drives it from its loop: AppLoop does that), tests. Calls: ui-platform (Window,
//   Monitors), ui-render (WindowTarget), UiContext.
// Coordinates: "screen" is the dock's screen logical space (virtual desktop pixels divided by the
//   scale of the monitor under the point, NativeCoords.h). A window's content rectangle is its client
//   area minus the drawn frame (BackendInfo::frame: 1 px border, 34 px title bar). Window coordinates
//   are logical pixels from the window's client origin.
// Threading and re-entrancy: UI thread only. Listener calls come from processEvents() (and from
//   nothing else); destroyWindow never destroys the window object at once: it hides the OS window and
//   parks the object until collectGarbage(), because the dock destroys a window from inside that
//   window's own event handler. Call collectGarbage() from the loop outside event dispatch.
// Echo rule: host calls never reach the listener. State the host changes is stored before the OS is
//   asked, and the events the OS produces afterwards are compared with the stored state, so only real
//   differences (the user dragged, resized, maximized, activated; the OS moved the window) are
//   reported.
// Lifetime: the main window, the device and the services must outlive the backend; destroy the
//   backend (and the DockHost's windows with it) before the main window. The destructor waits for the
//   GPU through the window targets.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "r1ui/platform/Window.h"
#include "r1ui/render/RenderDevice.h"
#include "r1ui/widgets/dock/FloatingBackend.h"
#include "r1ui/widgets/dock/native/NativeCoords.h"
#include "r1ui/widgets/dock/native/WindowTable.h"
#include "r1ui/widgets/runtime/Services.h"

namespace r1ui::widgets {

namespace native {
class NativeWindow;
struct WindowChanges;
}

// Where the pointer is, in screen physical pixels, and whether the left button is held. The default
// source reads the OS (cursor position and button state); tests supply their own.
struct PointerSample {
  platform::Point physical;
  bool leftDown = false;
};
using PointerSource = std::function<PointerSample()>;

struct NativeBackendOptions {
  size_t maxWindows = 32;
  native::FrameMetrics frame;             // border and title bar of the drawn frame (logical px)
  int visibleMarginLogical = 100;         // owner decision: a window keeps this much on a monitor (spec 03 rule 64)
  PointerSource pointerSource;            // empty = the OS
  unsigned trackingIntervalMs = 8;        // how often the loop samples the pointer while tracking
};

class NativeFloatingBackend final : public IFloatingBackend {
 public:
  NativeFloatingBackend(platform::Window& mainWindow, render::RenderDevice& device, Services& services, NativeBackendOptions options = {});
  ~NativeFloatingBackend() override;
  NativeFloatingBackend(const NativeFloatingBackend&) = delete;
  NativeFloatingBackend& operator=(const NativeFloatingBackend&) = delete;

  // ---- IFloatingBackend ----
  BackendInfo describe() const override;
  void setListener(IFloatingListener* listener) override { listener_ = listener; }
  void setMainContent(UiContext& ui, core::tree::WidgetId contentWidget) override;
  dock::Rect mainContentRect() const override;
  FloatCreateResult createWindow(const FloatRequest& request) override;
  bool destroyWindow(FloatId window) override;
  bool setContentRect(FloatId window, const dock::Rect& contentRect) override;
  std::optional<dock::Rect> contentRect(FloatId window) const override;
  std::optional<FloatContent> content(FloatId window) const override;
  bool bringToFront(FloatId window) override;
  std::vector<FloatId> stacking() const override;
  bool setTitle(FloatId window, std::string_view title) override;
  bool setMaximized(FloatId window, bool maximized) override;
  bool isMaximized(FloatId window) const override;
  bool setVisible(FloatId window, bool visible) override;
  dock::Point toScreen(FloatId window, dock::Point local) const override;
  dock::Point toWindow(FloatId window, dock::Point screen) const override;
  std::optional<FloatId> topmostWindowAt(dock::Point screen, std::span<const FloatId> exclude) const override;
  std::string monitorAt(dock::Point screen) const override;
  bool beginPointerTracking(IPointerSink& sink) override;
  void endPointerTracking() override;

  // ---- driven by the run loop (AppLoop) ----
  // Feeds every floating window its queued platform events, reports what the user or the OS did to
  // the listener, notices windows the OS destroyed (onFloatLost, once) and samples the pointer while
  // tracking. The messages themselves are pumped by the main window's pumpEvents().
  void processEvents(uint64_t nowMs);
  // Destroys the windows destroyWindow parked. Never call it from a window's own live callback.
  void collectGarbage();
  bool needsFrame() const;
  // Draws every floating window that needs a frame, one after the other.
  void renderFrames(uint64_t nowMs);
  // Smallest wait the floating windows allow (timers, pointer tracking); nullopt = nothing scheduled.
  std::optional<uint64_t> msUntilTick(uint64_t nowMs);
  // The callback every floating window runs during the OS move/size loop (AppLoop's live step).
  void setLiveCallback(std::function<void()> callback);
  // The application saw the main window's DisplayChanged event (floating windows see their own):
  // re-reads the monitors and brings unreachable windows back.
  void onDisplayChanged();

  // ---- inspection (tests, the manual harness, the integrator's diagnostics) ----
  size_t windowCount() const;
  size_t parkedCount() const;
  // The OS window of a floating window (null for unknown ids and the main window).
  platform::Window* nativeWindow(FloatId window);
  uint64_t framesPresented(FloatId window) const;
  const native::ScreenSpace& screenSpace() const { return space_; }

 private:
  struct TrackState {
    IPointerSink* sink = nullptr;
    bool wasDown = false;
    bool haveLast = false;
    platform::Point last;
  };

  native::NativeWindow* find(FloatId window);
  const native::NativeWindow* find(FloatId window) const;
  void refreshMonitors();
  dock::Point mainOrigin() const;
  dock::Point clientOrigin(const native::NativeWindow& w) const;
  dock::Rect readContent(const native::NativeWindow& w) const;
  dock::Point maxContentSize() const;
  void place(native::NativeWindow& w, dock::Rect content);
  void raiseOs(FloatId window);
  void reportLost(FloatId window);
  void handleChanges(FloatId window, const native::WindowChanges& changes);
  void pollTracking();
  PointerSample samplePointer() const;

  platform::Window& main_;
  render::RenderDevice& device_;
  Services& services_;
  NativeBackendOptions options_;
  IFloatingListener* listener_ = nullptr;
  UiContext* mainUi_ = nullptr;
  core::tree::WidgetId mainWidget_;
  mutable dock::Rect lastMain_;
  native::ScreenSpace space_;
  native::WindowTable<native::NativeWindow> table_;
  std::function<void()> live_;
  TrackState track_;
};

}  // namespace r1ui::widgets

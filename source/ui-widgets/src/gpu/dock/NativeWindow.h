// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: NativeWindow, one native floating window of the backend: the borderless owned tool window
//   (platform::Window), its swapchain (WindowTarget), its UiContext with the toolkit-drawn frame and
//   the content holder, and the per-window state the backend keeps (title, limits, maximize, last
//   known content rectangle, pending host move, redraw flag).
// Why: the backend's table, events and contract live in NativeFloatingBackend; everything that is
//   about ONE window (building it, feeding its context, rendering it, mapping its cursor) is here so
//   the backend files stay about the contract.
// Callers: NativeFloatingBackend only (private header).
// Lifetime: member order is the destruction order reversed: the context goes first (its widgets use
//   the services and the atlas consumer), then the swapchain, then the OS window. The context belongs
//   to the window's own AtlasConsumer (UiContext default), so atlas and icon uploads are per window.
// Threading: UI thread only. A NativeWindow is never destroyed while one of its event handlers is on
//   the stack: the backend parks it in WindowTable until the loop's safe point.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "NativeFrame.h"
#include "r1ui/platform/Window.h"
#include "r1ui/render/RenderDevice.h"
#include "r1ui/render/WindowTarget.h"
#include "r1ui/widgets/dock/FloatingBackend.h"
#include "r1ui/widgets/dock/native/NativeCoords.h"
#include "r1ui/widgets/runtime/Services.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets::native {

struct NativeWindowInit {
  FloatId id = 0;
  const FloatRequest* request = nullptr;
  platform::Window* owner = nullptr;
  render::RenderDevice* device = nullptr;
  Services* services = nullptr;
  FrameMetrics frame;
  platform::Point outerPosition;  // outer top-left, physical screen pixels
  dock::Point contentSize;        // logical
  dock::Point minContent;         // logical
};

// What one batch of events did to the window itself (not to its widgets).
struct WindowChanges {
  bool geometry = false;        // moved or resized
  bool dpi = false;
  bool closeRequested = false;
  bool pressed = false;         // a mouse press arrived in the window: it should be on top
  bool focused = false;         // the keyboard focus arrived (it may be stale by the time the event is read)
  bool displayChanged = false;
};

class NativeWindow {
 public:
  explicit NativeWindow(const NativeWindowInit& init);
  NativeWindow(const NativeWindow&) = delete;
  NativeWindow& operator=(const NativeWindow&) = delete;

  FloatId id() const { return id_; }
  platform::Window& window() { return *window_; }
  const platform::Window& window() const { return *window_; }
  UiContext& ui() const { return *ui_; }  // the context is owned, not part of this object's constness
  core::tree::WidgetId holder() const { return holder_; }
  const FrameMetrics& frameMetrics() const { return frame_; }

  // ---- state kept for the backend ----
  std::string title;
  dock::Point minContent{64.0, 64.0};
  bool resizable = true;
  bool maximized = false;        // mirrors the OS state; changed by the host before it acts, by events otherwise
  bool shown = true;             // false after setVisible(false)
  dock::Rect content;            // last known content rectangle, screen logical
  dock::Rect restore;            // content rectangle to return to when un-maximized
  double expectedScale = 0.0;    // scale a host-driven move announced (0 = none): that DPI event is not the user's
  bool lost = false;             // the OS destroyed the window; the listener was told

  // Client size in logical pixels (0 x 0 while minimized).
  dock::Point clientLogical() const;
  double scale() const { return static_cast<double>(window_->dpiScale()); }

  // ---- events and frames ----
  // Hands the window's queued events to its context and reports what happened to the window itself.
  WindowChanges processEvents(uint64_t nowMs);
  // Pushes the chrome description and the viewport to the platform window and the context.
  void syncViewport();
  void syncChrome();
  void applyTitle();
  bool needsFrame() const;
  // Lays out and draws one frame when needed; false when nothing was presented.
  bool render(uint64_t nowMs);
  void requestRedraw() { redraw_ = true; }
  void invalidateSwapchain();
  std::optional<uint64_t> msUntilTick(uint64_t nowMs);
  uint64_t framesPresented() const { return framesPresented_; }

 private:
  void applyCursor();

  FloatId id_;
  FrameMetrics frame_;
  std::unique_ptr<platform::Window> window_;
  std::unique_ptr<render::WindowTarget> target_;
  std::unique_ptr<UiContext> ui_;
  core::tree::WidgetId frameWidget_;
  core::tree::WidgetId holder_;
  Services* services_;
  bool redraw_ = true;
  uint64_t framesPresented_ = 0;
};

}  // namespace r1ui::widgets::native

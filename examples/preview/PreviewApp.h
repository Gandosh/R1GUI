// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the preview application shell: the borderless main window, the render device and window
//   target, text/icon engines, the widget scene, the four modes, the event loop that blocks while
//   idle, rendering during the OS move/size loop and the translation of platform events into scene
//   and mode input.
// Why: this is the integration of the Phase 3 stack on a real window; it is also what the
//   benchmark drives (Bench.cpp calls frame()/step() directly, so it measures the real paths).
// Callers: main.cpp (run), Bench.cpp (frame, step, mode switching, counters).
// Threading: UI thread only. Frame protocol: step(block) pumps messages, applies events, renders
//   when something changed (scene invalidation or an explicit request) and otherwise sleeps in
//   Window::waitForEvents until the next input or the next scheduled tick (caret blink).
// Failure behavior: errors from asset loading, painting or the device propagate as exceptions from
//   the constructor/step()/run(); main() turns them into a message box. Errors raised while Windows
//   runs its own move/size loop are stored and rethrown from the next step().
#pragma once

#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <string>

#include "Assets.h"
#include "DockSandbox.h"
#include "IconSet.h"
#include "ModeViews.h"
#include "Scene.h"
#include "TextEngine.h"
#include "r1ui/core/events/EventHandler.h"
#include "r1ui/platform/Window.h"
#include "r1ui/render/RenderDevice.h"
#include "r1ui/render/WindowTarget.h"

namespace preview {

struct AppOptions {
  int width = 1280;
  int height = 800;
  bool logicalSize = true;  // width/height are logical pixels (scaled by the display dpi)
  Mode mode = Mode::Widgets;
  r1ui::render::PresentMode presentMode = r1ui::render::PresentMode::Fifo;
};

// Wall-clock cost of one frame, milliseconds.
struct FrameTimes {
  double layoutMs = 0.0;
  double paintMs = 0.0;       // beginFrame + painting the tree and the mode + atlas upload
  double waitMs = 0.0;        // frame fence and image acquire inside endFrame
  double recordSubmitMs = 0.0;
  double presentMs = 0.0;
  double totalMs = 0.0;
  bool presented = false;
};

class PreviewApp final : private r1ui::core::events::GlobalKeyHandler {
 public:
  explicit PreviewApp(const AppOptions& options);
  ~PreviewApp();
  PreviewApp(const PreviewApp&) = delete;
  PreviewApp& operator=(const PreviewApp&) = delete;

  // Runs until the window closes or Escape quits. Throws on unrecoverable errors.
  void run();
  // One loop iteration: messages, input, a frame if needed, otherwise (block) a wait for events of
  // at most `maxWaitMs`. Returns false when the application should end.
  bool step(bool block, unsigned maxWaitMs = r1ui::platform::kWaitForever);
  // Pumps OS messages and applies the resulting input without drawing. False when the app should end.
  bool pumpMessages();
  // Renders a frame now, whether or not anything changed. Returns false when nothing could be
  // drawn (minimised, swapchain rebuilding).
  bool frame(FrameTimes* times = nullptr);

  void setMode(Mode mode);
  Mode mode() const { return scene_->mode(); }
  Scene& scene() { return *scene_; }
  r1ui::platform::Window& window() { return *window_; }
  r1ui::render::RenderDevice& device() { return *device_; }
  uint64_t framesPresented() const { return framesPresented_; }
  // Time from the constructor starting to the first presented frame, milliseconds (0 until then).
  double startupMs() const { return startupMs_; }

 private:
  bool onGlobalKey(const r1ui::core::events::Event& event, r1ui::core::events::Router& router) override;

  uint64_t nowMs() const;
  void processEvents();
  void routeToMode(const r1ui::platform::Event& event);
  void syncViewport();
  void applyCursor();
  void updateWindowTitle();
  void paintMode(r1ui::render::Painter& painter);
  void liveFrame();
  void rethrowPending();

  std::chrono::steady_clock::time_point started_;
  AssetPaths paths_;
  std::shared_ptr<const r1ui::theme::Tokens> tokens_;
  std::unique_ptr<r1ui::platform::Window> window_;
  std::unique_ptr<r1ui::render::RenderDevice> device_;
  std::unique_ptr<r1ui::render::WindowTarget> target_;
  std::unique_ptr<TextEngine> text_;
  std::unique_ptr<IconSet> icons_;
  std::unique_ptr<Scene> scene_;
  std::unique_ptr<SwatchesView> swatches_;
  std::unique_ptr<ScreensView> screens_;
  std::unique_ptr<DockSandbox> sandbox_;

  bool quit_ = false;
  bool redraw_ = true;
  bool inFrame_ = false;
  bool pointerInBody_ = false;
  float pointerX_ = 0.0f;
  float pointerY_ = 0.0f;
  std::string windowTitle_;
  std::exception_ptr pending_;
  uint64_t framesPresented_ = 0;
  double startupMs_ = 0.0;
};

}  // namespace preview

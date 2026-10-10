// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the preview application shell: the borderless main window, the render device and window
//   target, the shared widget services (theme, style sheet, text, icons), the title-bar scene, the
//   six modes (Editor, Gallery and Widgets are the real widget library in a UiContext each; Swatches,
//   Screens and Sandbox draw directly), the native floating-window backend, the multi-window event loop
//   (AppLoop) that blocks while idle and keeps every window drawing during the OS move/size loop, and
//   the translation of platform events into scene, widget and mode input.
// Why: this is the integration of the whole toolkit on a real window; it is also what the benchmark
//   drives (Bench.cpp calls frame()/step() directly, so it measures the real paths).
// Callers: main.cpp (run), Bench.cpp (frame, step, mode switching, counters).
// Threading: UI thread only. Frame protocol: step(block) pumps messages, applies events, renders
//   when something changed (invalidation of the scene or the active UiContext, or an explicit
//   request) and otherwise sleeps in Window::waitForEvents until the next input or the next
//   scheduled tick (a widget timer: tooltip delay, toast lifetime, menu delay).
// Keys (application level, reached through the routers' global handler after the focused widget and
//   the overlays declined them): Tab next mode (also Ctrl+Tab; plain Tab moves focus while a widget
//   has focus or a popup is open; on the Editor screen only Ctrl+Tab cycles, plain keys there belong
//   to the editor's commands), T dark/light, Escape clears focus, then quits (or cancels a dock
//   tab drag), Left/Right step reference screens, S/L/R dock layout save/load/reset. A focused text
//   field keeps the printable keys for itself.
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
#include "ComposedApp.h"
#include "editor/EditorApp.h"
#include "DockSandbox.h"
#include "GalleryApp.h"
#include "ModeViews.h"
#include "Scene.h"
#include "Toolkit.h"
#include "r1ui/core/events/EventHandler.h"
#include "r1ui/platform/Window.h"
#include "r1ui/render/RenderDevice.h"
#include "r1ui/render/WindowTarget.h"
#include "r1ui/widgets/dock/native/AppLoop.h"
#include "r1ui/widgets/dock/native/NativeFloatingBackend.h"
#include "r1ui/widgets/runtime/Services.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/thumbnailgrid/GpuThumbnailTextures.h"

namespace preview {

struct AppOptions {
  int width = 1440;
  int height = 900;
  bool logicalSize = true;  // width/height are logical pixels (scaled by the display dpi)
  Mode mode = Mode::Editor;
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
  // Pumps OS messages and applies the resulting input without drawing (benchmarks). False when the
  // app should end. The windows' own loop is step()/run().
  bool pumpMessages();
  // Renders a frame now, whether or not anything changed. Returns false when nothing could be
  // drawn (minimised, swapchain rebuilding).
  bool frame(FrameTimes* times = nullptr);
  // Discards every cached layout result of the shell and the active widget mode (benchmarks).
  void requestFullLayout();

  void setMode(Mode mode);
  Mode mode() const { return scene_->mode(); }
  Scene& scene() { return *scene_; }
  r1ui::platform::Window& window() { return *window_; }
  r1ui::render::RenderDevice& device() { return *device_; }
  // The widget contexts (null until their mode was shown once) and their screens.
  r1ui::widgets::UiContext* galleryUi() { return galleryUi_.get(); }
  r1ui::widgets::UiContext* widgetsUi() { return widgetsUi_.get(); }
  r1ui::widgets::UiContext* editorUi() { return editorUi_.get(); }
  editor::EditorApp* editor() { return editor_.get(); }
  r1ui::widgets::NativeFloatingBackend& backend() { return *backend_; }
  GalleryApp* gallery() { return gallery_.get(); }
  ComposedApp* composed() { return composed_.get(); }
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
  void writeDriveStatus();
  void updateWindowTitle();
  void paintMode(r1ui::render::Painter& painter);
  r1ui::dock::MonitorSet monitorSet();
  bool needsFrame();
  r1ui::widgets::UiContext* activeUi();
  std::unique_ptr<r1ui::widgets::UiContext> makeContext(r1ui::core::tree::WidgetId& content);
  void ensureModeBuilt(Mode mode);
  void setDarkTheme(bool dark);
  bool tabCyclesModes(const r1ui::platform::Event& event);

  std::chrono::steady_clock::time_point started_;
  AssetPaths paths_;
  std::shared_ptr<const r1ui::theme::Tokens> tokens_;
  std::unique_ptr<r1ui::platform::Window> window_;
  std::unique_ptr<r1ui::render::RenderDevice> device_;
  std::unique_ptr<r1ui::render::WindowTarget> target_;
  std::unique_ptr<GpuTextureFactory> textures_;
  std::unique_ptr<r1ui::widgets::Services> services_;
  // The brush library's picture textures (the Editor's tiles); destroyed after the Editor that uses them.
  std::unique_ptr<r1ui::widgets::GpuThumbnailTextures> brushTextures_;
  // This window's share of the glyph atlas: the shell scene, the active context and the direct-drawn
  // modes all draw text in one frame, so one consumer tracks the whole frame.
  std::unique_ptr<r1ui::widgets::AtlasConsumer> windowAtlas_;
  // The native floating windows of the dock. Outlives every context (the Editor's DockHost talks to it
  // while it detaches) and is destroyed before the services and the device.
  std::unique_ptr<r1ui::widgets::NativeFloatingBackend> backend_;
  std::unique_ptr<Scene> scene_;
  std::unique_ptr<SwatchesView> swatches_;
  std::unique_ptr<ScreensView> screens_;
  std::unique_ptr<DockSandbox> sandbox_;
  // Built the first time their mode is shown. Declared after the services they refer to; the
  // screens are destroyed before their contexts.
  std::unique_ptr<r1ui::widgets::UiContext> galleryUi_;
  std::unique_ptr<GalleryApp> gallery_;
  std::unique_ptr<r1ui::widgets::UiContext> widgetsUi_;
  std::unique_ptr<ComposedApp> composed_;
  std::unique_ptr<r1ui::widgets::UiContext> editorUi_;
  std::unique_ptr<editor::EditorApp> editor_;
  // The multi-window run loop; declared last so it is destroyed first (it removes the live callbacks).
  std::unique_ptr<r1ui::widgets::AppLoop> loop_;

  bool quit_ = false;
  bool redraw_ = true;
  bool inFrame_ = false;
  bool pointerInBody_ = false;
  float pointerX_ = 0.0f;
  float pointerY_ = 0.0f;
  std::string windowTitle_;
  std::string statusPath_;   // R1GUI_PREVIEW_STATUS: the scripted drive reads the Editor's coordinates from here
  std::string lastStatus_;
  uint64_t framesPresented_ = 0;
  double startupMs_ = 0.0;
};

}  // namespace preview

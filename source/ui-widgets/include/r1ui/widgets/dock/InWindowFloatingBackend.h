// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: InWindowFloatingBackend, the IFloatingBackend that shows floating panels inside the host
//   window: each floating window is a toolkit-drawn frame (rounded 8 px rectangle with shadow, a
//   title bar with the window title, maximize and close buttons, resize edges) that is a child of a
//   layer widget of the same UiContext, stacked above the docked content and below the overlay layer.
// Why: it is the backend the preview and the gallery use and the reference implementation of the
//   contract (FloatingBackend.h), against which the conformance suite is written; native OS windows
//   are a different backend built later against the same interface.
// Callers: the application (constructs it, passes it to DockHost), tests. Calls: UiContext
//   (widgets), FloatingFrame (internal widget).
// Coordinates: the screen is the UiContext's logical window coordinates and every window's
//   coordinates are the same, so toScreen/toWindow are the identity. A window's content rectangle
//   is the area inside the frame under the title bar; the frame adds FrameInsets around it.
// Gestures: dragging the title bar moves a window, dragging an edge or corner resizes it (never
//   below the request's minimum content size), a double click on the title bar maximizes or restores
//   it to the whole host window, the close button asks the listener (nothing is destroyed until the
//   host calls destroyWindow). A window is kept inside the host window so at least 48 px of its
//   title bar stay reachable (the in-window analogue of the 100 px rule for saved windows).
// Lifetime: the frames are widgets of the UiContext and go with it; the backend's destructor does not
//   touch the context (it may run while the context is being destroyed). A DockHost destroys the
//   windows it created when it detaches. Keep the backend alive as long as the host.
// Pointer tracking is not needed: the strip that starts a tab drag keeps the pointer capture.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "r1ui/widgets/dock/FloatingBackend.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

class FloatingFrame;

struct InWindowFloatingOptions {
  double titleHeight = 34.0;   // spec 03 rule 57
  double border = 1.0;
  double radius = 8.0;         // owner decision: rounded 8 px floating windows
  double resizeBand = 6.0;
  double minVisibleTitle = 48.0;
  size_t maxWindows = 64;
};

class InWindowFloatingBackend final : public IFloatingBackend {
 public:
  // `layer` is the widget the frames are added to (normally the context's root); it must fill the
  // window so frame coordinates equal window coordinates.
  InWindowFloatingBackend(UiContext& ui, core::tree::WidgetId layer, InWindowFloatingOptions options = {});
  InWindowFloatingBackend(const InWindowFloatingBackend&) = delete;
  InWindowFloatingBackend& operator=(const InWindowFloatingBackend&) = delete;

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
  dock::Point toScreen(FloatId, dock::Point local) const override { return local; }
  dock::Point toWindow(FloatId, dock::Point screen) const override { return screen; }
  std::optional<FloatId> topmostWindowAt(dock::Point screen, std::span<const FloatId> exclude) const override;
  std::string monitorAt(dock::Point) const override { return "window"; }
  bool beginPointerTracking(IPointerSink&) override { return false; }
  void endPointerTracking() override {}

  // The frame widget of a window (title bar, buttons), for tests that drive real pointer input.
  core::tree::WidgetId frameWidget(FloatId window) const;

 private:
  friend class FloatingFrame;
  struct Window {
    FloatId id = 0;
    core::tree::WidgetId frame;
    core::tree::WidgetId holder;
    dock::Rect content;
    dock::Rect restore;
    std::string title;
    dock::Point minSize{64.0, 64.0};
    bool resizable = true;
    bool maximized = false;
    bool visible = true;
  };
  Window* find(FloatId window);
  const Window* find(FloatId window) const;
  dock::Rect clampRect(const Window& w, dock::Rect rect) const;
  void apply(Window& w);
  void relayer();
  // Gestures from the frame: the frame has already computed the wanted content rectangle.
  void userChangedRect(FloatId window, const dock::Rect& wanted);
  void userPressed(FloatId window);
  void userRequestedClose(FloatId window);
  void userToggledMaximize(FloatId window);
  dock::Rect maximizedRect() const;

  UiContext& ui_;
  core::tree::WidgetId layer_;
  core::tree::WidgetId main_;
  InWindowFloatingOptions options_;
  IFloatingListener* listener_ = nullptr;
  std::vector<Window> windows_;  // bottom to top
  FloatId nextId_ = 1;
};

}  // namespace r1ui::widgets

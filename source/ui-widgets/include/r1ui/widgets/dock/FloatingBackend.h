// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: IFloatingBackend, the contract between the DockHost and whatever provides floating
//   windows (an in-window drawn frame today, native OS windows later), its request/result types and
//   the listener through which a backend reports what the user did to a window.
// Why: the dock model decides what is in a floating area; a backend decides how it is shown. Keeping
//   the seam this narrow lets the native backend be written against the contract and checked with
//   the same conformance suite (tests/ui-widgets/dock/BackendConformance.h) as the in-window one.
// Callers: DockHost (the only client), InWindowFloatingBackend and the native backend (implementors),
//   tests.
// The contract in short (the full text is docs/dev/docking.md, section "Floating backend"):
//  * Threading: UI thread only, for every method and every listener call. A backend whose windows
//    live on other threads must marshal before calling the listener.
//  * Coordinates: "screen" = logical pixels of one shared space in which the main content rectangle
//    and every window's content rectangle are expressed. In-window: the main UiContext's window
//    coordinates. Native: virtual-desktop coordinates divided by the scale of the monitor the point
//    is on, so a window dragged to a monitor with another scale keeps its logical size. "Window"
//    coordinates are logical pixels relative to a window's client origin. A window's *content
//    rectangle* is its client area without any frame the backend draws or the OS adds.
//  * Windows: id 0 (kMainWindow) is the main window and always exists. createWindow returns a new
//    id (never reused within a backend's lifetime) and makes the content area available through
//    content(): a UiContext and a parent widget the dock mounts its area view in. The parent fills
//    the content rectangle; the backend keeps it that size and lays it out.
//  * Echo rule: setContentRect/setMaximized/setTitle/bringToFront/setVisible called by the host never
//    trigger the listener; only changes the user (or the OS) made do.
//  * Lifetime: destroyWindow destroys the content widgets with the window; after it the id and any
//    FloatContent obtained earlier are invalid. When a window disappears without destroyWindow (the
//    user closed it, the OS destroyed it) the backend calls onFloatLost exactly once, after which
//    the id is invalid; a *close request* the user can still veto is onFloatCloseRequested and
//    nothing is destroyed until the host calls destroyWindow.
//  * Drop target queries: topmostWindowAt(screenPoint, exclude) answers which window shows the
//    point, honouring stacking, window frames and the exclude list (windows hidden or being dragged);
//    nullopt when no window of the application is there. It must be answerable while the pointer is
//    outside every window of the application (pointer tracking) for native backends.
//  * Pointer tracking: a backend that reports pointerTracking in describe() delivers every pointer
//    movement and the release of the left button to the sink in screen coordinates between
//    beginPointerTracking and endPointerTracking, also while the pointer is outside every window.
//    Backends whose origin context keeps pointer capture (in-window) report false and deliver
//    nothing; the host then relies on the capturing widget's events.
//  * DPI: FloatContent::scale is the display scale of the window; the host and the widgets work in
//    logical pixels only. onFloatScaleChanged reports a change (the window moved between monitors).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/core/tree/WidgetId.h"
#include "r1ui/dock/DockTypes.h"

namespace r1ui::widgets {

class UiContext;

using FloatId = uint32_t;
inline constexpr FloatId kMainWindow = 0;

struct FloatRequest {
  std::vector<dock::PanelId> panels;  // tabs the window will hold, in order (for the title)
  dock::PanelId active = 0;           // the front tab
  std::string title;                  // window title (the front tab's label)
  dock::Rect contentRect;             // wanted content rectangle, screen logical px
  FloatId owner = kMainWindow;        // the window this one belongs to (stays above it)
  dock::Point minContentSize{64.0, 64.0};
  bool resizable = true;
};

// Where the dock mounts an area view: a widget of `ui` that fills the window's content rectangle.
struct FloatContent {
  UiContext* ui = nullptr;
  core::tree::WidgetId parent;
  double scale = 1.0;
  bool valid() const { return ui != nullptr && parent.valid(); }
};

struct FloatCreateResult {
  bool ok = false;
  FloatId id = 0;
  std::string error;
};

// What the backend draws around a window's content rectangle (title bar, border, shadow-free), in
// logical px. A window dropped at the ghost's position is placed so that its OUTER top-left is
// there: content = ghost shifted by (left, top).
struct FrameInsets {
  double left = 0.0;
  double top = 0.0;
  double right = 0.0;
  double bottom = 0.0;
};

struct BackendInfo {
  std::string name;
  bool nativeWindows = false;    // each window has its own UiContext and OS window
  bool pointerTracking = false;  // supports beginPointerTracking (see the contract above)
  bool maximize = true;          // setMaximized is meaningful
  FrameInsets frame;
};

// What a backend reports about user (or OS) actions. Never called as an echo of a host call.
class IFloatingListener {
 public:
  virtual ~IFloatingListener() = default;
  // The user moved or resized the window (called repeatedly while dragging); `contentRect` is final.
  virtual void onFloatMoved(FloatId window, const dock::Rect& contentRect) = 0;
  // The user pressed the window's close button. Nothing is destroyed yet.
  virtual void onFloatCloseRequested(FloatId window) = 0;
  // The window was pressed or focused and is now on top.
  virtual void onFloatActivated(FloatId window) = 0;
  // The window no longer exists and the host did not destroy it.
  virtual void onFloatLost(FloatId window) = 0;
  virtual void onFloatScaleChanged(FloatId window, double scale) = 0;
  virtual void onFloatMaximizedChanged(FloatId window, bool maximized) = 0;
};

// Receives tracked pointer samples (see "Pointer tracking" above).
class IPointerSink {
 public:
  virtual ~IPointerSink() = default;
  virtual void onTrackedPointer(dock::Point screen, bool leftDown) = 0;
};

class IFloatingBackend {
 public:
  virtual ~IFloatingBackend() = default;

  virtual BackendInfo describe() const = 0;
  // At most one listener; null clears it. The backend never owns it.
  virtual void setListener(IFloatingListener* listener) = 0;

  // ---- main window ----
  // The DockHost registers the widget that is the main content area (called from its onAttached;
  // again with an invalid widget when it detaches).
  virtual void setMainContent(UiContext& ui, core::tree::WidgetId contentWidget) = 0;
  // Screen rectangle of the main content area (empty until setMainContent).
  virtual dock::Rect mainContentRect() const = 0;

  // ---- windows ----
  virtual FloatCreateResult createWindow(const FloatRequest& request) = 0;
  // False for an unknown id or kMainWindow. Idempotent for ids already gone.
  virtual bool destroyWindow(FloatId window) = 0;
  // Moves and resizes; the backend enforces its own limits (minimum size, at most the screen) and
  // contentRect() tells what it did. False for an unknown id.
  virtual bool setContentRect(FloatId window, const dock::Rect& contentRect) = 0;
  virtual std::optional<dock::Rect> contentRect(FloatId window) const = 0;
  // kMainWindow returns the main content widget.
  virtual std::optional<FloatContent> content(FloatId window) const = 0;
  virtual bool bringToFront(FloatId window) = 0;
  // Floating windows from bottom to top (the main window is not listed).
  virtual std::vector<FloatId> stacking() const = 0;
  virtual bool setTitle(FloatId window, std::string_view title) = 0;
  virtual bool setMaximized(FloatId window, bool maximized) = 0;
  virtual bool isMaximized(FloatId window) const = 0;
  // A hidden window is not drawn and takes no part in topmostWindowAt (the dock hides the window of a
  // dragged tab, spec 02 rule 35). It must stay alive with its widgets, and a pointer capture held
  // by one of them must survive: the tab drag that caused the hiding is still running in them.
  virtual bool setVisible(FloatId window, bool visible) = 0;

  // ---- coordinates and queries ----
  virtual dock::Point toScreen(FloatId window, dock::Point local) const = 0;
  virtual dock::Point toWindow(FloatId window, dock::Point screen) const = 0;
  // The window (kMainWindow included) showing `screen`, topmost first; windows in `exclude` and
  // hidden ones are skipped. "Showing" includes the frame the backend draws around the content.
  virtual std::optional<FloatId> topmostWindowAt(dock::Point screen, std::span<const FloatId> exclude) const = 0;
  // Name of the monitor showing `screen` (stored with the layout, spec 04 rule 4); may be empty.
  virtual std::string monitorAt(dock::Point screen) const = 0;

  // ---- pointer tracking (only when describe().pointerTracking) ----
  virtual bool beginPointerTracking(IPointerSink& sink) = 0;
  virtual void endPointerTracking() = 0;
};

}  // namespace r1ui::widgets

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: frame-related message handling for Win32 windows: removal of the OS frame for borderless
//   windows (WM_NCCALCSIZE), the non-client hit test fed by the toolkit's chrome description,
//   non-client pointer messages translated to client pointer events, press/release handling of
//   the minimize/maximize/close buttons, and the minimum tracking size.
// Why: a borderless window keeps WS_THICKFRAME/WS_CAPTION styles so Windows still provides edge
//   resize, snapping, Windows 11 snap layouts (when the maximize button hit-tests as
//   HTMAXBUTTON), maximize/restore and the DWM shadow; only the visible frame is removed.
// Callers: Impl::dispatch (WindowProc.cpp).
// Invariants: when maximized, a borderless window's client area is inset by the invisible resize
//   border so no content is clipped off-screen; with an auto-hide taskbar on an edge, one pixel
//   on that edge is left uncovered so the taskbar can still be summoned.
#include "WindowImpl.h"

#include <shellapi.h>
#include <windowsx.h>

namespace r1ui::platform {

namespace {

// Invisible resize border thickness at `dpi` (what Windows adds around a maximized window).
void frameThickness(UINT dpi, int& cx, int& cy) {
  cx = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
  cy = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

// Leaves one pixel uncovered on every monitor edge that hosts an auto-hide taskbar.
void leaveAutoHideEdge(HWND hwnd, RECT& client) {
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi) == FALSE) return;
  APPBARDATA bar{};
  bar.cbSize = sizeof(bar);
  bar.rc = mi.rcMonitor;
  const auto hasBar = [&](UINT edge) {
    bar.uEdge = edge;
    return SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &bar) != 0;
  };
  if (hasBar(ABE_LEFT)) client.left += 1;
  if (hasBar(ABE_TOP)) client.top += 1;
  if (hasBar(ABE_RIGHT)) client.right -= 1;
  if (hasBar(ABE_BOTTOM)) client.bottom -= 1;
}

LRESULT hitCodeFor(HitZone zone, bool resizable) {
  switch (zone) {
    case HitZone::Client: return HTCLIENT;
    case HitZone::Caption: return HTCAPTION;
    case HitZone::MinimizeButton: return HTMINBUTTON;
    case HitZone::MaximizeButton: return resizable ? HTMAXBUTTON : HTCLIENT;
    case HitZone::CloseButton: return HTCLOSE;
    case HitZone::ResizeLeft: return HTLEFT;
    case HitZone::ResizeRight: return HTRIGHT;
    case HitZone::ResizeTop: return HTTOP;
    case HitZone::ResizeBottom: return HTBOTTOM;
    case HitZone::ResizeTopLeft: return HTTOPLEFT;
    case HitZone::ResizeTopRight: return HTTOPRIGHT;
    case HitZone::ResizeBottomLeft: return HTBOTTOMLEFT;
    case HitZone::ResizeBottomRight: return HTBOTTOMRIGHT;
  }
  return HTCLIENT;
}

// Chrome button for a non-client hit code, or Client when the code is not a button.
HitZone buttonZoneFor(WPARAM hitCode) {
  switch (hitCode) {
    case HTMINBUTTON: return HitZone::MinimizeButton;
    case HTMAXBUTTON: return HitZone::MaximizeButton;
    case HTCLOSE: return HitZone::CloseButton;
    default: return HitZone::Client;
  }
}

}  // namespace

HitZone Window::Impl::zoneAt(Point clientPoint) const {
  return classifyHit(chrome, clientSize(), clientPoint, dpiScaleFromDpi(dpi), resizable, IsZoomed(hwnd) != FALSE);
}

// A chrome button acts only if the pointer is released over the same button it was pressed on.
void Window::Impl::finishChromePress(HitZone pressed, Point clientPoint) {
  chromePressed = HitZone::Client;
  if (pressed != HitZone::Client && zoneAt(clientPoint) == pressed) runChromeCommand(pressed);
}

void Window::Impl::runChromeCommand(HitZone zone) {
  switch (zone) {
    case HitZone::MinimizeButton: ShowWindow(hwnd, SW_MINIMIZE); break;
    case HitZone::MaximizeButton:
      if (resizable) ShowWindow(hwnd, IsZoomed(hwnd) != FALSE ? SW_RESTORE : SW_MAXIMIZE);
      break;
    case HitZone::CloseButton: PostMessageW(hwnd, WM_CLOSE, 0, 0); break;
    default: break;
  }
}

bool Window::Impl::handleFrame(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
  result = 0;
  switch (msg) {
    case WM_NCCALCSIZE: {
      // Returning 0 with the proposed rectangle makes the client area the whole window.
      if (!borderless || wp == FALSE) return false;
      auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
      if (params != nullptr && IsZoomed(hwnd) != FALSE) {
        int cx = 0;
        int cy = 0;
        frameThickness(GetDpiForWindow(hwnd), cx, cy);
        RECT& client = params->rgrc[0];
        client.left += cx;
        client.right -= cx;
        client.top += cy;
        client.bottom -= cy;
        leaveAutoHideEdge(hwnd, client);
      }
      return true;
    }
    case WM_NCHITTEST: {
      if (!borderless || IsIconic(hwnd) != FALSE) return false;
      const Point p = screenToClient(lp);
      const Rect client{0, 0, width, height};
      if (!client.contains(p)) return false;
      result = hitCodeFor(zoneAt(p), resizable);
      return true;
    }
    case WM_GETMINMAXINFO: {
      auto* info = reinterpret_cast<MINMAXINFO*>(lp);
      if (info == nullptr || minClientSize.width <= 0 || minClientSize.height <= 0) return false;
      const UINT currentDpi = GetDpiForWindow(hwnd);
      const float scale = dpiScaleFromDpi(currentDpi);
      RECT r{0, 0, minSizeLogical ? logicalToPhysical(static_cast<float>(minClientSize.width), scale) : minClientSize.width,
             minSizeLogical ? logicalToPhysical(static_cast<float>(minClientSize.height), scale) : minClientSize.height};
      if (!borderless) {
        AdjustWindowRectExForDpi(&r, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE,
                                 static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)), currentDpi);
      }
      info->ptMinTrackSize = {r.right - r.left, r.bottom - r.top};
      return true;
    }
    case WM_NCMOUSEMOVE: {
      // The pointer is over a non-client zone (caption, resize band, chrome button): the toolkit
      // still needs hover positions, so report them as ordinary moves, then let the OS proceed
      // (snap layouts and resize cursors depend on the default handling).
      if (!borderless) return false;
      armLeaveTracking(true);
      const Point p = screenToClient(lp);
      pushMouse(MouseEvent::Type::Move, MouseButton::Left, static_cast<float>(p.x), static_cast<float>(p.y));
      return false;
    }
    case WM_NCMOUSELEAVE:
      if (!borderless) return false;
      trackingNonClientLeave = false;
      if (!pointerOverWindow()) pushEvent(makeEvent(EventType::MouseLeave));
      return false;
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK: {
      // Chrome buttons are tracked here (press, capture, act on release over the same button) so
      // the toolkit can draw pressed state; caption and resize presses stay with the OS.
      const HitZone zone = buttonZoneFor(wp);
      if (!borderless || zone == HitZone::Client) return false;
      const Point p = screenToClient(lp);
      chromePressed = zone;
      buttonChange(MouseButton::Left, true, static_cast<float>(p.x), static_cast<float>(p.y));
      return true;
    }
    default:
      return false;
  }
}

}  // namespace r1ui::platform

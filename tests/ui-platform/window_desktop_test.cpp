// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: desktop-level oracle for the Win32 Window: creation options, borderless geometry,
//   non-client hit-testing, chrome buttons, maximize inset, the unified event stream driven by
//   posted messages, bounded queues under flood, several windows on one thread, DPI change,
//   cursors, clipboard, and hostile descriptors. Messages are posted/sent directly to the window,
//   so no real input device is needed, but a window must be creatable.
// Why: these behaviors cannot be proven without an OS window; the fast tier stays desktop-free.
// Callers: CTest (label desktop). Prints SKIPPED and exits 0 when no interactive desktop exists
//   (service session, locked workstation, remote session without a display).
// Side effects: the clipboard is saved and restored around the clipboard case.
#include <windows.h>
#include <windowsx.h>

#include <cstdio>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/platform/Monitors.h"
#include "r1ui/platform/Window.h"

using namespace r1ui::platform;
using platform_test::expect;
using platform_test::runCase;

namespace {

// ---- Environment and message helpers ----

bool desktopAvailable() {
  USEROBJECTFLAGS flags{};
  DWORD needed = 0;
  if (GetUserObjectInformationW(GetProcessWindowStation(), UOI_FLAGS, &flags, sizeof(flags), &needed) == FALSE ||
      (flags.dwFlags & WSF_VISIBLE) == 0) {
    return false;
  }
  HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_SWITCHDESKTOP);
  if (input == nullptr) return false;  // locked workstation or no access to the input desktop
  CloseDesktop(input);
  return GetSystemMetrics(SM_CMONITORS) > 0;
}

HWND handleOf(const Window& w) { return static_cast<HWND>(w.nativeHandle().window); }
LPARAM pack(int x, int y) { return MAKELPARAM(x, y); }
void post(const Window& w, UINT msg, WPARAM wp, LPARAM lp) { PostMessageW(handleOf(w), msg, wp, lp); }
LRESULT send(const Window& w, UINT msg, WPARAM wp, LPARAM lp) { return SendMessageW(handleOf(w), msg, wp, lp); }

// Drains the thread queue (several rounds: one pump handles a bounded number of messages).
void pumpAll(Window& w) {
  for (int i = 0; i < 4; ++i) w.pumpEvents();
}

// Events of the listed types only: real OS focus / move / leave events may interleave.
std::vector<Event> only(std::vector<Event> events, std::initializer_list<EventType> types) {
  std::vector<Event> out;
  for (const Event& e : events) {
    for (EventType t : types) {
      if (e.type == t) out.push_back(e);
    }
  }
  return out;
}

// Pumps until the window is shown and its startup events (focus, size, move) are consumed.
void settle(Window& w) {
  pumpAll(w);
  w.takeEvents();
  w.takeKeyEvents();
  w.takeMouseClicks();
  w.takeMouseEvents();
}

size_t liveThreadWindows() {
  size_t count = 0;
  EnumThreadWindows(
      GetCurrentThreadId(),
      [](HWND, LPARAM p) -> BOOL {
        ++*reinterpret_cast<size_t*>(p);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&count));
  return count;
}

ChromeLayout titleBar400() {
  ChromeLayout l;
  l.captionRects = {{0, 0, 400, 32}};
  l.minimizeButton = {256, 0, 48, 32};
  l.maximizeButton = {304, 0, 48, 32};
  l.closeButton = {352, 0, 48, 32};
  return l;
}

WindowDesc borderlessDesc() {
  WindowDesc d;
  d.title = "r1ui platform test";
  d.width = 400;
  d.height = 300;
  d.borderless = true;
  d.position = Point{100, 100};
  return d;
}

LRESULT hitAt(const Window& w, int clientX, int clientY) {
  const Rect r = w.windowRect();  // borderless and restored: client origin == window origin
  return send(w, WM_NCHITTEST, 0, pack(r.x + clientX, r.y + clientY));
}

// Simulates the OS sending a non-client press on a chrome button and the later client release.
void pressChrome(Window& w, WPARAM hitCode, int downX, int downY, int upX, int upY) {
  const Rect r = w.windowRect();
  send(w, WM_NCLBUTTONDOWN, hitCode, pack(r.x + downX, r.y + downY));
  send(w, WM_LBUTTONUP, 0, pack(upX, upY));
}

// ---- Cases ----

void borderlessGeometryAndOptions() {
  Window w(borderlessDesc());
  settle(w);
  expect(w.clientWidth() == 400 && w.clientHeight() == 300, "borderless client size is exactly the requested size");
  const Rect r = w.windowRect();
  expect(r == Rect{100, 100, 400, 300}, "borderless window rectangle equals the client rectangle at the requested position");
  expect(w.dpiScale() > 0.0f, "dpi scale known");
  expect(!w.isMaximized() && !w.isMinimized(), "starts restored");
  expect(w.setWindowRect({150, 160, 500, 350}) && w.windowRect() == Rect{150, 160, 500, 350}, "setWindowRect moves and sizes");
  expect(!w.setWindowRect({0, 0, 0, 10}) && !w.setWindowRect({0, 0, 10, -1}), "empty rectangles are rejected");

  WindowDesc tool = borderlessDesc();
  tool.toolWindow = true;
  Window t(tool);
  expect((GetWindowLongPtrW(handleOf(t), GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0, "tool window has no taskbar button");
  expect((GetWindowLongPtrW(handleOf(w), GWL_EXSTYLE) & WS_EX_TOOLWINDOW) == 0, "ordinary window keeps its taskbar button");

  WindowDesc fixed = borderlessDesc();
  fixed.resizable = false;
  Window f(fixed);
  expect((GetWindowLongPtrW(handleOf(f), GWL_STYLE) & (WS_THICKFRAME | WS_MAXIMIZEBOX)) == 0, "non-resizable has no sizing frame");
  f.maximizeToggle();
  expect(!f.isMaximized(), "non-resizable window ignores maximize");
}

void nativeFrameIsTheDefault() {
  WindowDesc d;
  d.width = 500;
  d.height = 320;
  Window w(d);
  settle(w);
  expect(w.clientWidth() == 500 && w.clientHeight() == 320, "native client size is the requested size");
  const Rect r = w.windowRect();
  expect(r.width > 500 && r.height > 320, "native window has a visible frame");
  expect((GetWindowLongPtrW(handleOf(w), GWL_STYLE) & WS_CAPTION) == WS_CAPTION, "native window has a caption");
  expect(send(w, WM_NCHITTEST, 0, pack(r.x + 10, r.y + 3)) != HTCLIENT, "title bar area is not client in a native window");
  WindowDesc withMin = d;
  withMin.minSize = Size{320, 240};
  Window m(withMin);
  MINMAXINFO info{};
  send(m, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&info));
  expect(info.ptMinTrackSize.x > 320 && info.ptMinTrackSize.y > 240, "native minimum track size includes the frame");
}

void logicalSizesScaleWithDpi() {
  WindowDesc d = borderlessDesc();
  Window w(d);
  settle(w);
  expect(w.clientWidth() == logicalToPhysical(400.0f, w.dpiScale()) && w.clientHeight() == logicalToPhysical(300.0f, w.dpiScale()),
         "logical size scaled by the opening monitor dpi");
  expect(w.logicalClientWidth() > 399.0f && w.logicalClientWidth() < 401.0f, "logical size reads back");
}

void minimumSizeIsEnforced() {
  WindowDesc d = borderlessDesc();
  d.minSize = Size{320, 240};
  Window w(d);
  MINMAXINFO info{};
  send(w, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&info));
  expect(info.ptMinTrackSize.x == 320 && info.ptMinTrackSize.y == 240, "borderless minimum track size is exact");
}

void hitTestingMatchesChromeLayout() {
  Window w(borderlessDesc());
  settle(w);
  expect(w.setChromeLayout(titleBar400()), "valid layout accepted");
  expect(hitAt(w, 100, 16) == HTCAPTION, "caption area drags");
  expect(hitAt(w, 280, 16) == HTMINBUTTON && hitAt(w, 330, 16) == HTMAXBUTTON && hitAt(w, 380, 16) == HTCLOSE, "buttons report their codes");
  expect(hitAt(w, 200, 150) == HTCLIENT, "body is client");
  expect(hitAt(w, 0, 150) == HTLEFT && hitAt(w, 399, 150) == HTRIGHT && hitAt(w, 200, 299) == HTBOTTOM && hitAt(w, 200, 0) == HTTOP,
         "edges resize");
  expect(hitAt(w, 0, 0) == HTTOPLEFT && hitAt(w, 399, 0) == HTTOPRIGHT && hitAt(w, 0, 299) == HTBOTTOMLEFT && hitAt(w, 399, 299) == HTBOTTOMRIGHT,
         "corners resize");
  expect(w.chromeZoneAt({330, 16}) == HitZone::MaximizeButton, "toolkit query agrees with the OS answer");

  ChromeLayout bad = titleBar400();
  bad.resizeBorderLogical = -1.0f;
  expect(!w.setChromeLayout(bad), "invalid layout rejected");
  expect(hitAt(w, 330, 16) == HTMAXBUTTON, "previous layout kept after a rejection");

  WindowDesc fixed = borderlessDesc();
  fixed.resizable = false;
  Window f(fixed);
  f.setChromeLayout(titleBar400());
  expect(hitAt(f, 0, 150) == HTCLIENT && hitAt(f, 330, 16) == HTCLIENT, "non-resizable: no resize edges and a disabled maximize button");
}

void chromeButtonsActOnRelease() {
  Window w(borderlessDesc());
  settle(w);
  w.setChromeLayout(titleBar400());

  // Press on close, release elsewhere: nothing happens.
  pressChrome(w, HTCLOSE, 380, 16, 100, 150);
  pumpAll(w);
  expect(w.pumpEvents() && only(w.takeEvents(), {EventType::CloseRequested}).empty(), "release off the button does not close");

  // Press and release on close: a close request, and the window closes.
  pressChrome(w, HTCLOSE, 380, 16, 380, 16);
  pumpAll(w);
  const auto events = w.takeEvents();
  expect(only(events, {EventType::CloseRequested}).size() == 1, "close button posts one close request");
  expect(only(events, {EventType::MouseDown, EventType::MouseUp}).size() == 2, "press and release reach the toolkit for pressed-state drawing");
  expect(!w.pumpEvents(), "the window is closed");

  Window m(borderlessDesc());
  settle(m);
  m.setChromeLayout(titleBar400());
  pressChrome(m, HTMINBUTTON, 280, 16, 280, 16);
  expect(m.isMinimized(), "minimize button minimizes");
  ShowWindow(handleOf(m), SW_RESTORE);
  expect(!m.isMinimized(), "restored");

  pressChrome(m, HTMAXBUTTON, 330, 16, 330, 16);
  expect(m.isMaximized(), "maximize button maximizes");
}

void maximizedClientFillsTheWorkArea() {
  Window w(borderlessDesc());
  settle(w);
  w.maximizeToggle();
  pumpAll(w);
  expect(w.isMaximized(), "maximized");
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  GetMonitorInfoW(MonitorFromWindow(handleOf(w), MONITOR_DEFAULTTONEAREST), &mi);
  const int workW = mi.rcWork.right - mi.rcWork.left;
  const int workH = mi.rcWork.bottom - mi.rcWork.top;
  // The client area is the work area (an auto-hide taskbar may take one pixel per edge).
  expect(w.clientWidth() <= workW && w.clientWidth() >= workW - 2, "maximized client width fills the work area, nothing clipped");
  expect(w.clientHeight() <= workH && w.clientHeight() >= workH - 2, "maximized client height fills the work area, nothing clipped");
  const Rect outer = w.windowRect();
  expect(outer.width > w.clientWidth(), "the invisible resize border lies outside the client area");
  w.setChromeLayout(titleBar400());
  expect(w.chromeZoneAt({0, 150}) == HitZone::Client, "no resize band while maximized");
  w.maximizeToggle();
  pumpAll(w);
  expect(!w.isMaximized() && w.clientWidth() == 400 && w.clientHeight() == 300, "restore returns the original size");
  w.minimize();
  pumpAll(w);
  expect(w.isMinimized() && w.clientWidth() == 0 && w.clientHeight() == 0, "minimized reports a 0x0 client");
}

void keyboardAndCharacters() {
  Window w(borderlessDesc());
  settle(w);
  constexpr LPARAM scan = (0x3F << 16);
  post(w, WM_KEYDOWN, VK_F5, 1 | scan);
  post(w, WM_KEYDOWN, VK_F5, 1 | scan | (1 << 30));  // auto-repeat
  post(w, WM_KEYUP, VK_F5, 1 | scan | (1 << 30) | (1 << 31));
  post(w, WM_KEYDOWN, VK_LEFT, 1 | (0x4B << 16) | (1 << 24));  // extended key
  post(w, WM_KEYDOWN, 0x1234, 1);                              // not a virtual key: ignored
  pumpAll(w);
  const auto keys = only(w.takeEvents(), {EventType::KeyDown, EventType::KeyUp});
  expect(keys.size() == 4, "four key events (the invalid virtual key is dropped)");
  expect(keys.size() == 4 && keys[0].type == EventType::KeyDown && keys[0].virtualKey == VK_F5 && keys[0].scanCode == 0x3F && !keys[0].repeat,
         "first press: vk, scancode, no repeat");
  expect(keys.size() == 4 && keys[1].repeat && keys[2].type == EventType::KeyUp && !keys[2].repeat, "repeat flag on the auto-repeat only");
  expect(keys.size() == 4 && keys[3].scanCode == (0x4B | 0xE000), "extended scancode bits");

  const auto legacy = w.takeKeyEvents();
  expect(legacy.size() == 3 && legacy[0].virtualKey == VK_F5 && legacy[2].virtualKey == VK_LEFT,
         "legacy key queue still works (key-downs only, invalid key dropped)");

  post(w, WM_CHAR, 'x', 1);
  post(w, WM_CHAR, 0xE9, 1);
  post(w, WM_CHAR, 0xD83D, 1);  // surrogate pair split across two WM_CHAR messages
  post(w, WM_CHAR, 0xDE00, 1);
  post(w, WM_CHAR, 0x08, 1);    // backspace: control character, filtered
  post(w, WM_CHAR, 0x0D, 1);    // enter
  post(w, WM_CHAR, 0x7F, 1);    // DEL
  post(w, WM_CHAR, 0xDC00, 1);  // unpaired low surrogate
  post(w, WM_CHAR, 0xD83D, 1);  // unpaired high surrogate followed by text
  post(w, WM_CHAR, 'y', 1);
  post(w, WM_CHAR, 0x110000, 1);  // not a UTF-16 unit: ignored
  pumpAll(w);
  const auto chars = only(w.takeEvents(), {EventType::Char});
  expect(chars.size() == 4, "x, e-acute, emoji, y");
  expect(chars.size() == 4 && chars[0].codePoint == U'x' && chars[1].codePoint == 0xE9 && chars[2].codePoint == 0x1F600 && chars[3].codePoint == U'y',
         "code points: BMP, accented, combined surrogate pair, text after an unpaired surrogate");
}

void pointerAndWheel() {
  Window w(borderlessDesc());
  settle(w);
  post(w, WM_MOUSEMOVE, 0, pack(10, 20));
  post(w, WM_MOUSEMOVE, 0, pack(11, 21));
  post(w, WM_LBUTTONDOWN, MK_LBUTTON, pack(11, 21));
  post(w, WM_LBUTTONUP, 0, pack(11, 21));
  post(w, WM_LBUTTONDBLCLK, MK_LBUTTON, pack(11, 21));
  post(w, WM_LBUTTONUP, 0, pack(11, 21));
  post(w, WM_RBUTTONDOWN, MK_RBUTTON, pack(-5, 300));
  post(w, WM_RBUTTONUP, 0, pack(-5, 300));
  post(w, WM_XBUTTONDOWN, MAKEWPARAM(MK_XBUTTON2, XBUTTON2), pack(1, 1));
  post(w, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), pack(1, 1));
  pumpAll(w);
  const auto ev = only(w.takeEvents(), {EventType::MouseMove, EventType::MouseDown, EventType::MouseUp, EventType::MouseDoubleClick});
  const EventType expected[] = {EventType::MouseMove,        EventType::MouseDown, EventType::MouseUp,   EventType::MouseDown,
                                EventType::MouseDoubleClick, EventType::MouseUp,   EventType::MouseDown, EventType::MouseUp,
                                EventType::MouseDown,        EventType::MouseUp};
  bool orderOk = ev.size() == std::size(expected);
  for (size_t i = 0; orderOk && i < ev.size(); ++i) orderOk = ev[i].type == expected[i];
  expect(orderOk, "events arrive in order, the two moves coalesce, a double click is Down then DoubleClick");
  expect(ev.size() == 10 && ev[0].x == 11.0f && ev[0].y == 21.0f, "coalesced move keeps the latest position");
  expect(ev.size() == 10 && ev[6].button == MouseButton::Right && ev[6].x == -5.0f && ev[6].y == 300.0f, "right button, negative coordinate preserved");
  expect(ev.size() == 10 && ev[8].button == MouseButton::X2, "extra button 2");
  expect(GetCapture() != handleOf(w), "capture released after the last button went up");

  const auto clicks = w.takeMouseClicks();
  expect(clicks.size() == 2 && clicks[0].x == 11.0f, "legacy click queue: two left presses");

  const Rect r = w.windowRect();
  post(w, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(static_cast<short>(60))), pack(r.x + 30, r.y + 40));
  post(w, WM_MOUSEHWHEEL, MAKEWPARAM(0, static_cast<WORD>(static_cast<short>(-120))), pack(r.x + 30, r.y + 40));
  pumpAll(w);
  const auto wheel = only(w.takeEvents(), {EventType::Wheel});
  expect(wheel.size() == 2, "two wheel events");
  expect(wheel.size() == 2 && wheel[0].wheelY == 0.5f && wheel[0].wheelX == 0.0f && wheel[0].x == 30.0f && wheel[0].y == 40.0f,
         "precise vertical delta in notches at client coordinates");
  expect(wheel.size() == 2 && wheel[1].wheelX == -1.0f && wheel[1].wheelY == 0.0f, "horizontal wheel to the left");
}

void focusSizeDpiAndClose() {
  Window w(borderlessDesc());
  settle(w);
  send(w, WM_SETFOCUS, 0, 0);
  send(w, WM_KILLFOCUS, 0, 0);
  const auto focus = only(w.takeEvents(), {EventType::FocusGained, EventType::FocusLost});
  expect(focus.size() == 2 && focus[0].type == EventType::FocusGained && focus[1].type == EventType::FocusLost, "focus gained then lost");

  expect(w.setWindowRect({120, 130, 520, 380}), "resize");
  const auto sized = only(w.takeEvents(), {EventType::Resized, EventType::Moved});
  bool sawSize = false;
  bool sawMove = false;
  for (const Event& e : sized) {
    sawSize = sawSize || (e.type == EventType::Resized && e.rect.width == 520 && e.rect.height == 380);
    sawMove = sawMove || (e.type == EventType::Moved && e.rect.x == 120 && e.rect.y == 130);
  }
  expect(sawSize && sawMove, "Resized carries the client size, Moved the outer rectangle");
  expect(w.consumeResized() && !w.consumeResized(), "legacy resize flag consumed once");

  RECT suggested{200, 210, 700, 610};
  send(w, WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&suggested));
  const auto dpi = only(w.takeEvents(), {EventType::DpiChanged});
  expect(dpi.size() == 1 && dpi[0].dpiScale == 1.5f, "dpi change event carries the new scale");
  expect(w.dpiScale() == 1.5f, "dpiScale follows");
  expect(w.windowRect() == Rect{200, 210, 500, 400}, "suggested rectangle applied");
  send(w, WM_DPICHANGED, MAKEWPARAM(96, 96), 0);  // hostile: no rectangle
  expect(w.dpiScale() == 1.5f, "a dpi change without a suggested rectangle is ignored");
  send(w, WM_DPICHANGED, MAKEWPARAM(96, 96), reinterpret_cast<LPARAM>(&suggested));
  expect(w.dpiScale() == 1.0f, "back to 100%");
  w.takeEvents();

  w.setCloseNeedsConfirmation(true);
  w.requestClose();
  pumpAll(w);
  expect(w.pumpEvents() && only(w.takeEvents(), {EventType::CloseRequested}).size() == 1, "close request is reported and the window stays");
  w.confirmClose();
  expect(!w.pumpEvents(), "confirmed close ends the pump");
}

// waitForEvents sleeps until a message arrives; the live callback runs during the OS move/size
// loop (simulated with its enter/exit messages), on size changes and on the timer, never after
// the loop ended, and an exception inside it does not escape the window procedure.
void waitAndLiveCallback() {
  Window w(borderlessDesc());
  settle(w);
  while (w.waitForEvents(0)) pumpAll(w);  // drain whatever the OS queued at startup
  const DWORD t0 = GetTickCount();
  expect(!w.waitForEvents(60) && GetTickCount() - t0 >= 40, "an idle wait times out instead of returning at once");
  post(w, WM_NULL, 0, 0);
  expect(w.waitForEvents(2000), "a posted message wakes the wait");
  pumpAll(w);

  int calls = 0;
  w.setLiveCallback([&] {
    ++calls;
    throw std::runtime_error("must not escape the window procedure");
  });
  send(w, WM_ENTERSIZEMOVE, 0, 0);
  expect(w.setWindowRect({100, 100, 500, 400}) && calls >= 1, "a size change inside the OS loop calls the callback");
  const int afterSize = calls;
  const DWORD start = GetTickCount();
  while (calls < afterSize + 2 && GetTickCount() - start < 1000) {
    w.waitForEvents(50);
    w.pumpEvents();
  }
  expect(calls >= afterSize + 2, "the timer keeps calling the callback while the loop runs");
  send(w, WM_EXITSIZEMOVE, 0, 0);
  const int afterExit = calls;
  expect(w.setWindowRect({120, 120, 520, 420}) && calls == afterExit, "no callback after the loop ended");
  w.setLiveCallback({});
  send(w, WM_ENTERSIZEMOVE, 0, 0);
  w.setWindowRect({140, 140, 540, 440});
  send(w, WM_EXITSIZEMOVE, 0, 0);
  expect(calls == afterExit, "a removed callback is not called");
}

void queuesAreBoundedUnderFlood() {
  Window w(borderlessDesc());
  settle(w);
  constexpr int kFlood = 5000;
  for (int i = 0; i < kFlood; ++i) post(w, WM_KEYDOWN, static_cast<WPARAM>(0x70 + (i % 12)), 1);
  pumpAll(w);
  const auto events = w.takeEvents();
  const auto legacy = w.takeKeyEvents();
  expect(events.size() == kMaxQueuedWindowEvents, "unified queue holds exactly its bound");
  expect(legacy.size() == kMaxQueuedEvents, "legacy key queue holds exactly its bound");
  expect(!events.empty() && events.back().virtualKey == static_cast<uint32_t>(0x70 + ((kFlood - 1) % 12)), "newest event survived");
  const size_t minimumDropped = (kFlood - kMaxQueuedWindowEvents) + (kFlood - kMaxQueuedEvents);
  expect(w.droppedEventCount() >= minimumDropped, "drops are counted");
}

void severalWindowsOneThread() {
  Window a(borderlessDesc());
  WindowDesc other = borderlessDesc();
  other.position = Point{600, 100};
  Window b(other);
  settle(a);
  settle(b);
  post(b, WM_CHAR, 'q', 1);
  post(a, WM_CHAR, 'p', 1);
  a.pumpEvents();  // pumping any window dispatches the whole thread queue
  const auto ea = only(a.takeEvents(), {EventType::Char});
  const auto eb = only(b.takeEvents(), {EventType::Char});
  expect(ea.size() == 1 && ea[0].codePoint == U'p', "window A sees only its own event");
  expect(eb.size() == 1 && eb[0].codePoint == U'q', "window B received its event although A pumped");
  a.requestClose();
  pumpAll(b);
  expect(!a.pumpEvents() && b.pumpEvents(), "closing A leaves B running");
}

void cursorsFollowTheShape() {
  Window w(borderlessDesc());
  settle(w);
  struct Case {
    CursorShape shape;
    const wchar_t* id;
  };
  const Case cases[] = {{CursorShape::Arrow, IDC_ARROW},       {CursorShape::ResizeHorizontal, IDC_SIZEWE}, {CursorShape::ResizeVertical, IDC_SIZENS},
                        {CursorShape::Text, IDC_IBEAM},        {CursorShape::Hand, IDC_HAND},               {CursorShape::ResizeNwSe, IDC_SIZENWSE},
                        {CursorShape::ResizeNeSw, IDC_SIZENESW}, {CursorShape::Move, IDC_SIZEALL},          {CursorShape::NotAllowed, IDC_NO},
                        {CursorShape::Wait, IDC_WAIT}};
  for (const Case& c : cases) {
    w.setCursor(c.shape);
    const LRESULT handled = send(w, WM_SETCURSOR, reinterpret_cast<WPARAM>(handleOf(w)), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    expect(handled == TRUE && GetCursor() == LoadCursorW(nullptr, c.id), "cursor matches the requested shape");
  }
}

void clipboardRoundTripAndHostileText() {
  Window w(borderlessDesc());
  settle(w);
  const ClipboardText saved = w.getClipboardText();  // restored at the end
  const std::string text = "h\xC3\xA9llo \xF0\x9F\x98\x80 line\nnext";
  const ClipboardStatus set = w.setClipboardText(text);
  if (set == ClipboardStatus::Busy) {
    std::fprintf(stderr, "note: clipboard held by another process; round trip not checked\n");
  } else {
    expect(set == ClipboardStatus::Ok, "set succeeds");
    const ClipboardText got = w.getClipboardText();
    expect(got.status == ClipboardStatus::Ok && got.text == text, "UTF-8 text survives the round trip");

    expect(w.setClipboardText("\xC3\x28") == ClipboardStatus::InvalidText, "invalid UTF-8 rejected");
    expect(w.setClipboardText(std::string("a\0b", 3)) == ClipboardStatus::InvalidText, "embedded NUL rejected");
    expect(w.setClipboardText(std::string(kMaxClipboardTextBytes + 1, 'a')) == ClipboardStatus::TooLarge, "over the size limit rejected");
    const ClipboardText after = w.getClipboardText();
    expect(after.status == ClipboardStatus::Ok && after.text == text, "rejections leave the previous content intact");

    expect(w.setClipboardText("") == ClipboardStatus::Ok && w.getClipboardText().text.empty(), "empty text is allowed");
  }
  // Put back what the user had (text only: other formats cannot be restored here).
  if (saved.status == ClipboardStatus::Ok) w.setClipboardText(saved.text);
}

void hostileDescriptorsAreRejectedWithoutLeaks() {
  const size_t before = liveThreadWindows();
  const auto throwsInvalid = [](WindowDesc d) {
    try {
      Window w(d);
    } catch (const std::invalid_argument&) {
      return true;
    } catch (...) {
      return false;
    }
    return false;
  };
  WindowDesc d = borderlessDesc();
  d.width = 0;
  expect(throwsInvalid(d), "zero width");
  d = borderlessDesc();
  d.height = -5;
  expect(throwsInvalid(d), "negative height");
  d = borderlessDesc();
  d.width = 100000;
  expect(throwsInvalid(d), "absurd width");
  d = borderlessDesc();
  d.title = "\xC3\x28";
  expect(throwsInvalid(d), "invalid UTF-8 title");
  d = borderlessDesc();
  d.minSize = Size{0, 100};
  expect(throwsInvalid(d), "zero minimum width");
  d = borderlessDesc();
  d.width = 32768;
  d.height = 32769;  // one over the limit
  expect(throwsInvalid(d), "height one over the limit");
  expect(liveThreadWindows() == before, "no window leaked by a rejected descriptor");

  Window ok(borderlessDesc());
  expect(liveThreadWindows() > before, "a valid window is created after rejections");
  bool threw = false;
  try {
    ok.setTitle("\xC3\x28");
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expect(threw, "setTitle rejects invalid UTF-8 and the window survives");
  ok.setTitle("fine \xE2\x82\xAC");
  expect(ok.pumpEvents(), "window still alive");
}

void monitorsAreEnumerated() {
  const auto monitors = enumerateMonitors();
  expect(!monitors.empty() && monitors[0].primary, "at least one monitor, primary first");
  for (const MonitorInfo& m : monitors) {
    expect(!m.bounds.empty() && !m.workArea.empty() && m.dpiScale >= 1.0f, "monitor rectangles and scale are sane");
  }
}

}  // namespace

int main() {
  if (!desktopAvailable()) {
    std::printf("SKIPPED: no interactive desktop in this session\n");
    return 0;
  }
  runCase("borderless_geometry_and_options", borderlessGeometryAndOptions);
  runCase("native_frame_is_default", nativeFrameIsTheDefault);
  runCase("logical_sizes", logicalSizesScaleWithDpi);
  runCase("minimum_size", minimumSizeIsEnforced);
  runCase("hit_testing", hitTestingMatchesChromeLayout);
  runCase("chrome_buttons", chromeButtonsActOnRelease);
  runCase("maximized_client", maximizedClientFillsTheWorkArea);
  runCase("keyboard_and_characters", keyboardAndCharacters);
  runCase("pointer_and_wheel", pointerAndWheel);
  runCase("focus_size_dpi_close", focusSizeDpiAndClose);
  runCase("wait_and_live_callback", waitAndLiveCallback);
  runCase("queues_bounded", queuesAreBoundedUnderFlood);
  runCase("several_windows", severalWindowsOneThread);
  runCase("cursors", cursorsFollowTheShape);
  runCase("clipboard", clipboardRoundTripAndHostileText);
  runCase("hostile_descriptors", hostileDescriptorsAreRejectedWithoutLeaks);
  runCase("monitors", monitorsAreEnumerated);
  return platform_test::finish("ui-platform desktop test");
}

// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the Win32 implementation of r1ui::platform::Window's lifecycle and public API: class
//   registration, window creation (native or borderless, tool window, DPI-aware sizing), message
//   pumping, commands, geometry, chrome layout, event queue access, clipboard delegation.
// Why: first backend for Windows-first delivery; behind the neutral Window.h interface. Message
//   handling is in WindowProc.cpp and WindowChrome.cpp.
// Callers: any module via Window.h. Calls: user32, dwmapi, shell32, shcore.
// Lifetime: Impl owns the HWND and destroys it in its destructor, so a constructor that throws
//   after creating the window still cleans up; the window proc reaches Impl through
//   GWLP_USERDATA, which is cleared before the HWND is destroyed.
#include <dwmapi.h>

#include <stdexcept>
#include <utility>

#include "ClipboardWin32.h"
#include "WindowImpl.h"

namespace r1ui::platform {

namespace {

constexpr wchar_t kClassName[] = L"R1GUI.Window";
constexpr int kMaxWindowDimension = 32768;
// One pump handles at most this many messages so a message flood cannot starve the caller's frame.
constexpr int kMaxMessagesPerPump = 4096;

// Converts UTF-8 to UTF-16 for the OS; invalid text is rejected, never truncated.
std::wstring widen(const std::string& utf8) {
  const auto utf16 = utf8ToUtf16(utf8);
  if (!utf16) throw std::invalid_argument("Window: invalid UTF-8 title");
  return std::wstring(utf16->begin(), utf16->end());
}

void registerWindowClass(HINSTANCE instance, WNDPROC proc) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kClassName;
  if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    throw std::runtime_error("Window: RegisterClassEx failed");
  }
}

void validateDesc(const WindowDesc& desc) {
  const auto bad = [](int v) { return v <= 0 || v > kMaxWindowDimension; };
  if (bad(desc.width) || bad(desc.height)) throw std::invalid_argument("Window: invalid initial size");
  if (desc.minSize && (bad(desc.minSize->width) || bad(desc.minSize->height))) {
    throw std::invalid_argument("Window: invalid minimum size");
  }
}

}  // namespace

Window::Impl::~Impl() {
  if (hwnd != nullptr) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);  // no callbacks into a dying Impl
    DestroyWindow(hwnd);
  }
}

// ---- Construction ----

Window::Window(const WindowDesc& desc) : impl_(std::make_unique<Impl>()) {
  validateDesc(desc);
  const std::wstring title = widen(desc.title);

  // Per-monitor-v2 awareness so sizes are physical pixels and windows can cross monitors. Fails
  // harmlessly when the process already has an awareness (manifest or an earlier window).
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  registerWindowClass(instance, &Impl::proc);

  Impl& impl = *impl_;
  impl.borderless = desc.borderless;
  impl.resizable = desc.resizable;
  if (desc.minSize) impl.minClientSize = *desc.minSize;
  impl.minSizeLogical = desc.sizesAreLogical;

  // The monitor the window will open on decides the dpi used to turn logical sizes physical and
  // to size the native frame.
  const POINT anchor{desc.position ? desc.position->x : 0, desc.position ? desc.position->y : 0};
  const UINT openDpi = monitorDpi(MonitorFromPoint(anchor, MONITOR_DEFAULTTOPRIMARY));
  const float openScale = dpiScaleFromDpi(openDpi);
  const int clientW = desc.sizesAreLogical ? logicalToPhysical(static_cast<float>(desc.width), openScale) : desc.width;
  const int clientH = desc.sizesAreLogical ? logicalToPhysical(static_cast<float>(desc.height), openScale) : desc.height;
  if (clientW <= 0 || clientW > kMaxWindowDimension || clientH <= 0 || clientH > kMaxWindowDimension) {
    throw std::invalid_argument("Window: scaled size out of range");
  }

  // Borderless windows keep the full overlapped style: that is what makes Windows provide edge
  // resize, snapping and the shadow. WM_NCCALCSIZE removes the visible frame.
  DWORD style = WS_OVERLAPPEDWINDOW;
  if (!desc.resizable) style &= ~static_cast<DWORD>(WS_THICKFRAME | WS_MAXIMIZEBOX);
  const DWORD exStyle = desc.toolWindow ? WS_EX_TOOLWINDOW : 0;
  RECT rect{0, 0, clientW, clientH};
  if (!desc.borderless) AdjustWindowRectExForDpi(&rect, style, FALSE, exStyle, openDpi);

  impl.hwnd = CreateWindowExW(exStyle, kClassName, title.c_str(), style,
                              desc.position ? desc.position->x : CW_USEDEFAULT,
                              desc.position ? desc.position->y : CW_USEDEFAULT, rect.right - rect.left,
                              rect.bottom - rect.top, nullptr, nullptr, instance, &impl);
  if (impl.hwnd == nullptr) throw std::runtime_error("Window: CreateWindowEx failed");

  impl.dpi = GetDpiForWindow(impl.hwnd);
  if (desc.borderless) {
    // A 1 px frame extension is what gives a frameless window its DWM shadow; failure only
    // loses the shadow, so the result is not an error. SWP_FRAMECHANGED applies the new frame.
    const MARGINS margins{0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(impl.hwnd, &margins);
    SetWindowPos(impl.hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  RECT client{};
  GetClientRect(impl.hwnd, &client);
  impl.width = client.right;
  impl.height = client.bottom;
  ShowWindow(impl.hwnd, SW_SHOW);
}

Window::~Window() = default;

// ---- Pumping and simple state ----

bool Window::pumpEvents() {
  MSG msg;
  for (int handled = 0; handled < kMaxMessagesPerPump && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); ++handled) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return !impl_->closed;
}

int Window::clientWidth() const { return impl_->width; }
int Window::clientHeight() const { return impl_->height; }
bool Window::consumeResized() {
  const bool was = impl_->resized;
  impl_->resized = false;
  return was;
}
float Window::mouseX() const { return impl_->mouseX; }
float Window::mouseY() const { return impl_->mouseY; }
bool Window::escapePressed() const { return impl_->escape; }
float Window::dpiScale() const { return dpiScaleFromDpi(impl_->dpi); }

// ---- Geometry and commands ----

Rect Window::windowRect() const {
  RECT r{};
  if (GetWindowRect(impl_->hwnd, &r) == FALSE) return {};
  return {r.left, r.top, r.right - r.left, r.bottom - r.top};
}

bool Window::setWindowRect(const Rect& rect) {
  if (rect.empty()) return false;
  return SetWindowPos(impl_->hwnd, nullptr, rect.x, rect.y, rect.width, rect.height,
                      SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
}

void Window::minimize() { ShowWindow(impl_->hwnd, SW_MINIMIZE); }
void Window::maximizeToggle() {
  if (!impl_->resizable) return;
  ShowWindow(impl_->hwnd, (IsZoomed(impl_->hwnd) != FALSE) ? SW_RESTORE : SW_MAXIMIZE);
}
bool Window::isMaximized() const { return IsZoomed(impl_->hwnd) != FALSE; }
bool Window::isMinimized() const { return IsIconic(impl_->hwnd) != FALSE; }
void Window::requestClose() { PostMessageW(impl_->hwnd, WM_CLOSE, 0, 0); }
void Window::setCloseNeedsConfirmation(bool enabled) { impl_->needsConfirm = enabled; }
void Window::confirmClose() { impl_->closed = true; }

// ---- Chrome ----

bool Window::setChromeLayout(const ChromeLayout& layout) {
  if (!isValidChromeLayout(layout)) return false;
  impl_->chrome = layout;
  return true;
}

HitZone Window::chromeZoneAt(Point clientPoint) const { return impl_->zoneAt(clientPoint); }

// ---- Events and cursor ----

std::vector<Event> Window::takeEvents() { return impl_->events.drain(); }
size_t Window::droppedEventCount() const {
  return impl_->events.dropped() + impl_->keyQueue.dropped() + impl_->clickQueue.dropped() +
         impl_->mouseQueue.dropped();
}
std::vector<KeyEvent> Window::takeKeyEvents() { return impl_->keyQueue.drain(); }
std::vector<MouseClick> Window::takeMouseClicks() { return impl_->clickQueue.drain(); }
std::vector<MouseEvent> Window::takeMouseEvents() { return impl_->mouseQueue.drain(); }
void Window::setCursor(CursorShape shape) { impl_->cursor = shape; }

void Window::setTitle(const std::string& utf8Title) {
  const std::wstring title = widen(utf8Title);
  SetWindowTextW(impl_->hwnd, title.c_str());
}

// ---- Clipboard and native handle ----

ClipboardText Window::getClipboardText() { return readClipboardText(impl_->hwnd); }
ClipboardStatus Window::setClipboardText(std::string_view utf8) { return writeClipboardText(impl_->hwnd, utf8); }

NativeHandle Window::nativeHandle() const { return {impl_->hwnd, GetModuleHandleW(nullptr)}; }

}  // namespace r1ui::platform

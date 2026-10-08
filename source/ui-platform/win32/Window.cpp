// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the Win32 implementation of r1ui::platform::Window (class registration, window proc,
//   per-monitor-v2 DPI awareness, size/mouse/key state).
// Why: first backend for Windows-first delivery; behind the neutral Window.h interface.
// Callers: any module via Window.h. Calls: user32/kernel32 only.
// Lifetime: Impl owns the HWND and destroys it in the Window destructor; the window proc
//   reaches Impl through GWLP_USERDATA, which is cleared before the HWND is destroyed.
#include "r1ui/platform/Window.h"

#include <windows.h>
#include <windowsx.h>

#include <stdexcept>

#include "r1ui/core/CheckedCast.h"

namespace r1ui::platform {

namespace {

constexpr wchar_t kClassName[] = L"R1GUI.Window";

// Converts UTF-8 to UTF-16 with explicit failure instead of silent truncation.
std::wstring widen(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int inLen = core::checkedCast<int>(utf8.size());
  const int needed =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), inLen, nullptr, 0);
  if (needed <= 0) throw std::runtime_error("Window: invalid UTF-8 title");
  std::wstring out(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), inLen, out.data(), needed);
  return out;
}

}  // namespace

struct Window::Impl {
  HWND hwnd = nullptr;
  int width = 0;
  int height = 0;
  bool resized = true;  // first frame always sees a size
  bool closed = false;
  bool escape = false;
  float mouseX = 0.0f;
  float mouseY = 0.0f;

  static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
      switch (msg) {
        case WM_SIZE:
          self->width = LOWORD(lp);
          self->height = HIWORD(lp);
          self->resized = true;
          return 0;
        case WM_MOUSEMOVE:
          self->mouseX = static_cast<float>(GET_X_LPARAM(lp));
          self->mouseY = static_cast<float>(GET_Y_LPARAM(lp));
          return 0;
        case WM_KEYDOWN:
          if (wp == VK_ESCAPE) self->escape = true;
          return 0;
        case WM_CLOSE:
          self->closed = true;
          return 0;
        default:
          break;
      }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
};

Window::Window(const WindowDesc& desc) : impl_(std::make_unique<Impl>()) {
  // Per-monitor-v2 awareness so sizes are physical pixels and panels can later cross monitors.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &Impl::proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kClassName;
  if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    throw std::runtime_error("Window: RegisterClassEx failed");
  }

  RECT rect{0, 0, desc.width, desc.height};
  AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForSystem());
  impl_->hwnd = CreateWindowExW(0, kClassName, widen(desc.title).c_str(), WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
                                rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
  if (impl_->hwnd == nullptr) throw std::runtime_error("Window: CreateWindowEx failed");

  SetWindowLongPtrW(impl_->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl_.get()));
  RECT client{};
  GetClientRect(impl_->hwnd, &client);
  impl_->width = client.right;
  impl_->height = client.bottom;
  ShowWindow(impl_->hwnd, SW_SHOW);
}

Window::~Window() {
  if (impl_ && impl_->hwnd != nullptr) {
    SetWindowLongPtrW(impl_->hwnd, GWLP_USERDATA, 0);  // no callbacks into a dying Impl
    DestroyWindow(impl_->hwnd);
  }
}

bool Window::pumpEvents() {
  MSG msg;
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
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
void Window::setTitle(const std::string& utf8Title) {
  SetWindowTextW(impl_->hwnd, widen(utf8Title).c_str());
}
NativeHandle Window::nativeHandle() const { return {impl_->hwnd, GetModuleHandleW(nullptr)}; }

}  // namespace r1ui::platform

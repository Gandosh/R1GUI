// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the Win32 side of the native backend's pointer sampling and z-order placement (see
//   NativeWin32.h).
// Invariants: no call here can throw; a failed OS call yields the neutral answer (pointer at the
//   origin with the button up, z-order unchanged).
// Callers: NativeFloatingBackend.
#include "NativeWin32.h"

#include <windows.h>

namespace r1ui::widgets::native::os {

PointerSample readPointer() {
  PointerSample sample;
  POINT p{0, 0};
  if (GetCursorPos(&p) == FALSE) return sample;
  sample.physical = {p.x, p.y};
  sample.leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;  // physical state: independent of which window has capture
  return sample;
}

bool isForeground(const platform::Window& window) {
  HWND w = static_cast<HWND>(window.nativeHandle().window);
  return w != nullptr && GetForegroundWindow() == w;
}

void placeAbove(platform::Window& window, platform::Window& sibling) {
  HWND w = static_cast<HWND>(window.nativeHandle().window);
  HWND s = static_cast<HWND>(sibling.nativeHandle().window);
  if (w == nullptr || s == nullptr || w == s) return;
  // The insert-after window is the one that ends up ABOVE the moved window, so the sibling is the one
  // moved: it goes directly below `window`.
  SetWindowPos(s, w, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

}  // namespace r1ui::widgets::native::os

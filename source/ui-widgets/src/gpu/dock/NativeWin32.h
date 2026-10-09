// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the few Win32 calls the native backend needs beyond ui-platform's Window: reading the pointer
//   (cursor position and left button state) and ordering one floating window directly above another
//   without activating it.
// Why: kept in one small file so the rest of the backend stays free of OS types and a port replaces
//   exactly this file.
// Callers: NativeFloatingBackend.
#pragma once

#include "r1ui/platform/Window.h"
#include "r1ui/widgets/dock/native/NativeFloatingBackend.h"

namespace r1ui::widgets::native::os {

// The OS pointer in screen physical pixels and the physical state of the left button.
PointerSample readPointer();

// True when the window is the one the user is working in right now (the OS foreground window).
bool isForeground(const platform::Window& window);

// Puts `window` directly above `sibling` in the z-order (no activation, no move or size change).
// Both must be windows of the same owner so the application's other windows keep their place.
void placeAbove(platform::Window& window, platform::Window& sibling);

}  // namespace r1ui::widgets::native::os

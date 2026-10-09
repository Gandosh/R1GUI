// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: declaration of the Win32 text clipboard access used by Window::get/setClipboardText.
// Why: keeps clipboard locking, retry and UTF-16 handling out of Window.cpp.
// Callers: Window.cpp. Not a public header.
// Invariants: never throws for clipboard contention; see Clipboard.h for the status meanings.
#pragma once

#include <windows.h>

#include <string_view>

#include "r1ui/platform/Clipboard.h"

namespace r1ui::platform {

ClipboardText readClipboardText(HWND owner);
ClipboardStatus writeClipboardText(HWND owner, std::string_view utf8);

}  // namespace r1ui::platform

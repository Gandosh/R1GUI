// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the result vocabulary and limits for text clipboard access (Window::getClipboardText /
//   setClipboardText).
// Why: the OS clipboard is shared and can be locked by other processes or hold hostile data;
//   callers get a status, never an exception, and never unvalidated text.
// Callers: Window.h and text-editing widgets.
// Invariants: text is UTF-8, valid, without NUL, at most kMaxClipboardTextBytes bytes.
#pragma once

#include <cstddef>
#include <string>

namespace r1ui::platform {

inline constexpr size_t kMaxClipboardTextBytes = 16u * 1024u * 1024u;

enum class ClipboardStatus {
  Ok,
  Empty,        // get: the clipboard holds no text
  Busy,         // another process kept the clipboard locked past the retry window
  TooLarge,     // text exceeds kMaxClipboardTextBytes
  InvalidText,  // not valid UTF-8 (set) / not valid UTF-16 (get), or contains NUL
  Failed        // any other OS failure (allocation, SetClipboardData)
};

struct ClipboardText {
  ClipboardStatus status = ClipboardStatus::Failed;
  std::string text;  // valid only when status == Ok
};

}  // namespace r1ui::platform

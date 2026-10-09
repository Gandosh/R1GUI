// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: reading and writing UTF-8 text through CF_UNICODETEXT with validation, a size limit and
//   a short retry when another process holds the clipboard.
// Why: the clipboard is process-shared state; contention and hostile content are normal and must
//   surface as a status, not an exception or a crash.
// Callers: Window::getClipboardText / setClipboardText.
// Invariants: the clipboard is closed on every exit path (RAII); an allocated global block is
//   freed unless SetClipboardData took ownership; the lock is held only for the copy.
#include "ClipboardWin32.h"

#include <cwchar>
#include <string>

#include "r1ui/platform/Utf.h"

namespace r1ui::platform {

namespace {

constexpr int kOpenAttempts = 10;
constexpr DWORD kOpenRetryDelayMs = 10;  // about 100 ms in total before reporting Busy

class ClipboardLock {
 public:
  explicit ClipboardLock(HWND owner) {
    for (int attempt = 0; attempt < kOpenAttempts && !open_; ++attempt) {
      if (attempt > 0) Sleep(kOpenRetryDelayMs);
      open_ = OpenClipboard(owner) != FALSE;
    }
  }
  ~ClipboardLock() {
    if (open_) CloseClipboard();
  }
  ClipboardLock(const ClipboardLock&) = delete;
  ClipboardLock& operator=(const ClipboardLock&) = delete;
  bool open() const { return open_; }

 private:
  bool open_ = false;
};

}  // namespace

ClipboardText readClipboardText(HWND owner) {
  ClipboardText result;
  ClipboardLock lock(owner);
  if (!lock.open()) {
    result.status = ClipboardStatus::Busy;
    return result;
  }
  if (IsClipboardFormatAvailable(CF_UNICODETEXT) == FALSE) {
    result.status = ClipboardStatus::Empty;
    return result;
  }
  HANDLE data = GetClipboardData(CF_UNICODETEXT);
  if (data == nullptr) return result;  // Failed
  const SIZE_T bytes = GlobalSize(data);
  const auto* units = static_cast<const wchar_t*>(GlobalLock(data));
  if (units == nullptr) return result;

  // Bound the scan by the block size: another process may have stored an unterminated string.
  const size_t length = wcsnlen(units, bytes / sizeof(wchar_t));
  if (length > kMaxClipboardTextBytes) {
    result.status = ClipboardStatus::TooLarge;
  } else {
    const std::u16string utf16(units, units + length);
    if (auto utf8 = utf16ToUtf8(utf16)) {
      if (utf8->size() > kMaxClipboardTextBytes) {
        result.status = ClipboardStatus::TooLarge;
      } else {
        result.text = std::move(*utf8);
        result.status = ClipboardStatus::Ok;
      }
    } else {
      result.status = ClipboardStatus::InvalidText;
    }
  }
  GlobalUnlock(data);
  return result;
}

ClipboardStatus writeClipboardText(HWND owner, std::string_view utf8) {
  if (utf8.size() > kMaxClipboardTextBytes) return ClipboardStatus::TooLarge;
  // NUL would silently truncate the text for every reader of CF_UNICODETEXT.
  if (utf8.find('\0') != std::string_view::npos) return ClipboardStatus::InvalidText;
  const auto utf16 = utf8ToUtf16(utf8);
  if (!utf16) return ClipboardStatus::InvalidText;

  // Allocate and fill before opening the clipboard so it is held for as short a time as possible.
  const size_t bytes = (utf16->size() + 1) * sizeof(wchar_t);  // at most ~32 MiB: cannot overflow
  HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (block == nullptr) return ClipboardStatus::Failed;
  auto* dest = static_cast<wchar_t*>(GlobalLock(block));
  if (dest == nullptr) {
    GlobalFree(block);
    return ClipboardStatus::Failed;
  }
  for (size_t i = 0; i < utf16->size(); ++i) dest[i] = static_cast<wchar_t>((*utf16)[i]);
  dest[utf16->size()] = L'\0';
  GlobalUnlock(block);

  ClipboardLock lock(owner);
  if (!lock.open()) {
    GlobalFree(block);
    return ClipboardStatus::Busy;
  }
  if (EmptyClipboard() == FALSE || SetClipboardData(CF_UNICODETEXT, block) == nullptr) {
    GlobalFree(block);
    return ClipboardStatus::Failed;
  }
  return ClipboardStatus::Ok;  // the clipboard owns the block now
}

}  // namespace r1ui::platform

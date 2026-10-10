// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: HangWatchdog, a test-only guard for tests that drive a real message loop: a thread that watches a
//   progress counter and, when the counter does not move for the given time, prints the call stack of the
//   test's main thread and then of every other thread (symbols from the PDB) to stderr, writes a minidump
//   when R1GUI_HANG_DUMP names a file, and ends the process with exit code 3.
// Why: a hang in a test is otherwise a silent CTest timeout; the stack of the stuck thread is the evidence
//   the fix needs, and an unattended run must never leave a stuck process or a dialog behind.
// Callers: tests/preview/editor_float_native_test.cpp. Windows only (dbghelp). The process's main thread
//   constructs it and calls progress() whenever it makes progress; the destructor stops the thread.
// Lifetime: the watchdog thread only reads the counter and the duplicated main-thread handle, both owned
//   by this object, and is joined in the destructor.
#pragma once

#include <windows.h>

#include <dbghelp.h>
#include <dwmapi.h>
#include <tlhelp32.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "dwmapi.lib")

namespace r1test {

class HangWatchdog {
 public:
  explicit HangWatchdog(std::chrono::seconds limit) : limit_(limit) {
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &mainThread_, 0, FALSE, DUPLICATE_SAME_ACCESS);
    worker_ = std::thread([this] { watch(); });
  }
  ~HangWatchdog() {
    stop_ = true;
    if (worker_.joinable()) worker_.join();
    if (mainThread_ != nullptr) CloseHandle(mainThread_);
  }
  HangWatchdog(const HangWatchdog&) = delete;
  HangWatchdog& operator=(const HangWatchdog&) = delete;

  void progress() { ++counter_; }
  static void listWindows();

 private:
  void watch() {
    uint64_t last = counter_;
    auto since = std::chrono::steady_clock::now();
    while (!stop_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      const uint64_t now = counter_;
      if (now != last) {
        last = now;
        since = std::chrono::steady_clock::now();
      } else if (std::chrono::steady_clock::now() - since > limit_) {
        report();
        ExitProcess(3);
      }
    }
  }

  // Prints the stack of the main thread, then of every other thread of the process, and writes the
  // minidump. The main thread is suspended for good; the others are suspended one at a time.
  void report() {
    std::fprintf(stderr, "HANG: no progress for %lld s; stack of the main thread:\n", static_cast<long long>(limit_.count()));
    if (mainThread_ == nullptr || SuspendThread(mainThread_) == static_cast<DWORD>(-1)) return;
    const HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(process, nullptr, TRUE);
    printStack(mainThread_);
    writeDump();
    const DWORD self = GetCurrentThreadId();
    const DWORD mainId = GetThreadId(mainThread_);
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
      THREADENTRY32 entry{};
      entry.dwSize = sizeof(entry);
      for (BOOL more = Thread32First(snapshot, &entry); more != FALSE; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == self || entry.th32ThreadID == mainId) continue;
        const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
        if (thread == nullptr) continue;
        std::fprintf(stderr, "thread %lu:\n", entry.th32ThreadID);
        if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
          printStack(thread);
          ResumeThread(thread);
        }
        CloseHandle(thread);
      }
      CloseHandle(snapshot);
    }
    listWindowsImpl();
    std::fflush(stderr);
  }

  // Walks one suspended thread's stack with dbghelp and prints one line per frame.
  static void printStack(HANDLE thread) {
    const HANDLE process = GetCurrentProcess();
    CONTEXT context{};
    context.ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(thread, &context)) return;
    STACKFRAME64 frame{};
    frame.AddrPC = {context.Rip, 0, AddrModeFlat};
    frame.AddrFrame = {context.Rbp, 0, AddrModeFlat};
    frame.AddrStack = {context.Rsp, 0, AddrModeFlat};
    for (int depth = 0; depth < 48; ++depth) {
      if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
      if (frame.AddrPC.Offset == 0) break;
      alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
      auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
      symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
      symbol->MaxNameLen = 255;
      DWORD64 displacement = 0;
      const bool named = SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol) != FALSE;
      IMAGEHLP_LINE64 line{};
      line.SizeOfStruct = sizeof(line);
      DWORD lineDisplacement = 0;
      const bool lined = SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line) != FALSE;
      std::fprintf(stderr, "  #%d %s%s%s:%lu\n", depth, named ? symbol->Name : "?", lined ? " at " : "", lined ? line.FileName : "", lined ? line.LineNumber : 0UL);
    }
  }

  // A full-memory minidump to the file named by R1GUI_HANG_DUMP (nothing when the variable is unset).
  static void writeDump() {
    char path[MAX_PATH] = {};
    if (GetEnvironmentVariableA("R1GUI_HANG_DUMP", path, MAX_PATH) == 0) return;
    const HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, static_cast<MINIDUMP_TYPE>(MiniDumpWithFullMemory | MiniDumpWithThreadInfo), nullptr, nullptr, nullptr);
    CloseHandle(file);
    std::fprintf(stderr, "dump written to %s\n", path);
  }

  // The state of every top-level window of the process: a window the compositor does not show (hidden,
  // minimized, cloaked) never releases the swapchain images queued for it. Only calls that do not send a
  // message to the suspended thread's windows (GetWindowText would block for good).
  static inline void listWindowsImpl() {
    EnumWindows(
        [](HWND window, LPARAM) -> BOOL {
          DWORD pid = 0;
          GetWindowThreadProcessId(window, &pid);
          if (pid != GetCurrentProcessId()) return TRUE;
          RECT r{};
          GetWindowRect(window, &r);
          DWORD cloaked = 0;
          DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
          std::fprintf(stderr, "  window %p visible=%d iconic=%d cloaked=%lu rect=%ld,%ld %ldx%ld\n", static_cast<void*>(window), IsWindowVisible(window) ? 1 : 0,
                       IsIconic(window) ? 1 : 0, cloaked, r.left, r.top, r.right - r.left, r.bottom - r.top);
          return TRUE;
        },
        0);
  }

  std::chrono::seconds limit_;
  HANDLE mainThread_ = nullptr;
  std::atomic<uint64_t> counter_{0};
  std::atomic<bool> stop_{false};
  std::thread worker_;
};

inline void HangWatchdog::listWindows() { listWindowsImpl(); }

}  // namespace r1test

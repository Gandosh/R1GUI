// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: a static initializer that keeps a failing test from putting a modal dialog on the owner's
//   screen: CRT assertion and error reports go to stderr, abort() writes no message box and no fault
//   report, and the process error mode suppresses the critical-error and general-protection boxes.
// Why: owner rule for builders: probes and tests must never leave a modal dialog on the screen; a Debug
//   assertion would otherwise wait for a click forever.
// Callers: every test of tests/ui-widgets/customize (include it once per executable).
#pragma once

#ifdef _WIN32
#include <crtdbg.h>
#include <cstdlib>
#include <windows.h>

namespace r1test {

inline const bool kDialogsSuppressed = [] {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
  const int types[] = {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT};
  for (const int type : types) {
    _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
  }
#endif
  return true;
}();

}  // namespace r1test
#endif

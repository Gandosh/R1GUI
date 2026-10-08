// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: headless sanity check that the platform interface compiles, links and its descriptor
//   defaults are sane. Does not create an OS window (CI sessions may have no desktop).
// Callers: CTest (label fast).
#include "r1ui/platform/Window.h"

int main() {
  const r1ui::platform::WindowDesc desc;
  return (desc.width > 0 && desc.height > 0 && !desc.title.empty()) ? 0 : 1;
}

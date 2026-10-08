// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the R1GUI interactive preview entry point. Phase 0 content: a Vulkan window whose
//   colour follows the mouse; Esc or the close button exits.
// Why: owner requirement that every phase ends with something launchable to interact with.
// Callers: the OS. Calls: r1ui::platform::Window and r1ui::render::Renderer only.
#include <windows.h>

#include <exception>
#include <string>

#include "r1ui/platform/Window.h"
#include "r1ui/render/Renderer.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  try {
    r1ui::platform::Window window({.title = "R1GUI Preview - Phase 0", .width = 1280, .height = 720});
    r1ui::render::Renderer renderer(window);

    while (window.pumpEvents() && !window.escapePressed()) {
      window.consumeResized();  // the renderer compares sizes itself; this just clears the flag
      const float w = static_cast<float>(window.clientWidth());
      const float h = static_cast<float>(window.clientHeight());
      const float x = w > 0.0f ? window.mouseX() / w : 0.0f;
      const float y = h > 0.0f ? window.mouseY() / h : 0.0f;
      renderer.drawFrame(window, {0.10f + 0.5f * x, 0.12f + 0.4f * y, 0.20f + 0.3f * (1.0f - x)});
      if (window.clientWidth() == 0) Sleep(16);  // minimized: avoid spinning
    }
    return 0;
  } catch (const std::exception& e) {
    MessageBoxA(nullptr, e.what(), "R1GUI Preview error", MB_OK | MB_ICONERROR);
    return 1;
  }
}

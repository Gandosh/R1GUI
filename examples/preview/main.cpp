// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the R1GUI interactive preview entry point and its command line:
//   (no arguments)            the preview: a borderless window that opens on the Editor screen (docking,
//                             native floating windows, commands, properties, customization); Ctrl+Tab
//                             cycles to the widget gallery, the composed Widgets screen, token swatches,
//                             reference screens and the docking sandbox (Window > Screen too). On the
//                             other screens Tab cycles, T toggles dark/light, Esc leaves a focused widget,
//                             then quits.
//   --bench <directory>       the Phase 4 performance baseline (Bench.h); writes phase4_baseline.json/.md
//   --bench-editor <dir>      the Phase 5 baseline of the Editor screen; writes phase5_baseline.md
//   --shot <directory>        offscreen renders of the Editor screen (arrangement, hotkey editor, menu creator,
//                             floating panel), the Widgets screen and the gallery pages
//                             (both themes) for visual comparison
// Why: owner requirement that every phase ends with something launchable to interact with, built
//   from the real modules (ui-core, ui-theme, ui-text, ui-render, ui-platform, ui-dock, ui-widgets).
// Callers: the OS. Errors: any failure (missing asset, device lost, bad paint call) becomes one
//   message box naming the problem and exit code 1, never a crash or a silent exit.
#include <windows.h>
#include <shellapi.h>

#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "Bench.h"
#include "Shots.h"
#include "PreviewApp.h"
#include "r1ui/render/RenderDevice.h"

namespace {

std::wstring widen(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), length);
  return out;
}

void showError(const std::string& message) {
  MessageBoxW(nullptr, widen(message).c_str(), L"R1GUI Preview error", MB_OK | MB_ICONERROR);
}

std::vector<std::wstring> commandLine() {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::vector<std::wstring> args;
  if (argv != nullptr) {
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    LocalFree(argv);
  }
  return args;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  try {
    const std::vector<std::wstring> args = commandLine();
    if (args.size() == 2 && args[0] == L"--bench") return preview::runBench(std::filesystem::path(args[1]));
    if (args.size() == 2 && args[0] == L"--bench-editor") return preview::runBenchEditor(std::filesystem::path(args[1]));
    if (args.size() == 2 && args[0] == L"--shot") {
      preview::renderShots(args[1], r1ui::theme::ThemeId::Dark);
      preview::renderShots(args[1], r1ui::theme::ThemeId::Light);
      return 0;
    }
    if (!args.empty()) {
      showError("Unknown arguments. Usage: r1gui-preview [--bench <directory> | --bench-editor <directory> | --shot <directory>]");
      return 2;
    }
    preview::PreviewApp app(preview::AppOptions{});
    app.run();
    return 0;
  } catch (const r1ui::render::DeviceLostError& e) {
    showError(std::string("The graphics device was lost (driver reset or GPU removed), so the preview cannot continue.\n\n") + e.what());
    return 1;
  } catch (const std::exception& e) {
    showError(e.what());
    return 1;
  }
}

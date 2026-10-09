// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: NativeRig, the fixture of the native backend's tests and of the manual harness: a real
//   borderless main window, a RenderDevice on the RTX 4080 (never the 3090), the shared Services, a
//   main UiContext with one growing content box, a NativeFloatingBackend and the AppLoop that drives
//   them, plus settle() (a few non-blocking loop steps), a scriptable pointer source and the helpers
//   to find the Win32 handles of the windows.
// Why: both native test executables (conformance, dock integration) and the harness need the same
//   scaffolding; keeping it in one header leaves the tests about behaviour.
// Callers: tests/ui-widgets/dock/native/*_gpu_test.cpp, the manual harness.
// Skipping: create() returns null with a reason when the session has no interactive desktop (no
//   monitors, windows cannot be created) or no usable Vulkan GPU; the tests print "SKIPPED: reason"
//   and exit 0 (CTest label gpu; the reason is on stdout).
// GPU rule: the device is requested by name ("RTX 4080" unless R1UI_GPU says otherwise) and a device
//   that turns out to be a 3090 is refused before any window target is built.
// Destruction order (members are declared so that reverse order is correct): the loop, then the main
//   context (a DockHost in it still talks to the backend while it detaches), then the backend (its
//   windows hold targets on the device), the main target, the services, the textures, the device,
//   and last the main window.
#pragma once

#include <windows.h>

#include <cstdlib>
#include <memory>
#include <string>

#include "TestSupport.h"
#include "r1ui/platform/Monitors.h"
#include "r1ui/render/RenderDevice.h"
#include "r1ui/render/WindowTarget.h"
#include "r1ui/widgets/dock/native/AppLoop.h"
#include "r1ui/widgets/dock/native/NativeFloatingBackend.h"
#include "r1ui/widgets/gpu/GpuTextures.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace native_test {

using namespace r1ui::widgets;
namespace dock = r1ui::dock;
namespace platform = r1ui::platform;

class Box : public WidgetObject {
 public:
  const char* typeName() const override { return "Box"; }
  void onAttached() override {
    style().flexGrow = 1.0;
    style().minHeight = r1ui::core::layout::Length::px(0);
  }
};

inline HWND hwndOf(platform::Window* w) { return w != nullptr ? static_cast<HWND>(w->nativeHandle().window) : nullptr; }

class NativeRig {
 public:
  // Null (with `skipReason`) when the machine cannot run native window tests.
  static std::unique_ptr<NativeRig> create(std::string& skipReason, bool scriptedPointer = true) {
    try {
      if (r1ui::platform::enumerateMonitors().empty()) {
        skipReason = "no monitor: the session has no interactive desktop";
        return nullptr;
      }
      return std::unique_ptr<NativeRig>(new NativeRig(scriptedPointer));
    } catch (const std::exception& ex) {
      skipReason = std::string("cannot create windows or a GPU device: ") + ex.what();
      return nullptr;
    }
  }

  // A few non-blocking loop steps: messages, events, listener calls, destruction of parked windows,
  // frames.
  void settle(int steps = 4) {
    for (int i = 0; i < steps; ++i) loop->step(false);
  }

  dock::Point pointInMain() const {
    const dock::Rect r = backend->mainContentRect();
    return {r.x + r.w / 2.0, r.y + r.h / 2.0};
  }

  // Sets what the backend's pointer source reports (screen LOGICAL pixels turned into physical).
  void setPointer(dock::Point logical, bool leftDown) {
    const dock::Point p = backend->screenSpace().toPhysical(logical);
    pointer.physical = {static_cast<int>(std::lround(p.x)), static_cast<int>(std::lround(p.y))};
    pointer.leftDown = leftDown;
  }

  std::unique_ptr<platform::Window> window;
  std::unique_ptr<r1ui::render::RenderDevice> device;
  std::unique_ptr<GpuTextureFactory> textures;
  std::unique_ptr<Services> services;
  std::unique_ptr<r1ui::render::WindowTarget> target;
  std::unique_ptr<NativeFloatingBackend> backend;
  std::unique_ptr<UiContext> ui;
  std::unique_ptr<AppLoop> loop;
  r1ui::core::tree::WidgetId mainBox;
  PointerSample pointer;
  bool redrawMain = true;

 private:
  explicit NativeRig(bool scriptedPointer) {
    platform::WindowDesc desc;
    desc.title = "native backend rig (main)";
    desc.width = 900;
    desc.height = 600;
    desc.sizesAreLogical = true;
    desc.borderless = true;
    desc.minSize = platform::Size{320, 200};
    desc.position = platform::Point{160, 120};
    window = std::make_unique<platform::Window>(desc);

    r1ui::render::DeviceOptions options;
    char* wanted = nullptr;
    size_t wantedLength = 0;
    _dupenv_s(&wanted, &wantedLength, "R1UI_GPU");
    options.gpuName = wanted != nullptr && *wanted != '\0' ? wanted : "RTX 4080";
    std::free(wanted);
    if (options.gpuName.find("3090") != std::string::npos) throw std::runtime_error("refusing to use the RTX 3090 (GPU rule)");
    device = std::make_unique<r1ui::render::RenderDevice>(options);
    if (device->gpu().name.find("3090") != std::string::npos) throw std::runtime_error("the device is a 3090 (GPU rule)");
    textures = std::make_unique<GpuTextureFactory>(*device);
    services = std::make_unique<Services>(r1test::loadTokens(), *textures, r1test::assetPaths());
    target = std::make_unique<r1ui::render::WindowTarget>(*device, *window);

    NativeBackendOptions backendOptions;
    if (scriptedPointer) backendOptions.pointerSource = [this] { return pointer; };
    backend = std::make_unique<NativeFloatingBackend>(*window, *device, *services, backendOptions);

    ui = std::make_unique<UiContext>(*services);
    ui->setFrameLoopRunning(true);
    ui->setAnimationsEnabled(false);
    ui->setViewport(window->clientWidth(), window->clientHeight(), window->dpiScale());
    ui->rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
    mainBox = ui->create<Box>(ui->root()).id();
    ui->frame();
    backend->setMainContent(*ui, mainBox);

    AppLoopHooks hooks;
    hooks.processMain = [this](uint64_t now) {
      ui->setTime(now);
      for (const platform::Event& e : window->takeEvents()) {
        if (e.type == platform::EventType::Resized || e.type == platform::EventType::DpiChanged) {
          ui->setViewport(window->clientWidth(), window->clientHeight(), window->dpiScale());
          redrawMain = true;
        } else if (e.type == platform::EventType::DisplayChanged) {
          backend->onDisplayChanged();
        } else {
          ui->handlePlatformEvent(e);
        }
      }
      if (ui->tick()) redrawMain = true;
    };
    hooks.mainNeedsFrame = [this] { return redrawMain || ui->needsFrame(); };
    hooks.renderMain = [this](uint64_t now) {
      ui->setTime(now);
      redrawMain = !renderFrame(*ui, *target, services->color("panel"));
    };
    hooks.mainMsUntilTick = [this](uint64_t now) {
      ui->setTime(now);
      return ui->msUntilTick();
    };
    loop = std::make_unique<AppLoop>(*window, *backend, hooks);
    settle();
  }
};

}  // namespace native_test

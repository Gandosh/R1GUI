// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the manual harness of the native floating backend and, without arguments, its smoke test. A real
//   borderless main window hosts a DockHost (six coloured panels, two regions) on the native backend
//   and the multi-window AppLoop. Interactive use (the real-desktop drive script, or a person): drag a
//   tab out of the main window to empty desktop space, drag it back, drag it onto another floating
//   window, move or resize the floating window, close it with its X or Alt+F4, minimize the main window.
// Why: examples/preview is wired by the integrator later; until then this is the real application of
//   the backend that the scripted drive (scratch/p5_native_drive.ps1) and a human can run.
// Usage: native_harness_gpu_test.exe                    smoke: build, run 30 loop steps, print, exit 0
//        native_harness_gpu_test.exe --interactive [--seconds N] [--status FILE]
//   Interactive runs until the main window is closed or N seconds (default 90) passed, so a forgotten
//   harness never lingers. With --status the harness rewrites FILE after every change of the layout
//   or of a window: one line per window (hwnd, screen rectangle in physical pixels) and per area.
// Callers: CTest (smoke, label gpu; SKIPPED without a desktop), the drive script.
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "ExpectWithMessage.h"
#include "NativeRig.h"
#include "r1ui/widgets/dock/DockHost.h"
#include "r1ui/widgets/label/Label.h"

using namespace native_test;
using r1ui::core::tree::WidgetId;

namespace {

// A panel body in a distinct colour with its title, so a capture shows which panel is where.
class ColorPanel : public WidgetObject {
 public:
  ColorPanel(dock::PanelId id, std::string title) : number_(id), title_(std::move(title)) {}
  const char* typeName() const override { return "ColorPanel"; }
  void onAttached() override {
    ui().create<Label>(this->id(), title_, LabelRole::Title);
    style().padding[r1ui::core::layout::kLeft] = 12.0;
    style().padding[r1ui::core::layout::kTop] = 12.0;
  }
  void paint(PaintContext& ctx) override {
    static const uint8_t palette[6][3] = {{52, 92, 160}, {120, 70, 150}, {60, 140, 90}, {160, 110, 40}, {150, 70, 70}, {60, 130, 140}};
    const uint8_t* c = palette[(number_ - 1) % 6];
    ctx.painter().fillRect(ctx.box(), r1ui::render::Color::fromRgba8(c[0], c[1], c[2]));
  }

 private:
  dock::PanelId number_;
  std::string title_;
};

std::string rectText(const platform::Rect& r) {
  return std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.width) + "," + std::to_string(r.height);
}

// One line per window and area, the whole text (the drive script reads the file between steps).
std::string statusText(NativeRig& rig, DockHost& host) {
  std::ostringstream out;
  char hex[32];
  std::snprintf(hex, sizeof(hex), "%p", static_cast<void*>(hwndOf(rig.window.get())));
  out << "main hwnd=" << hex << " rect=" << rectText(rig.window->windowRect()) << " dpi=" << rig.window->dpiScale() << "\n";
  for (const FloatId w : rig.backend->stacking()) {
    platform::Window* win = rig.backend->nativeWindow(w);
    if (win == nullptr) continue;
    std::snprintf(hex, sizeof(hex), "%p", static_cast<void*>(hwndOf(win)));
    const dock::Rect c = *rig.backend->contentRect(w);
    out << "float id=" << w << " hwnd=" << hex << " rect=" << rectText(win->windowRect()) << " content=" << c.x << "," << c.y << "," << c.w << "," << c.h
        << " dpi=" << win->dpiScale() << " visible=" << (win->isVisible() ? 1 : 0) << " monitor=" << rig.backend->monitorAt({c.x + c.w / 2, c.y + c.h / 2}) << "\n";
  }
  for (const dock::Area& a : host.layout().areas()) {
    out << "area id=" << a.id << " main=" << (a.id == dock::kMainAreaId ? 1 : 0) << " panels=";
    std::vector<const dock::Node*> pending;
    if (a.root) pending.push_back(&*a.root);
    while (!pending.empty()) {
      const dock::Node* n = pending.back();
      pending.pop_back();
      for (const dock::PanelId p : n->tabs) out << p << ",";
      for (const dock::Node& c : n->children) pending.push_back(&c);
    }
    out << " rect=" << a.rect.x << "," << a.rect.y << "," << a.rect.w << "," << a.rect.h << "\n";
  }
  out << "tabs";
  for (dock::PanelId p = 1; p <= 6; ++p) {
    if (DockTabStrip* s = host.stripOf(p)) {
      const dock::Rect r = s->tabRect(*s->indexOf(p));
      const dock::Point c = rig.backend->toScreen(s->window(), {r.x + r.w / 2.0, r.y + r.h / 2.0});
      const dock::Point phys = rig.backend->screenSpace().toPhysical(c);
      out << " " << p << "=" << std::lround(phys.x) << "," << std::lround(phys.y);
    }
  }
  out << "\n";
  return out.str();
}

}  // namespace

int main(int argc, char** argv) try {
  bool interactive = false;
  int seconds = 90;
  std::string statusPath;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--interactive") interactive = true;
    else if (arg == "--seconds" && i + 1 < argc) seconds = std::max(1, std::atoi(argv[++i]));
    else if (arg == "--status" && i + 1 < argc) statusPath = argv[++i];
  }
  PanelRegistry registry;  // before the rig: the host in the rig's context must not outlive it
  std::string skip;
  auto rig = NativeRig::create(skip);
  if (!rig) {
    std::printf("SKIPPED: %s\n", skip.c_str());
    return 0;
  }
  const char* names[] = {"Outliner", "Assets", "Console", "Inspector", "Timeline", "Properties"};
  for (dock::PanelId id = 1; id <= 6; ++id) {
    PanelDescriptor d;
    d.id = id;
    d.title = names[id - 1];
    d.floatSize = {340.0, 240.0};
    d.factory = [id, title = d.title](UiContext& ui, WidgetId parent) { return ui.create<ColorPanel>(parent, id, title).id(); };
    registry.add(std::move(d));
  }
  rig->ui->destroy(rig->mainBox);  // the rig's placeholder content box: the dock fills the window
  DockHost& host = rig->ui->create<DockHost>(rig->ui->root(), registry, *rig->backend);
  size_t changes = 0;
  host.setOnChanged([&](DockChange) { ++changes; });
  dock::DockLayoutResult created = dock::DockLayout::create(
      registry.infos(), host.options().config,
      dock::Node::split(dock::Axis::Row, {dock::Node::stack({1, 2, 3}, 0, 1.0), dock::Node::stack({4, 5, 6}, 0, 1.0)}));
  if (!created.ok()) throw std::runtime_error(created.error);
  host.setLayout(std::move(*created.layout));
  rig->settle();

  std::printf("harness: pid %lu, main hwnd %p, GPU '%s'\n", GetCurrentProcessId(), static_cast<void*>(hwndOf(rig->window.get())), rig->device->gpu().name.c_str());
  for (const platform::MonitorInfo& m : rig->backend->screenSpace().monitors()) {
    std::printf("  monitor %s %dx%d at %d,%d scale %.2f%s\n", m.name.c_str(), m.bounds.width, m.bounds.height, m.bounds.x, m.bounds.y, static_cast<double>(m.dpiScale), m.primary ? " primary" : "");
  }
  std::fflush(stdout);

  if (!interactive) {
    for (int i = 0; i < 30; ++i) rig->loop->step(false);
    R1_EXPECT(rig->backend->windowCount() == 0 && host.layout().isDocked(1));
    R1_EXPECT(host.floatPanel(2));
    rig->settle(6);
    R1_EXPECT(rig->backend->windowCount() == 1 && rig->backend->framesPresented(1) > 0, "a floating window opened and drew");
    host.setLayout(*dock::DockLayout::create(registry.infos(), host.options().config, dock::Node::stack({1, 2, 3, 4, 5, 6})).layout);
    rig->settle(4);
    R1_EXPECT(rig->backend->windowCount() == 0, "resetting the layout closed it");
    rig->ui->destroy(host.id());
    return r1test::finish();
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  std::string lastStatus;
  while (std::chrono::steady_clock::now() < deadline) {
    if (rig->window->isAlive() && !statusPath.empty()) {
      // Rewritten only when something changed: a dock change, a window moved, a panel moved.
      const std::string status = statusText(*rig, host);
      if (status != lastStatus) {
        std::ofstream(statusPath, std::ios::trunc) << status;
        lastStatus = status;
      }
    }
    if (!rig->loop->step(true, 100)) break;
  }
  std::printf("harness: ended after %s; %zu dock changes, loop idle waits %llu, live steps %llu, floating windows left %zu\n",
              std::chrono::steady_clock::now() >= deadline ? "the time limit" : "the main window closed", changes,
              static_cast<unsigned long long>(rig->loop->idleWaits()), static_cast<unsigned long long>(rig->loop->liveSteps()), rig->backend->windowCount());
  if (rig->window->isAlive()) rig->ui->destroy(host.id());
  return 0;
} catch (const std::exception& e) {
  std::fprintf(stderr, "uncaught exception: %s\n", e.what());
  return 2;
}

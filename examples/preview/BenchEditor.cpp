// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the Phase 5 baseline run declared in Bench.h (`--bench-editor <dir>`) and the writing of
//   phase5_baseline.md.
// Method: one PreviewApp on the Editor screen in a borderless 1440x900 (physical) window, FIFO
//   presentation, a throwaway data folder. (a) frame cost of the main window: 60 warm-up frames, then 400
//   forced frames, twice (redraw only; redraw with a full relayout); per frame the layout, paint-list
//   build, record + submit, present call and wait come from PreviewApp::frame. (b) the same with one
//   panel floating in a native window, measured as the cost of one whole loop step that draws both
//   windows. (c) idle: after the UI settled, 10 s of the real loop (PreviewApp::step(true)) with no
//   input, with and without the native window; process CPU time and frames presented. (d) memory and
//   startup.
// Callers: main.cpp. Numbers are a baseline for this machine, not a budget.
#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "Bench.h"
#include "PreviewApp.h"

namespace preview {

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kWarmupFrames = 60;
constexpr int kMeasuredFrames = 400;
constexpr double kIdleSeconds = 10.0;

struct Stats {
  double median = 0, p95 = 0, p99 = 0, max = 0;
};

Stats summarize(std::vector<double> v) {
  Stats s;
  if (v.empty()) return s;
  std::sort(v.begin(), v.end());
  const auto at = [&](double q) { return v[std::min(v.size() - 1, static_cast<size_t>(q * static_cast<double>(v.size())))]; };
  s.median = at(0.5);
  s.p95 = at(0.95);
  s.p99 = at(0.99);
  s.max = v.back();
  return s;
}

std::string fixed(double value, int digits = 3) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(digits) << value;
  return out.str();
}

double fileTimeSeconds(const FILETIME& f) {
  ULARGE_INTEGER u;
  u.LowPart = f.dwLowDateTime;
  u.HighPart = f.dwHighDateTime;
  return static_cast<double>(u.QuadPart) / 1.0e7;
}

double processCpuSeconds() {
  FILETIME create, exit, kernel, user;
  GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user);
  return fileTimeSeconds(kernel) + fileTimeSeconds(user);
}

struct Series {
  std::vector<double> layout, paint, record, present, wait, cpu, total;
  void add(const FrameTimes& t) {
    layout.push_back(t.layoutMs);
    paint.push_back(t.paintMs);
    record.push_back(t.recordSubmitMs);
    present.push_back(t.presentMs);
    wait.push_back(t.waitMs);
    cpu.push_back(t.layoutMs + t.paintMs + t.recordSubmitMs + t.presentMs);
    total.push_back(t.totalMs);
  }
};

struct Idle {
  double wallSeconds = 0, cpuSeconds = 0;
  uint64_t frames = 0;
  double percent() const { return wallSeconds > 0.0 ? 100.0 * cpuSeconds / wallSeconds : 0.0; }
};

Idle runIdle(PreviewApp& app, double seconds) {
  const uint64_t frames0 = app.framesPresented();
  const double cpu0 = processCpuSeconds();
  const auto start = Clock::now();
  while (std::chrono::duration<double>(Clock::now() - start).count() < seconds) {
    const double left = seconds - std::chrono::duration<double>(Clock::now() - start).count();
    if (!app.step(true, static_cast<unsigned>(std::max(1.0, std::min(left * 1000.0, 1000.0))))) throw std::runtime_error("the window closed during the benchmark");
  }
  Idle r;
  r.wallSeconds = std::chrono::duration<double>(Clock::now() - start).count();
  r.cpuSeconds = processCpuSeconds() - cpu0;
  r.frames = app.framesPresented() - frames0;
  return r;
}

void settle(PreviewApp& app) {
  for (int quiet = 0; quiet < 20;) {
    const uint64_t before = app.framesPresented();
    app.step(false);
    Sleep(5);
    quiet = app.framesPresented() == before ? quiet + 1 : 0;
  }
}

Series runFrames(PreviewApp& app, bool relayout) {
  Series s;
  FrameTimes t;
  for (int i = 0; i < kWarmupFrames + kMeasuredFrames; ++i) {
    if (!app.pumpMessages()) throw std::runtime_error("the window closed during the benchmark");
    if (relayout) app.requestFullLayout();
    if (!app.frame(&t)) {
      --i;
      continue;
    }
    if (i >= kWarmupFrames) s.add(t);
  }
  return s;
}

// The wall time of whole loop steps that must draw (main window and floating windows), one invalidated
// main frame per step.
std::vector<double> runSteps(PreviewApp& app) {
  std::vector<double> ms;
  for (int i = 0; i < kWarmupFrames + kMeasuredFrames; ++i) {
    app.requestFullLayout();
    const auto t0 = Clock::now();
    app.step(false);
    if (i >= kWarmupFrames) ms.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
  }
  return ms;
}

struct Memory {
  double workingSetMiB = 0, privateMiB = 0, gpuUsedMiB = 0;
};

Memory sample(PreviewApp& app) {
  Memory m;
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
  m.workingSetMiB = static_cast<double>(counters.WorkingSetSize) / 1048576.0;
  m.privateMiB = static_cast<double>(counters.PrivateUsage) / 1048576.0;
  m.gpuUsedMiB = static_cast<double>(app.device().memoryUsage().deviceLocalUsageBytes) / 1048576.0;
  return m;
}

void seriesTable(std::ostringstream& md, const char* title, const Series& s) {
  const auto row = [&](const char* name, const std::vector<double>& v) {
    const Stats st = summarize(v);
    md << "| " << name << " | " << fixed(st.median) << " | " << fixed(st.p95) << " | " << fixed(st.p99) << " | " << fixed(st.max) << " |\n";
  };
  md << "\n#### " << title << "\n\n| stage | median | p95 | p99 | max |\n|---|---|---|---|---|\n";
  row("layout", s.layout);
  row("paint-list build", s.paint);
  row("record + submit", s.record);
  row("present call", s.present);
  row("wait (fence + acquire)", s.wait);
  row("**CPU active**", s.cpu);
  row("frame to frame", s.total);
}

}  // namespace

int runBenchEditor(const std::filesystem::path& outputDirectory) {
  std::filesystem::create_directories(outputDirectory);
  const std::filesystem::path data = outputDirectory / "editor-data";
  std::error_code ignored;
  std::filesystem::remove_all(data, ignored);
  _putenv_s("R1GUI_PREVIEW_DATA", data.string().c_str());

  AppOptions options;
  options.width = 1440;
  options.height = 900;
  options.logicalSize = false;
  options.mode = Mode::Editor;
  PreviewApp app(options);
  while (app.framesPresented() == 0) app.step(false);
  const double startupMs = app.startupMs();
  settle(app);
  const int clientW = app.window().clientWidth();
  const int clientH = app.window().clientHeight();
  const size_t widgets = app.editorUi()->widgetCount();

  const Series redraw = runFrames(app, false);
  const Series relayout = runFrames(app, true);
  settle(app);
  const Idle idle = runIdle(app, kIdleSeconds);
  const Memory mainOnly = sample(app);

  // One panel in a native window: the same measurements with two windows to draw.
  app.editor()->dock().floatPanel(editor::panel::kCurves);
  settle(app);
  const size_t floating = app.backend().windowCount();
  const std::vector<double> stepMs = runSteps(app);
  settle(app);
  const Idle idleFloating = runIdle(app, kIdleSeconds);
  const Memory withFloating = sample(app);

  SYSTEM_INFO si;
  GetNativeSystemInfo(&si);
  MEMORYSTATUSEX ms{};
  ms.dwLength = sizeof(ms);
  GlobalMemoryStatusEx(&ms);
  const auto& gpu = app.device().gpu();
#ifdef NDEBUG
  const char* build = "RelWithDebInfo (NDEBUG)";
#else
  const char* build = "Debug";
#endif

  std::ostringstream md;
  md << "# Phase 5 performance baseline (slice 5.12)\n\n"
     << "Recorded by `r1gui-preview --bench-editor <dir>`; numbers are a baseline for later regression checks on this machine, not a budget. "
        "They describe the Editor screen (docking, commands, property panel, customizable menus) in the real multi-window loop; the Phase 4 numbers are in `phase4_baseline.md`.\n\n"
     << "## Machine\n\n- GPU: " << gpu.name << " (" << (gpu.deviceLocalBytes >> 20) << " MiB device-local), selected with the `R1UI_GPU` environment variable\n"
     << "- CPU logical processors: " << si.dwNumberOfProcessors << ", RAM " << (ms.ullTotalPhys >> 20) << " MiB\n"
     << "- Build: " << build << ", validation layer " << (app.device().validationActive() ? "on" : "off") << ", FIFO presentation (vsync), borderless window, client " << clientW << "x" << clientH
     << " physical pixels, " << widgets << " widgets in the Editor context\n\n"
     << "## Methodology\n\n"
     << "1. **Frame cost (main window only)**: " << kWarmupFrames << " warm-up frames then " << kMeasuredFrames << " forced frames (`PreviewApp::frame`), twice: redraw only, and with a full relayout of the tree every frame. "
        "Stages as in the Phase 4 baseline; the CPU active figure is layout + paint-list build + record/submit + present call.\n"
     << "2. **Two windows**: the Curves panel is floated into a native OS window (`" << floating << "` floating window); the cost is the wall time of one `PreviewApp::step(false)` that draws the invalidated main window and the floating window.\n"
     << "3. **Idle**: after the UI settled, " << kIdleSeconds << " s of the real loop (`PreviewApp::step(true)` over `AppLoop`) with no input; process CPU time (`GetProcessTimes`) and frames presented, without and with the native window.\n\n"
     << "## Frame cost (ms, " << kMeasuredFrames << " frames)\n";
  seriesTable(md, "Redraw only", redraw);
  seriesTable(md, "Full relayout every frame", relayout);
  const Stats steps = summarize(stepMs);
  md << "\n#### Loop step drawing the main window and one native window (full relayout of the main window)\n\n| median | p95 | p99 | max |\n|---|---|---|---|\n| " << fixed(steps.median) << " | "
     << fixed(steps.p95) << " | " << fixed(steps.p99) << " | " << fixed(steps.max) << " |\n"
     << "\n## Idle\n\n| situation | seconds | CPU s | CPU % of one core | frames |\n|---|---|---|---|---|\n";
  const auto idleRow = [&](const char* name, const Idle& r) {
    md << "| " << name << " | " << fixed(r.wallSeconds, 1) << " | " << fixed(r.cpuSeconds, 4) << " | " << fixed(r.percent(), 3) << " | " << r.frames << " |\n";
  };
  idleRow("Editor, settled, nothing focused", idle);
  idleRow("Editor with one native window, settled", idleFloating);
  md << "\n## Memory (MiB)\n\n| point | working set | private | GPU device-local |\n|---|---|---|---|\n"
     << "| after the first idle run | " << fixed(mainOnly.workingSetMiB, 1) << " | " << fixed(mainOnly.privateMiB, 1) << " | " << fixed(mainOnly.gpuUsedMiB, 1) << " |\n"
     << "| with one native window, after its idle run | " << fixed(withFloating.workingSetMiB, 1) << " | " << fixed(withFloating.privateMiB, 1) << " | " << fixed(withFloating.gpuUsedMiB, 1) << " |\n"
     << "\n## Startup\n\n- constructor to first presented frame (Editor screen: window, device, shell, Editor, layouts): **" << fixed(startupMs, 1) << " ms**\n";
  std::ofstream out(outputDirectory / "phase5_baseline.md", std::ios::binary | std::ios::trunc);
  out << md.str();
  if (!out) throw std::runtime_error("cannot write phase5_baseline.md");
  return 0;
}

}  // namespace preview

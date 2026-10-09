// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the Phase 3 baseline run declared in Bench.h and the writing of its JSON and Markdown files.
// Method: one PreviewApp in a borderless 1440x900 (physical) window, FIFO presentation.
//   (a) frame cost: 60 warm-up frames, then 600 forced frames in Widgets mode, twice: redraw only
//       and redraw with a full relayout each frame; per frame the layout, paint-list build, record
//       + submit, present-call and wait (fence + acquire) times are taken from PreviewApp::frame.
//   (b) idle: after the UI settled, 10 s of PreviewApp::step(block) with no input and no focus;
//       process CPU time (GetProcessTimes) and frames presented.
//   (c) memory: working set, private bytes and GPU device-local usage after 30 s idle in Widgets,
//       then after each other mode was opened and after returning to Widgets.
//   (d) startup: constructor entry to the first presented frame, and process creation to it.
// Callers: main.cpp. Numbers are a baseline for this machine, not a budget.
#include "Bench.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "PreviewApp.h"
#include "r1ui/core/JsonWriter.h"

namespace preview {

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kWarmupFrames = 60;
constexpr int kMeasuredFrames = 600;
constexpr double kIdleSeconds = 10.0;
constexpr double kSettleIdleSeconds = 30.0;
constexpr double kModeIdleSeconds = 3.0;

struct Stats {
  double median = 0, p95 = 0, p99 = 0, max = 0, mean = 0;
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
  double sum = 0;
  for (double x : v) sum += x;
  s.mean = sum / static_cast<double>(v.size());
  return s;
}

struct Series {
  std::vector<double> layout, paint, recordSubmit, present, wait, cpu, total;
  void add(const FrameTimes& t) {
    layout.push_back(t.layoutMs);
    paint.push_back(t.paintMs);
    recordSubmit.push_back(t.recordSubmitMs);
    present.push_back(t.presentMs);
    wait.push_back(t.waitMs);
    cpu.push_back(t.layoutMs + t.paintMs + t.recordSubmitMs + t.presentMs);
    total.push_back(t.totalMs);
  }
};

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

// Milliseconds from process creation to now.
double sinceProcessStartMs() {
  FILETIME create, exit, kernel, user, now;
  GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user);
  GetSystemTimeAsFileTime(&now);
  return (fileTimeSeconds(now) - fileTimeSeconds(create)) * 1000.0;
}

struct Memory {
  double workingSetMiB = 0, privateMiB = 0, gpuUsedMiB = 0, gpuBudgetMiB = 0;
  bool gpuAvailable = false;
};

Memory sample(PreviewApp& app) {
  Memory m;
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
  m.workingSetMiB = static_cast<double>(counters.WorkingSetSize) / 1048576.0;
  m.privateMiB = static_cast<double>(counters.PrivateUsage) / 1048576.0;
  const auto gpu = app.device().memoryUsage();
  m.gpuAvailable = gpu.available;
  m.gpuUsedMiB = static_cast<double>(gpu.deviceLocalUsageBytes) / 1048576.0;
  m.gpuBudgetMiB = static_cast<double>(gpu.deviceLocalBudgetBytes) / 1048576.0;
  return m;
}

// Runs the real event loop with no input for `seconds`; returns frames presented and CPU seconds.
struct IdleResult {
  double wallSeconds = 0, cpuSeconds = 0;
  uint64_t frames = 0;
};

IdleResult runIdle(PreviewApp& app, double seconds) {
  const uint64_t frames0 = app.framesPresented();
  const double cpu0 = processCpuSeconds();
  const auto start = Clock::now();
  while (std::chrono::duration<double>(Clock::now() - start).count() < seconds) {
    const double left = seconds - std::chrono::duration<double>(Clock::now() - start).count();
    if (!app.step(true, static_cast<unsigned>(std::max(1.0, std::min(left * 1000.0, 1000.0))))) throw std::runtime_error("the window closed during the benchmark");
  }
  IdleResult r;
  r.wallSeconds = std::chrono::duration<double>(Clock::now() - start).count();
  r.cpuSeconds = processCpuSeconds() - cpu0;
  r.frames = app.framesPresented() - frames0;
  return r;
}

// Steps until the UI stops requesting frames (layout, first draws, atlas fills).
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
    if (relayout) app.scene().requestFullLayout();
    if (!app.frame(&t)) {
      --i;
      continue;
    }
    if (i >= kWarmupFrames) s.add(t);
  }
  return s;
}

std::string registryString(HKEY root, const wchar_t* path, const wchar_t* name) {
  wchar_t buffer[256] = {};
  DWORD size = sizeof(buffer) - sizeof(wchar_t);
  if (RegGetValueW(root, path, name, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) return "unknown";
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(std::max(bytes, 1)), '\0');
  WideCharToMultiByte(CP_UTF8, 0, buffer, -1, out.data(), bytes, nullptr, nullptr);
  out.resize(out.size() - 1);
  return out;
}

std::string windowsVersion() {
  const wchar_t* key = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
  return registryString(HKEY_LOCAL_MACHINE, key, L"ProductName") + " " + registryString(HKEY_LOCAL_MACHINE, key, L"DisplayVersion") +
         " (build " + registryString(HKEY_LOCAL_MACHINE, key, L"CurrentBuildNumber") + ")";
}

// ---- Output ---------------------------------------------------------------------------------

class Json {
 public:
  Json& key(const std::string& k) {
    comma();
    r1ui::core::appendQuoted(out_, k);
    out_ += ':';
    fresh_ = true;
    return *this;
  }
  Json& str(const std::string& v) { comma(); r1ui::core::appendQuoted(out_, v); return *this; }
  Json& num(double v) { comma(); r1ui::core::appendNumber(out_, v); return *this; }
  Json& boolean(bool v) { comma(); out_ += v ? "true" : "false"; return *this; }
  Json& open(char c) { comma(); out_ += c; fresh_ = true; return *this; }
  Json& close(char c) { out_ += c; fresh_ = false; return *this; }
  const std::string& text() const { return out_; }

 private:
  void comma() {
    if (!fresh_ && !out_.empty() && out_.back() != ':') out_ += ',';
    fresh_ = false;
  }
  std::string out_;
  bool fresh_ = true;
};

void writeStats(Json& j, const char* name, const std::vector<double>& v) {
  const Stats s = summarize(v);
  j.key(name).open('{');
  j.key("median").num(s.median).key("p95").num(s.p95).key("p99").num(s.p99).key("max").num(s.max).key("mean").num(s.mean);
  j.close('}');
}

void writeSeries(Json& j, const char* name, const Series& s) {
  j.key(name).open('{');
  writeStats(j, "layoutMs", s.layout);
  writeStats(j, "paintBuildMs", s.paint);
  writeStats(j, "recordSubmitMs", s.recordSubmit);
  writeStats(j, "presentCallMs", s.present);
  writeStats(j, "waitMs", s.wait);
  writeStats(j, "cpuActiveMs", s.cpu);
  writeStats(j, "frameToFrameMs", s.total);
  j.close('}');
}

void writeMemory(Json& j, const char* name, const Memory& m) {
  j.key(name).open('{');
  j.key("workingSetMiB").num(m.workingSetMiB).key("privateMiB").num(m.privateMiB);
  j.key("gpuDeviceLocalUsedMiB").num(m.gpuUsedMiB).key("gpuBudgetMiB").num(m.gpuBudgetMiB);
  j.close('}');
}

std::string fixed(double v, int digits = 3) {
  std::ostringstream o;
  o.setf(std::ios::fixed);
  o.precision(digits);
  o << v;
  return o.str();
}

std::string statRow(const char* name, const std::vector<double>& v) {
  const Stats s = summarize(v);
  return std::string("| ") + name + " | " + fixed(s.median) + " | " + fixed(s.p95) + " | " + fixed(s.p99) + " | " + fixed(s.max) + " |\n";
}

}  // namespace

int runBench(const std::filesystem::path& outputDirectory) {
  std::filesystem::create_directories(outputDirectory);
  AppOptions options;
  options.width = 1440;
  options.height = 900;
  options.logicalSize = false;
  PreviewApp app(options);
  while (app.framesPresented() == 0) app.step(false);
  const double startupMs = app.startupMs();
  const double processStartupMs = sinceProcessStartMs();
  settle(app);
  const int clientW = app.window().clientWidth();
  const int clientH = app.window().clientHeight();

  const Series redraw = runFrames(app, false);
  const Series relayout = runFrames(app, true);
  settle(app);

  const IdleResult idle = runIdle(app, kIdleSeconds);
  const Memory beforeSettle = sample(app);
  const IdleResult longIdle = runIdle(app, kSettleIdleSeconds);
  const Memory widgetsMem = sample(app);
  std::vector<std::pair<std::string, Memory>> modeMem;
  for (Mode mode : {Mode::Swatches, Mode::Screens, Mode::Sandbox}) {
    app.setMode(mode);
    if (mode == Mode::Screens) {
      // Step through every reference screen so the texture swap path is exercised too.
      for (int i = 0; i < 9; ++i) {
        for (int k = 0; k < 3; ++k) app.step(false);
        app.frame();
      }
    }
    runIdle(app, kModeIdleSeconds);
    modeMem.emplace_back(modeName(mode), sample(app));
  }
  app.setMode(Mode::Widgets);
  runIdle(app, kModeIdleSeconds);
  const Memory backMem = sample(app);

  SYSTEM_INFO si;
  GetNativeSystemInfo(&si);
  MEMORYSTATUSEX ms{};
  ms.dwLength = sizeof(ms);
  GlobalMemoryStatusEx(&ms);
  const std::string cpuModel = registryString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
  const std::string windows = windowsVersion();
  const auto& gpu = app.device().gpu();
#ifdef NDEBUG
  const char* build = "RelWithDebInfo (NDEBUG)";
#else
  const char* build = "Debug";
#endif
  const double idlePercent = 100.0 * idle.cpuSeconds / idle.wallSeconds;
  const double longIdlePercent = 100.0 * longIdle.cpuSeconds / longIdle.wallSeconds;

  Json j;
  j.open('{');
  j.key("slice").str("3.11 performance baseline (Phase 3)");
  j.key("note").str("Baseline for later regression checks on this machine, not a budget.");
  j.key("machine").open('{');
  j.key("gpu").str(gpu.name).key("gpuDeviceLocalMiB").num(static_cast<double>(gpu.deviceLocalBytes >> 20));
  j.key("cpu").str(cpuModel).key("logicalCpus").num(static_cast<double>(si.dwNumberOfProcessors));
  j.key("ramMiB").num(static_cast<double>(ms.ullTotalPhys >> 20)).key("windows").str(windows);
  j.key("build").str(build).key("validationLayer").boolean(app.device().validationActive());
  j.key("presentMode").str("FIFO (vsync)").key("clientSize").str(std::to_string(clientW) + "x" + std::to_string(clientH));
  j.close('}');
  j.key("frames").open('{');
  j.key("measured").num(kMeasuredFrames).key("warmup").num(kWarmupFrames);
  writeSeries(j, "widgetsRedraw", redraw);
  writeSeries(j, "widgetsFullRelayoutEachFrame", relayout);
  j.close('}');
  j.key("idle").open('{');
  j.key("seconds").num(idle.wallSeconds).key("cpuSeconds").num(idle.cpuSeconds).key("cpuPercent").num(idlePercent);
  j.key("framesPresented").num(static_cast<double>(idle.frames));
  j.key("longIdleSeconds").num(longIdle.wallSeconds).key("longIdleCpuPercent").num(longIdlePercent);
  j.key("longIdleFramesPresented").num(static_cast<double>(longIdle.frames));
  j.close('}');
  j.key("memory").open('{');
  j.key("gpuSource").str(widgetsMem.gpuAvailable ? "VK_EXT_memory_budget heapUsage (device-local heaps, this process)" : "unavailable");
  writeMemory(j, "widgetsAfterSettle", beforeSettle);
  writeMemory(j, "widgetsAfter30sIdle", widgetsMem);
  for (const auto& [name, m] : modeMem) writeMemory(j, ("after" + name).c_str(), m);
  writeMemory(j, "widgetsAfterReturn", backMem);
  j.close('}');
  j.key("startup").open('{');
  j.key("constructorToFirstFrameMs").num(startupMs).key("processCreationToFirstFrameMeasuredMs").num(processStartupMs);
  j.close('}');
  j.close('}');
  {
    std::ofstream out(outputDirectory / "phase3_baseline.json", std::ios::binary | std::ios::trunc);
    out << j.text() << '\n';
    if (!out) throw std::runtime_error("cannot write phase3_baseline.json");
  }

  std::ostringstream md;
  md << "# Phase 3 performance baseline (slice 3.11)\n\n"
     << "Recorded by `r1gui-preview --bench <dir>`; numbers are a baseline for later regression checks on this machine, not a budget.\n\n"
     << "## Machine\n\n"
     << "- GPU: " << gpu.name << " (" << (gpu.deviceLocalBytes >> 20) << " MiB device-local), selected through `R1UI_GPU` or the per-user GPU preference file\n"
     << "- CPU: " << cpuModel << " (" << si.dwNumberOfProcessors << " logical processors), RAM " << (ms.ullTotalPhys >> 20) << " MiB\n"
     << "- OS: " << windows << "\n- Build: " << build << ", validation layer " << (app.device().validationActive() ? "on" : "off")
     << ", FIFO presentation (vsync), borderless window, client " << clientW << "x" << clientH << " physical pixels\n\n"
     << "## Methodology\n\n"
     << "1. **Frame cost**: Widgets mode, " << kWarmupFrames << " warm-up frames then " << kMeasuredFrames
     << " measured frames, each forced (`PreviewApp::frame`), twice: redraw only, and redraw with a full relayout of the tree every frame. "
        "Layout = `Scene::layout`; paint-list build = `beginFrame` + painting the tree + glyph atlas upload staging; record+submit and present call "
        "come from `WindowTarget::lastFrameTimings`; wait = frame fence + swapchain acquire (this is where vsync shows). CPU active = layout + paint + record/submit + present call.\n"
     << "2. **Idle**: after the UI settled, " << kIdleSeconds << " s of the real event loop (`PreviewApp::step(true)`) with no input and no focused field; process CPU time from `GetProcessTimes`, frames presented counted by the app.\n"
     << "3. **Memory**: `GetProcessMemoryInfo` working set and private bytes; GPU = " << (widgetsMem.gpuAvailable ? "`VK_EXT_memory_budget` device-local heap usage of this process" : "unavailable on this GPU")
     << ". Taken after the idle runs, after " << kSettleIdleSeconds << " s idle in Widgets, then " << kModeIdleSeconds << " s after opening each other mode (Screens: all 9 screens visited), and back in Widgets.\n"
     << "4. **Startup**: from the start of the `PreviewApp` constructor (window, device, swapchain, fonts, icons, scene) to the first presented frame, and from process creation to the same point.\n\n"
     << "## Frame cost (ms, " << kMeasuredFrames << " frames)\n\n### Redraw only\n\n| stage | median | p95 | p99 | max |\n|---|---|---|---|---|\n"
     << statRow("layout", redraw.layout) << statRow("paint-list build", redraw.paint) << statRow("record + submit", redraw.recordSubmit)
     << statRow("present call", redraw.present) << statRow("wait (fence + acquire)", redraw.wait) << statRow("**CPU active**", redraw.cpu)
     << statRow("frame to frame", redraw.total)
     << "\n### Full relayout every frame\n\n| stage | median | p95 | p99 | max |\n|---|---|---|---|---|\n"
     << statRow("layout", relayout.layout) << statRow("paint-list build", relayout.paint) << statRow("record + submit", relayout.recordSubmit)
     << statRow("present call", relayout.present) << statRow("wait (fence + acquire)", relayout.wait) << statRow("**CPU active**", relayout.cpu)
     << statRow("frame to frame", relayout.total)
     << "\n## Idle\n\n- " << fixed(idle.wallSeconds, 1) << " s without input: " << fixed(idle.cpuSeconds, 4) << " s CPU = **" << fixed(idlePercent, 3)
     << " %** of one core, **" << idle.frames << " frames** rendered\n- " << fixed(longIdle.wallSeconds, 1) << " s (memory settle): " << fixed(longIdlePercent, 3)
     << " % CPU, " << longIdle.frames << " frames\n\n## Memory (MiB)\n\n| point | working set | private | GPU device-local |\n|---|---|---|---|\n";
  const auto memRow = [&](const std::string& name, const Memory& m) {
    md << "| " << name << " | " << fixed(m.workingSetMiB, 1) << " | " << fixed(m.privateMiB, 1) << " | " << fixed(m.gpuUsedMiB, 1) << " |\n";
  };
  memRow("Widgets after settle", beforeSettle);
  memRow("Widgets after 30 s idle", widgetsMem);
  for (const auto& [name, m] : modeMem) memRow("after opening " + name, m);
  memRow("back in Widgets", backMem);
  md << "\n## Startup\n\n- constructor to first presented frame: **" << fixed(startupMs, 1) << " ms**\n- process creation to first presented frame: **"
     << fixed(processStartupMs, 1) << " ms**\n";
  std::ofstream mdOut(outputDirectory / "phase3_baseline.md", std::ios::binary | std::ios::trunc);
  mdOut << md.str();
  if (!mdOut) throw std::runtime_error("cannot write phase3_baseline.md");
  return 0;
}

}  // namespace preview

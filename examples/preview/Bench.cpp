// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the Phase 4 baseline run declared in Bench.h and the writing of its JSON and Markdown files.
// Method: one PreviewApp in a borderless 1440x900 (physical) window, FIFO presentation.
//   (a) frame cost, for the Widgets mode (the composed screen) and the Gallery mode (default page),
//       each: 60 warm-up frames, then 600 forced frames, twice: redraw only and redraw with a full
//       relayout each frame; per frame the layout, paint-list build, record + submit, present-call
//       and wait (fence + acquire) times are taken from PreviewApp::frame. Then the redraw cost of
//       each of the five Gallery pages (200 frames each) with their widget counts.
//   (b) idle: after the UI settled, 10 s of PreviewApp::step(block) with no input and no focus;
//       process CPU time (GetProcessTimes) and frames presented, per mode; and 5 s with the Name
//       field focused (the caret blinks, so frames keep coming).
//   (c) memory: working set, private bytes and GPU device-local usage after the idle runs, then
//       after each other mode was opened and after returning to Widgets.
//   (d) startup: constructor entry to the first presented frame (Gallery mode, built at start), the
//       first show of the Widgets mode, and process creation to the first frame.
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
#include "r1ui/widgets/runtime/UiContext.h"

namespace preview {

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kWarmupFrames = 60;
constexpr int kMeasuredFrames = 600;
constexpr int kPageFrames = 200;
constexpr double kIdleSeconds = 10.0;
constexpr double kFocusIdleSeconds = 5.0;
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
  double cpuPercent() const { return wallSeconds > 0.0 ? 100.0 * cpuSeconds / wallSeconds : 0.0; }
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

Series runFrames(PreviewApp& app, bool relayout, int measured = kMeasuredFrames) {
  Series s;
  FrameTimes t;
  for (int i = 0; i < kWarmupFrames + measured; ++i) {
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

// Everything measured for one widget mode.
struct ModeRun {
  Series redraw, relayout;
  IdleResult idle;
  size_t widgets = 0;
  Memory afterSettle;
};

ModeRun runMode(PreviewApp& app, Mode mode) {
  app.setMode(mode);
  settle(app);
  ModeRun run;
  const r1ui::widgets::UiContext* ui = mode == Mode::Gallery ? app.galleryUi() : app.widgetsUi();
  run.widgets = ui != nullptr ? ui->widgetCount() : 0;
  run.redraw = runFrames(app, false);
  run.relayout = runFrames(app, true);
  settle(app);
  run.idle = runIdle(app, kIdleSeconds);
  run.afterSettle = sample(app);
  return run;
}

struct PageRun {
  std::string name;
  size_t widgets = 0;
  Series redraw;
};

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

void writeIdle(Json& j, const char* name, const IdleResult& r) {
  j.key(name).open('{');
  j.key("seconds").num(r.wallSeconds).key("cpuSeconds").num(r.cpuSeconds).key("cpuPercent").num(r.cpuPercent());
  j.key("framesPresented").num(static_cast<double>(r.frames));
  j.close('}');
}

void writeMemory(Json& j, const char* name, const Memory& m) {
  j.key(name).open('{');
  j.key("workingSetMiB").num(m.workingSetMiB).key("privateMiB").num(m.privateMiB);
  j.key("gpuDeviceLocalUsedMiB").num(m.gpuUsedMiB).key("gpuBudgetMiB").num(m.gpuBudgetMiB);
  j.close('}');
}

void writeMode(Json& j, const char* name, const ModeRun& run) {
  j.key(name).open('{');
  j.key("widgets").num(static_cast<double>(run.widgets));
  writeSeries(j, "redraw", run.redraw);
  writeSeries(j, "fullRelayoutEachFrame", run.relayout);
  writeIdle(j, "idle", run.idle);
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

void writeSeriesTable(std::ostringstream& md, const char* heading, const Series& s) {
  md << "\n#### " << heading << "\n\n| stage | median | p95 | p99 | max |\n|---|---|---|---|---|\n"
     << statRow("layout", s.layout) << statRow("paint-list build", s.paint) << statRow("record + submit", s.recordSubmit)
     << statRow("present call", s.present) << statRow("wait (fence + acquire)", s.wait) << statRow("**CPU active**", s.cpu)
     << statRow("frame to frame", s.total);
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
  const double galleryStartupMs = app.startupMs();
  const double processStartupMs = sinceProcessStartMs();
  settle(app);
  const int clientW = app.window().clientWidth();
  const int clientH = app.window().clientHeight();

  // The first show of the Widgets mode builds its context and the composed screen.
  const auto widgetsBuildStart = Clock::now();
  app.setMode(Mode::Widgets);
  while (app.framesPresented() < 2) app.step(false);
  const double widgetsFirstShowMs = std::chrono::duration<double, std::milli>(Clock::now() - widgetsBuildStart).count();
  app.setMode(Mode::Gallery);
  settle(app);

  const ModeRun widgets = runMode(app, Mode::Widgets);
  const ModeRun gallery = runMode(app, Mode::Gallery);

  std::vector<PageRun> pages;
  for (size_t i = 0; i < GalleryApp::kPageCount; ++i) {
    app.gallery()->selectPage(i);
    settle(app);
    PageRun page;
    page.name = GalleryApp::pageName(i);
    page.widgets = app.galleryUi()->widgetCount();
    page.redraw = runFrames(app, false, kPageFrames);
    pages.push_back(std::move(page));
  }
  app.gallery()->selectPage(0);

  // A focused Name field blinks its caret: frames keep coming at the display rate, nothing else.
  app.setMode(Mode::Widgets);
  settle(app);
  app.widgetsUi()->focusWidget(app.composed()->ids().nameInput);
  for (int i = 0; i < 40; ++i) {  // the caret blinks, so the frames never stop: let a few go by instead of settling
    app.step(false);
    Sleep(5);
  }
  const IdleResult focusIdle = runIdle(app, kFocusIdleSeconds);
  app.widgetsUi()->clearFocus();
  settle(app);
  const IdleResult longIdle = runIdle(app, kSettleIdleSeconds);
  const Memory widgetsMem = sample(app);

  std::vector<std::pair<std::string, Memory>> modeMem;
  for (Mode mode : {Mode::Gallery, Mode::Swatches, Mode::Screens, Mode::Sandbox}) {
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

  Json j;
  j.open('{');
  j.key("slice").str("4.17 performance baseline (Phase 4)");
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
  writeMode(j, "widgets", widgets);
  writeMode(j, "gallery", gallery);
  j.key("galleryPages").open('[');
  for (const PageRun& page : pages) {
    j.open('{');
    j.key("page").str(page.name).key("widgets").num(static_cast<double>(page.widgets));
    writeStats(j, "cpuActiveMs", page.redraw.cpu);
    writeStats(j, "frameToFrameMs", page.redraw.total);
    j.close('}');
  }
  j.close(']');
  j.close('}');
  j.key("idle").open('{');
  writeIdle(j, "widgetsFocusedField", focusIdle);
  writeIdle(j, "widgetsLongIdle", longIdle);
  j.close('}');
  j.key("memory").open('{');
  j.key("gpuSource").str(widgetsMem.gpuAvailable ? "VK_EXT_memory_budget heapUsage (device-local heaps, this process)" : "unavailable");
  writeMemory(j, "widgetsAfterSettle", widgets.afterSettle);
  writeMemory(j, "galleryAfterSettle", gallery.afterSettle);
  writeMemory(j, "widgetsAfter30sIdle", widgetsMem);
  for (const auto& [name, m] : modeMem) writeMemory(j, ("after" + name).c_str(), m);
  writeMemory(j, "widgetsAfterReturn", backMem);
  j.close('}');
  j.key("startup").open('{');
  j.key("constructorToFirstFrameMs").num(galleryStartupMs).key("processCreationToFirstFrameMeasuredMs").num(processStartupMs);
  j.key("widgetsFirstShowMs").num(widgetsFirstShowMs);
  j.close('}');
  j.close('}');
  {
    std::ofstream out(outputDirectory / "phase4_baseline.json", std::ios::binary | std::ios::trunc);
    out << j.text() << '\n';
    if (!out) throw std::runtime_error("cannot write phase4_baseline.json");
  }

  std::ostringstream md;
  md << "# Phase 4 performance baseline (slice 4.17)\n\n"
     << "Recorded by `r1gui-preview --bench <dir>`; numbers are a baseline for later regression checks on this machine, not a budget. "
        "The Widgets mode is now the composed widget-library screen and the Gallery mode (the new default) the widget galleries; "
        "the Phase 3 numbers (`phase3_baseline.md`) were taken on the hand-drawn panel.\n\n"
     << "## Machine\n\n"
     << "- GPU: " << gpu.name << " (" << (gpu.deviceLocalBytes >> 20) << " MiB device-local), selected with the `R1UI_GPU` environment variable\n"
     << "- CPU: " << cpuModel << " (" << si.dwNumberOfProcessors << " logical processors), RAM " << (ms.ullTotalPhys >> 20) << " MiB\n"
     << "- OS: " << windows << "\n- Build: " << build << ", validation layer " << (app.device().validationActive() ? "on" : "off")
     << ", FIFO presentation (vsync), borderless window, client " << clientW << "x" << clientH << " physical pixels\n\n"
     << "## Methodology\n\n"
     << "1. **Frame cost**: for the Widgets and the Gallery mode, " << kWarmupFrames << " warm-up frames then " << kMeasuredFrames
     << " measured frames, each forced (`PreviewApp::frame`), twice: redraw only, and redraw with a full relayout of the tree every frame. "
        "Layout = shell layout plus `UiContext::frame` (layout, overlay placement); paint-list build = `beginFrame` + painting the shell and the widget tree + glyph atlas upload staging; "
        "record+submit and present call come from `WindowTarget::lastFrameTimings`; wait = frame fence + swapchain acquire (this is where vsync shows). "
        "CPU active = layout + paint + record/submit + present call. Then the redraw cost of each of the five Gallery pages (" << kPageFrames << " frames).\n"
     << "2. **Idle**: after the UI settled, " << kIdleSeconds << " s of the real event loop (`PreviewApp::step(true)`) with no input and no focused field, per mode; "
        "process CPU time from `GetProcessTimes`, frames presented counted by the app. Also " << kFocusIdleSeconds << " s with the Name field focused (caret blink), and "
        << kSettleIdleSeconds << " s after the focus was removed.\n"
     << "3. **Memory**: `GetProcessMemoryInfo` working set and private bytes; GPU = " << (widgetsMem.gpuAvailable ? "`VK_EXT_memory_budget` device-local heap usage of this process" : "unavailable on this GPU")
     << ". Taken after the idle runs, then " << kModeIdleSeconds << " s after opening each mode (Screens: all 9 screens visited), and back in Widgets.\n"
     << "4. **Startup**: from the start of the `PreviewApp` constructor (window, device, swapchain, fonts, icons, shell, Gallery mode) to the first presented frame, "
        "the first show of the Widgets mode (build of its context and screen to its second presented frame), and process creation to the first frame.\n\n"
     << "## Frame cost (ms, " << kMeasuredFrames << " frames)\n\n### Widgets mode (" << widgets.widgets << " widgets)\n";
  writeSeriesTable(md, "Redraw only", widgets.redraw);
  writeSeriesTable(md, "Full relayout every frame", widgets.relayout);
  md << "\n### Gallery mode, Buttons page (" << gallery.widgets << " widgets)\n";
  writeSeriesTable(md, "Redraw only", gallery.redraw);
  writeSeriesTable(md, "Full relayout every frame", gallery.relayout);
  md << "\n### Gallery pages, redraw only (" << kPageFrames << " frames)\n\n| page | widgets | CPU active median | p95 | p99 | max | frame to frame median |\n|---|---|---|---|---|---|---|\n";
  for (const PageRun& page : pages) {
    const Stats cpu = summarize(page.redraw.cpu);
    const Stats total = summarize(page.redraw.total);
    md << "| " << page.name << " | " << page.widgets << " | " << fixed(cpu.median) << " | " << fixed(cpu.p95) << " | " << fixed(cpu.p99) << " | " << fixed(cpu.max)
       << " | " << fixed(total.median) << " |\n";
  }
  md << "\n## Idle\n\n| situation | seconds | CPU s | CPU % of one core | frames |\n|---|---|---|---|---|\n";
  const auto idleRow = [&](const std::string& name, const IdleResult& r) {
    md << "| " << name << " | " << fixed(r.wallSeconds, 1) << " | " << fixed(r.cpuSeconds, 4) << " | " << fixed(r.cpuPercent(), 3) << " | " << r.frames << " |\n";
  };
  idleRow("Widgets, settled, nothing focused", widgets.idle);
  idleRow("Gallery (Buttons), settled, nothing focused", gallery.idle);
  idleRow("Widgets, Name field focused (caret blink)", focusIdle);
  idleRow("Widgets, after the focus was removed", longIdle);
  md << "\n## Memory (MiB)\n\n| point | working set | private | GPU device-local |\n|---|---|---|---|\n";
  const auto memRow = [&](const std::string& name, const Memory& m) {
    md << "| " << name << " | " << fixed(m.workingSetMiB, 1) << " | " << fixed(m.privateMiB, 1) << " | " << fixed(m.gpuUsedMiB, 1) << " |\n";
  };
  memRow("Widgets after settle", widgets.afterSettle);
  memRow("Gallery after settle", gallery.afterSettle);
  memRow("Widgets after 30 s idle", widgetsMem);
  for (const auto& [name, m] : modeMem) memRow("after opening " + name, m);
  memRow("back in Widgets", backMem);
  md << "\n## Startup\n\n- constructor to first presented frame (Gallery mode): **" << fixed(galleryStartupMs, 1) << " ms**\n- first show of the Widgets mode: **"
     << fixed(widgetsFirstShowMs, 1) << " ms**\n- process creation to first presented frame: **" << fixed(processStartupMs, 1) << " ms**\n";
  std::ofstream mdOut(outputDirectory / "phase4_baseline.md", std::ios::binary | std::ios::trunc);
  mdOut << md.str();
  if (!mdOut) throw std::runtime_error("cannot write phase4_baseline.md");
  return 0;
}

}  // namespace preview

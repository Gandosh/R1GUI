# Phase 3 performance baseline (slice 3.11)

Recorded by `r1gui-preview --bench <dir>`; numbers are a baseline for later regression checks on this machine, not a budget.

## Machine

- GPU: NVIDIA GeForce RTX 3090 (24540 MiB device-local), selected with `R1UI_GPU="RTX 3090"`
- CPU: AMD Ryzen Threadripper 2990WX 32-Core Processor (64 logical processors), RAM 262014 MiB
- OS: Windows 10 Pro for Workstations 25H2 (build 26200)
- Build: RelWithDebInfo (NDEBUG), validation layer off, FIFO presentation (vsync), borderless window, client 1440x900 physical pixels

## Methodology

1. **Frame cost**: Widgets mode, 60 warm-up frames then 600 measured frames, each forced (`PreviewApp::frame`), twice: redraw only, and redraw with a full relayout of the tree every frame. Layout = `Scene::layout`; paint-list build = `beginFrame` + painting the tree + glyph atlas upload staging; record+submit and present call come from `WindowTarget::lastFrameTimings`; wait = frame fence + swapchain acquire (this is where vsync shows). CPU active = layout + paint + record/submit + present call.
2. **Idle**: after the UI settled, 10 s of the real event loop (`PreviewApp::step(true)`) with no input and no focused field; process CPU time from `GetProcessTimes`, frames presented counted by the app.
3. **Memory**: `GetProcessMemoryInfo` working set and private bytes; GPU = `VK_EXT_memory_budget` device-local heap usage of this process. Taken after the idle runs, after 30 s idle in Widgets, then 3 s after opening each other mode (Screens: all 9 screens visited), and back in Widgets.
4. **Startup**: from the start of the `PreviewApp` constructor (window, device, swapchain, fonts, icons, scene) to the first presented frame, and from process creation to the same point.

## Frame cost (ms, 600 frames)

### Redraw only

| stage | median | p95 | p99 | max |
|---|---|---|---|---|
| layout | 0.002 | 0.005 | 0.007 | 0.012 |
| paint-list build | 0.158 | 0.239 | 0.363 | 1.011 |
| record + submit | 0.162 | 0.238 | 0.389 | 0.720 |
| present call | 0.151 | 0.328 | 0.423 | 1.153 |
| wait (fence + acquire) | 5.009 | 5.236 | 5.434 | 5.859 |
| **CPU active** | 0.508 | 0.711 | 1.103 | 1.587 |
| frame to frame | 5.517 | 5.740 | 6.140 | 6.916 |

### Full relayout every frame

| stage | median | p95 | p99 | max |
|---|---|---|---|---|
| layout | 0.348 | 0.433 | 0.489 | 0.641 |
| paint-list build | 0.152 | 0.196 | 0.224 | 0.239 |
| record + submit | 0.148 | 0.231 | 0.267 | 0.699 |
| present call | 0.088 | 0.161 | 0.215 | 1.569 |
| wait (fence + acquire) | 4.753 | 5.678 | 10.585 | 11.220 |
| **CPU active** | 0.753 | 0.979 | 1.145 | 2.364 |
| frame to frame | 5.511 | 6.562 | 11.356 | 12.219 |

## Idle

- 10.0 s without input: 0.0469 s CPU = **0.469 %** of one core, **0 frames** rendered
- 30.0 s (memory settle): 0.469 % CPU, 0 frames

## Memory (MiB)

| point | working set | private | GPU device-local |
|---|---|---|---|
| Widgets after settle | 167.5 | 201.8 | 81.2 |
| Widgets after 30 s idle | 167.6 | 202.0 | 81.2 |
| after opening Token swatches | 167.7 | 202.1 | 81.2 |
| after opening Reference screens | 172.7 | 214.7 | 86.8 |
| after opening Docking sandbox | 172.8 | 214.7 | 86.8 |
| back in Widgets | 167.7 | 209.6 | 86.8 |

## Startup

- constructor to first presented frame: **1338.1 ms**
- process creation to first presented frame: **1406.3 ms**

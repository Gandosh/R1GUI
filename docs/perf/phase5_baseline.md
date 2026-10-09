# Phase 5 performance baseline (slice 5.12)

Recorded by `r1gui-preview --bench-editor <dir>`; numbers are a baseline for later regression checks on this machine, not a budget. They describe the Editor screen (docking, commands, property panel, customizable menus) in the real multi-window loop; the Phase 4 numbers are in `phase4_baseline.md`.

## Machine

- GPU: NVIDIA GeForce RTX 4080 (16264 MiB device-local), selected with the `R1UI_GPU` environment variable
- CPU: AMD Ryzen Threadripper 2990WX 32-Core Processor (64 logical processors), RAM 262014 MiB
- OS: Windows 11 Pro for Workstations 10.0.26200; two monitors (3440x1440 and 1920x1080, both 100 %)
- Build: RelWithDebInfo (NDEBUG), validation layer off, FIFO presentation (vsync), borderless window, client 1440x900 physical pixels, 139 widgets in the Editor context

## Methodology

1. **Frame cost (main window only)**: 60 warm-up frames then 400 forced frames (`PreviewApp::frame`), twice: redraw only, and with a full relayout of the tree every frame. Stages as in the Phase 4 baseline; the CPU active figure is layout + paint-list build + record/submit + present call.
2. **Two windows**: the Curves panel is floated into a native OS window (`1` floating window); the cost is the wall time of one `PreviewApp::step(false)` that draws the invalidated main window and the floating window.
3. **Idle**: after the UI settled, 10 s of the real loop (`PreviewApp::step(true)` over `AppLoop`) with no input; process CPU time (`GetProcessTimes`) and frames presented, without and with the native window.

## Frame cost (ms, 400 frames)

#### Redraw only

| stage | median | p95 | p99 | max |
|---|---|---|---|---|
| layout | 0.028 | 0.040 | 0.056 | 0.067 |
| paint-list build | 0.372 | 0.473 | 0.651 | 0.776 |
| record + submit | 0.221 | 0.361 | 0.439 | 0.681 |
| present call | 0.136 | 0.379 | 0.686 | 0.852 |
| wait (fence + acquire) | 4.713 | 5.019 | 5.228 | 6.059 |
| **CPU active** | 0.783 | 1.146 | 1.294 | 1.525 |
| frame to frame | 5.499 | 5.932 | 6.369 | 6.910 |

#### Full relayout every frame

| stage | median | p95 | p99 | max |
|---|---|---|---|---|
| layout | 0.440 | 0.681 | 0.786 | 0.886 |
| paint-list build | 0.356 | 0.581 | 0.702 | 0.759 |
| record + submit | 0.278 | 0.355 | 0.473 | 0.619 |
| present call | 0.151 | 0.487 | 0.942 | 2.359 |
| wait (fence + acquire) | 4.267 | 4.636 | 5.667 | 8.077 |
| **CPU active** | 1.217 | 1.912 | 2.363 | 3.740 |
| frame to frame | 5.493 | 6.029 | 7.465 | 9.202 |

#### Loop step drawing the main window and one native window (full relayout of the main window)

| median | p95 | p99 | max |
|---|---|---|---|
| 5.563 | 6.202 | 8.292 | 11.979 |

## Idle

| situation | seconds | CPU s | CPU % of one core | frames |
|---|---|---|---|---|
| Editor, settled, nothing focused | 10.0 | 0.0000 | 0.000 | 0 |
| Editor with one native window, settled | 10.0 | 0.0312 | 0.312 | 0 |

## Memory (MiB)

| point | working set | private | GPU device-local |
|---|---|---|---|
| after the first idle run | 133.5 | 150.8 | 91.3 |
| with one native window, after its idle run | 158.7 | 179.4 | 104.2 |

## Startup

- constructor to first presented frame (Editor screen: window, device, shell, Editor, layouts): **1107.2 ms**

## Reading the numbers

- A redraw-only frame of the Editor costs 0.78 ms of CPU (median) on the main thread, a frame with a full relayout of its 139 widgets 1.2 ms; the 4.3 to 4.7 ms "wait" is the vsync (FIFO) wait on the 4080, not work. The Phase 4 Widgets screen (182 widgets) was 0.91 and 1.30 ms.
- One loop step that has to draw the main window and one native window takes 5.6 ms median, the same as one window: the two swapchains are presented back to back and the vsync wait dominates.
- **Idle**: an unchanging Editor is not drawn at all (0 frames in 10 s, 0.000 s of CPU). With one native floating window open it is still 0 frames; the 0.03 s of CPU in 10 s (0.3 % of one core) is the loop waking for the window's own timers. The dock integration test (`native_dock_gpu_test`) checks the same without the preview.
- **Real desktop** (`tests/preview/editor_drive.ps1`, the same process under real mouse and keyboard input, one native window open, 6 s without input, `TotalProcessorTime`): 0.3 to 0.8 % of one core in three runs, on the RTX 4080 (`nvidia-smi` lists the preview under the 4080's UUID, never under the 3090's).
- Memory: 134 MiB working set for the Editor alone, 159 MiB with a native window (one more swapchain, atlas upload and context: +25 MiB working set, +13 MiB GPU).
- Startup: 1.1 s from the constructor to the first presented frame on a first start (the three named layouts are written then); the Phase 4 Gallery start was recorded in `phase4_baseline.md`.

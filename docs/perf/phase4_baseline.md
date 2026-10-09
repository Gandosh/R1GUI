# Phase 4 performance baseline (slice 4.17)

Recorded by `r1gui-preview --bench <dir>`; numbers are a baseline for later regression checks on this machine, not a budget. The Widgets mode is now the composed widget-library screen and the Gallery mode (the new default) the widget galleries; the Phase 3 numbers (`phase3_baseline.md`) were taken on the hand-drawn panel.

## Machine

- GPU: NVIDIA GeForce RTX 4080 (16264 MiB device-local), selected with the `R1UI_GPU` environment variable
- CPU: AMD Ryzen Threadripper 2990WX 32-Core Processor (64 logical processors), RAM 262014 MiB
- OS: Windows 10 Pro for Workstations 25H2 (build 26200)
- Build: RelWithDebInfo (NDEBUG), validation layer off, FIFO presentation (vsync), borderless window, client 1440x900 physical pixels

## Methodology

1. **Frame cost**: for the Widgets and the Gallery mode, 60 warm-up frames then 600 measured frames, each forced (`PreviewApp::frame`), twice: redraw only, and redraw with a full relayout of the tree every frame. Layout = shell layout plus `UiContext::frame` (layout, overlay placement); paint-list build = `beginFrame` + painting the shell and the widget tree + glyph atlas upload staging; record+submit and present call come from `WindowTarget::lastFrameTimings`; wait = frame fence + swapchain acquire (this is where vsync shows). CPU active = layout + paint + record/submit + present call. Then the redraw cost of each of the five Gallery pages (200 frames).
2. **Idle**: after the UI settled, 10 s of the real event loop (`PreviewApp::step(true)`) with no input and no focused field, per mode; process CPU time from `GetProcessTimes`, frames presented counted by the app. Also 5 s with the Name field focused (caret blink), and 30 s after the focus was removed.
3. **Memory**: `GetProcessMemoryInfo` working set and private bytes; GPU = `VK_EXT_memory_budget` device-local heap usage of this process. Taken after the idle runs, then 3 s after opening each mode (Screens: all 9 screens visited), and back in Widgets.
4. **Startup**: from the start of the `PreviewApp` constructor (window, device, swapchain, fonts, icons, shell, Gallery mode) to the first presented frame, the first show of the Widgets mode (build of its context and screen to its second presented frame), and process creation to the first frame.

## Frame cost (ms, 600 frames)

### Widgets mode (182 widgets)

#### Redraw only

| stage | median | p95 | p99 | max |
|---|---|---|---|---|
| layout | 0.009 | 0.016 | 0.019 | 0.022 |
| paint-list build | 0.460 | 0.653 | 0.765 | 1.152 |
| record + submit | 0.238 | 0.341 | 0.427 | 8.953 |
| present call | 0.177 | 0.597 | 1.010 | 3.565 |
| wait (fence + acquire) | 4.522 | 5.031 | 5.981 | 14.826 |
| **CPU active** | 0.910 | 1.455 | 1.960 | 9.793 |
| frame to frame | 5.454 | 6.114 | 6.924 | 24.624 |

#### Full relayout every frame

| stage | median | p95 | p99 | max |
|---|---|---|---|---|
| layout | 0.515 | 0.785 | 0.924 | 1.166 |
| paint-list build | 0.352 | 0.547 | 0.729 | 1.502 |
| record + submit | 0.219 | 0.352 | 0.465 | 0.662 |
| present call | 0.171 | 0.526 | 0.870 | 3.175 |
| wait (fence + acquire) | 4.157 | 5.761 | 7.542 | 10.127 |
| **CPU active** | 1.303 | 1.933 | 2.271 | 5.333 |
| frame to frame | 5.501 | 7.058 | 8.842 | 12.418 |

### Gallery mode, Buttons page (216 widgets)

#### Redraw only

| stage | median | p95 | p99 | max |
|---|---|---|---|---|
| layout | 0.008 | 0.013 | 0.018 | 0.056 |
| paint-list build | 0.550 | 0.871 | 1.090 | 2.486 |
| record + submit | 0.244 | 0.385 | 0.589 | 1.573 |
| present call | 0.179 | 0.540 | 0.781 | 1.185 |
| wait (fence + acquire) | 4.395 | 5.826 | 7.583 | 12.417 |
| **CPU active** | 1.044 | 1.581 | 2.190 | 3.300 |
| frame to frame | 5.464 | 6.884 | 8.492 | 13.611 |

#### Full relayout every frame

| stage | median | p95 | p99 | max |
|---|---|---|---|---|
| layout | 1.039 | 1.540 | 1.798 | 5.138 |
| paint-list build | 0.465 | 0.727 | 0.904 | 1.151 |
| record + submit | 0.225 | 0.346 | 0.446 | 19.764 |
| present call | 0.175 | 0.682 | 2.218 | 14.621 |
| wait (fence + acquire) | 3.029 | 9.055 | 15.370 | 30.473 |
| **CPU active** | 1.967 | 3.069 | 4.650 | 26.228 |
| frame to frame | 5.177 | 12.232 | 18.232 | 41.603 |

### Gallery pages, redraw only (200 frames)

| page | widgets | CPU active median | p95 | p99 | max | frame to frame median |
|---|---|---|---|---|---|---|
| Buttons | 216 | 0.947 | 1.418 | 1.768 | 1.831 | 5.489 |
| Fields | 169 | 0.969 | 1.515 | 1.976 | 1.981 | 5.436 |
| Containers | 213 | 2.009 | 3.108 | 4.429 | 4.723 | 5.533 |
| Overlays | 213 | 1.282 | 2.274 | 7.163 | 9.413 | 5.485 |
| Editors | 98 | 1.240 | 1.609 | 1.812 | 2.012 | 5.499 |

## Idle

| situation | seconds | CPU s | CPU % of one core | frames |
|---|---|---|---|---|
| Widgets, settled, nothing focused | 10.0 | 0.0156 | 0.156 | 0 |
| Gallery (Buttons), settled, nothing focused | 10.0 | 0.0312 | 0.312 | 0 |
| Widgets, Name field focused (caret blink) | 5.0 | 1.6250 | 32.483 | 905 |
| Widgets, after the focus was removed | 30.0 | 0.1562 | 0.521 | 0 |

## Memory (MiB)

| point | working set | private | GPU device-local |
|---|---|---|---|
| Widgets after settle | 132.2 | 151.4 | 88.0 |
| Gallery after settle | 132.9 | 152.2 | 88.0 |
| Widgets after 30 s idle | 135.2 | 154.1 | 88.0 |
| after opening Widget gallery | 135.2 | 154.1 | 88.0 |
| after opening Token swatches | 135.2 | 154.1 | 88.0 |
| after opening Reference screens | 135.3 | 161.0 | 93.7 |
| after opening Docking sandbox | 135.3 | 161.0 | 93.7 |
| back in Widgets | 135.3 | 161.0 | 93.7 |

## Startup

- constructor to first presented frame (Gallery mode): **1013.9 ms**
- first show of the Widgets mode: **1.8 ms**
- process creation to first presented frame: **1137.7 ms**

## Comparison with Phase 3 (`phase3_baseline.md`)

The Phase 3 numbers were recorded on the RTX 3090 with the hand-drawn panel; these on the **RTX 4080** (`R1UI_GPU="RTX 4080"`, the 3090 must stay idle), with the real widget library, so only the CPU-side rows are comparable in spirit. Wait times reflect vsync of different displays.

| measure | Phase 3 (3090, hand-drawn panel) | Phase 4 (4080, Widgets mode, 182 widgets) |
|---|---|---|
| CPU active per forced redraw, median | 0.508 ms | 0.910 ms |
| CPU active, full relayout every frame, median | 0.753 ms | 1.303 ms |
| idle, nothing focused | 0 frames | 0 frames, 0.16 % CPU of one core |
| idle after a focused field was blurred | not measured | 0 frames, 0.52 % |
| Name field focused (caret blink) | frames at the blink rate only | continuous frames at the display rate (905 in 5 s): the field has no timer service, the cost is about 33 % of one core |
| working set after settle | 167.5 MiB | 132.2 MiB |
| private bytes after settle | 201.8 MiB | 151.4 MiB |
| constructor to first frame | 1338 ms | 1014 ms (Gallery mode) |

Reading: the composed screen costs about twice the CPU per frame of the Phase 3 panel (more widgets, real text and icon services, tree and scroll clipping) and stays under 1 ms to 2 ms; a settled UI renders nothing and wakes only for events. The one regression to know about is the caret blink of a focused field, which renders continuously (documented in P4_g2_notes.md); a timer-driven blink (`UiContext::setTimer`) would remove it. Overlays add no idle cost: a popover schedules only its 120 ms anchor watch while it is open (asserted in `tests/preview/modes_test.cpp`).

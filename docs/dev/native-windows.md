# Native floating windows (slice 5.2)

Developer guide to the native OS-window backend of the dock: `NativeFloatingBackend`, the multi-window run loop `AppLoop` and the platform additions they rely on. Read it with `docs/dev/docking.md` (section 6, the `IFloatingBackend` contract, and section 10), `source/ui-widgets/include/r1ui/widgets/dock/native/` and `docs/dev/widgets.md` section 11 (multi-window rules). Behaviour spec: `docs/spec/interaction/03-floating-windows-multi-monitor.md`; owner decisions: rounded 8 px floating windows, unreachable windows brought back with a 100 px visible margin, D8 (follow the operating system for scale while a window straddles two monitors), per-window full screen.

## 1. Parts

| Part | Where | Owns |
|---|---|---|
| `NativeFloatingBackend` | `include/.../dock/native/NativeFloatingBackend.h`, `src/gpu/dock/NativeFloatingBackend.cpp`, `NativeBackendEvents.cpp` | the `IFloatingBackend` surface, the window table, rectangle rules, listener calls, pointer tracking, display changes |
| `NativeWindow` | `src/gpu/dock/NativeWindow.{h,cpp}` (private) | one floating window: `platform::Window`, `WindowTarget`, `UiContext`, frame and holder widgets, event feed, frames |
| `NativeFrame`, `NativeHolder` | `src/gpu/dock/NativeFrame.{h,cpp}` (private) | the toolkit-drawn title bar, border, buttons; the chrome description for the platform; the content area |
| `ScreenSpace`, frame arithmetic, `topmostWindow` | `include/.../dock/native/NativeCoords.h`, `src/gpu/dock/NativeCoords.cpp` | pure geometry: physical <-> screen logical for any monitor layout, limits, the drop-target query |
| `WindowTable<W>` | `include/.../dock/native/WindowTable.h` | ids, stacking, deferred destruction (header only, tested with a fake window) |
| `AppLoop` | `include/.../dock/native/AppLoop.h`, `src/gpu/dock/AppLoop.cpp` | the run loop over the main window and every floating window |
| `NativeWin32` | `src/gpu/dock/NativeWin32.{h,cpp}` (private) | the three Win32 calls the backend needs beyond `platform::Window` |

Everything lives in the `ui-widgets-gpu` library (the backend needs the platform window and the GPU target). The only OS-specific file of the backend is `NativeWin32.cpp`.

## 2. What a floating window is

A real top-level window created through `platform::Window` with `borderless`, `toolWindow`, `resizable` and the new `WindowDesc::owner` (the main window):

* **Owned by the main window** (`GetWindow(GW_OWNER)`): stays above it, is hidden and restored with it when it is minimized or restored, is destroyed with it (spec 03 rule 38). A tool window (`WS_EX_TOOLWINDOW`): no taskbar button and no Alt+Tab entry.
* **Borderless with the platform's chrome**: the visible frame is removed, `WS_THICKFRAME` stays, so the OS provides edge resize, snapping, double-click maximize and the shadow. The platform requests rounded corners from the window manager (`DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND`), the owner decision for floating windows; Windows squares them again while the window is maximized or snapped. Windows before 11 ignore the attribute and stay square.
* **A toolkit-drawn title bar** (`NativeFrame`): 34 px, the title of the front tab, a maximize/restore button and a close button, a 1 px border. The platform answers the OS hit test from `ChromeLayout` (caption rectangle, button rectangles, 6 px resize bands), so a caption drag is an OS move loop, a double click on the caption maximizes, the buttons are tracked by the platform and the close button ends in a close request. `NativeWindow::syncChrome` refreshes the layout after every size or DPI change.
* **Its own `UiContext`** on the application's shared `Services` and `RenderDevice`, one `WindowTarget` (swapchain) and one `AtlasConsumer` (the context's own), so glyph and icon uploads are per window. The root holds `NativeFrame` (fills the window) with the `NativeHolder` inside it; the holder is `FloatContent::parent`, the dock mounts a `DockAreaView` in it.
* The **content rectangle** is the client area minus the frame: `BackendInfo::frame = {1, 34, 1, 1}` logical px. `contentRect(id)` is what the OS shows, read back after every change.

Not a goal: a minimize button (the window minimizes with its owner), a taskbar button, per-window full screen (see section 9).

## 3. Coordinates

`NativeCoords.h` defines screen logical space as the contract says: virtual-desktop pixels divided by the scale of the monitor under the point. With monitors of different scale the mapping is piecewise:

* `toLogical(p) = p / scale(monitor containing p)`, the nearest monitor when none contains it.
* `toPhysical(l, hint)` looks for a monitor whose logical rectangle (bounds / scale) contains `l`. A 100 % monitor next to a 200 % monitor overlap in logical space; the hint (the monitor the window is on) wins where both show the point, else the first monitor of the list (the primary comes first), else the nearest one decides.
* `toScreen(window, local)` = the window's client origin (logical) + `local`; `toWindow` is its exact inverse (a subtraction), so conversions round-trip exactly. The main window's origin is `Window::clientOrigin()` (new) converted the same way.
* A window keeps its **logical size** across monitors: physical size = logical size x the scale of the monitor it lands on. `place()` repeats the `SetWindowPos` once when the OS handed over to another DPI during the call (the platform applies the OS's suggested rectangle at once), so the last pass is exact.

Pure tests with several layouts (one monitor, 150 %, this machine's 3440x1440 + 1920x1080, 100 % + 200 % side by side, a 150 % monitor left of the primary with negative coordinates, three monitors, none): `tests/ui-widgets/dock/native/native_coords_gpu_test.cpp`.

## 4. Lifetime and safety rules

* **A window is never destroyed under its own handler.** `destroyWindow` hides the OS window at once, removes the window from every lookup (`WindowTable::retire`) and parks the object; `collectGarbage()` (AppLoop calls it after event dispatch) frees the parked windows in retirement order. The dock destroys a floating window from inside that window's own pointer-up handler (dropping its last tab elsewhere); a test destroys a window from a timer of its own context to prove the object outlives the handler.
* **Close is a request.** The close button, Alt+F4 and the system menu end in `WM_CLOSE`; the platform window is created with `setCloseNeedsConfirmation(true)`, so the backend only reports `onFloatCloseRequested` and nothing is destroyed until the host calls `destroyWindow` (spec 03 rule 43: the host may refuse).
* **OS destruction** (`DestroyWindow` from outside, a crash of the owner chain): `Window::isAlive()` turns false; the next `processEvents` retires the window and calls `onFloatLost` exactly once. A window the host destroyed is never reported lost. Every `platform::Window` call on a dead window is a harmless no-op.
* **Echo rule by comparison.** State the host changes (rectangle, maximize, title, visibility, stacking) is stored before the OS is asked; the events the OS produces afterwards are compared with the stored state, so only real differences are reported. A DPI change caused by a host move is recognised by the scale the move announced (`expectedScale`).
* **Hidden windows ignore focus changes.** The dock hides the window of a dragged sole tab; the OS then moves the focus away. A `UiContext` that hears "window inactive" cancels the pointer interaction, i.e. the drag the hiding belongs to. Found by the dock integration test; `NativeWindow::processEvents` drops focus events of a window the host hid.
* **Hiding keeps the pointer capture** (`Window::setVisible(false)`): hiding releases the OS capture, so the platform takes it back at once and suppresses the capture-loss event. Moves and the release then keep arriving at the hidden window, also outside every window.
* **Activation.** A press in a window raises it in the table and reports `onFloatActivated` when the order changed. A focus event counts only while the window still is the foreground window (a focus event queued at creation can be stale by the time it is read; found by the conformance suite's echo clause).
* **Stacking.** `stacking()` is the table's order, kept in step with the OS: `bringToFront` places every other floating window directly below the raised one, bottom to top, which preserves their order and leaves other applications' windows alone. The activation of a window by the user is mirrored into the table.
* **Threading.** UI thread only. Nothing is called with a table iterator held; every listener call is followed by a fresh lookup.

## 5. The run loop

`AppLoop(main, backend, hooks)`; the application supplies `processMain` (drain the main window's events and apply them), `mainNeedsFrame`, `renderMain`, `mainMsUntilTick` and optionally `quitRequested`. One `step(block, maxWaitMs)`:

1. `main.pumpEvents()` dispatches the thread's whole message queue; each window records its own events.
2. `processMain(now)`, then `backend.processEvents(now)` (floating windows' events, listener calls, lost windows, pointer sampling), then `backend.collectGarbage()`.
3. Draw the main window when it needs a frame, then every floating window that needs one (one after the other, each with its own swapchain and atlas consumer).
4. When nothing was drawn and `block` is set, `waitForEvents(min(timers of every context, pointer-tracking interval, maxWaitMs))` (`MsgWaitForMultipleObjectsEx`): an idle application uses no CPU (the dock integration test measures 0 ms of CPU over 700 ms of blocking steps).

**OS move/size loop.** While Windows runs its own loop (title-bar drag, edge resize) `pumpEvents` does not return. Every window calls the live callback on size changes and a ~60 Hz timer; AppLoop's `liveStep` does the same work as a step minus pumping, minus freeing windows (the OS still uses the one that called) and never nests (`busy_`, `inLive_`). Errors thrown there are stored and rethrown by the next `step`.

Recipe for an application (PreviewApp today has a single-window loop; this is what the integrator wires):

```cpp
platform::Window main(desc);                       // the main window
render::RenderDevice device;  GpuTextureFactory textures(device);  Services services(tokens, textures, paths);
render::WindowTarget mainTarget(device, main);
NativeFloatingBackend backend(main, device, services);   // outlives the DockHost
UiContext mainUi(services);  ...                         // main context, root holds the DockHost
DockHost& dock = mainUi.create<DockHost>(parent, registry, backend);
AppLoop loop(main, backend, {processMain, mainNeedsFrame, renderMain, mainMsUntilTick});
loop.run();                                        // a floating window is just another context
// teardown: destroy the AppLoop, then the DockHost (mainUi), then the backend, then target/services/device/window
```

The application keeps its own Escape policy: Escape cancels a tab drag through the dock's key handling in the window that has the keyboard focus; while the window of a dragged sole tab is hidden the focus is in another window of the application.

## 6. Pointer tracking and the cross-window tab drag

A tab drag starts in the window whose strip was pressed: that platform window has the OS pointer capture, so its moves and the release arrive even outside every window, and the strip reports them in its own window coordinates; the host converts with `toScreen`. `describe().pointerTracking` is true and the host also calls `beginPointerTracking`: while tracking, `processEvents` samples the OS pointer (cursor position and the physical left-button state) every `trackingIntervalMs` (8 ms; the loop wakes for it) and delivers screen-logical samples while the button is down plus exactly one release sample, independent of which window has the capture, of whether it is hidden and of the pointer being outside every window. A button never seen down (input posted to a window instead of real input) delivers nothing; the strip's own events drive that drag. The sample source is replaceable (`NativeBackendOptions::pointerSource`), which is how the conformance suite scripts it.

`topmostWindowAt(screen, exclude)` uses the table's order, each window's outer rectangle (content plus frame), the host's hidden flag, `IsWindowVisible` (false while the owner is minimized) and the main window's content rectangle; it needs no window under the point (outside every window it answers nullopt).

## 7. Monitors, DPI, display changes

* `WM_DPICHANGED`: the platform applies the OS's suggested rectangle and posts `DpiChanged`; `NativeWindow` sets the new viewport and scale (the context re-lays out), invalidates the swapchain and refreshes the chrome; the backend reports `onFloatScaleChanged` unless a host move announced that scale. The logical size is kept because the OS suggestion scales the physical size by the DPI ratio; the new content rectangle is read back and reported with `onFloatMoved` when it differs.
* D8 (follow the operating system while a window straddles two monitors): the window's DPI is whatever the OS assigns; the backend never overrides it.
* `WM_DISPLAYCHANGE` (new `EventType::DisplayChanged`, seen by every window): the monitor list is re-read and every window that no longer overlaps a monitor's work area by 100 logical px (`kVisibleMarginLogical`, the dock's `MonitorSet` rule and the platform's `clampToVisible`) is moved back and reported with `onFloatMoved`. The same clamp is applied when a window is created. The application calls `onDisplayChanged()` when the main window sees the event.
* `monitorAt(point)` returns the OS device name (new `MonitorInfo::name`, e.g. `\\.\DISPLAY2`), stored with the layout (spec 04 rule 4).

## 8. Platform additions (ui-platform, additive)

| Addition | Why | Test |
|---|---|---|
| `WindowDesc::owner` | owned floating windows (stay above, minimize and restore with the main window) | `platform_additions_gpu_test` owned window, minimize/restore |
| `Window::clientOrigin()` | screen position of the client area: the base of `toScreen` | same, against `ClientToScreen` |
| `Window::setVisible(bool)`, `isVisible()` | hiding the window of a dragged sole tab without losing the pointer capture | same, capture kept, no `CaptureLost`, moves and release arrive |
| `Window::isAlive()` and no-op calls on a dead window | an OS-destroyed window must be noticed and every later call must be harmless | same, `DestroyWindow` from outside |
| `EventType::DisplayChanged` (rank 2) | monitors added, removed or rearranged | same, `WM_DISPLAYCHANGE` becomes the event |
| `MonitorInfo::name` | the monitor name stored with the layout | same, names are unique OS device names |

## 9. Tests, harness and drive

| Test (all under `tests/ui-widgets/dock/native/`) | Label | What it proves |
|---|---|---|
| `window_table_test` | fast | ids, stacking, deferred destruction order with a fake window layer |
| `native_coords_gpu_test` | gpu (pure) | coordinate conversion for several layouts and scales, limits, drop-target stacking |
| `native_backend_conformance_gpu_test` | gpu | the whole `BackendConformance.h` suite over real windows, including the optional clauses (user move, raise, close, OS destruction, pointer tracking outside every window), plus window styles, owner, corner attribute, coordinates against the OS, Alt+F4, user maximize, hidden window keeps the capture, destruction from an own handler, minimize, bringing a window back, stacking against the OS z-order |
| `native_dock_gpu_test` | gpu | DockHost on the native backend: tab out of the main window, back, between two floating windows, OS move/resize/close/destroy, minimize, idle CPU |
| `native_render_gpu_test` | gpu | the frame rendered offscreen in both themes; a real floating window captured with PrintWindow |
| `platform_additions_gpu_test` | gpu | the platform additions of section 8 |
| `native_harness_gpu_test` | gpu | smoke; `--interactive` is the manual harness |
| `native_drive.ps1` | manual | the real-desktop drive with injected OS input |

The tests that need a desktop print `SKIPPED: <reason>` and exit 0 when the session has no monitor or window creation fails; they never inject real input and destroy every window they create. The GPU tests pick the RTX 4080 by name (`R1UI_GPU` or the default "RTX 4080") and refuse a 3090.

Manual run: `native_harness_gpu_test.exe --interactive --seconds 120` (R1UI_GPU set to the 4080): drag a tab out of the main window, drop it on empty desktop space, drag it back, between floating windows, move/resize with the title bar and edges, close with the X, minimize the main window, drag a window to the second monitor. The script `native_drive.ps1 -Exe <harness> -OutDir <dir>` does all of this with real mouse input and writes `drive.log` and captures.

## 10. Not implemented, approximate, unverified

* **Per-window full screen** is not implemented (cheap in principle: a borderless window of the monitor's bounds; but the owner's wording leaves the toggle, the key and the taskbar behaviour open). The maximize button maximizes to the work area.
* **No minimize button**; a floating window minimizes only with the main window (owned).
* **Popups are clipped to their window** (docs/dev/widgets.md section 11): a menu opened from a small floating window cannot extend outside it. The dock's tab context menu works inside the window.
* **Escape while the window of a dragged sole tab is hidden** is handled by the application's global key handler, not by the backend.
* **Snap layouts, real DPI changes, shadows and `WM_DISPLAYCHANGE` with a real monitor change** could not be verified on the machine this was built on (see the evidence in `Goal/evidence/P5_S02.md` for the real-desktop run): both monitors report 100 %, so a scale change between monitors is covered by the pure tests and the platform's own DPI handling only. The DWM shadow comes from the platform's 1 px frame extension; whether it is drawn is the window manager's choice.
* **Owner other than the main window:** `FloatRequest::owner` is accepted but every floating window is owned by the main window.
* **Pointer sampling polls the OS pointer** every 8 ms while a tab drag runs; there is no hook, so a drag moved faster than the poll is delivered as the sequence of the polls plus the strip's own events (the strip's events carry every movement).

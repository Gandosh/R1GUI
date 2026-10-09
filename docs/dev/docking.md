# Docking (ui-dock and ui-widgets/dock)

Developer guide to Phase 5 slices 5.1 to 5.4: the dock model, the DockHost widget, floating windows, layout files and the layout manager. Read it with `source/ui-dock/include/r1ui/dock/DockLayout.h`, `source/ui-widgets/include/r1ui/widgets/dock/DockHost.h` and `.../FloatingBackend.h` open. Behaviour specs: `docs/spec/interaction/02-docking.md`, `03-floating-windows-multi-monitor.md`, `04-layouts.md`, `05-panel-resize-adaptive.md` (owner decisions D9 to D24 in `00-index-and-decisions.md`).

## 1. Parts and who may call whom

```
application ──► DockHost (widget) ──► IFloatingBackend ──► InWindowFloatingBackend (now)
     │              │   │                                  native OS-window backend (later)
     │              │   └─► PanelRegistry (titles, flags, content factories)
     │              ▼
     │         dock::DockLayout (headless model, one writer: the host)
     ▼
dock::LayoutManager ──► dock::ILayoutTarget (DockHost implements it) ──► ILayoutStore, IClock
```

| Part | Where | Owns |
|---|---|---|
| Model | `source/ui-dock` | tree of splits and tab stacks, geometry, drop zones, editing operations, closed-panel memory, window state, JSON schema 1 and 2 |
| Layout manager | `source/ui-dock` | active layout, named layouts, auto-save, import and export, per application mode; headless |
| DockHost and views | `source/ui-widgets/.../dock` | the model rendered as widgets, drags, commands, the change observer |
| Floating backend | `FloatingBackend.h`, `InWindowFloatingBackend.h` | how floating windows are shown |

The model never knows about widgets; the views never edit the model (they report intents through `IDockInteraction`, the host decides). Every model operation works on a copy, normalises, validates and only then commits, so a refused call changes nothing and `status.error` says why.

## 2. The model (ui-dock)

Coordinates: one shared logical space ("screen logical"). The main rectangle given to `computeLayout` and every floating area's rectangle live in it, which is what lets a drop target in another window be found with one hit test.

Operations added in Phase 5 (all validated, all keep the previous layout on refusal):

| Call | Rule |
|---|---|
| `closePanel` | remembers a `ClosedSlot` (the other tabs of its stack and its index, else a panel of the neighbouring region and the side, else its floating rectangle); documents are forgotten (spec 02 rule 41); locked or `canClose == false` panels are refused |
| `openPanel(panel, options)` | spec 02 rule 55 in order: already open (brought to front), remembered slot, the module's suggested position, then the default region (first stack of the main area, or the empty main area); `floatWhenUnplaced` gives the reference editor's behaviour (a floating window of the panel's default size centred in `floatBounds`) |
| `dock` | refuses moving a locked docked panel and floating a panel with `canFloat == false`; forgets the closed slot |
| `moveSplitter` | floor `minPanelSize` (20 px, D11); a collapsed neighbour is expanded instead of resized; pinned neighbours store the new size |
| `setPinned` (D10) | a pinned child keeps its pixel size when the window resizes; at least one child of a split must stay flexible |
| `setCollapsed` (D11) | the explicit collapse command; dragging never collapses; the last visible child cannot collapse |
| `setPanelLocked` (D13) | per-tab lock flag, part of the saved layout |
| `setAreaRect`, `raiseArea`, `setWindowState`, `setMeta` | window rectangles, stacking order, per-window state, layout name and description |
| `fitWindows(MonitorSet)` | brings windows that overlap no monitor by at least 100 logical px (`DockConfig::visibleMargin`) back: centred on the primary work area, size reduced to fit (spec 03 rule 64, D13) |

`PanelInfo` carries `kind` (application page, shared panel, ordinary panel, document), `canClose`, `canFloat`, `locked`, a suggested first position and a default floating size. Application pages get 50 px strips and 210 px tabs (spec 02 rule 9). Tabs shrink to 60 px, then the strip reports `overflow` (D12); `StackLayout::tabsWidth` is the unscrolled width.

## 3. Layout files (schema version 2)

```
{"version":2,"name":S,"description":S,
 "main":{"root":NODE|null,"window":W},
 "floating":[{"rect":R,"root":NODE,"window":W}],
 "panels":[{"id":N,"locked":B}],
 "closed":[{"panel":N,"neighbours":[N],"index":N,"anchor":N,"side":"left","floating":B,"rect":R}]}
NODE  {"type":"split","weight":w,["pinned":px,]["collapsed":true,]"axis":"row"|"column","children":[NODE]}
      {"type":"stack","weight":w,...,"tabs":[N],"active":N}
W     {"maximized":B,"monitor":S,"monitorIndex":N,"dpi":x,["rect":R]}     R {"x","y","w","h"} logical units
```

* `panels` lists every panel the host knew at save time (docked or not): that is how a later load tells a panel the user closed from one that is new (spec 04 rules 42, 43). New panels with a suggested position appear there; others stay closed until opened.
* Version 1 files are still read (strictly, as before) and written back as version 2. A version greater than 2 is rejected cleanly (`"unsupported layout version N"`), the caller keeps its layout.
* Version 2 is read leniently where a defect is local: unknown, duplicate or invalid panel ids are dropped, weights outside `[1e-9, 1e9]`, active indexes, floating rectangles, monitor names and display scales are repaired, overlong strings are cut at a UTF-8 boundary with control characters replaced, damaged closed-panel records are dropped. Every repair is counted (`LoadResult::repairedValues`, `droppedPanels`, `duplicatePanels`) and listed in `warnings` (at most 100). Structural defects reject the file: wrong types, unknown members, duplicate keys, depth above 32 tree levels, more than 4096 nodes, more than 64 floating areas, input above 4 MiB or 200k JSON values, a layout with no docked panel at all.
* `LoadOptions::monitors` brings unreachable windows back (counted in `windowsMoved`); `placeNewPanels` can be switched off.
* Golden files: `tests/ui-dock/golden/layout_v1.json` (read) and `layout_v2.json` (byte-exact output of the canonical layout).

## 4. LayoutManager (slice 5.4)

`LayoutManager(ILayoutStore&, IClock&, ILayoutTarget&)`; stores: `MemoryLayoutStore` (tests, tools) and `FileLayoutStore(root)` (`<root>/<mode>/<key>.layout.json`, written through a temporary file and an atomic replace). Keys are 1 to 64 ASCII characters (letters, digits, space, `_-.()`), never start or end with a space or dot, are not Windows device names and contain no `..`; the store re-validates them, so a key cannot leave its folder. Keys starting with `_` are internal (`_active`, `_corrupt-N`) and hidden from lists.

| Call | Behaviour |
|---|---|
| `setMode(mode)` | each application mode has its own scope, active layout and named layouts; the pending change of the old mode is saved first, a mode that cannot be saved is refused |
| `startup()` | applies `_active`; when it is missing, damaged, newer than this build or unusable the default (`setDefaultProvider`) is applied and a damaged file is kept aside as `_corrupt-N` (never deleted or overwritten; if it cannot be kept aside auto-save stays off until the user loads or resets) |
| `list`, `save`, `saveAs`, `rename`, `duplicate`, `remove` | user layouts; display names may be any UTF-8 text (a name with no file-name characters gets a key `layout-N`); `save` overwrites an existing layout only; a copy does not carry the description unless asked |
| `load(key)` | validates, applies through the target, makes it the active layout, writes `_active`; a rejected layout changes nothing (current layout and stored files untouched) |
| `resetToDefault(confirm)` | asks `confirm(question)`; declining or no callback cancels |
| `notifyChanged`, `tick`, `msUntilSave`, `flush(force)`, `setSuspended` | auto-save 5 s after the last change (every change restarts the countdown), held while suspended (tab drag, layout switch), a failed write retries one delay later, `flush` for application exit |
| `importText`, `importFile`, `exportFile` | import validates against the registered panels first and refuses an existing name (unless asked), a file larger than 4 MiB, and a layout imported onto itself; export writes the current arrangement (with a name and description) or a stored layout to any file |

Wiring the host: `host.setOnChanged([&](DockChange c){ if (c == DragStarted) mgr.setSuspended(true); else if (c == DragEnded) mgr.setSuspended(false); else mgr.notifyChanged(); });` and call `mgr.tick()` from a timer driven by `msUntilSave()`.

## 5. Using DockHost (slice 5.1)

```cpp
PanelRegistry registry;
registry.add({.id = 1, .title = "Outliner", .icon = "layout-panel-top", .factory = [](UiContext& ui, WidgetId parent) { return ui.create<Outliner>(parent).id(); }});
InWindowFloatingBackend backend(ui, ui.root());
DockHost& dock = ui.create<DockHost>(parent, registry, backend);   // fills `parent`
dock.setLayout(*DockLayout::create(registry.infos(), {}, root).layout);
```

* Content: the factory runs lazily, once while the panel is open, the first time the panel is the front tab of a region; the widget is kept while the panel stays open (moves between regions and windows of the same UiContext reparent it) and destroyed when the panel closes. The host sizes it to its body (100 percent). A throwing factory leaves an empty body and sets `lastError()`.
* Commands (plain methods for the command layer): `closeActiveTab`, `nextTab`, `previousTab`, `floatActiveTab`, `resetLayout` (needs `setDefaultLayout`), `closePanel`, `openPanel`, `activatePanel`, `closeOthers/ToLeft/ToRight`, `floatPanel`, `moveStackToNewWindow`, `setPanelLocked`, `togglePinned`, `toggleCollapsed`, `setWindowRect`, `focusNextRegion`, `focusActivePanel`, `setMainWindowState`. Shortcuts are not bound here; Ctrl+W, Left/Right/Home/End and Down work inside a focused strip, arrows, Page Up/Down, Home/End on a focused splitter handle.
* Interaction (all verified with synthetic events in `tests/ui-widgets/dock`): activation on press; close button (hovered or front tabs; hidden for locked tabs, space kept); middle click closes on release over the same tab; right click activates and opens the context menu (Close, Close Others, Close to the Left/Right, Float, Move to New Window, Lock/Unlock Tab, Pin/Unpin Region, Collapse/Expand Region); double click on a tab does nothing, on empty strip space of a floating window it maximizes or restores it.
* Tab drag: starts after strictly more than 5 px; the tab is lifted out of its strip (the region shows its right neighbour, else left); the model is untouched until the drop; zones are the model's (`hitTestDropZone`): the four sides of a body (30 percent cross, 5 to 150 px), the strip insertion slot (scroll aware, live gap), the area edges (6 px bars), the single centre target of an empty area; the middle of a body or anywhere outside drops as a floating window at the ghost position; hovering a tab header for 0.75 s activates it; Escape (D9) restores everything including fronts changed by hovering; a floating window that held only the dragged tab is hidden during the drag; losing the pointer capture drops on nothing. Application pages may only join strips; with `allowDocking = false` tabs only reorder.
* Splitters: 5 px bars drawn as a 1 px line (hover `border-strong`, drag `accent`), capture for the whole drag, floor 20 px, Escape reverts, hit band 9 px with `DockHostOptions::touch` (D22), keyboard resize 10 px (Shift 50) on a focused handle (D10).
* Empty area: a framed hint card (spec 02 rule 48); during a drag the centre target replaces it.
* Observer: `setOnChanged(DockChange)`; `Arrangement`, `Active`, `Window`, `Lock` mean the layout may need saving, `DragStarted` and `DragEnded` bracket a tab drag. `setLayout` does not notify.
* `refreshPanels()` after adding panels to the registry rebuilds the model over the new list keeping the arrangement.

Differences from the reference editor kept as options: `closeGroupsAnyKind` (default true; spec 02 rule 40 restricts close others/left/right to document tabs and application pages), `floatWhenUnplaced` (default false: a panel with no slot opens in the default region instead of a 1000 by 600 window).

## 6. Floating backend contract (IFloatingBackend)

This is what the native OS-window backend must implement. It is checked by the conformance suite `tests/ui-widgets/dock/BackendConformance.h`: write a `BackendRig` for the backend and call `runBackendConformance("name", factory)` (see `in_window_backend_test.cpp` for the in-window rig and for a deliberately broken backend the suite must reject).

**Threading.** Everything, methods and listener calls, on the UI thread. A backend whose windows live on other threads marshals first.

**Coordinate spaces.** *Screen* is one logical-pixel space in which the main content rectangle and every window's content rectangle are expressed; for a native backend it is the virtual desktop divided by the scale of the monitor under the point, so a window dragged to a monitor with another scale keeps its logical size (spec 03 rules 51, 54). *Window* coordinates are logical pixels from a window's client origin: strips inside a window deliver pointer positions in them, `toScreen`/`toWindow` convert (the in-window backend uses the context's coordinates for both, so the conversions are the identity). A window's *content rectangle* is its client area without any frame the backend draws or the OS adds; `BackendInfo::frame` tells the frame insets so a window dropped at the ghost's position gets its outer top-left there.

**DPI.** `FloatContent::scale` is the window's display scale; the dock works in logical pixels only and re-lays-out when the context's scale changes. Report a monitor change with `onFloatScaleChanged`; `monitorAt(screenPoint)` returns the monitor name stored with the layout (spec 04 rule 4).

**Windows and ids.** `kMainWindow` (0) always exists. `createWindow(FloatRequest)` returns a new non-zero id, never reused during the backend's lifetime, and makes a content area available through `content(id)`: a `UiContext` and a parent widget filling the content rectangle, in which the dock mounts a `DockAreaView`. The request carries the tabs (for titles), the wanted content rectangle in screen coordinates, the owner window (workspace panels stay above their owner, spec 03 rule 38), a minimum content size and `resizable`. The backend enforces its own limits (minimum size, at most the screen, a bounded window count) and `contentRect(id)` tells what it did; a non-finite rectangle or a refused creation returns `ok == false` with a message and no side effects. Calls with an unknown id return false and never crash; `destroyWindow` is idempotent (false the second time) and destroys the window's widgets.

**Echo rule.** `setContentRect`, `setMaximized`, `setTitle`, `bringToFront`, `setVisible` and `createWindow` never call the listener; only things the user (or the OS) did do: `onFloatMoved(window, finalContentRect)` while dragging or resizing (called repeatedly), `onFloatCloseRequested` (the close button; nothing is destroyed until the host calls `destroyWindow`, so the host can refuse when a tab cannot close, spec 03 rule 43), `onFloatActivated` (a press or focus raised the window), `onFloatMaximizedChanged`, `onFloatScaleChanged`.

**When the OS or the user destroys a window behind the host's back** the backend calls `onFloatLost(id)` exactly once; the id is invalid afterwards and the host closes the window's panels into the closed-panel memory (they reopen where they were). Never call `onFloatLost` for a window the host destroyed.

**Drop target queries.** `topmostWindowAt(screenPoint, exclude)` returns the window (the main window included) showing the point, honouring stacking, the frame the backend draws, hidden windows and the exclude list; `nullopt` when no window of the application is there. It must be answerable while the pointer is outside every window, since a tab drag leaves the originating window. The host uses it to decide whether the model's drop zone is really visible (a window's frame or a window above the zone means "no target": the drop floats).

**Pointer tracking.** While a tab drag runs the host calls `beginPointerTracking(sink)` when `describe().pointerTracking` is true; the backend then delivers every pointer movement and the release of the left button to the sink in screen coordinates, also outside every application window (Win32: pointer capture on the dragging window or a low-level hook), until `endPointerTracking`. A backend whose originating widget keeps the pointer capture (the in-window backend; a native backend that captures the pointer when the press happens) may report false and deliver nothing; the strip's own events then drive the drag.

**Hiding.** `setVisible(false)` hides a window without destroying it: not drawn, ignored by `topmostWindowAt`, widgets alive and a pointer capture held by one of them preserved (the drag that caused the hiding is still running in them, spec 02 rule 35).

**Stacking.** `stacking()` lists floating windows bottom to top; the host keeps the model's order and the backend's in step (`raiseArea` / `bringToFront`).

**Contents across contexts.** A panel's content widget keeps its state when it moves between regions or windows that share a `UiContext`; when it moves to a window with another context (native windows) the host recreates it from its factory.

**Lifetimes.** The backend and the registry must outlive the host; the host destroys the windows it created when it detaches. The in-window backend's destructor does not touch its context (it may run while the context is destroyed).

## 7. In-window backend

`InWindowFloatingBackend(ui, layerWidget, options)` draws each window as a frame widget added to `layerWidget` (normally the root): rounded 8 px rectangle with the `xl` shadow, a 34 px title bar with the title, maximize and close buttons, resize edges and corners (6 px, also over the content), move by the title bar, maximize by double click, a window is raised by any press in it. At least 48 px of the title bar stay inside the host window, a window never exceeds it. Frames sit at layer 100 + stacking index, below the overlay layer (menus, drag overlay).

## 8. Owner decisions applied

| Decision | Where |
|---|---|
| D9 Escape cancels a tab drag, the tab returns | `DockHost::cancelDragRequested`, test `escape_restores_everything` |
| D10 pinning, keyboard resize | `DockLayout::setPinned`, `DockAreaView::onKeyDown`; pinned width test |
| D11 20 px floor, collapse only by command | `moveSplitter`, `setCollapsed`; resize tests |
| D12 tabs 60 px minimum, arrows, all-tabs list, tooltip | `DockTabStrip`, tests `many_tabs_overflow_...`, visual overflow render |
| D13 safe loading, reset, unreachable windows (100 px margin), lock flags persisted | `fromJson`, `fitWindows`, `LayoutManager`, schema `panels[].locked` |
| D22 touch handles 9 px; rounded 8 px floating windows | `DockHostOptions::touch`; `InWindowFloatingOptions::radius` |

## 9. Not implemented, approximate, unverified

* Sidebars and drawers, hidden tab strips, the 1 s tab flash for an already-open request (spec 02 rules 50, 58-63, 8): not implemented.
* Regions holding only remembered panels are removed (the closed-panel memory restores the slot) instead of hiding with their weight remembered (rules 44, 47).
* Tab overlap of 2 px (rule 9) is drawn as adjacent tabs with a 1 px separator; the icon-only narrow tab (rule 11) is not implemented.
* Per-panel minimum sizes apply to floating windows only; docked regions use the 20 px floor.
* The sliding of other tabs to make a gap is a jump, not an animation; the ghost does not morph into the preview (rules 18, 19 timings); there is no animation on Escape.
* The floating window has its own title bar above the area's tab strip (two bars); the reference editor lets the strip double as the title bar, so dragging empty strip space does not move the window here (spec 02 rule 13) and there is no minimize button.
* Keyboard focus entering a panel does not make its region active (rule 4); a press in the region does.
* The in-window backend has never run in a real OS window here; the native OS-window backend (slice 5.2, section 10 and `docs/dev/native-windows.md`) is the one that does, and what could not be verified for it is listed there.

## 10. Native backend (slice 5.2)

`NativeFloatingBackend` (`source/ui-widgets/include/r1ui/widgets/dock/native/`, implementation in `source/ui-widgets/src/gpu/dock/`, library `ui-widgets-gpu`) implements the contract of section 6 with real OS windows. The full guide is `docs/dev/native-windows.md`; the points that matter to someone using or changing the dock:

* **Windows.** Each floating window is a borderless, owned tool window of the main window (above it, minimizes and restores with it, no taskbar button) with the window manager's rounded corners, its own `UiContext` and swapchain on the shared `Services` and `RenderDevice`, and a toolkit-drawn 34 px title bar (title, maximize, close). The platform's chrome hit test gives caption drag, edge resize, double-click maximize and snapping. `BackendInfo`: `nativeWindows = true`, `pointerTracking = true`, frame insets {1, 34, 1, 1}.
* **Content contexts.** The area view of a floating area lives in the window's own context, so a panel moved into or out of a native window is recreated by its factory (section 6, "Contents across contexts"): the dock integration test shows the factory running again when a tab is dropped on the desktop.
* **Coordinates.** Screen logical space is the virtual desktop divided by the scale of the monitor under the point; with monitors of different scale it is piecewise, and where two monitors' logical ranges overlap the monitor the window is on decides (`NativeCoords.h`). A window keeps its logical size across monitors. `monitorAt` returns the OS device name that `captureWindowStates` stores in the layout.
* **Listener rules as implemented.** The echo rule holds by comparison with stored state; `onFloatLost` is called exactly once for a window the OS destroyed and never for one the host destroyed; a close request destroys nothing; the host's `destroyWindow` hides the window at once and frees the object at the loop's next safe point (the dock destroys a window from inside that window's own pointer handler); a user maximize is reported as `onFloatMaximizedChanged` followed by `onFloatMoved` with the maximized content rectangle, a user restore the same way (what the in-window backend does).
* **Hidden window of a dragged sole tab.** `setVisible(false)` keeps the OS pointer capture of that window (a platform guarantee added for this), and a hidden window ignores focus changes (otherwise the context would cancel the drag the moment the OS moves the focus away). Tracking samples the OS pointer while the button is down, also outside every window.
* **Stacking and drops.** `topmostWindowAt` uses the backend's order (synchronised with the OS z-order and with user activation), the drawn frame, the hidden flag and whether the owner is minimized. A drop on a floating window's tab strip, on another window or on the main window's zones works across windows (tested with synthetic input in `native_dock_gpu_test` and with real input in `native_drive.ps1`).
* **Unreachable windows.** At creation and on a display change a window that shows less than 100 logical px on any monitor is moved back (platform `clampToVisible`, the same rule as the dock's `MonitorSet` fit for saved layouts).
* **Driving it.** The application runs `AppLoop` (pump, main events, floating events, collect parked windows, draw, wait for the nearest timer, live rendering during the OS move/size loop); the backend must be destroyed after the `DockHost` and before the main window.


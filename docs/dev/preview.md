# The preview application and its Editor screen (slice 5.12)

`examples/preview` is the launchable application of every phase (owner requirement 2026-10-09). From Phase 5 it opens on the **Editor** screen: a sample editor workspace built only from the toolkit's docking, native floating windows, command, property and customization modules. The earlier screens (Gallery, Widgets, Swatches, Screens, Sandbox) are still there.

## 1. Launch

```
tools\run\preview.cmd          builds the dev preset and starts the preview
r1gui-preview.exe              (build\dev\bin) the same without building
```

* **GPU.** The preview takes its device from `R1UI_GPU` (`tools\build\env.local.cmd` sets `RTX 4080`) or from the per-user preference file `%LOCALAPPDATA%\R1GUI\gpu.txt` (it contains `RTX 4080`); without either the renderer would pick the largest GPU, which is the RTX 3090 that must stay idle. `preview.cmd` is unchanged.
* **Data.** Everything the Editor remembers lives in `%LOCALAPPDATA%\R1GUI\preview\` (override: environment variable `R1GUI_PREVIEW_DATA`, used by the tests and the scripted drive so your own settings are never touched):

| File | Content | Written |
|---|---|---|
| `layouts\editor\_active.layout.json` | the arrangement on screen (schema version 2, `docs/dev/docking.md` section 3) | 5 s after the last change, when a layout is loaded or saved, on File > Save and on exit |
| `layouts\editor\<name>.layout.json` | the named layouts (Default, Modeling, Review, yours) | Layout > Save layout, Save layout as, Rename |
| `layouts\active-layout.txt` | the key of the named layout the arrangement came from (the layout manager restores the arrangement, not its name) | whenever the active layout changes |
| `keybindings.json` | your key bindings: only what differs from the defaults (`r1ui-keybindings`, version 1) | on the next loop step after a binding changes |
| `customization.json` | your menu and toolbar changes (`r1ui-customization`, version 1) | when you leave Customize mode, and on File > Save |
| `keybindings-export.json`, `keybindings-import.json` | the shortcut editor's export, and the file its Import reads (the preview has no file dialog) | on demand |

Every write goes through a temporary file and an atomic replace. A file that cannot be used is **never deleted or overwritten**: the layout manager keeps a damaged `_active` layout as `_corrupt-N` (and the default arrangement is used), the customization store moves a damaged file to `customization.json.corrupt-N`, and a damaged `keybindings.json` is ignored (the defaults apply) with a warning in the console panel.

* **Command line.** `--shot <dir>` offscreen renders (Editor, customize mode, shortcut editor, a floating panel, Widgets, every gallery page; both themes), `--bench <dir>` the Phase 4 baseline, `--bench-editor <dir>` the Phase 5 baseline (`docs/perf/phase5_baseline.md`).

## 2. Screens and keys

`Ctrl+Tab` (Shift reverses) cycles the six screens at any time; **Window > Screen** lists them. On the Editor screen plain keys belong to the commands (Tab is focus navigation), on the other screens Tab cycles, `T` toggles dark and light, `Esc` leaves a focused widget and then quits. The Gallery has four new pages: Docking, Commands, Properties, Customize.

## 3. What is on the Editor screen

| Panel | Is | Shows |
|---|---|---|
| Viewport | `ViewportCanvas` (preview code) | grid and the scene as flat shapes; click selects (Ctrl toggles); with the Move tool a drag moves the selection, one undo step per drag |
| Outliner | `TreeView` over the sample scene | meshes and lights, multi-select shared with the viewport and the inspector |
| Inspector | the generated `PropertyPanel` | the selected objects' properties: mixed values for several objects, reset arrows, search, advanced toggle, undo |
| Assets | `ThumbnailGrid` | 160 sample items |
| Curves | `CurveEditor` | a sample curve |
| Console | a log list | every command, layout and file event; warnings and errors in colour |
| Commands | `CommandPalette` | every command by category; double-click runs it, in Customize mode drag a row onto the menu bar or the toolbar |
| Quick Actions | `CustomizeToolStrip` + `FreeFormPanel` | Customize, New menu, Restore, Reset, Revert, and a free-placement panel of command buttons |
| Shortcuts | `KeybindingEditor` | search, two chord boxes per command, conflicts, import, export (closed until opened with Edit > Keyboard shortcuts) |

Above the dock: the customizable **menu bar** (File, Edit, View, Tools, Window, Layout, Help), a **layout drop-down** (right) and the customizable **toolbar**; below it a status line (last action, the key sequence in progress, active layout, the undo label).

### Commands and default shortcuts

| Menu | Command (shortcut) |
|---|---|
| File | New scene (Ctrl+N), Save (Ctrl+S: layout, shortcuts, customization), Exit (Ctrl+Q) |
| Edit | Undo (Ctrl+Z), Redo (Ctrl+Y, Ctrl+Shift+Z), Select all (Ctrl+A), Deselect (Ctrl+Shift+A), Reset values (Ctrl+Shift+R), Keyboard shortcuts... (Ctrl+K, Ctrl+S: a two-chord sequence), Customize... (Ctrl+Shift+C) |
| View | Dark theme (Ctrl+Shift+T), Show grid (Ctrl+G), Show lights (Ctrl+L) |
| Tools | Select (V), Move (W), Rotate (E), Scale (R) |
| Window | Panels > the nine panels (toggles), Float tab (Ctrl+Alt+F), Move tab group to new window (Ctrl+Alt+M), Next tab (Ctrl+PageDown), Previous tab (Ctrl+PageUp), Close tab (Ctrl+W), Screen > the six preview screens |
| Layout | Default, Modeling, Review (Ctrl+Alt+1, 2, 3), Switch layout... (Ctrl+Alt+L: opens the drop-down), Save layout, Save layout as... (Ctrl+Shift+S), Rename layout..., Delete layout..., Reset layout... |
| Help | Keyboard shortcuts..., About the preview (F1) |

Plain letter keys do not fire while a text field has the focus; chords with Ctrl or Alt do. Shortcuts typed in a native floating window run the same commands (a forwarding handler per window context).

## 4. Owner interaction checklist

Run `tools\run\preview.cmd`. The Editor opens in the Default layout.

1. **Tear a tab out.** Press a tab (for example Outliner), drag it out of the window and release over empty desktop: it becomes a real window (rounded corners, own title bar, no taskbar button) that stays above the preview. Escape during the drag cancels. Drag it by its title bar onto the **second monitor**; resize it by its edges; double-click its title bar to maximize; close it with the X (the panel goes to the closed-panel memory: Window > Panels brings it back where it was).
2. **Dock it back.** Drag the tab of the floating window onto a tab strip of the main window (it joins that region), onto the side of a region (it splits) or onto an edge. Drag a tab between two floating windows. Drag a splitter; right-click a tab (close others, float, lock, pin, collapse).
3. **Resize the preview window** by its edges or corners (the title bar drags it): the regions follow in proportion. Minimize it: the floating windows go with it.
4. **Layouts.** Pick **Modeling** or **Review** in the layout drop-down (or Layout menu): the arrangement changes at once. Drag a tab somewhere, then Layout > **Save layout as...**, type a name, Enter. Layout > **Rename layout...** renames it; **Delete layout...** asks first; **Reset layout...** returns to the built-in default. Close the preview and start it again: the arrangement, the floating windows and the active layout name are back (auto-save runs 5 s after a change, and on exit).
5. **Customize a menu.** Edit > **Customize...** (Ctrl+Shift+C): the Commands and Quick Actions panels open and the menu bar and toolbar show their edit displays. Click **New menu** in Quick Actions and type a name; drag a command from the Commands panel onto the new menu title or onto the toolbar; use the eye icons to hide entries and double-click a label to rename it. Leave the mode with the same command: the changes are saved. Restart: they are still there. "Revert customization changes" returns to the state at the start of the session.
6. **Rebind a key.** Ctrl+K, Ctrl+S (or Edit > Keyboard shortcuts...) opens the shortcut editor: search "Move", click its first chord box, press a key combination. A combination that another command uses opens a conflict popup (Override or Cancel). Escape cancels a capture. The new key works at once and survives a restart.
7. **Undo in the inspector.** Select the Cube in the outliner, type a new value into Position X in the inspector (or scrub the field), then press Ctrl+Z / Ctrl+Y; with two objects selected (Ctrl-click in the outliner) the differing values read "Mixed". Press W, drag the cube in the viewport: one drag is one undo step.
8. **Dark and light.** View > Dark theme.

## 5. How it is wired (for developers)

| Piece | Where | Note |
|---|---|---|
| `PreviewApp` | `examples/preview/PreviewApp.*` | owns the window, device, services, the `NativeFloatingBackend` and the `AppLoop`; the Editor context is built the first time the Editor is shown |
| `AppLoop` hooks | `PreviewApp.cpp` | `processMain` = `processEvents`, `mainNeedsFrame` = `needsFrame`, `renderMain` = `frame`, `mainMsUntilTick` = the active context's timer and the layout auto-save, `quitRequested` = `quit_`; the loop also runs the OS move/size live callbacks, so every window keeps drawing while one is dragged |
| `EditorApp` | `examples/preview/editor/` | the workspace: `EditorCommands.cpp` (commands), `EditorMenus.cpp` (built-in menus and toolbar), `EditorPanels.cpp` (panels), `EditorLayouts.cpp` (named layouts, dialogs), `EditorViewport.cpp`, `EditorModel.cpp` (sample scene, properties, undo, log) |
| `DriveStatus` | `examples/preview/DriveStatus.cpp` | with `R1GUI_PREVIEW_STATUS=<file>` the app writes window, tab and widget coordinates there; the scripted drive reads them |

* **Destroy order** (`docs/dev/native-windows.md` section 5): the loop, then `EditorApp` (it writes its files, then destroys the dock, which closes the native windows and detaches every panel), then its context, then (member order) the backend, services, device and window. `PreviewApp::~PreviewApp` does this explicitly.
* **A panel recreated in another window.** The dock rebuilds a panel's content from its factory when it moves into another window's context, so every panel is a thin widget over state that lives in `EditorApp` (model, registry, controller). A native window has no global key handler of its own: the panel factory installs a forwarder that sends keys to the same command router (`FloatKeys` in `EditorApp.cpp`).
* **Dialogs open from a timer**, not from the command that asked for them: a command run from a menu is followed by the menu closing, which gives the focus back to the widget that had it before and would take it from the dialog (found by the scripted drive: typed text went to the viewport).
* **Context teardown.** A native window's context is destroyed with the window while the application's models live on. `UiContext`'s destructor now detaches every widget (`onDetached`, children first, the latest created first) before freeing them; before, a widget that unsubscribed from a model or registry in `onDetached` (property panel, command palette, shortcut editor...) left a dangling callback behind when its window closed and the next property change crashed (the Editor's drive found it). Test: `tests/ui-widgets/runtime/context_teardown_test.cpp`. Consequence for hosts: everything a widget talks to in `onDetached` (the dock's backend and registry, a command registry) must outlive the context, or the widgets must be destroyed first; the test fixtures `DockRig` and `CommandFixture` now do that in their destructors (the Debug build's checked iterators caught the old order).
* **Name clash fixed on the way.** The command layer's router clock and the property layer's undo clock were both `r1ui::widgets::UiClock`; the property one is now `PropsUiClock` (`tests/ui-widgets/props/clock_names_test.cpp`).

## 6. Tests and the drive

| Test | Label | What it proves |
|---|---|---|
| `preview.editor` (`tests/preview/editor_test.cpp`) | fast | every panel builds (also in a second context); every menu and toolbar command exists; no default chord lost to a conflict; first-run layouts; undo/redo by command and by key; a viewport drag is one undo step; the layout, its name, a rebound key and a customized menu survive a restart; a damaged layout is kept aside; a damaged keybindings file is ignored; the layout dialogs work from the keyboard; an idle Editor needs no frame and schedules no timer |
| `preview.modes` | fast | the gallery pages (now nine) build and switch |
| `tests/preview/editor_drive.ps1` | manual | the real-desktop drive of `r1gui-preview.exe` (below) |

`powershell -File tests\preview\editor_drive.ps1 -Exe <r1gui-preview.exe> -OutDir <dir>` starts the preview on the RTX 4080 with a throwaway data folder and, with real mouse and keyboard input (`SetCursorPos`, `mouse_event`, `keybd_event`; before every press it checks that the point belongs to a preview window and before every key that the foreground window does), tears a tab out into a native window, moves it to the second monitor, docks it back, resizes the main window, saves and renames a layout through the Layout menu, types shortcuts, drags in the viewport and undoes, edits an inspector field, adds a user menu in Customize mode, rebinds a key in the shortcut editor, measures idle CPU, closes the app, restarts it and checks that the layout, the floating window, the menu and the key are back, then corrupts the layout file and checks the fallback. It writes `drive.log` and PNG captures (`PrintWindow`) and kills the preview in a `finally` block.

## 7. Not wired, approximate, unverified

* **Floating palette and drag and drop.** The customization drag ghost lives in the controller's (main window) context: with the Commands panel in a native window, dragging a row onto the main menu bar does not work; use the picker (Insert in the edit display) or keep the panel docked. The Quick Actions panel works in either place.
* **Screens other than the Editor** keep their own keys and do not use the command layer; the native floating windows stay open when another screen is shown (they belong to the Editor workspace).
* **Sample content.** The viewport is a flat 2D placeholder (no 3D); the assets have no thumbnails; the curve editor and assets are not connected to the scene; the "New scene" command only resets the selection and the undo history.
* **The main window's own position and size** are not restored (it opens 1440x900 logical; the dock's regions follow its size); floating windows are, with their rectangles and monitors.
* **Keybinding import/export** use fixed file names in the data folder (no file dialog exists yet).
* **Auto-save** writes the arrangement on screen (`_active`); a named layout changes only when you save it, so the layout name is shown without a modified marker.
* A window crossing monitors of different scale, snap layouts, the DWM shadow and `WM_DISPLAYCHANGE` with a real monitor change are not verified on this machine (both monitors are 100 %); see `docs/dev/native-windows.md` section 10.

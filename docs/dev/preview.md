# The preview application and its Editor screen (slices 5.12 and 5.19)

`examples/preview` is the launchable application of every phase (owner requirement 2026-10-09). From Phase 5 it opens on the **Editor** screen: a sample editor workspace built only from the toolkit's docking, native floating windows, command, property and menu modules. The earlier screens (Gallery, Widgets, Swatches, Screens, Sandbox) are still there. Slice 5.19 (2026-10-10) replaced the Customize mode with the Create Custom Menu window and added custom menus, the pie menu, custom workspaces and the hotkey editor.

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
| `customization.json` | menu and toolbar customization (`r1ui-customization`, version 1); the Editor has no UI for it any more, a file from an earlier version is still read | on File > Save and at exit |
| `custom-menus.json` | your custom menus, pie and dockable (`r1ui-custom-menus`, version 1) | after every change of a menu |
| `viewport-pie.txt` | the id of the pie the right mouse button opens in the viewport | when a pie is created, edited or loaded |
| `menus\*.r1mn` | single menus saved from the Custom Menus menu or the creator (default folder of the file dialogs) | on demand |
| `workspaces\*.r1ws` | custom workspaces: layout, custom menus, customization, key bindings (default folder of the workspace dialogs, which list the files) | on demand |
| `hotkey-sets\*.json` | the named hotkey sets of the hotkey editor | when a set is saved |
| `keybindings\*.json` | import and export files of the hotkey editor (default folder of its dialogs) | on demand |

Every write goes through a temporary file and an atomic replace. A file that cannot be used is **never deleted or overwritten**: the layout manager keeps a damaged `_active` layout as `_corrupt-N` (and the default arrangement is used), the customization store moves a damaged file to `customization.json.corrupt-N`, and a damaged `keybindings.json` is ignored (the defaults apply) with a warning in the console panel.

* **Command line.** `--shot <dir>` offscreen renders (Editor, hotkey editor, the menu creator, a floating panel, Widgets, every gallery page; both themes), `--bench <dir>` the Phase 4 baseline, `--bench-editor <dir>` the Phase 5 baseline (`docs/perf/phase5_baseline.md`).

## 2. Screens and keys

`Ctrl+Tab` (Shift reverses) cycles the six screens at any time; **Window > Screen** lists them. On the Editor screen plain keys belong to the commands (Tab is focus navigation), on the other screens Tab cycles, `T` toggles dark and light, `Esc` leaves a focused widget and then quits. The Gallery has 13 pages; the newest are Docking, Commands, Properties, Customize, Actions, Hotkeys, CustomMenus and Creator.

## 3. What is on the Editor screen

| Panel | Is | Shows |
|---|---|---|
| Viewport | `ViewportCanvas` inside a `PieTrigger` | grid and the scene as flat shapes; click selects (Ctrl toggles); with the Move tool a drag moves the selection, one undo step per drag; **hold the right mouse button and flick toward a slot to run a pie menu action** (Escape cancels); View > Frame selection (F) centres the view, Wireframe (Z) draws outlines |
| Outliner | `TreeView` over the sample scene | meshes and lights, multi-select shared with the viewport and the inspector |
| Inspector | the generated `PropertyPanel` | the selected objects' properties: mixed values for several objects, reset arrows, search, advanced toggle, undo |
| Assets | `ThumbnailGrid` | 160 sample items |
| Curves | `CurveEditor` | a sample curve |
| Console | a log list | every command, layout and file event; warnings and errors in colour |
| Actions | `ActionList` | every command by category with its description and shortcut, searchable; double-click or Enter runs one |
| Hotkey Editor | `HotkeyEditor` | the Maya-style hotkey editor: action list, drawn keyboard, hotkey sets, import and export (closed until opened with Edit > Hotkey editor...; it opens as a window of its own) |
| Create Custom Menu | `CreateCustomMenuWindow` | a native floating window opened by Custom Menus > Create Custom Menu... or an Edit entry |
| one panel per dockable menu | `CustomMenuPanel` | the buttons of a menu you made (the sample **Quick Tools** on the first start); it opens next to the viewport and is listed under Custom Menus |

Above the dock: the customizable **menu bar** (File, Edit, View, Tools, Window, Layout, Help, **Custom Menus**), a **layout drop-down** (right) and the customizable **toolbar**; below it a status line (last action, the key sequence in progress, active layout, the undo label).

The Quick Actions panel and Edit > Customize (the edit mode with its tool strip) were removed in slice 5.19 because they customized areas nobody needed; the customization library is untouched and the menu bar and toolbar are still built from it. The command palette panel became the **Actions** panel (the new list shows the same commands with their descriptions); a stored layout that names the old panel 8 drops it.

### Commands and default shortcuts

| Menu | Command (shortcut) |
|---|---|
| File | New scene (Ctrl+N), Save (Ctrl+S: layout, shortcuts, customization, menus), Exit (Ctrl+Q) |
| Edit | Undo (Ctrl+Z), Redo (Ctrl+Y, Ctrl+Shift+Z), Select all (Ctrl+A), Deselect (Ctrl+Shift+A), Reset values (Ctrl+Shift+R), Hotkey editor... (Ctrl+K, Ctrl+S: a two-chord sequence) |
| View | Dark theme (Ctrl+Shift+T), Show grid (Ctrl+G), Show lights (Ctrl+L), Frame selection (F), Wireframe (Z) |
| Tools | Select (V), Move (W), Rotate (E), Scale (R) |
| Window | Panels > the eight panels (toggles), Float tab (Ctrl+Alt+F), Move tab group to new window (Ctrl+Alt+M), Next tab (Ctrl+PageDown), Previous tab (Ctrl+PageUp), Close tab (Ctrl+W), Screen > the six preview screens |
| Layout | Default, Modeling, Review (Ctrl+Alt+1, 2, 3), Switch layout... (Ctrl+Alt+L: opens the drop-down), Save layout, Save layout as... (Ctrl+Shift+S), Rename layout..., Delete layout..., Reset layout..., **Save custom workspace...**, **Load custom workspace...** |
| Custom Menus | the dockable menus (one entry each: opens or focuses the panel), Pie menus (a submenu per pie: Edit..., Save As..., Delete), Edit Dockable Menu (a submenu per dockable menu: Edit..., Save As..., Delete), **Create Custom Menu...**, **Load Custom Menu...** |
| Help | Hotkey editor..., About the preview (F1) |

Plain letter keys do not fire while a text field has the focus; chords with Ctrl or Alt do. Shortcuts typed in a native floating window run the same commands (a forwarding handler per window context). Every command carries a description (a test asserts it).

## 4. Owner interaction checklist

Run `tools\run\preview.cmd`. The Editor opens in the Default layout with the sample **Tools Pie** and the **Quick Tools** panel next to the viewport (first start only).

1. **Tear a tab out.** Press a tab (for example Outliner), drag it out of the window and release over empty desktop: it becomes a real window (rounded corners, own title bar, no taskbar button) that stays above the preview. Escape during the drag cancels. Drag it by its title bar onto the **second monitor**; resize it by its edges; double-click its title bar to maximize; close it with the X (the panel goes to the closed-panel memory: Window > Panels brings it back where it was).
2. **Dock it back.** Drag the tab of the floating window onto a tab strip of the main window (it joins that region), onto the side of a region (it splits) or onto an edge. Drag a tab between two floating windows. Drag a splitter; right-click a tab (close others, float, lock, pin, collapse).
3. **Resize the preview window** by its edges or corners (the title bar drags it): the regions follow in proportion. Minimize it: the floating windows go with it.
4. **Layouts.** Pick **Modeling** or **Review** in the layout drop-down (or Layout menu): the arrangement changes at once. Drag a tab somewhere, then Layout > **Save layout as...**, type a name, Enter. **Rename layout...**, **Delete layout...** (asks first), **Reset layout...**. Close the preview and start it again: the arrangement, the floating windows and the active layout name are back.
5. **Use the sample pie.** Hold the **right mouse button** in the viewport: after a moment a ring of eight actions appears around the pointer; move toward one (a quick flick is enough, it need not reach the slot) and release: it runs (Move is up, Rotate up-right, Scale right, Frame selection down-right, Select down, Wireframe down-left, Undo left, Redo up-left). A quick right click does nothing; Escape while holding cancels.
6. **Create a pie menu.** Custom Menus > **Create Custom Menu...**: a new window opens. Choose **Pie menu**. Type "move" in the search box on the right and drag the Move row onto a slot of the pie on the left; do the same for two more actions (every row shows what the action does). Drag a filled slot onto another to swap them, right-click a slot to clear it, try the 4 / 6 / 8 slot selector, select a slot and type a label of its own. Type a name, press **Create**. The window closes; hold the right mouse button in the viewport: your pie opens (the pie you created, edited or loaded last is the viewport's pie).
7. **Create a dockable menu.** Custom Menus > Create Custom Menu... > **Dockable panel**: drag Move and Rotate (double-click a row adds it too) into the panel preview, set columns, button size and Show labels, name it, Create. The panel opens in the dock and a click on its buttons runs the tools. Close its tab, then open it again from the Custom Menus menu.
8. **Edit and delete.** Custom Menus > (Pie menus or Edit Dockable Menu) > your menu > **Edit...** opens the same window filled in; **Cancel** throws every change away, **Save** applies them. **Delete** asks first.
9. **Save a menu as a file.** Custom Menus > your menu > **Save As...**: the file dialog opens in `%LOCALAPPDATA%\R1GUI\preview\menus` (type a name, Enter; a file that exists asks for a second press). Delete the menu, then Custom Menus > **Load Custom Menu...**, pick the file from the list: it is back (a taken name gets a number).
10. **Hotkey editor.** Edit > **Hotkey editor...** (Ctrl+K, Ctrl+S): a window with the action list on the left and a keyboard on the right. Click an action (for example Move), click the **M** key on the keyboard: M now runs Move (a key another command uses opens Replace / Keep both / Cancel). Hold Shift or Ctrl on the keyboard view to see the other layers. **Save as...** stores a hotkey set, Export and Import use the file dialog.
11. **Custom workspace.** Layout > **Save custom workspace...**: type a name. Change the layout, delete a menu, rebind a key; then Layout > **Load custom workspace...** and pick the file from the list: the layout, the menus and the keys are back. Restart the preview: your menus, their panels and the viewport pie are still there.
12. **Undo in the inspector.** Select the Cube in the outliner, type a new value into Position X in the inspector, then Ctrl+Z / Ctrl+Y; with two objects selected the differing values read "Mixed". Press W, drag the cube in the viewport: one drag is one undo step.
13. **Dark and light.** View > Dark theme.

## 5. How it is wired (for developers)

| Piece | Where | Note |
|---|---|---|
| `PreviewApp` | `examples/preview/PreviewApp.*` | owns the window, device, services, the `NativeFloatingBackend` and the `AppLoop`; the Editor context is built the first time the Editor is shown |
| `AppLoop` hooks | `PreviewApp.cpp` | `processMain` = `processEvents`, `mainNeedsFrame` = `needsFrame`, `renderMain` = `frame`, `mainMsUntilTick` = the active context's timer and the layout auto-save (it also rewrites the drive status), `quitRequested` = `quit_` |
| `EditorApp` | `examples/preview/editor/` | the workspace: `EditorCommands.cpp` (commands), `EditorMenus.cpp` (built-in menus and toolbar, the Custom Menus node), `EditorPanels.cpp` (panels, hotkey sets, keybinding files), `EditorLayouts.cpp` (named layouts, dialogs), `EditorCustomMenus.cpp` (custom menus: storage, panels, creator window, pie, files), `EditorWorkspaces.cpp` (.r1ws), `EditorViewport.cpp`, `EditorModel.cpp` |
| `DriveStatus` | `examples/preview/DriveStatus.cpp` | with `R1GUI_PREVIEW_STATUS=<file>` the app writes window, tab and widget coordinates (also of widgets inside native windows: the creator, the hotkey editor, dockable menu panels) and the custom menus there; the scripted drives read them |

* **Destroy order** (`docs/dev/native-windows.md` section 5): the loop, then `EditorApp` (it writes its files, then destroys the dock, which closes the native windows and detaches every panel), then its context, then (member order) the backend, services, device and window. `PreviewApp::~PreviewApp` does this explicitly.
* **A panel recreated in another window.** The dock rebuilds a panel's content from its factory when it moves into another window's context, so every panel is a thin widget over state that lives in `EditorApp` (model, registry, controller, the custom menu set, the creator session). The creator window keeps the draft in `CreatorSession` so moving the window never loses the user's work. A native window has no global key handler of its own: the panel factory installs a forwarder that sends keys to the same command router.
* **Custom menus** (`docs/dev/custom-menus.md`): the set is loaded from `custom-menus.json` (atomic saves after every change, a damaged file is kept aside), the commands behind the Custom Menus menu are registered by `CustomMenuCommands`, a dockable menu with serial n is dock panel 1000 + n (registered when the menu appears; the registry has no remove, so a deleted menu's panel is closed and stays unused), the menu bar is rebuilt from a timer with `Customization::setBuiltin`. The dock and the menu bar are updated from a timer and never inside the set's change notification (the notification runs on a copy of the listener list, so closing a panel there would still call the destroyed panel's listener).
* **Dialogs open from a timer** of the context they belong to, not from the command that asked for them: a command run from a menu is followed by the menu closing, which gives the focus back to the widget that had it before and would take it from the dialog. File choices (menus, workspaces, hotkeys) use `FilePathDialog` (`source/ui-widgets/.../filepath`), never an OS dialog.
* **Workspaces** (`EditorWorkspaces.cpp`): load checks every part first (menus parsed, customization and key bindings on scratch copies), applies the menus before the layout (its panels must exist), and puts the menus back when the layout is refused.
* **Context teardown.** A native window's context is destroyed with the window while the application's models live on. `UiContext`'s destructor detaches every widget (`onDetached`, children first) before freeing them. Consequence for hosts: everything a widget talks to in `onDetached` must outlive the context.

## 6. Tests and the drive

| Test | Label | What it proves |
|---|---|---|
| `preview.editor` (`tests/preview/editor_test.cpp`) | fast | every panel builds (also in a second context); every menu and toolbar command exists; no default chord lost to a conflict; every command has a description; first-run layouts and sample menus; undo/redo; a viewport drag is one undo step; the layout, a rebound key and the menus survive a restart; damaged layout, keybindings and menus files are kept aside or ignored; the layout dialogs work from the keyboard; the creator window creates a pie and a dockable menu and the Custom Menus menu lists them; a panel can be closed and opened again; the right-mouse pie runs a command and Escape cancels it; a menu saved as .r1mn, deleted and loaded; workspaces save, load and refuse bad files without changing anything; the hotkey editor panel assigns a key and saves a set; an idle Editor needs no frame and schedules no timer |
| `preview.modes` | fast | the gallery pages (now thirteen) build and switch |
| `tests/preview/editor_drive.ps1` | manual | the real-desktop drive of window and layout behaviour (native windows, layouts, undo, restart) |
| `tests/preview/menus_drive.ps1` | manual | the real-desktop drive of the custom menu features: pie in the viewport with the real right mouse button, the creator window (type chooser, drags from the list), the dockable menu, close and reopen, .r1mn save, delete and load, the hotkey editor with a key click, workspaces, idle CPU, restart |

`powershell -File tests\preview\menus_drive.ps1 -Exe <r1gui-preview.exe> -OutDir <dir>` starts the preview on the RTX 4080 with a throwaway data folder and drives it with real mouse and keyboard input (`SetCursorPos`, `mouse_event`, `keybd_event`; before every press it checks that the point belongs to a preview window and before every key that the foreground window does). It writes `drive.log` and PNG captures (`PrintWindow`) and kills the preview in a `finally` block.

## 7. Not wired, approximate, unverified

* **Pie trigger area.** Only the viewport opens the pie; other panels have no trigger. With several pies the viewport uses the one created, edited or loaded last (there is no "use in viewport" entry yet).
* **No drag from the Actions panel into the creator** (they are different windows): use the creator's own list. A drag from a floated list onto the main window does not exist either.
* **Screens other than the Editor** keep their own keys and do not use the command layer; the native floating windows stay open when another screen is shown (they belong to the Editor workspace).
* **Sample content.** The viewport is a flat 2D placeholder (no 3D); Frame selection only centres the view, there is no zoom; the assets have no thumbnails; the curve editor and assets are not connected to the scene; the "New scene" command only resets the selection and the undo history.
* **The main window's own position and size** are not restored (it opens 1440x900 logical; the dock's regions follow its size); floating windows are, with their rectangles and monitors.
* **The creator window is a dock panel that opens floating.** Closing it with the X discards the draft (the next Create or Edit starts fresh); it can be docked like any panel but is meant to float.
* **The customization of menus and toolbars has no user interface any more** (the Customize mode was removed by the owner); `customization.json` is still read and written, so an earlier user menu is kept in the menu bar.
* A window crossing monitors of different scale, snap layouts, the DWM shadow and `WM_DISPLAYCHANGE` with a real monitor change are not verified on this machine (both monitors are 100 %); see `docs/dev/native-windows.md` section 10.

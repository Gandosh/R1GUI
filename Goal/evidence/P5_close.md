# Phase 5 close record

**Status:** TECHNICAL PASS; owner gate PENDING   **Date:** 2026-10-10

## Slices
| Slice | Status | Evidence |
|---|---|---|
| 5.1 Dock tree, tabs, splits, drop targets | TECHNICAL PASS | [P5_S01](P5_S01.md) |
| 5.2 Native floating windows, rounded corners | TECHNICAL PASS (real DPI change unverified) | [P5_S02](P5_S02.md) |
| 5.3 Layout serialization | TECHNICAL PASS | [P5_S03](P5_S03.md) |
| 5.4 Layout save, load, switch | TECHNICAL PASS | [P5_S04](P5_S04.md) |
| 5.5 Command registry | TECHNICAL PASS | [P5_S05](P5_S05.md) |
| 5.6 Shortcut editor | TECHNICAL PASS | [P5_S06](P5_S06.md) |
| 5.7 Customizable menus | TECHNICAL PASS | [P5_S07](P5_S07.md) |
| 5.8 Customizable toolbars | TECHNICAL PASS | [P5_S08](P5_S08.md) |
| 5.9 Customization persistence | TECHNICAL PASS | [P5_S09](P5_S09.md) |
| 5.10 Generated property panel | TECHNICAL PASS | [P5_S10](P5_S10.md) |
| 5.11 Undo grouping, mixed values | TECHNICAL PASS | [P5_S11](P5_S11.md) |
| 5.12 Preview: sample editor | TECHNICAL PASS (owner interaction pending) | [P5_S12](P5_S12.md) |
| 5.13 Action list | TECHNICAL PASS (owner interaction pending) | [P5_S13](P5_S13.md) |
| 5.14 Hotkey editor with keyboard view | TECHNICAL PASS (owner interaction pending) | [P5_S14](P5_S14.md) |
| 5.15 Custom menu model | TECHNICAL PASS (owner interaction pending) | [P5_S15](P5_S15.md) |
| 5.16 Pie menu | TECHNICAL PASS (owner interaction pending) | [P5_S16](P5_S16.md) |
| 5.17 Create Custom Menu window, Custom Menus menu | TECHNICAL PASS (owner interaction pending) | [P5_S17](P5_S17.md) |
| 5.18 .r1mn and custom workspace files | TECHNICAL PASS (owner interaction pending) | [P5_S18](P5_S18.md) |
| 5.19 Preview: creator, hotkey editor, pie, workspaces | TECHNICAL PASS (owner interaction pending) | [P5_S19](P5_S19.md) |
| 5.20 Brush library model (headless) | TECHNICAL PASS (owner interaction pending) | [P5_S20](P5_S20.md) |
| 5.21 Brush library popup, controller, gallery | TECHNICAL PASS (owner interaction pending) | [P5_S21](P5_S21.md) |
| 5.22 Brush pictures, chips, search, favourites, recents, letters, settings, docs | TECHNICAL PASS (owner interaction pending) | [P5_S22](P5_S22.md) |
| 5.23 Preview: the brush library in the Editor | TECHNICAL PASS (owner interaction pending) | [P5_S23](P5_S23.md) |

## Launch the preview
`tools/run/preview.cmd`. It opens on the Editor screen. Full checklist in `docs/dev/preview.md`. Quick tour:
- Drag a tab (for example Outliner) out of the window onto the desktop: a rounded native window appears. Drag it to your second monitor by its title bar, resize it, drop its tab back onto a panel tab strip to dock it.
- Layout menu: switch Default, Modeling, Review; Save as; rename; close the app and start it again, the arrangement comes back.
- Hold the right mouse button in the viewport and flick toward a slot of the sample Tools Pie (Escape cancels).
- Custom Menus > Create Custom Menu...: choose Pie menu or Dockable panel, drag actions from the list on the right onto the slots or into the panel, name it, Create. The dockable menu appears as a panel and under Custom Menus (close it, open it again). Custom Menus > (menu) > Edit, Save As (.r1mn), Delete; Custom Menus > Load Custom Menu.
- Edit > Hotkey editor (Ctrl+K, Ctrl+S): click an action, click a key on the keyboard to assign it.
- Layout > Save custom workspace... and Load custom workspace... (layout, menus, key bindings in one .r1ws file).
- Inspector: type a value, drag in the viewport, Ctrl+Z and Ctrl+Y. Exit with File > Exit (Ctrl+Q).
- Brush library (slices 5.20 to 5.23): press **B** over the viewport; type **S**, then **N**: Snake Hook is picked (status line and viewport follow). Try arrows and Enter, a click, the star, Backspace, Esc, B again, **Tab** (search), right click a tile > Assign letter, B inside a number field, B in a floating window. Full list in `docs/dev/preview.md` item 14.
Ctrl+Tab cycles to the earlier screens (Gallery has 14 pages, new ones: Docking, Commands, Properties, Customize, Actions, Hotkeys, CustomMenus, Creator, Brushes). User data lives in `%LOCALAPPDATA%\R1GUI\preview\`.

## Verification summary
- Merged main, fresh MSVC build with warnings as errors; fast and GPU tiers pass on the RTX 4080 (fast 144 of 144, GPU 72 of 72, desktop 1 of 1). The builders' Debug trees passed with the validation layer clean.
- Real-desktop drives (native windows 31 of 31 checks; editor drive 0 failures on release and Debug) with real mouse and keyboard input.
- Performance (docs/perf/phase5_baseline.md): idle 0 frames, 0.00 percent CPU with a native window open; redraw frame 0.78 ms median.
- Two library defects found by the drive and fixed with tests: widgets in a closed window were not detached on UiContext destruction (dangling subscription crash), and two classes named UiClock in different modules.

## Slices 5.13 to 5.19 (2026-10-10, same day)
Action list, hotkey editor with keyboard, custom menus (pie and dockable), pie trigger, the Create Custom Menu window, .r1mn and .r1ws files, the file path dialog and their preview integration. The Customize mode of the Editor was removed on the owner's decision. Verification of the final tree: build exit 0 (dev and Debug, warnings as errors); fast 167 of 167, GPU 78 of 78, desktop 1 of 1 in both trees; real-desktop drives `menus_drive.ps1` and `editor_drive.ps1` 0 failures.

## Slices 5.20 to 5.23 (brush library, 2026-10-10)
Owner requirement: "pressing B opens the brush library anywhere in the scene, so people can access their brush quickly by typing its letter." Headless model and letter algorithm (`ui-commands/brushes`), the popup, controller and gallery (`ui-widgets/brushes`), pictures through the asset browser's machinery, persistence (`brushes.json`), the preview wiring (40 sample brushes with procedural pictures, status line, viewport label and cursor ring, hotkey editor entry) and `docs/dev/brush-library.md`. Verification is in the four evidence records; the owner's checklist is `docs/dev/preview.md` item 14.

## Accepted risks and open items
- **Brush library behaviour decisions** (docs/dev/brush-library.md section 4): B closes the library only before anything is typed (Shift+B, the search mode or a letter override reach brushes that start with B); type-ahead widgets (the preview's Assets grid and Actions list) keep plain letters; folding of letters is an approximation (Latin, Greek, Cyrillic); the first opening of a session is slower (about 21 ms offscreen) than later ones (under 1 ms). No independent review of the new overlay and key flow yet.
- Not verified on this machine: a window crossing monitors of different display scale (both monitors are 100 percent), snap layouts, DWM shadow, a real display-change event.
- Per-window full screen not implemented; floating windows have no minimize button; popups clip to their window.
- Dragging from a floated palette onto the main menu bar does not work; the main window position is not restored; floating windows stay open when another screen is shown; auto-save does not mark named layouts as modified.
- Host contract introduced: whatever a widget talks to in onDetached must outlive its UiContext.
- No independent code review of Phase 5 code (recommended before Phase 6; multi-window lifetime and serialization are the risky parts).
- The interaction specs remain local only.
- Disk: J: filled up during parallel builds; build trees for builders moved to C:. Old worktrees were removed.
- **Native window hang (open):** one of five real-desktop runs of the new drive hung the preview after a floating window closed. A separate builder owns the fix (swapchain creation `vkDeviceWaitIdle`, `destroyWindow` hiding the window). The slice 5.17 to 5.19 verification ran with those two uncommitted changes applied; the branch of slice 5.19 does not contain them. Re-run `tests/preview/menus_drive.ps1` after that fix is merged.
- ActionList defect fixed in 5.19 (search field blur activated the selected action) and a host contract found: do not close dock panels inside the custom menu set's change notification.
- Hotkey editor layout below 860 px (caption overlap) not fixed; the preview opens it at 1100 px.

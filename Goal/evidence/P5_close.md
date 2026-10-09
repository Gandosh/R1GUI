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

## Launch the preview
`tools/run/preview.cmd`. It opens on the Editor screen. Full checklist in `docs/dev/preview.md`. Quick tour:
- Drag a tab (for example Outliner) out of the window onto the desktop: a rounded native window appears. Drag it to your second monitor by its title bar, resize it, drop its tab back onto a panel tab strip to dock it.
- Layout menu: switch Default, Modeling, Review; Save as; rename; close the app and start it again, the arrangement comes back.
- Edit > Customize (Ctrl+Shift+C): New menu, rename it, drag commands from the palette into it, hide items with the eye, restart to see it kept.
- Edit > Keyboard Shortcuts (Ctrl+K, Ctrl+S): rebind Move to M and see a conflict warning when you reuse a key.
- Inspector: type a value, drag in the viewport, Ctrl+Z and Ctrl+Y. Exit with File > Exit (Ctrl+Q).
Ctrl+Tab cycles to the earlier screens (Gallery has new Docking, Commands, Properties and Customize pages). User data lives in `%LOCALAPPDATA%\R1GUI\preview\`.

## Verification summary
- Merged main, fresh MSVC build with warnings as errors; fast and GPU tiers pass on the RTX 4080 (fast 144 of 144, GPU 72 of 72, desktop 1 of 1). The builders' Debug trees passed with the validation layer clean.
- Real-desktop drives (native windows 31 of 31 checks; editor drive 0 failures on release and Debug) with real mouse and keyboard input.
- Performance (docs/perf/phase5_baseline.md): idle 0 frames, 0.00 percent CPU with a native window open; redraw frame 0.78 ms median.
- Two library defects found by the drive and fixed with tests: widgets in a closed window were not detached on UiContext destruction (dangling subscription crash), and two classes named UiClock in different modules.

## Accepted risks and open items
- Not verified on this machine: a window crossing monitors of different display scale (both monitors are 100 percent), snap layouts, DWM shadow, a real display-change event.
- Per-window full screen not implemented; floating windows have no minimize button; popups clip to their window.
- Dragging from a floated palette onto the main menu bar does not work; the main window position is not restored; floating windows stay open when another screen is shown; auto-save does not mark named layouts as modified.
- Host contract introduced: whatever a widget talks to in onDetached must outlive its UiContext.
- No independent code review of Phase 5 code (recommended before Phase 6; multi-window lifetime and serialization are the risky parts).
- The interaction specs remain local only.
- Disk: J: filled up during parallel builds; build trees for builders moved to C:. Old worktrees were removed.

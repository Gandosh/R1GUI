# R1GUI goal

A reusable, fully owned C++ UI toolkit (Vulkan, Windows first) with OpenPencil's look and an Unreal-style editor interaction model: dockable panels that resize and can be dragged to a second monitor, fully user-customizable menus and toolbars, curve editors and thumbnails. It will host a ZBrush-style sculpting and polygon-modeling application and a D5-style archviz application. Draft plan: [scratch/native-ui-toolkit-plan.md](scratch/native-ui-toolkit-plan.md).

How to read this file: `[ ]` open, `[x]` done by its declared rule. One line per slice (checkbox, title, evidence link). Decisions, measurements and history live in the evidence files, not here. Every phase ends with a launchable interactive preview (`examples/preview`).

Evidence files are created when a slice is worked: `Goal/evidence/P<phase>_S<nn>.md`. Phase close records: `Goal/evidence/P<phase>_close.md`.

---

## Phase 0: Decisions and groundwork

- [x] 0.1 [Confirm Epic EULA permits reading Unreal source for behavior specs (owner)](Goal/evidence/P0_S01.md)
- [x] 0.2 Decide graphics API: Vulkan (owner, 2026-10-09)
- [x] 0.3 Decide platforms: Windows first, macOS and possibly Linux after first release (owner, 2026-10-09)
- [x] 0.4 [Decide windowing layer: own Win32 (owner, 2026-10-09)](Goal/evidence/P0_S04.md)
- [x] 0.5 [Decide build system, C++ standard and compiler: CMake + Ninja, C++20, MSVC (owner, 2026-10-09)](Goal/evidence/P0_S05.md)
- [x] 0.6 [Decide text strategy: FreeType + HarfBuzz, own atlas (owner, 2026-10-09)](Goal/evidence/P0_S06.md)
- [x] 0.7 [Third-party allowlist with license and version per entry](Goal/evidence/P0_S07.md)
- [x] 0.8 [Confirm OpenPencil license permits reuse of tokens and visual designs](Goal/evidence/P0_S08.md)
- [x] 0.9 [Decide repository layout and toolkit license: this folder, proprietary (owner, 2026-10-09)](Goal/evidence/P0_S09.md)
- [x] 0.10 [Create git repository, .gitignore, .gitattributes](Goal/evidence/P0_S10.md)
- [x] 0.11 [Buildable empty skeleton: source/, tests/, examples/, tools/ per the workspace layout](Goal/evidence/P0_S11.md)
- [x] 0.12 [Developer wrappers: tools/build, tools/run (detached, ctest tiers, sanitize)](Goal/evidence/P0_S12.md)
- [x] 0.13 [Audit tools: license boundary and provenance log](Goal/evidence/P0_S13.md)
- [x] 0.14 [Local clean-build check replaces hosted CI; GitHub is storage only (owner, 2026-10-09)](Goal/evidence/P0_S14.md)
- [x] 0.15 [Preview: minimal Vulkan window opens, resizes and closes cleanly](Goal/evidence/P0_S15.md)

**P0 owner gate: ACCEPTED** (2026-10-09) — [phase close record](Goal/evidence/P0_close.md)

## Phase 1: Visual spec extraction

- [x] 1.1 [Export OpenPencil design tokens to one neutral tokens.json](Goal/evidence/P1_S01.md)
- [x] 1.2 [Widget catalogue widgets.md: states, sizes, behaviors for every widget](Goal/evidence/P1_S02.md)
- [x] 1.3 [Reference screenshot set from Storybook, per widget and state](Goal/evidence/P1_S03.md)
- [x] 1.4 [Icon pipeline decision and Lucide atlas/font build](Goal/evidence/P1_S04.md)
- [x] 1.5 [Font files and metrics captured for pixel-matching text](Goal/evidence/P1_S05.md)
- [x] 1.6 [Pixel-comparison method: diff tool and tolerance defined](Goal/evidence/P1_S06.md)
- [x] 1.7 [Preview: token and reference-screenshot viewer](Goal/evidence/P1_S07.md)

**P1 owner gate: ACCEPTED** (2026-10-09) — [phase close record](Goal/evidence/P1_close.md)

## Phase 2: Interaction spec

All specs are behavior-only, with a provenance-log entry per spec.

- [x] 2.1 [Focus, keyboard navigation, shortcut routing and conflicts](Goal/evidence/P2_S01.md)
- [x] 2.2 [Docking: tabs, splits, drag previews, drop targets](Goal/evidence/P2_S02.md)
- [x] 2.3 [Floating panels and dragging to a second monitor (multi-window, DPI, monitor changes)](Goal/evidence/P2_S03.md)
- [x] 2.4 [Saved, reloadable and switchable layouts](Goal/evidence/P2_S04.md)
- [x] 2.5 [Panel resize and adaptive behavior](Goal/evidence/P2_S05.md)
- [x] 2.6 [User-customizable menus and toolbars: create, resize, place, remove buttons](Goal/evidence/P2_S06.md)
- [x] 2.7 [Commands: one registration drives menu item, toolbar button and shortcut](Goal/evidence/P2_S07.md)
- [x] 2.8 [Drag and drop, selection, multi-selection](Goal/evidence/P2_S08.md)
- [x] 2.9 [Property binding, live-updating panels, mixed values, undo grouping](Goal/evidence/P2_S09.md)
- [x] 2.10 [Tooltips, popups, context menus, modal behavior](Goal/evidence/P2_S10.md)
- [x] 2.11 [Curve editor behavior](Goal/evidence/P2_S11.md)
- [x] 2.12 [Asset browser and thumbnail behavior](Goal/evidence/P2_S12.md)
- [x] 2.13 [Unreal reference reads via separate reader session (only after 0.1)](Goal/evidence/P2_S13.md)
- [x] 2.14 [Preview: click-through of the interaction specs](Goal/evidence/P2_S14.md)

**P2 owner gate: ACCEPTED** (2026-10-09) — [phase close record](Goal/evidence/P2_close.md)

## Phase 3: Core toolkit

- [x] 3.1 [ui-platform: Win32 window, input, clipboard, cursors, DPI; borderless windows with our own title bar (owner, 2026-10-09)](Goal/evidence/P3_S01.md)
- [x] 3.2 [ui-render: Vulkan device, swapchain per window, frame loop](Goal/evidence/P3_S02.md)
- [x] 3.3 [ui-render: 2D batcher (rects, rounded rects, borders, shadows, clipping)](Goal/evidence/P3_S03.md)
- [x] 3.4 [ui-text: font loading, shaping, glyph atlas](Goal/evidence/P3_S04.md)
- [x] 3.5 [ui-text: single-line editing](Goal/evidence/P3_S05.md)
- [x] 3.6 [ui-core: retained widget tree with generation-checked handles](Goal/evidence/P3_S06.md)
- [x] 3.7 [ui-core: flexbox-style layout (Yoga evaluation vs own)](Goal/evidence/P3_S07.md)
- [x] 3.8 [ui-core: event routing, hit testing, focus, pointer capture](Goal/evidence/P3_S08.md)
- [x] 3.9 [ui-core: invalidation, redraw only what changed](Goal/evidence/P3_S09.md)
- [x] 3.10 [ui-theme: tokens.json loader and style resolution](Goal/evidence/P3_S10.md)
- [x] 3.11 [Frame-time, idle CPU and memory baseline recorded](Goal/evidence/P3_S11.md)
- [x] 3.12 [Preview: themed window with a panel of static widgets](Goal/evidence/P3_S12.md)

**P3 owner gate: ACCEPTED** (2026-10-09) — [phase close record](Goal/evidence/P3_close.md)

## Phase 4: Widgets

Each widget is compared against its Phase 1 reference screenshots.

- [x] 4.1 [Button, icon button, label](Goal/evidence/P4_S01.md)
- [x] 4.2 [Text input](Goal/evidence/P4_S02.md)
- [x] 4.3 [Number field with scrubbing](Goal/evidence/P4_S03.md)
- [x] 4.4 [Segmented control, switch, checkbox](Goal/evidence/P4_S04.md)
- [x] 4.5 [Select / combobox](Goal/evidence/P4_S05.md)
- [x] 4.6 [Property section and rows](Goal/evidence/P4_S06.md)
- [x] 4.7 [Scroll areas and splitters](Goal/evidence/P4_S07.md)
- [x] 4.8 [Menus, context menus, popovers](Goal/evidence/P4_S08.md)
- [x] 4.9 [Tooltips and dialogs](Goal/evidence/P4_S09.md)
- [x] 4.10 [Tab bar and toolbar](Goal/evidence/P4_S10.md)
- [x] 4.11 [Tree and list views (outliner, layer panel)](Goal/evidence/P4_S11.md)
- [x] 4.12 [Color picker](Goal/evidence/P4_S12.md)
- [x] 4.13 [Gradient editor](Goal/evidence/P4_S13.md)
- [x] 4.14 [Curve editor widget](Goal/evidence/P4_S14.md)
- [x] 4.15 [Thumbnail grid / asset browser widget](Goal/evidence/P4_S15.md)
- [x] 4.16 [Widgets pass visual comparison and match the Phase 2 specs](Goal/evidence/P4_S16.md)
- [x] 4.17 [Preview: widget gallery with every widget and state](Goal/evidence/P4_S17.md)

**P4 owner gate: ACCEPTED** (2026-10-10) — [phase close record](Goal/evidence/P4_close.md)

## Phase 5: Docking, commands, customization

- [x] 5.1 [ui-dock: dock tree, tabs, splits, drop targets](Goal/evidence/P5_S01.md)
- [x] 5.2 [ui-dock: floating panels as native windows with rounded corners, drag to second monitor (owner, 2026-10-09)](Goal/evidence/P5_S02.md)
- [x] 5.3 [ui-dock: layout serialization, versioned schema, validated load](Goal/evidence/P5_S03.md)
- [x] 5.4 [ui-dock: layout save, load and switch at runtime](Goal/evidence/P5_S04.md)
- [x] 5.5 [ui-commands: command registry (menu, toolbar, shortcut from one source)](Goal/evidence/P5_S05.md)
- [x] 5.6 [ui-commands: shortcut editor and conflict resolution](Goal/evidence/P5_S06.md)
- [x] 5.7 [Customizable menus: user creates menus, adds, moves and removes items](Goal/evidence/P5_S07.md)
- [x] 5.8 [Customizable toolbars: user places, resizes and removes buttons](Goal/evidence/P5_S08.md)
- [x] 5.9 [Customization persistence: user menu and toolbar files, validated load](Goal/evidence/P5_S09.md)
- [x] 5.10 [ui-props: property panel generated from declared field metadata](Goal/evidence/P5_S10.md)
- [x] 5.11 [Undo grouping and mixed-value handling in property panels](Goal/evidence/P5_S11.md)
- [x] 5.12 [Preview: sample editor with rearrangeable panels, floating windows, user-built menus, reloadable layouts](Goal/evidence/P5_S12.md)

**P5 owner gate: PENDING** — [phase close record](Goal/evidence/P5_close.md)

## Phase 6: Integration

- [ ] 6.1 [3D viewport widget rendering in the host graphics context](Goal/evidence/P6_S01.md)
- [ ] 6.2 [Input forwarding between viewport and panels, pen pressure and tilt](Goal/evidence/P6_S02.md)
- [ ] 6.3 [Stable public API and packaged library](Goal/evidence/P6_S03.md)
- [ ] 6.4 [Sculpting app skeleton running on the toolkit UI](Goal/evidence/P6_S04.md)
- [ ] 6.5 [Archviz app skeleton running on the toolkit UI](Goal/evidence/P6_S05.md)
- [ ] 6.6 [Performance and memory budget check against the Phase 3 baseline](Goal/evidence/P6_S06.md)
- [ ] 6.7 [Preview: full example editor with a live 3D viewport](Goal/evidence/P6_S07.md)

**P6 owner gate: PENDING** — [phase close record](Goal/evidence/P6_close.md)

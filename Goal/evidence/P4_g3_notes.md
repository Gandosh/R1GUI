# P4 group g3: section, scroll, splitter, tabbar, toolbar, tree (slices 4.6, 4.7, 4.10, 4.11)

Date: 2026-10-09. Branch `worktree-agent-ab52c33c1c5fbed04`, builds `build/p4-g3` (dev) and `build/p4-g3d` (debug, validation layers on), GPU `RTX 3090`.

## What was built

All code is under `source/ui-widgets/{include/r1ui/widgets,src}/{section,scroll,splitter,tabbar,toolbar,tree}` and `tests/ui-widgets/{section,scroll,splitter,tabbar,toolbar,tree,g3support}`.

| Widget | Classes | Notes |
|---|---|---|
| section | `PropertySection`, `PanelHeader`, `FieldGroup`, `FieldGrid`, `SectionBox`, `ActionButton` (private icon button shared by section, tab bar and toolbar code), `GalleryContainers` | header 26 px with its 1 px separator inside, header with action buttons 35 px (1 + 8 + 26), content 12 px sides, 8 px bottom, label 11 px / 4 px gap, grid gap 6, rail 26 |
| scroll | `ScrollBar` (reusable thin bar object), `ScrollArea`, `ScrollContent` | gutter 10 px with a 6 px thumb, wheel notches x 48 px, Shift = horizontal, chaining, drag, track click (page), Page/Home/End/arrows, scroll-into-view (nearest / start / centre / end), Gutter and Overlay styles, both axes (horizontal needs `setContentMinWidth`) |
| splitter | `Splitter`, `SplitterPane` | 5 px handle, 20 px floor, only the two neighbours change, fixed / hidden panes, explicit `collapse` / `expand` (double click does nothing), `state()` / `restoreState()`, keyboard resize (D10), hit band API (9 px for touch) |
| tabbar | `TabBar` (one widget, tabs are data), uses `FlyoutList` for the all-tabs list | 109 px "Untitled" tab, max 192, even shrink to 60 px (D12) then scroll arrows + list, activation on press, close button / middle click / Ctrl+W, drag reorder with live gap, Escape cancel, full-title tooltips, keyboard |
| toolbar | `Toolbar`, `ToolbarButton` (tool / action / toggle, badge), `ToolbarTrigger`, `ToolbarSeparator`, `ToolbarGroupBox`, `FlyoutList` + `openFlyout` | 32 px buttons, 12 px flyout chevron, 4 px separator, gap 2, surface radius 12 with shadow, exclusive tools, flyout groups, vertical and horizontal, roving keyboard navigation |
| tree | `TreeView`, `TreeModel`, `SimpleTreeModel`, `RenameEditor` | virtualized rows (rows are never widgets), 100k rows, selection single / multi per spec 08, keyboard per spec 01, type-to-search, inline rename (F2, slow click, validation), drag and drop with above / below / onto zones, edge auto-scroll, hover actions, selection and expansion persistence, list appearance, scrollbar policy |

Gallery: `buildGalleryContainers(UiContext&, WidgetId parent)` in `section/GalleryContainers.{h,cpp}`: one labelled instance of every widget and every statically showable state (see `gallery_test`, `gallery_visual_test`; render in `build/p4-g3/artifacts/ui-widgets/gallery-containers-{dark,light}.png`).

## Foundation changes (smallest possible, each with a regression test)

1. `Invalidator::layoutPending()` + `UiContext::frame()` keeps iterating while a layout callback requested layout (a scroll area adds its scrollbar gutter in `onLayout`; without it the content reflowed one frame late). Test: `scroll_test` (content width after one `layout()`).
2. `Router::validateCapture()` now also releases the capture of a widget that became disabled (its own header comment already promised it; the code only checked "shown"). Test: `scroll_test` (disabled during a thumb drag), `tabbar_test`, `splitter_test`.
3. `Label::measure()` rounds the width up to a whole logical pixel: layout rounds a fractional width to the nearest pixel and `paint()` then shortened labels that had been measured to fit ("Appearance" 62.4 px was drawn as "Appearan..."). Test: `section_test` (title fits).

Not changed but worth the merge's attention: `OverlayHost` padding does not include its 1 px border, so a menu surface's content sits 1 px too far out (measured: 137.7 px popup = 127.7 item + 2 x (4 padding + 1 border)); `FlyoutList` compensates with a 1 px margin, the Menu builder will hit the same. The TextEngine draws weight 500 like 400; the reference's 12 px menu / flyout labels are visibly heavier (ink 1.33x of regular), which needs a calibrated weight-500 strength.

## Verification (real output)

`tools\build\msvc_env.cmd cmake --preset dev -B build/p4-g3` (exit 0), `cmake --build build/p4-g3` (exit 0), `ctest --test-dir build/p4-g3 -L fast`: 100% passed, 46 of 46; `-L gpu` (R1UI_GPU="RTX 3090"): 100% passed, 35 of 35. Debug tree `--preset debug -B build/p4-g3d`: build exit 0, `-L fast` 46 of 46, `-L gpu` 35 of 35, validation on, `validationMessageCount() == 0` is asserted by every visual test of the group.

Performance (tree_scale_test, dev build, printed by the test): 100,100 rows (100 groups of 1000): model build 33 ms, expandAll 43 ms, worst paint 2.7 ms, select all 24 ms, 2000 Down keys 3 ms, refresh with a 100k selection 43 ms, pruning a removed 1001-node subtree 41 ms. A 100,000 row flat list paints in under 60 ms (bound asserted) and type-to-search finds "item 9999".

## Visual comparison numbers (profile `text` + luminance unless noted; failing fraction / max channel / mean)

Sources: the widget crops of `tests/reference/openpencil` and cut-outs of the full screens (`RegionCompare.h`). All 94 comparisons pass their profile.

| Reference | dark | light |
|---|---|---|
| widget-panel-header-rectangle idle / hover | 1.495% / 145 / 2.95 | 2.458% / 178 / 5.29 |
| widget-section-add-button idle (profile icons) | 0.000% / 23 / 0.07 | 0.277% / 37 / 0.13 |
| widget-section-add-button hover | 0.277% / 42 / 0.13 | 0.277% / 53 / 0.18 |
| widget-panel-field-label idle | 0.000% / 16 / 0.67 | 0.000% / 26 / 1.10 |
| section header Layout / Appearance / Fill (screen-rectangle-selected) | 1.625% / 2.137% / 0.742% | 0.507% / 0.687% / 0.111% |
| tab bar two documents (screen), inactive idle / hover | 0.301%, 0.000% / 0.813% | 0.231%, 0.000% / 0.367% |
| tab active idle / hover, new idle / hover | 0.655%, 0.655% / 0.833%, 1.369% | 0.419%, 0.419% / 0.833%, 1.667% |
| toolbar whole bar idle / rectangle hovered (screens) | 0.311% / 0.484% | 2.582% / 2.914% |
| toolbar button idle, hover (active select tool) | 1.446% / 99 / 2.22 | 1.963% / 119 / 3.11 |
| toolbar toggle idle / hover (theme button) | 0.000% / 0.000% | 0.000% / 1.136% |
| flyout content | 0.688% / 91 / 1.46 | 0.284% / 111 / 0.94 |
| layer tree idle / collapsed frame / selected focused | 0.117% / 0.456% / 0.562% | 0.096% / 0.438% / 0.017% |
| row idle / hover / hidden | 0.118% / 0.513% / 0.150% | 0.075% / 0.556% / 0.470% |
| row selected (first pass) idle / hover | 0.021% / 0.403% | 1.145% / 1.601% |
| row selected unfocused idle / hover | 0.021% / 0.406% | 0.000% / 0.459% |
| row selected focused idle / hover | 0.021% / 0.214% | 0.000% / 0.459% |
| row rename | 0.587% / 174 / 1.37 | 0.354% / 194 / 1.60 |
| row action (row hovered / action hovered) | 1.276% / 2.041% | 0.765% / 2.934% |
| disclosure expanded idle / hover, collapsed | 0.744% / 1.935% / 0.000% | 1.935% / 2.083% / 0.000% |
| scrollbar idle / thumb hover (profile default, only the gutter column compared) | 0.000% (max 2) / 0.000% (max 8) | 0.000% (max 6) / 0.005% (max 20) |

Unreferenced visuals are checked by pixel assertions: splitter (idle line `border`, hover bar `border-strong`, drag bar `accent`, focus ring `panel-focus`), tree drop feedback (2 px accent line above / below, accent outline onto, dragged row at 30%), gallery smoke render.

## What visibly differs and why (documented, not hidden)

* LCD text antialiasing of the reference: every text comparison is on luminance (as the Label test).
* The reference toolbar sits on a half-pixel offset (x 547.5; the bar is centred in a 1440 px window), ours on whole pixels: vertical strokes of icons and the side edges of the accent button differ by half a pixel of coverage. The two edge columns of the active button (`kHalfPixelEdges`) are masked in the 44 x 44 crops, the whole-bar comparison needs the `text` profile (light bar 2.6% / 2.9% of pixels, close to the 3% limit).
* The page behind the toolbar and the flyout is the document colour #4d4d4d, which is not a theme token: pixels outside the rounded surface are ignored (`RegionSpec::clip`); the tooltip that overlaps the first three rows of the bar in `screen-toolbar-tooltip` is ignored (`{60, 0, 80, 3}`).
* Flyout / menu labels: the capture is heavier than ours (ink 0.75x of the reference) because weight 500 is rendered like 400 by the engine (measured, see above); geometry matches to the pixel when the same glyph run is used.
* The renaming tree row is 26 px high and moves the rows below down 2 px (as measured); the harness crop shows the same.
* Light theme text selection colour (#3367d1) and dark (#063aa4) in the rename field are Chrome's measured highlight colours, not tokens; selected text is white.
* The icons `mouse-pointer` (select tool) is the closest available Lucide file to the capture's arrow.

## Behaviours not implemented / approximate

* Toolbar overflow with an overflow button and menu (specs 05 rules 32-36 and 06 rules 49-54), drop priorities, small-icon mode, customization.
* Tab strip: tearing a tab out for docking, the sliding animation of the neighbours during a reorder (they jump), the 0.75 s tab activation while dragging, tab context menu content (callback only), tab icon-only modes (rule 11).
* Tree: drag ghost image, spring-loaded expansion while hovering a collapsed row during a drag, filters / sort, multi-column rows, the 0.15 s transitions of the disclosure rotation and the row action fade (the chevron swaps between `chevron-right` and `chevron-down`; the actions appear at once), payload nodes that are hidden inside collapsed branches are not dragged.
* Section: collapse animation (instant), a chevron on collapsible headers (the reference shows none).
* Splitter: handle spring-back or snapping (none in the reference), animated collapse.
* ScrollArea: kinetic / smooth scrolling and rubber-band overscroll (the offset clamps), auto-scroll while dragging inside a ScrollArea (the tree has its own).
* Accessibility: names are stored (`accessibleName`) but nothing consumes them yet.

## Unverified

* Behaviour on a real window with the platform shell (no window was opened; all input is synthetic through `UiContext`), real IME composition in the rename field, touch widening of the splitter band (API only), fractional display scales for the visual crops (the gallery smoke test builds at 1.5 and 2, the reference comparisons are 1.0 only), the clipboard path of the rename field (hooks exist, a shell must provide them).
* The drop indicator colours, the 25% / 3..10 px zone bands and the 30% dragged opacity are source-derived (spec 08 starting values), not measured.

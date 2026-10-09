# Phase 4 close record

**Status:** TECHNICAL PASS; owner gate PENDING   **Date:** 2026-10-09

## Slices
| Slice | Status | Evidence |
|---|---|---|
| 4.1 Button, icon button, label | TECHNICAL PASS | [P4_S01](P4_S01.md) |
| 4.2 Text input | TECHNICAL PASS | [P4_S02](P4_S02.md) |
| 4.3 Number field with scrubbing | TECHNICAL PASS | [P4_S03](P4_S03.md) |
| 4.4 Segmented control, switch, checkbox | TECHNICAL PASS | [P4_S04](P4_S04.md) |
| 4.5 Select / combobox | TECHNICAL PASS (dark open list masks item text) | [P4_S05](P4_S05.md) |
| 4.6 Property section and rows | TECHNICAL PASS | [P4_S06](P4_S06.md) |
| 4.7 Scroll areas and splitters | TECHNICAL PASS | [P4_S07](P4_S07.md) |
| 4.8 Menus, context menus, popovers | TECHNICAL PASS | [P4_S08](P4_S08.md) |
| 4.9 Tooltips, dialogs, toasts | TECHNICAL PASS | [P4_S09](P4_S09.md) |
| 4.10 Tab bar and toolbar | TECHNICAL PASS | [P4_S10](P4_S10.md) |
| 4.11 Tree and list views | TECHNICAL PASS | [P4_S11](P4_S11.md) |
| 4.12 Color picker | TECHNICAL PASS | [P4_S12](P4_S12.md) |
| 4.13 Gradient editor | TECHNICAL PASS | [P4_S13](P4_S13.md) |
| 4.14 Curve editor | TECHNICAL PASS (no reference) | [P4_S14](P4_S14.md) |
| 4.15 Thumbnail grid | TECHNICAL PASS (no reference) | [P4_S15](P4_S15.md) |
| 4.16 Visual comparison and calibration | TECHNICAL PASS (owner-accepted tolerances) | [P4_S16](P4_S16.md) |
| 4.17 Preview: widget gallery | TECHNICAL PASS (owner interaction pending) | [P4_S17](P4_S17.md) |

Supporting records: [P4_S00_capture](P4_S00_capture.md) (reference captures for the previously missing states), [P4_calibration_notes](P4_calibration_notes.md), group notes P4_g1 to P4_g5.

## Launch the preview
`tools/run/preview.cmd`. It opens on the widget gallery (page list on the left: Buttons, Fields, Containers, Overlays, Editors). Tab cycles Gallery, Widgets (a composed editor screen built only from the widget library), Swatches, Screens and the docking Sandbox when nothing has keyboard focus (Ctrl+Tab always cycles); T toggles dark and light. Five small squares at the top right show the mode.
In the Widgets screen try: hover and type in the fields, scrub a number field by dragging it, open the select, the File, Edit and View menus, right-click the canvas, click the fill swatch for the color picker, press Variables for a dialog and Show toast, rename or drag layers in the tree, drag the splitters, switch document tabs, press the theme buttons.

## Verification summary
- Fresh MSVC build (/W4 /WX): 82 of 82 fast tests pass; GPU tier 57 of 57 (offscreen visual tests, both themes) on the RTX 4080. The Debug tree passed with the validation layer clean in the builders' runs. clang-cl and sanitizer runs were not done.
- 300 visual comparisons against the 430 reference images pass under the adopted tolerance profiles (before adoption 42 of 300 failed).
- Preview benchmark on the RTX 4080 (docs/perf/phase4_baseline.md): 0.9 ms CPU per forced redraw of the composed screen (182 widgets), idle with nothing focused 0 frames and 0.16% CPU, 132 MiB working set, 1.0 s startup. A focused text field blinks its caret with continuous frames (about 33% of one core): there is no timer-driven blink yet.

## Owner decisions in this phase
| Decision | Date |
|---|---|
| Tolerance profiles adopted as proposed (text 72/6%, icons 24/6%, screen 16/4%, default 8/0.5%) | 2026-10-09 |
| GPU rule: the RTX 3090 stays idle, all GPU work uses the RTX 4080 (renderer reads a per-user preference file) | 2026-10-09 |
| Detached floating windows have rounded corners (applied to borderless windows) | 2026-10-09 |

## Accepted risks and open items
- Visible 1 px differences remain (tooltip box width, toast text, flyout and menu bar text, dialog close X) and dark text on a light surface is 1 to 8% heavy; the adopted text profile lets 6% of pixels differ.
- Curve editor and thumbnail grid have no reference: structure tests and your eye are the oracle. CurveGraph.cpp is 1308 lines.
- Consolidation candidates (private duplicates inside the picker and navigation widgets: PickerButton, PickerDropdown, PickerEntry and channel fields, ActionButton, toolbar and tab bar button drawing) are left because swapping them moves measured visuals.
- Not implemented across widgets: text-input IME connection, toolbar overflow and customization, tab tear-off, drag ghost images, toast slide, menu open transition, marquee selection in the thumbnail grid, rotation drawing in the composed screen (painter has no rotated rectangles), document-level undo.
- No independent code review of the widget library; recommended before Phase 5 builds docking and customization on top.
- The interaction specs remain local only (publication undecided).
- Benchmark numbers are not comparable to Phase 3 (different GPU, different panel).
- Earlier GPU runs in this phase, before the owner's rule, used the RTX 3090 (tests and a few GUI sessions); everything after used the 4080.

## Build notes
- New GPU tier convention: visual tests end with `_visual_test`, GPU tests `_gpu_test`; set `R1UI_TOLERANCE_FILE` to try other profiles and `R1UI_DIFF_SWEEP=1` for the sweep (docs/dev/widgets.md).
- Preview commands: `r1gui-preview --bench <dir>` rewrites the benchmark; `--shot <dir>` renders the gallery pages and the Widgets screen offscreen in both themes.

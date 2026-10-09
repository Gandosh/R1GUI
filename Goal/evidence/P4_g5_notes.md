# Phase 4, group g5: colour picker, gradient editor, curve editor, thumbnail grid

Slices 4.12 to 4.15. All numbers below were printed by the
tests in this tree (Dev build `build/p4-g5`, GPU "RTX 3090"), not copied from earlier runs.

Decisions applied: spec 11 and 12 with the D17 and D18 defaults, 4 px field radius, drag threshold
strictly greater than max(1 mm, 5 px), calibrated text emboldening.

## 1. Colour picker (`colorpicker/`)

Built: `ColorModel` (RGB / HSV / HSL / hex, hue memory so greys keep their hue), `NumberText`,
`PickerEntry`, `PickerButton`, `PickerDropdown`, `ColorControls` (saturation / value square, slider
tracks and rows), `ChannelFields` (RGB, HSL, HEX), `Swatches` (max 256), `ColorPicker` (mode tabs,
square, hue and alpha rows, component select, eyedropper slot, swatches, begin / changed / end gesture
bracket). Private helpers only (`PickerDraw`); nothing depends on another group's widgets.

References matched (profile in brackets):

| reference | theme | failing | max | mean | result |
|---|---|---|---|---|---|
| screen-color-picker-open (screen, region 87120 px) | dark | 1142 px, 1.311 % of region | 194 | 0.094 | pass |
| screen-color-picker-open | light | 1309 px, 1.503 % | 222 | 0.116 | pass |
| widget-color-slider-hue-idle / hover (text) | dark | 0.664 % | 173 | 0.847 | pass |
| widget-color-slider-alpha-idle / hover (text) | dark | 1.428 % | 173 | 1.861 | pass |
| widget-color-slider-hue-idle / hover (text) | light | 0.596 % | 206 | 0.949 | pass |
| widget-color-slider-alpha-idle / hover (text) | light | 1.631 % | 208 | 2.177 | pass |
| widget-fill-picker-tab-idle (icons) | dark / light | 0.000 % / 0.154 % | 22 / 35 | 1.275 / 1.986 | pass |
| widget-fill-picker-tab-hover (icons) | dark / light | 0.463 % / 0.772 % | 41 / 49 | 1.723 / 2.341 | pass |
| widget-fill-picker-tab-active (icons) | dark / light | 0.309 % / 0.772 % | 45 / 56 | 1.037 / 1.298 | pass |

The popover reference is a crop of a whole editor screen; `RegionVisual.h` judges the failing fraction
against the popover rectangle only (the rest of the screen is not part of the widget).

Visible differences (looked at the render next to the reference):
- The saturation / value square shows faint stepped contour bands in the render; the reference is
  smooth. The square is built from 1 px strips with two alpha layers (the Painter has no gradient
  primitive), so 8-bit rounding accumulates. It is within the screen profile but visible when zoomed.
- The square's handle is drawn at the colour's position (top left for grey 212); the reference crop
  shows no handle there.
- Text weight is an approximation: the engine has Regular plus a synthetic bold from weight 600, so
  500 weight text in the reference is a little lighter or heavier than the render.
- The reference shows the editor behind the popover; the render has a plain panel behind it.

Not implemented: the Image tab has its tab but no image-fill content (not part of the slice); the
eyedropper only reports `onEyedropper` (the host samples the screen); no wide-gamut or CMYK input.

## 2. Gradient editor (`gradient/`)

Built: `GradientModel` (2 to 64 stops with stable ids, evaluation, add / move / remove), `GradientBar`
(7 px overhang so end handles are reachable; click adds, drag moves, drag off the bar removes, Escape
restores, keyboard moves and deletes), `GradientStopRow` (position, colour swatch, hex, alpha),
`GradientEditor` (tabs, type select or buttons, angle and centre fields, bar, stop rows, embedded
`ColorPicker` for the selected stop).

| reference | theme | failing | max | mean | result |
|---|---|---|---|---|---|
| screen-gradient-editor (screen, region 124320 px) | dark | 2601 px, 2.092 % | 213 | 0.223 | pass |
| screen-gradient-editor-stop-selected | dark | 2785 px, 2.240 % | 213 | 0.245 | pass |
| screen-gradient-editor | light | 2998 px, 2.412 % | 222 | 0.257 | pass |
| screen-gradient-editor-stop-selected | light | 3015 px, 2.425 % | 222 | 0.258 | pass |
| widget-gradient-bar-idle / dragging (icons) | dark | 0.024 % | 41 / 35 | 0.280 / 0.230 | pass |
| widget-gradient-bar-idle / dragging (icons) | light | 0.000 % | 10 | 0.202 / 0.170 | pass |
| widget-gradient-stop-idle (icons) | dark / light | 0.000 % | 31 / 9 | 1.244 / 0.565 | pass |
| widget-gradient-stop-inactive-idle / selected (icons) | dark | 0.592 % | 41 / 39 | 0.956 / 1.117 | pass |
| widget-gradient-stop-inactive-idle / selected (icons) | light | 0.000 % | 10 | 0.342 / 0.339 | pass |
| widget-gradient-stop-row-idle (text) | dark / light | 0.000 % | 28 / 35 | 1.224 / 1.567 | pass |
| widget-gradient-stop-row-inactive-idle (text) | dark / light | 0.000 % | 28 / 35 | 1.437 / 2.157 | pass |

Visible differences (looked at the render next to the reference): the same banding in the embedded
square as above, and the square's handle at the top left that the reference crop does not show; the
bar end handles are small rounded squares in the render, slightly different in shape from the
reference; the "Stops" header, stop rows, tabs and colour fields line up with the reference.

Not covered by a reference comparison: only the Linear type is rendered by the visual tests; the other
types and the geometry fields are covered by behaviour tests only. No on-canvas gradient handles (a
canvas feature, outside this widget).

## 3. Curve editor (`curveeditor/`)

Built: `CurveMath` (Hermite and Bezier segments, weighted tangents solved with a safeguarded Newton so
time stays monotone, five extrapolation kinds, flattening to 0.5 px), `CurveView` (pan and zoom, grid
step selection), `CurveOps` (insert, delete, move, tangent modes, copy and paste, snapping),
`CurveGraph` (ruler, grid, curves, key markers, tangent handles, scrub line, marquee, culling by binary
search and per-pixel-column decimation for dense curves, the undo bracket protocol: begin at the first
modification, Escape and capture loss restore), `CurveKeyFields`, `CurveEditor`.

No reference image exists for a curve editor, so the test is golden-image with structural checks
(`curve_visual_test`): canvas and ruler colours, major and minor grid lines at the predicted columns and
rows, key markers, handles, scrub line. The red curve was compared with the analytic curve on 20
columns: worst deviation 0.81 px dark (column 135) and 0.80 px light, limit 1.0 px. The render
`build/p4-g5/artifacts/ui-widgets/curve-graph-golden-dark.png` was looked at: curves, stepped green
curve with repeat extrapolation, selected keys in the accent colour with tangent handles, dotted value
indicators and the blue scrub line all appear as intended.

Defect found by the 100000-key test and fixed: selecting all keys and nudging them took 4.7 s plus
17.6 s in the Release build (and over 400 s for the whole test in Debug) because every selected key was
found with a linear scan (`indexOfKey`), which is quadratic. `curve::KeyLookup` (O(n log n) build,
O(log n) per lookup) now serves `Selection::prune`, `selectionInfo`, `frameSelected`, the value
indicators in paint and the tangent flatten / straighten commands; set-value iterates the keys once.
Select all plus nudge of 100000 keys now takes 66 ms in Release; `curvegraph_test` runs in well under
a second instead of 35 s. `testKeyLookup` covers the new class. The two timing bounds that guard
complexity in `curveops_test` are relaxed by a fixed factor in Debug builds only.

Not implemented: no behaviour of spec 11 is knowingly missing. `CurveGraph.cpp` is 1308 lines, above
the 500 line guideline (the interaction code is one state machine; a split into commands and input
files is possible but was not done).

## 4. Thumbnail grid (`thumbnailgrid/`)

Built: `GridLayout` (metrics, hit test, visible range with clamped scroll, navigation, zoom stops
64, 96, 128, 160, 192, 256 within 48 to 320), `GridSupport` (navigation history capped at 300, natural
sort, case-insensitive find, type-ahead with 2 s reset, name wrapping), `ThumbnailCache` (LRU of 4096
entries and 256 MiB with the visible window plus or minus 64 protected) and `ThumbnailScheduler`
(5 ms budget, 32 per pass, visible first), `ThumbnailGrid` (virtualised: no widget per item; model view,
incremental filter in 15 ms slices, selection by key, press / Ctrl / Shift / collapse-on-release rules,
drag threshold, context menu requests, keyboard navigation, type-ahead, rename with Enter / Escape /
invalid-name feedback, scrollbar, notices), `GpuThumbnailTextures` (the one Vulkan-bound sink), and the
gallery entry `buildGalleryEditors` (all four editors).

Measured (printed by `thumbnailgrid_test`): 5000 items, 200 scroll frames, average 0.16 ms, worst
0.37 ms, 3 widgets; 100000 items, average 0.18 ms, worst 0.43 ms, 3 widgets; first filter frame over
300000 items 19.9 ms (the filter continues in later frames); first frame with 2 ms pictures 18.5 ms with
3 of 9 visible pictures requested (the 5 ms budget stopped it).

No reference image exists, so `thumbnailgrid_visual_test` checks the render structurally in both themes:
selected tile fill (muted when unfocused), plain tiles, thumbnail square, type strip colour (also over a
picture), modified marker, a real GPU picture (red above, blue below), icon and label ink, scrollbar
thumb. Both renders (`thumbnail-grid-golden-dark.png` / `-light.png`) were looked at: four columns, the
long name wraps to two lines, the selected tiles are filled, the icon falls back to `file` when the
model names an icon that does not exist (the model's "folder" icon is not in the shipped set).

Behaviours not implemented: no marquee (rubber-band) selection; no drag-and-drop target handling (the
widget only starts drags and reports them); no thumbnail generation (the host provides a
`ThumbnailProvider`); folder navigation is reported (`onActivate`, history type) but the widget does not
own the folder model.

A real defect found by the tests and fixed: starting a filter cleared the selection because it was
rebuilt against the still empty result list; it is now rebuilt when the filter finishes. A missing
icon name made painting throw; the grid now falls back to the `file` icon.

## Unverified

- Debug tree: `ctest -L fast` 51 of 51 pass and `-L gpu` 33 of 33 pass in `build/p4-g5d`; the GPU tests
  report "validation on" and no validation message was printed by the visual tests that were run
  directly (thumbnail grid, colour picker, gradient, curve). The ctest log of passing tests does not
  keep their output, so absence of messages in the other GPU tests is not shown line by line.
- High DPI (scale 1.25 and 2.0) is exercised for crashes and basic state only, not compared with
  references (no reference exists at those scales).
- Behaviour with a real mouse, touch and IME composition in the rename box: only synthetic input.
- The two golden tests assert structure, not that the design is right; the renders were judged by eye.

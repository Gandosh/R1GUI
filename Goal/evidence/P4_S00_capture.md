# Slice evidence: P4 S00 Reference capture of the widget states missed by the first pass

**Status:** TECHNICAL PASS (coverage partial, see not captured)   **Date:** 2026-10-09   **Risk tier:** Low

## Goal
Phase 4 widgets are compared pixel-wise against the OpenPencil production build. Slice 1.3 (`P1_S03.md`) could not reach the switch, dialog, toast, default-tone text input, document tab bar, gradient editor, binding picker, layer tree states and several other states. This slice captures them, in both themes, with computed-style numbers, and folds the numbers into `docs/spec/widgets.md`.

## Method
- Reference app: `reference/OpenPencil/dist` (read-only), served by the Node built-in static server and driven through headless Chrome (DevTools protocol over Node's WebSocket, no npm packages), viewport 1440x900, device scale 1, sRGB, software GL, same as `tools/spec/capture.mjs`. Scrollbars are NOT hidden in this pass (`launchChrome({ hideScrollbars: false })`) so the scrollbar can be captured.
- States were found by reading the app source (`reference/OpenPencil/src`, read for navigation only) and then driven through real input events: Grid settings popover (switches), Share popover (default-tone input, disabled accent buttons), Assets tab (small default-tone input), Object context menu "Copy node ID" (toast), a rejected promise (error toast, the app's global handler), Variables dialog opened from the empty selection panel (create collection, add number and colour variables), "Apply variable" (binding picker, bound field), shift-selecting two rectangles (mixed X), fill swatch > gradient tab (gradient editor), File > New (second document, tab bar), a document with nested frames drawn on the canvas (layer tree, constraints), "Add auto layout" (alignment grid, clip content checkbox), toolbar chevron (flyout), View/Edit menus, page row context menu.
- Single command (regenerates every new image and the measurements, leaves first-pass images untouched):
  `node tools/spec/capture_gaps.mjs reference/OpenPencil/dist tests/reference/openpencil docs/spec/measurements_gaps.json`
  It takes about 5 minutes and rewrites only entries with `"set": "gaps"` in `manifest.json`.
- Code: `tools/spec/capture_gaps.mjs` (orchestration, document build, per-theme loop), `tools/spec/lib/drive.mjs` (input, locate, crop and measure helpers), `tools/spec/gaps/{layers,overlays,dialogs,docchrome}.mjs` (scenes, each section isolated by `step` so one failure does not stop the run), `tools/spec/lib/chrome.mjs` (two small additions: `hideScrollbars` option, protocol event listener). `tools/spec/capture.mjs` is unchanged; the first-pass PNGs are byte-identical (git shows no modified PNG).
- Naming as in slice 1.3: `screen-<name>.png` full screens, `widget-<name>-<state>.png` crops with 6 px padding (clamped to the viewport). Measurements in `docs/spec/measurements_gaps.json`: per `<theme>/<widget>` the rect, computed style (typography, colours, border, radius, padding, gap, shadow, opacity, cursor, outline, transition properties) of the first state at top level, and every state with its own rect and style; widgets with inner parts (switch thumb, slider track and thumb, rename input, pill label, dialog title/header) list them under `parts`; the scrollbar entry also carries the `::-webkit-scrollbar*` computed values; the tooltip entry carries the measured open delay.

## Counts
| | dark | light | total |
|---|---|---|---|
| New images | 165 | 165 | 330 |
| of which full screens | 27 | 27 | 54 |
| of which widget crops | 138 | 138 | 276 |
| Widget measurement records | 70 | 70 | 140 |

Manifest total after the slice: 430 images (100 first pass + 330 new). Added size: 4.65 MB of PNG (limit 8 MB); `tests/reference/openpencil` is 6.9 MB in total.

## Captured (gap -> ref in `docs/spec/widgets.md`)
Switch (off idle/hover/focus, on idle/hover); dialog (overlay, content, header controls, collection tab, search, add button, table row, add menu with two-line items); toast (default and error); default-tone text input (md and sm: idle, hover, focus, filled), the popover native number input, accent button enabled and disabled, neutral text button; document tab bar (bar, inactive and active tab with hover, close, new); gradient editor (bar, active and inactive stop, stop rows, add stop, stop selected, stop dragged with the button held); fill picker tabs; hue and alpha sliders; binding picker (popover, search, item, footer action), binding pill, apply-variable trigger, bound width field, mixed X field; layer tree (whole tree idle, collapsed, selected focused; row idle/hover, selected unfocused and focused, hidden, rename input, disclosure expanded/collapsed/hover, eye action hover); constraints diagram and pins (active, inactive, hover); layout alignment grid and cells; clip content checkbox (unchecked, checked, hover); tooltips (toolbar button with shortcut text, panel icon button, tab close); menu items (idle, hover, disabled, component tone, submenu trigger closed and open, item inside a submenu, checkbox, described, disabled in menubar and page menu), menu, submenu, flyout and popover content; scrollbar (idle, pointer on the thumb) with pseudo-element values.

## Not captured (unreachable) and why
| Item | Reason |
|---|---|
| Layer tree drag states (dragged row, drop line, child outline) | Native HTML5 drag and drop. The protocol drag interception reports a payload (the drag starts) but the app never entered its dragging state when enter/over events were replayed (checked: no dragging attribute, no indicator), so nothing is drawn. Source-derived values stay in the catalogue. |
| Toast warning tone | Raised only for an unresolved pasted clipboard image in the web build. |
| Default-tone input invalid, mixed, bound, disabled | Supported by the component, passed by no screen; disabled is used only by the chat input while streaming (needs a configured chat provider). |
| Switch mixed and md size | Mixed only for boolean component properties of an instance; md unused. |
| Disabled icon buttons and number fields | Never disabled in the default build. |
| Keyboard focus rings of tabs and toolbar buttons | Only the switch was driven with Tab. |
| Constraints scale badge, table resize handle, status text, avatars, HSL/HEX modes, eyedropper | Not needed by the first Phase 4 widgets. |
The list is also written into `docs/spec/measurements_gaps.json` (`notCaptured`) and into section 7 of the catalogue.

## Findings that changed the catalogue ("measured X, catalogue said Y")
- Tooltip open delay: measured about 410 ms (412 dark, 416 light), catalogue said about 700 ms.
- Disabled accent buttons: measured opacity 0.5 only, pointer cursor and the 90% accent hover fill stay; catalogue said no hover change.
- Switch hover: no visible change, the class names a token (`border-strong`) the stylesheet does not define.
- Context menu shadow: measured `0 8px 30px rgb(0 0 0 / .4)` (overlay elevation), catalogue said shadow lg for menus; the flyout and the Add variable menu do use lg.
- Gradient stop radius 4 px (theme `rounded-sm`), catalogue only said "squares"; inactive stop border white at 60%, active white confirmed.
- `sm` default input is 26.5 px high (11 px text, 16.5 line), `md` 26 px.
- Tab bar tab height 35 inside the 36 px bar (1 px bottom border); inactive tab hover changes the text only.
- Error toast carries a warning icon, a copy button and a close button (not in the catalogue); toasts sit 8 px from the top, centred.
- Layer disclosure is a 16 x 12 button (12 x 16 bounding box when expanded); selected focused fill (`panel-selected`, #0d64d8) is only reached after an inline rename in this build, a plain click leaves the muted fill.
- Dialog overlay is `rgb(0 0 0 / .5)` also in the light theme; title 14 px weight 600 confirmed.
- Colour-slider rows: label 10 px weight 500, track 94 x 12 radius 6, thumb 14 x 14 with 2 px white border, confirming the catalogue.

## Verification
- `python tools/spec/assets_check.py`: `assets_check: ok (0 problems; 34 colours/theme, 430 reference images, 156 icons)`.
- Full run of `capture_gaps.mjs` ended with `captured 330 gap images, 140 widget measurements, 7 not captured, 0 scene failures` (the seven entries are the items of the table above; the script writes them into `notCaptured` of the measurements file).
- Viewed by eye (dark and light): `screen-toast-default` (default toast at the top centre with a check icon, plus a stray toolbar tooltip from the pointer), `screen-grid-settings-popover` (Canvas grid popover, two sm switches off, native inputs, first input focused), `screen-dialog-variables-add-menu` (dimmed overlay, dialog, Add menu with two-line items), `screen-tab-bar-two-documents` (36 px tab bar, inactive and active "Untitled" tab, close x, plus button, window pushed down), `screen-gradient-editor-stop-selected` (gradient type select, bar with two stops, second stop selected, stop list, colour area, sliders), `screen-binding-picker-open` (search row, "New number" item, create footer), `screen-context-submenu-open` (context menu with disabled items, purple "Create component", Copy/Paste as submenu), `screen-layer-tree-drag-child` (an early run, before the drag scene was removed; shows the bright selected row), plus contact sheets of widget crops: switch (off, hover, focus ring, on, on hover), toast default (blue) and error (red with three icons), tooltip "Pen (P)", default-tone inputs (focus ring, placeholder, filled with clear glyph), disabled and enabled accent buttons, checkboxes, layer rows (idle, hover with lock and eye actions, hidden, selected muted and bright, rename input), layer tree with collapsed frame, disclosure crops, constraints pins, alignment grid, tab crops, binding pill and picker, hue and alpha sliders, gradient stops and bar, menu items (hover, disabled, component tone, submenu), scrollbar panel. Everything matched the expected UI; the light set renders the same scenes with the light tokens.
- Chrome cleanup: the headless instances started by the scripts exit through `chrome.kill()`; the earlier interactive inspection session was stopped by process id after checking `--headless` and the debugging port.

## Unresolved
- Drag states of the layer tree stay source-derived.
- The software-rendered text anti-aliasing can differ from a GPU machine by small noise (same caveat as slice 1.3).
- The tooltip in `screen-toast-default` is a side effect of the pointer position after the menu click (it is genuine app output, not removed).

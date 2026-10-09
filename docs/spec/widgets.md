# Widget catalogue (Phase 1, slice 1.2)

Source: the OpenPencil UI at commit `dd3161e44`, read from its theme definitions (`src/theme/*.ts`, `src/components/ui/*.ts`) and measured in the real build with `tools/spec/capture.mjs` (`docs/spec/measurements.json`) and, for the states the first pass could not reach, `tools/spec/capture_gaps.mjs` (`docs/spec/measurements_gaps.json`, Phase 4 capture pass, see `Goal/evidence/P4_S00_capture.md`). Where a measured value differs from what this catalogue said before the second pass, the text says "measured X, catalogue said Y". Only visual values and behavior are recorded here; no source code is reused. All pixel values are logical pixels at 100% scale; the Tailwind unit is 4 px (`p-1` = 4, `px-2` = 8, `size-3` = 12, `h-7` = 28). Colours are token names from `assets/theme/tokens.json` (`themes.dark` / `themes.light`); `/N` means the colour at N% alpha.

Reference images: `tests/reference/openpencil/{dark,light}/` (`screen-*` full screens at 1440x900, `widget-<name>-<state>.png` crops with 6 px padding; the manifest marks second-pass entries with `"set": "gaps"`). Status: **ref** = reference crops/screens and measurements exist, **gap** = could not be captured (reason in section 7). Colour values below are dark / light token values from `assets/theme/tokens.json`; measured RGB values were mapped to the token whose value they equal.

## 1. Global rules

| Rule | Value |
|---|---|
| UI font | Inter, body 13 px, weights 400/500/600 (see 1.1 note on synthesized weights) |
| Text sizes in use | 9, 10, 11, 12 (`text-xs`, line height 16), 13 (body), 14 (`text-sm`, line height 20), 16, 18, 24 |
| Control height | 26 px (`space.control`); field radius 4 px (`radius.panel`) |
| Panel horizontal padding | 12 px; section header rows 26 px; field label 11 px muted above fields, 4 px gap |
| Transitions | Measured: 0.15 s, `cubic-bezier(0.4, 0, 0.2, 1)` on colour properties (`transition-colors`: color, background, border, outline, fill, stroke); switch thumb and layer disclosure transition `transform`/`translate` over 0.15 s; the tab close button transitions `opacity` over 0.15 s; hover on menu items, layer rows and text buttons has no transition |
| Focus ring | fields: 1 px border `panel-focus`; buttons/tabs: 1 px ring `accent` or `panel-focus`; never a thick outline |
| Hover | fields: `panel-field` -> `panel-field-hover`; ghost buttons: transparent -> `hover` with text `muted` -> `surface` |
| Disabled | Measured: accent buttons only drop to opacity 0.5 and keep the pointer cursor and the 90% accent hover fill (catalogue said "no hover change"); menu items turn `muted` at 50% alpha, cursor `default`, no hover fill; panel fields 60% opacity with `not-allowed` (source, not reachable) |
| Scrollbars | Measured (`scrollbar-thin` on the design panel): `::-webkit-scrollbar` 6 x 6 px, track transparent, thumb `border` (#3a3a3a / #d8dce3) radius 3 px, thumb hover `muted` (stylesheet value; the hover crop shows the pointer over the thumb); Firefox `scrollbar-width: thin`. `scrollbar-none` hides it entirely (tab bar). The scrolled element's `offsetWidth - clientWidth` is 10 px (258 vs 248) |
| Selection / text | `user-select: none` application-wide except inputs |
| Mixed value | text `muted`, shows the word "Mixed" in value position (measured: X of two selected rectangles, field 114 x 26, hover fill `panel-field-hover`) |
| Bound (variable) value | label 11 px weight 500 `component` (#9747ff dark, #7c3aed light) inside a pill (padding 0 x 4, radius 4); the field keeps its `panel-field` look; cursor `ew-resize` (measured) |

Note (1.5): the DOM UI loads Inter Regular only; weights 500 and 600 in headings and tabs are browser-synthesized bold with Regular advance widths. The toolkit must reproduce either the same synthesized look or choose real Medium/SemiBold and accept slightly different widths; decision belongs to slice 3.4.

## 2. Primitives

### 2.1 Button (`ui/button`)
Variants: tone `ghost` (transparent, `muted`, hover `hover`/`surface`), `accent` (`accent`, white, hover accent at 90%), `panel` (`panel` at 70% with backdrop blur 24 px, `surface`), `panelAccent` (like panel, text `accent`, border accent at 20%). Shapes: square r4, rounded r6, pill. Sizes: `sm` 28 x (8 px side padding, 12 px text), `md` 32 (12 px side padding), `icon` 32x32, `iconSm` 28x28. Optional 1 px border white at 10%. Measured: Share button 77x28, `accent`, 12 px weight 500, radius 6, hover = accent at 90%. Second pass, in the share popover: accent button 262 x 32 (12 px weight 500, gap 6, radius 4) and small accent button 48 x 28 (12 px, padding 12 x 6); disabled state = opacity 0.5 only, the hover fill and pointer cursor stay (measured; catalogue said no hover change). Neutral text button (variables dialog "Create collection", 120 x 28; "Add", 81 x 28): bg `hover` (#353535 / #eef1f5), text `surface` 12 px, padding 12 x 6, radius 4, hover bg `border` (#3a3a3a / #d8dce3). Status: ref (share-button, accent enabled/disabled, text-button).

### 2.2 Icon button (`ui/icon-button`)
`sm` 20x20 radius 4, 14 px glyph; `md` (panel) 26x26 radius 4. Transparent, `muted` glyph; hover bg `hover`, glyph `surface`; focus border `panel-focus`; active border and glyph `accent`; disabled 50% opacity. Measured md: 26x26, hover bg #353535. Status: ref (icon-button-panel, icon-button-small, section-add-button).

### 2.3 Text input (`ui/input`)
Tone `default`: radius 4, 1 px border `border`, bg `input`, text `surface`, focus border `accent`. Tone `panel`: field style (26 px high, bg `panel-field`, transparent border, hover `panel-field-hover`, focus border `panel-focus`). Sizes `sm` (11 px text, padding 8x4) and `md` (12 px). States: idle, mixed (`muted`), bound (`component`), invalid (border `danger`), disabled. Tabular numerals.
Measured (default tone): `md` 26 x (12 px text, line height 16, padding 8 x 4, 1 px border) with the share popover width 262 and 208 beside a button; `sm` (assets search) 242 x 26.5 (11 px text, line height 16.5; so the `sm` input is 0.5 px taller than `md`). Border `border` (#3a3a3a / #d8dce3), bg `input` (#1e1e1e / #ffffff), text `surface`, cursor `text`; hover: no change (catalogue implied none); focus: border `accent` (#3b82f6 / #2563eb), outline none, no ring; the focused value is selected text with a caret; a `type=search` input shows the native clear glyph once it has text. A third, ad-hoc input style exists in the grid popover (native `type=number`, 111 x 32, 14 px text, radius 6, bg `panel`, same border/focus colours). Disabled accent buttons are in 2.1.
Status: ref (default md and sm: idle, hover, focus, filled; popover number input); gap: mixed, bound, invalid and disabled states of the default tone (no screen of the default build uses them, section 7). The hex field in the paint field is a panel input.

### 2.4 Number field (`ui/number-field`)
Root 26 px high, `panel-field` background, radius 4, flex row: optional leading glyph/label (centered, 5 px side padding, `muted`), value text 12 px `surface`, optional suffix (`%`, `deg`) `muted` with 6 px right padding. Cursor `ew-resize` (drag-scrubs the value); clicking enters edit mode with a text caret (cursor `text`) and focus border. Half-width fields are 114 px wide in a 234 px panel (gap 6). Mixed shows "Mixed" in `muted`. Variable-capable fields show a small "apply variable" icon button (20x20, radius 4, `muted` -> `surface` on hover, 0.15 s colour transition; measured) inside the right edge. Measured mixed field: 114 x 26, bg `panel-field` (#333), hover `panel-field-hover` (#3b3b3b), cursor `ew-resize`; the bound field keeps its geometry and shows the binding pill (section 5.4). Interaction rules (from the app): first value mutation starts a detach/edit transaction; focusing never detaches. Measured: 114x26, bg #333, hover #3b3b3b. Status: ref (idle, hover, focus, editing screen).

### 2.5 Segmented control (`ui/segmented-control`)
Container: field style, 2 px padding, 2 px gap, radius 4. Items: 22 px high, radius 2, `muted` text, hover `hover`/`surface`, selected (`data-state=on`) bg `panel-selected-muted` (#344054 dark) text `surface`; sizes sm (11 px, side padding 6) and md (12 px, 8). Measured File/Assets control: items 117x22. Status: ref.

### 2.6 Select / combobox (`ui/select`, `AppSelect`)
Trigger: field style 26 px, text 12 px, padding 6, chevron right; value truncates. Content: radius 6 (md) or 8 (lg), 1 px border `border`, bg `panel`, shadow lg, padding 2 (sm) or 4 (md), min width = trigger width, z above panels; item: radius 4, vertical padding 6, left padding 24 with a check indicator at 6 px, hover `hover`; `max-height` 224 px scrolls. Grouped variant: 11 px items, group labels 10 px `muted`, separators 1 px `border` with 4 px margin. Status: ref (trigger idle/hover, open list screen).

### 2.7 Switch (`ui/switch`)
Pill with 1 px border `border`, bg `panel-field`; checked: border and bg `accent`, thumb white. Sizes: sm 28x16 (thumb 12, travel 12), md 36x20 (thumb 16, travel 16); 2 px padding; mixed: bg accent at 20%, border accent at 60%, thumb `accent` centered-left. Transition 150 ms.
Measured (grid popover, `AppSwitch` sm, the only size used by a reachable screen): 28 x 16, padding 2, border 1 px `border` (#3a3a3a / #d8dce3), bg `panel-field` (#333 / #f0f1f3), thumb 12 x 12 circle in `muted` (#888 / #6b7280) with a small shadow (shadow-sm); on: bg and border `accent` (#3b82f6 / #2563eb), thumb white; focus (keyboard): 1 px ring `panel-focus` (#4c8dff / #2563eb) as box-shadow, no outline; hover: no visible change (measured; the `hover:border-border-strong` class refers to a token that the stylesheet does not define); transitions 0.15 s `cubic-bezier(0.4, 0, 0.2, 1)` on the colour properties and 0.15 s on the thumb translation. The label sits left of the switch in 14 px `surface` text, row gap 12.
Status: ref (off idle/hover/focus, on idle/hover); gap: mixed and md (not used by any reachable screen, section 7).

### 2.8 Tooltip (`ui/tooltip`)
Radius 6, 1 px border `border`, bg `panel`, padding 8x4, text 12 px `surface`, shadow lg. Appears after a hover delay of 400 ms.
Measured: 26 px high (12 px / 16 line + 4 + 4 padding + 2 border), width = text + 18; border `border` (#3a3a3a / #d8dce3), bg `panel`, text `surface`; shadow `0 10px 15px -3px rgb(0 0 0 / .1), 0 4px 6px -4px rgb(0 0 0 / .1)`; radius 6; placed centred above the trigger with a 3 px gap (toolbar pen button at y 847, tooltip bottom at y 844). Open delay measured by polling after the pointer landed: 412 ms dark and 416 ms light (measured about 410 ms, catalogue said about 700 ms; source constant 400 ms plus event latency). Variants captured: toolbar button with shortcut text ("Pen (P)", the shortcut is part of the label), panel icon button ("Flip horizontal"), tab close ("Close Untitled"); all share one style, there is no separate shortcut styling.
Status: ref (toolbar screen, `tooltip-*` crops, `screen-tooltip-toolbar-shortcut`).

### 2.9 Menu / context menu (`ui/menu`)
Content: radius 8, 1 px border, bg `panel`, padding 4, shadow lg. Item: padding 8x6, radius 6, 12 px, gap 8, justify-between with min gap 24 to the shortcut; shortcut 11 px `muted`; icon 12 px `muted`; highlighted bg `hover`; disabled text `muted` at 50%; separator 1 px `border` with 4 px margins; tone `component` colours items with `component` (highlight at 12%). Sub-menus open to the side.
Measured: item 28 high (12 px / 16 line, padding 6 x 8, radius 6), 214 wide in the 224 px canvas context menu (content padding 4, border 1, radius 8); gap 24 between label and shortcut (gap 8 for items with a leading icon, e.g. page "Delete"); highlight/hover bg `hover` (#353535 / #eef1f5); disabled: colour `muted` at 50% alpha (colour alpha, not element opacity), cursor `default`, no hover fill; component tone ("Create component"): colour `component`, hover bg `component` at 12%; submenu trigger shows a chevron, and while its submenu is open it keeps the `hover` fill; submenu opens to the right with the same content style (content 190 wide, aligned to the end of the trigger). Described items (Add variable menu): 182 x 41.3, title 12 px plus a second 11 px `muted` line, gap 8; menubar menus and the toolbar flyout use the same item (flyout items 128 x 28, content 138 x 66). Shadow: measured the canvas context menu uses `0 8px 30px rgb(0 0 0 / .4)` (the overlay elevation, section 2.13), while the toolbar flyout and the Add variable menu use shadow lg (`0 10px 15px -3px rgb(0 0 0 / .1)`); the catalogue said shadow lg for every menu. Checkbox items (View menu "Performance profiler" etc.) have the same box (198 x 28) and no check mark when off.
Status: ref (File menu, context menu, items: idle, hover, disabled, component tone, submenu closed/open, checkbox, described, flyout).

### 2.10 Popover (`ui/popover`)
Radius 8, 1 px border, bg `panel`, shadow xl, high z-index; no built-in padding (content decides). Measured: grid settings popover 256 x 183, radius 8, border `border`, bg `panel`, padding 12, shadow `0 20px 25px -5px rgb(0 0 0 / .1), 0 8px 10px -6px rgb(0 0 0 / .1)` (xl), opens above its toolbar button with an 8 px offset. Status: ref (fill popover, color picker, grid settings, share, binding picker).

### 2.11 Dialog (`ui/dialog`)
Overlay black at 50% covering the window; content centered, radius 12, 1 px border, bg `panel`, shadow 2xl; title 14 px weight 600, description 12 px `muted`.
Measured (variables dialog): overlay 1440 x 900 at (0, 0), `rgb(0 0 0 / .5)` in both themes (it dims the light theme too, the content stays `panel` white); content 800 x 675 at (320, 113) (centred on the 1440 x 900 window), radius 12, border 1 px `border`, bg `panel` (#2a2a2a / #ffffff), shadow `0 25px 50px -12px rgb(0 0 0 / .25)`; empty-state header: padding 16 x 12, bottom border, title (`h2`, 99 x 20 at (337, 127)) 14 px / 20 weight 600 `surface` (measured, matches the catalogue), close button 24 x 24 radius 4 (`muted` -> `surface` with bg `hover` on hover); with a collection the header becomes a tab strip (collection tab 104 x 24, bg `hover`, radius 4, 12 px text, padding 10 x 4), a search field (inner input 96 x 16, 12 px, transparent) with a magnifier, a folder button and the close button; table header row, variable rows 33 px high (bottom border `border` at 30%, hover bg `hover` at 50%, delete cross on hover), footer with the Add button (81 x 28, chevron) and the hint text "Create variable" in `muted`.
Status: ref (screens: empty, empty table, add menu, table with rows; crops: overlay, content, close, tab, search, row, buttons).

### 2.12 Toast (`ui/toast`)
Max width 384, padding 10x6, radius 6, text 12 px, shadow md; tones default (accent, white), warning (warning bg/border/text tokens), error (red-600, white); enter/exit slide 4 px with fade.
Measured: default toast ("Copied as node ID") 142 x 28 at (649, 8): top centre of the window, 8 px from the top edge, padding 10 x 6, gap 6, radius 6, 12 px / 16 text white on `accent` (#3b82f6 / #2563eb), leading check icon 12 px, shadow `0 4px 6px -1px rgb(0 0 0 / .1), 0 2px 4px -2px rgb(0 0 0 / .1)` (md); error toast 291 x 30: same box on red-600 (#dc2626), text white, with a warning triangle, a copy button and a close button added at the right (not in the catalogue); lifetimes 3 s default, 10 s error (source); the toast text is selectable.
Status: ref (default, error; screens `toast-default`, `toast-error`); gap: warning tone (section 7).

### 2.13 Surface helper
Border `border`, bg `panel`; radius md/lg/xl (6/8/12); elevation md/lg/xl/overlay (overlay = 0 8 30 black at 40%); padding none/4/8/12. Used by floating bars.

## 3. Panel anatomy (properties panel)

| Part | Spec |
|---|---|
| Panel header (selection name) | 258 x 43 at the top: padding 12x8, icon 14, title 13 px weight 600 truncated, trailing action icon button |
| Section | header row 26 px: title 11 px weight 600 `surface`, optional trailing actions (icon buttons 26x26); content padding 12 sides; sections separated by 1 px `border` |
| Field group | label 11 px `muted` above (4 px gap), control below; group min-width 0 |
| Grid | `two-rail` = two 114 px columns plus a 26 px rail; gap 6 |
| Rail | 26 px column for icon buttons beside fields |
| Item row | row inside a section, fixed 26 px control height |
| Paint field | 26 px, bg `input`, 1 px border, radius 4: swatch (20x20) + hex text + divider + opacity field (56 px) + binding button |
| Page list row | 24 px high, padding 8x4, gap 6; active bg `hover`; icon 12 |

Status: ref (full design panel in `screen-rectangle-selected`, crops: panel-header, panel-section-title, panel-field-label, paint-field). The second pass adds the constraints and auto layout sections (`screen-constraints-selected`, `screen-auto-layout-section`) and the overflowing design panel with its scrollbar (`screen-scrollbar-design-panel`).

## 4. Document chrome

### 4.1 Tab bar (`tab-bar`)
Height 36, bottom border, bg `canvas`; triggers: padding 12, 12 px text, max 192 wide, right border; active bg `panel`/`surface` text, inactive `muted`; close button 16 px shown on hover/active; "new" action 36x36. Shown only with two or more documents (File > New).
Measured: root 1440 x 36, bg `canvas` (#1e1e1e / #f3f4f6), bottom border `border`; tab 109 x 35 for the label "Untitled" (35 because the root's 1 px bottom border is inside the 36), padding 0 x 12, gap 6, 12 px / 16, right border `border`; leading file icon 12 px at 50% opacity; active: bg `panel` (#2a2a2a / #ffffff), text `surface`; inactive: no fill, text `muted`, hover = text `surface` only (no fill); transitions 0.15 s on the colours; close button 16 x 16, radius 4, hover bg `hover`, icon 12 px, opacity 1 on the active tab and on a hovered tab, 0 otherwise (0.15 s opacity transition); new button 36 x 36, icon 14 px, `muted` -> `surface` on hover; its tooltip is a standard tooltip (2.8). The 36 px bar pushes the whole window down by 36 px.
Status: ref (screen `tab-bar-two-documents`; crops: bar, inactive tab, active tab, close, new).

### 4.2 Toolbar (`toolbar`)
Floating bar bottom center: buttons 32x32 radius 8 (mobile 6), icon 16, `muted`; active = bg `accent` + white; flyout trigger 12x32 beside tool groups; separators 1 px. Measured: active button 32x32 `accent`, r8; idle hover = bg `hover`. Status: ref (button crops, tooltip).

### 4.3 Menu bar
Items 24 px high, padding 8x4, radius 4, 12 px `muted`; hover `hover`/`surface`; opens a menu (2.9). Status: ref.

### 4.4 Tabs (Design / Code / third tab)
24 px high, padding 10x4, radius 4, 12 px; active weight 600 `surface`, inactive `muted`. Status: ref.

### 4.5 Layer tree (`layer-tree`)
Rows: flex, gap 4, padding 4 vertical and 4 right, radius 4, 12 px text `surface`; disclosure 16 px wide rotating 90 degrees when expanded; node icon 12 px (opacity 70%, `component` colour for component types); hover `hover`; selected `panel-selected` when the tree has focus, `panel-selected-muted` otherwise; hidden nodes 50% opacity; row actions (4 px icons) appear on hover; drag: dragged row 30% opacity, drop indicators 2 px `accent` line above/below or a rounded accent outline for child drop; inline rename input (border `accent`, bg `input`).
Measured: row 250 x 24 in the 258 px panel (padding 4 vertical, 4 right, left padding 16 per nesting level: 0, 16, 32, gap 4, radius 4, 12 px / 16 text); hover bg `hover` (#353535 / #eef1f5); selected without tree focus bg `panel-selected-muted` (#344054 / #e5e7eb); selected with tree focus bg `panel-selected` (#0d64d8 / #dbeafe), reached after an inline rename closes (a plain pointer click leaves the focus flag off in this build), hover keeps the selected fill; hidden layer: row opacity 0.5; disclosure button 16 x 12 (the rotated expanded chevron has a 12 x 16 bounding box), `muted` -> `surface` on hover, 0.15 s transform transition, collapsed = chevron pointing right; row actions (lock, eye): 16 x 16, radius 4, 12 px icons, visible at opacity 0 -> 1 on row hover, action hover bg `white` at 15%; inline rename: row 26 high with a 12 px icon, input 198 x 18 (border 1 px `accent`, bg `input`, radius 4, 12 px, padding 0 x 4), text preselected. Catalogue values confirmed; the focused-selected fill and the 0.5 hidden opacity were not captured before.
Status: ref (idle, hover, selected focused / unfocused, hidden, disclosure expanded / collapsed, action hover, rename; whole tree idle / focused / collapsed). gap: drag states (dragged row, drop line, child outline): the row drag uses native drag and drop, which the headless capture could not start in the app (section 7); values stay source-derived.

## 5. Pickers and editors

### 5.1 Color picker
Popover with a solid/gradient/image tab strip (24 px icon tabs), a saturation/value square (about 222x140) with a round handle, hue slider and alpha slider (12 px high tracks, radius 6, thumb 14 px circle with 2 px white border; alpha track over a checkerboard of 8 px cells), numeric fields (hue, alpha %, RGB triple), mode select (RGB), eyedropper button, and a swatch row with add/save actions. Measured: hue and alpha rows 222 x 26 (gap 8): label 10 px weight 500 `muted` (16 px wide), track 94 x 12 with radius 6, thumb 14 x 14 circle with a 2 px white border and a small shadow, value field 56 px wide at the right; the hue track is the rainbow gradient, the alpha track shows the checkerboard under the colour ramp (track fill `border`); tab strip tabs 24 x 24 radius 4 (active bg `hover` + text `surface`, inactive `muted`, hover = `hover`/`surface`, 0.15 s colour transition). Status: ref (`screen-color-picker-open`, `screen-fill-picker-solid`, slider and tab crops), gap (HSL/HEX modes, eyedropper).

### 5.2 Gradient editor (`fill-picker`)
Gradient bar with draggable stop handles (14 px squares, 2 px border white at 60%, white when active), stop list rows (hover bg `hover` at 50%), angle/position fields.
Measured (`screen-gradient-editor`): type select ("Linear", 26 px high) above the bar; bar 222 x 24, radius 4, gradient fill; stop handles 14 x 14 with radius 4 (the catalogue said squares; the theme's `rounded-sm` is 4 px), 2 px border white at 60% (inactive) or white (active), fill = the stop colour, small shadow, cursor `grab` (`grabbing` while dragged); "Stops" header with a 16 x 16 add button (`muted` -> `surface`); stop list rows 222 x 30 (26 px fields, padding 2 vertical, gap 4): position field with `%`, 16 px swatch with 1 px border, 11 px monospace hex input (bg `input`, 1 px `border`, radius 4), opacity field; the active row has bg `hover` at 50% and radius 4, inactive rows are transparent and show no hover tint; below the stops the saturation/value area and sliders from 5.1 follow, so the popover grows to about 520 px and moves up. Dragging a stop (button held) was captured (`widget-gradient-bar-dragging`).
Status: ref.

### 5.3 Fill swatch / checkerboard
Swatch radius 4, 1 px `border`, over a checkerboard (`checkerboard` + `checkerboard-muted`, 8 px cells in a 4 px offset pattern) so transparency is visible. Status: ref (swatch in paint field).

### 5.4 Binding (variable) field
Pill with `component` coloured label (11 px weight 500); trigger icon button 20x20; picker popover 224 wide, radius 8, search row 26 px, item rows 26 px (11 px text, radius 4), footer action rows.
Measured: pill 47 x 15 for the label "New number" truncated to "New ..." (padding 0 x 4, radius 4, label 11 px weight 500 `component`, cursor `ew-resize`, no hover change); trigger 20 x 20, radius 4, 1 px transparent border, `muted` -> `surface` on hover, `component` colour while the picker is open (source); picker 224 x 97 with one variable (radius 8, border `border`, bg `panel`, shadow xl as 2.10), search row 222 x 26 (11 px, bottom border `border`, focus border `panel-focus`), item row 214 x 26 (11 px / 16.5, padding 0 x 8, gap 8, radius 4, leading diamond icon 12 px `component`, hover `hover`), footer row 214 x 26 (11 px `muted` -> `surface`, hover `hover`, top border `border` above the footer, "Create number variable from 100").
Status: ref (screen `binding-picker-open`, `number-field-bound`; crops: picker, search, item, action, pill, trigger).

### 5.5 Other
Constraints diagram (72x64 box, pins, scale badge 9 px uppercase accent), layout-alignment 3x3 grid (cells 24x24, gap 2, active accent at 10% with accent border), variable table rows (border `border` at 30%, resize handle 4 px), status text (10 px, tones neutral/success/warning/error), collaboration avatars (24/28 px circles, 10 px initials) and presence pill (32 px, blur).
Measured: constraints diagram 72 x 64 (border 1 px `border`, radius 4, bg `panel-field`); pins are 16 x 28 (left/right bars) and 28 x 16 (top/bottom bars) buttons with a 1 px transparent border and radius 4; active pin: border `accent`, bg `accent` at 12%, glyph `accent`; inactive: glyph `muted`, hover bg `hover` + glyph `surface`; the two constraint selects (Left / Top) sit to the right of the diagram. Layout alignment grid 76 x 76 (3 cells of 24 plus gap 2), cell radius 4, border 1 px; inactive cell border `border`, dot `muted`, hover bg `hover` + dot `surface`; active cell border `accent`, bg `accent` at 10%, dot `accent`. "Clip content" is a native checkbox (13 x 13, `accent-color: accent`, 12 px `surface` label, gap 8, 8 px above). Variable table row 798 x 33 (see 2.11).
Status: ref (constraints diagram and pins, alignment grid and cells, checkbox, variable rows); gap: scale badge, table resize handle, status text, collaboration avatars/presence (not reachable or not needed, section 7), built only if the target applications need them.

## 6. Canvas furniture (not widgets, listed for the viewport work)
Rulers (24 px, bg `ruler-bg`, ticks `ruler-tick`, labels `ruler-text`, selection range badge `ruler-label` on accent), selection handles (white 8 px squares with accent border), canvas background `canvas`, page background colour from the document (#4d4d4d default).

## 7. Coverage summary
Captured by the first pass (`tools/spec/capture.mjs`): menubar item, segmented control, page row, tabs, share button, toolbar button (idle, active, hover), panel icon button, small icon button, number fields (idle, hover, focus), select trigger, paint field, panel header, section title, field label, selected layer row, add button; screens: empty, rectangle selected, File menu, select open, fill popover, color picker, context menu, toolbar tooltip, number field editing; both themes.
Captured by the second pass (`tools/spec/capture_gaps.mjs`, 165 images per theme, measurements in `docs/spec/measurements_gaps.json`): switch, dialog (overlay, content, header controls, table row), toast (default, error), default-tone text input (md, sm: idle, hover, focus, filled) and the popover number input, accent button disabled/enabled, neutral text button, document tab bar (bar, inactive and active tab, close, new), gradient editor (bar, stops, stop rows, add stop, drag), fill picker tabs, hue and alpha sliders, binding picker (popover, search, item, action, pill, trigger, bound and mixed number fields), layer tree (whole tree, row idle/hover/selected focused and unfocused/hidden/rename, disclosure, row action), constraints diagram and pins, layout alignment grid and cells, checkbox, tooltip variants, menu items (idle, hover, disabled, component tone, submenu, checkbox, described, flyout), menu and submenu content, scrollbar; 27 full screens per theme for the context around them.
Still not captured, and why:
| Item | Reason |
|---|---|
| Layer tree drag states (dragged row 30% opacity, 2 px drop line, child drop outline) | Rows use native HTML5 drag and drop. Headless Chrome starts the drag (the protocol reports a drag payload) but the app never enters its dragging state when enter/over events are replayed, so no indicator is drawn. Values stay source-derived (`theme/layer-tree.ts`). |
| Toast warning tone | Only raised when a pasted clipboard image cannot be resolved in the web build; no reachable UI path. Values stay source-derived (`--color-warning-*`). |
| Default-tone input invalid, mixed, bound, disabled | `AppInput` supports the states but no screen passes them; disabled is used only by the chat input while a provider streams (needs a configured chat provider). |
| Switch mixed state and md size | Mixed is only for boolean component properties of an instance; no screen uses md. |
| Disabled icon buttons and number fields | The default build never disables them; disabled appears on menu items, accent buttons (share popover) and the page "Delete" item, all captured. |
| Keyboard focus rings of tabs, tab bar items and toolbar buttons | Only the switch focus ring was driven through the keyboard (Tab); the rest follow the 1 px `accent`/`panel-focus` rule in section 1. |
| Constraints scale badge, variable table resize handle, status text, collaboration avatars and presence pill, HSL/HEX colour modes, eyedropper | Not needed by the first Phase 4 widgets; capture when a target application needs them. |
The images and numbers are regenerated with `node tools/spec/capture_gaps.mjs reference/OpenPencil/dist tests/reference/openpencil docs/spec/measurements_gaps.json`.

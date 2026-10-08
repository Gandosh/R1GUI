# Widget catalogue (Phase 1, slice 1.2)

Source: the OpenPencil UI at commit `dd3161e44`, read from its theme definitions (`src/theme/*.ts`, `src/components/ui/*.ts`) and measured in the real build with `tools/spec/capture.mjs` (`docs/spec/measurements.json`). Only visual values and behavior are recorded here; no source code is reused. All pixel values are logical pixels at 100% scale; the Tailwind unit is 4 px (`p-1` = 4, `px-2` = 8, `size-3` = 12, `h-7` = 28). Colours are token names from `assets/theme/tokens.json` (`themes.dark` / `themes.light`); `/N` means the colour at N% alpha.

Reference images: `tests/reference/openpencil/{dark,light}/` (`screen-*` full screens at 1440x900, `widget-<name>-<state>.png` crops with 6 px padding). Status column: **ref** = reference crops/screens exist, **gap** = no capture yet (to be captured when the widget is built in Phase 4).

## 1. Global rules

| Rule | Value |
|---|---|
| UI font | Inter, body 13 px, weights 400/500/600 (see 1.1 note on synthesized weights) |
| Text sizes in use | 9, 10, 11, 12 (`text-xs`, line height 16), 13 (body), 14 (`text-sm`, line height 20), 16, 18, 24 |
| Control height | 26 px (`space.control`); field radius 4 px (`radius.panel`) |
| Panel horizontal padding | 12 px; section header rows 26 px; field label 11 px muted above fields, 4 px gap |
| Transitions | 150 ms, ease `(0.4, 0, 0.2, 1)` on colour changes (`transition-colors`); opacity swaps are instant unless stated |
| Focus ring | fields: 1 px border `panel-focus`; buttons/tabs: 1 px ring `accent` or `panel-focus`; never a thick outline |
| Hover | fields: `panel-field` -> `panel-field-hover`; ghost buttons: transparent -> `hover` with text `muted` -> `surface` |
| Disabled | 50% opacity (60% for fields), `not-allowed` cursor, no hover change |
| Scrollbars | thin variant: 6 px, thumb `border` colour radius 3 px, thumb hover `muted`; or hidden |
| Selection / text | `user-select: none` application-wide except inputs |
| Mixed value | text `muted`, shows the word "Mixed" in value position |
| Bound (variable) value | text `component` (#9747ff dark, #7c3aed light) |

Note (1.5): the DOM UI loads Inter Regular only; weights 500 and 600 in headings and tabs are browser-synthesized bold with Regular advance widths. The toolkit must reproduce either the same synthesized look or choose real Medium/SemiBold and accept slightly different widths; decision belongs to slice 3.4.

## 2. Primitives

### 2.1 Button (`ui/button`)
Variants: tone `ghost` (transparent, `muted`, hover `hover`/`surface`), `accent` (`accent`, white, hover accent at 90%), `panel` (`panel` at 70% with backdrop blur 24 px, `surface`), `panelAccent` (like panel, text `accent`, border accent at 20%). Shapes: square r4, rounded r6, pill. Sizes: `sm` 28 x (8 px side padding, 12 px text), `md` 32 (12 px side padding), `icon` 32x32, `iconSm` 28x28. Optional 1 px border white at 10%. Measured: Share button 77x28, `accent`, 12 px weight 500, radius 6, hover = accent at 90%. Status: ref (share-button idle/hover).

### 2.2 Icon button (`ui/icon-button`)
`sm` 20x20 radius 4, 14 px glyph; `md` (panel) 26x26 radius 4. Transparent, `muted` glyph; hover bg `hover`, glyph `surface`; focus border `panel-focus`; active border and glyph `accent`; disabled 50% opacity. Measured md: 26x26, hover bg #353535. Status: ref (icon-button-panel, icon-button-small, section-add-button).

### 2.3 Text input (`ui/input`)
Tone `default`: radius 4, 1 px border `border`, bg `input`, text `surface`, focus border `accent`. Tone `panel`: field style (26 px high, bg `panel-field`, transparent border, hover `panel-field-hover`, focus border `panel-focus`). Sizes `sm` (11 px text, padding 8x4) and `md` (12 px). States: idle, mixed (`muted`), bound (`component`), invalid (border `danger`), disabled. Tabular numerals. Status: gap for default tone and states (the hex field in the paint field is a panel input).

### 2.4 Number field (`ui/number-field`)
Root 26 px high, `panel-field` background, radius 4, flex row: optional leading glyph/label (centered, 5 px side padding, `muted`), value text 12 px `surface`, optional suffix (`%`, `deg`) `muted` with 6 px right padding. Cursor `ew-resize` (drag-scrubs the value); clicking enters edit mode with a text caret (cursor `text`) and focus border. Half-width fields are 114 px wide in a 234 px panel (gap 6). Mixed shows "Mixed" in `muted`. Variable-capable fields show a small "apply variable" icon button (20x20) inside the right edge. Interaction rules (from the app): first value mutation starts a detach/edit transaction; focusing never detaches. Measured: 114x26, bg #333, hover #3b3b3b. Status: ref (idle, hover, focus, editing screen).

### 2.5 Segmented control (`ui/segmented-control`)
Container: field style, 2 px padding, 2 px gap, radius 4. Items: 22 px high, radius 2, `muted` text, hover `hover`/`surface`, selected (`data-state=on`) bg `panel-selected-muted` (#344054 dark) text `surface`; sizes sm (11 px, side padding 6) and md (12 px, 8). Measured File/Assets control: items 117x22. Status: ref.

### 2.6 Select / combobox (`ui/select`, `AppSelect`)
Trigger: field style 26 px, text 12 px, padding 6, chevron right; value truncates. Content: radius 6 (md) or 8 (lg), 1 px border `border`, bg `panel`, shadow lg, padding 2 (sm) or 4 (md), min width = trigger width, z above panels; item: radius 4, vertical padding 6, left padding 24 with a check indicator at 6 px, hover `hover`; `max-height` 224 px scrolls. Grouped variant: 11 px items, group labels 10 px `muted`, separators 1 px `border` with 4 px margin. Status: ref (trigger idle/hover, open list screen).

### 2.7 Switch (`ui/switch`)
Pill with 1 px border `border`, bg `panel-field`; checked: border and bg `accent`, thumb white. Sizes: sm 28x16 (thumb 12, travel 12), md 36x20 (thumb 16, travel 16); 2 px padding; mixed: bg accent at 20%, border accent at 60%, thumb `accent` centered-left. Transition 150 ms. Status: gap (not visible in the default document; capture in Phase 4).

### 2.8 Tooltip (`ui/tooltip`)
Radius 6, 1 px border `border`, bg `panel`, padding 8x4, text 12 px `surface`, shadow lg. Appears after hover delay (about 700 ms in the reference). Status: ref (toolbar tooltip screen).

### 2.9 Menu / context menu (`ui/menu`)
Content: radius 8, 1 px border, bg `panel`, padding 4, shadow lg. Item: padding 8x6, radius 6, 12 px, gap 8, justify-between with min gap 24 to the shortcut; shortcut 11 px `muted`; icon 12 px `muted`; highlighted bg `hover`; disabled text `muted` at 50%; separator 1 px `border` with 4 px margins; tone `component` colours items with `component` (highlight at 12%). Sub-menus open to the side. Status: ref (File menu, context menu).

### 2.10 Popover (`ui/popover`)
Radius 8, 1 px border, bg `panel`, shadow xl, high z-index; no built-in padding (content decides). Status: ref (fill popover, color picker).

### 2.11 Dialog (`ui/dialog`)
Overlay black at 50% covering the window; content centered, radius 12, 1 px border, bg `panel`, shadow 2xl; title 14 px weight 600, description 12 px `muted`. Status: gap.

### 2.12 Toast (`ui/toast`)
Max width 384, padding 10x6, radius 6, text 12 px, shadow md; tones default (accent, white), warning (warning bg/border/text tokens), error (red-600, white); enter/exit slide 4 px with fade. Status: gap.

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

Status: ref (full design panel in `screen-rectangle-selected`, crops: panel-header, panel-section-title, panel-field-label, paint-field).

## 4. Document chrome

### 4.1 Tab bar (`tab-bar`)
Height 36, bottom border, bg `canvas`; triggers: padding 12, 12 px text, max 192 wide, right border; active bg `panel`/`surface` text, inactive `muted`; close button 16 px shown on hover/active; "new" action 36x36. Status: gap (single-document default build shows none).

### 4.2 Toolbar (`toolbar`)
Floating bar bottom center: buttons 32x32 radius 8 (mobile 6), icon 16, `muted`; active = bg `accent` + white; flyout trigger 12x32 beside tool groups; separators 1 px. Measured: active button 32x32 `accent`, r8; idle hover = bg `hover`. Status: ref (button crops, tooltip).

### 4.3 Menu bar
Items 24 px high, padding 8x4, radius 4, 12 px `muted`; hover `hover`/`surface`; opens a menu (2.9). Status: ref.

### 4.4 Tabs (Design / Code / third tab)
24 px high, padding 10x4, radius 4, 12 px; active weight 600 `surface`, inactive `muted`. Status: ref.

### 4.5 Layer tree (`layer-tree`)
Rows: flex, gap 4, padding 4 vertical and 4 right, radius 4, 12 px text `surface`; disclosure 16 px wide rotating 90 degrees when expanded; node icon 12 px (opacity 70%, `component` colour for component types); hover `hover`; selected `panel-selected` when the tree has focus, `panel-selected-muted` otherwise; hidden nodes 50% opacity; row actions (4 px icons) appear on hover; drag: dragged row 30% opacity, drop indicators 2 px `accent` line above/below or a rounded accent outline for child drop; inline rename input (border `accent`, bg `input`). Status: ref (selected row), gap (hover, drag, rename).

## 5. Pickers and editors

### 5.1 Color picker
Popover with a solid/gradient/image tab strip (24 px icon tabs), a saturation/value square (about 222x140) with a round handle, hue slider and alpha slider (12 px high tracks, radius 6, thumb 14 px circle with 2 px white border; alpha track over a checkerboard of 8 px cells), numeric fields (hue, alpha %, RGB triple), mode select (RGB), eyedropper button, and a swatch row with add/save actions. Status: ref (`screen-color-picker-open`), gap (HSL/HEX modes, eyedropper).

### 5.2 Gradient editor (`fill-picker`)
Gradient bar with draggable stop handles (14 px squares, 2 px border white at 60%, white when active), stop list rows (hover bg `hover` at 50%), angle/position fields. Status: gap.

### 5.3 Fill swatch / checkerboard
Swatch radius 4, 1 px `border`, over a checkerboard (`checkerboard` + `checkerboard-muted`, 8 px cells in a 4 px offset pattern) so transparency is visible. Status: ref (swatch in paint field).

### 5.4 Binding (variable) field
Pill with `component` coloured label (11 px weight 500); trigger icon button 20x20; picker popover 224 wide, radius 8, search row 26 px, item rows 26 px (11 px text, radius 4), footer action rows. Status: gap.

### 5.5 Other
Constraints diagram (72x64 box, pins, scale badge 9 px uppercase accent), layout-alignment 3x3 grid (cells 24x24, gap 2, active accent at 10% with accent border), variable table rows (border `border` at 30%, resize handle 4 px), status text (10 px, tones neutral/success/warning/error), collaboration avatars (24/28 px circles, 10 px initials) and presence pill (32 px, blur). Status: gap, built only if the target applications need them.

## 6. Canvas furniture (not widgets, listed for the viewport work)
Rulers (24 px, bg `ruler-bg`, ticks `ruler-tick`, labels `ruler-text`, selection range badge `ruler-label` on accent), selection handles (white 8 px squares with accent border), canvas background `canvas`, page background colour from the document (#4d4d4d default).

## 7. Coverage summary
Captured: menubar item, segmented control, page row, tabs, share button, toolbar button (idle, active, hover), panel icon button, small icon button, number fields (idle, hover, focus), select trigger, paint field, panel header, section title, field label, selected layer row, add button; screens: empty, rectangle selected, File menu, select open, fill popover, color picker, context menu, toolbar tooltip, number field editing; both themes.
Not captured (gaps listed above): switch, dialog, toast, default-tone input and its states, tab bar documents, gradient editor, binding picker, layer tree hover/drag/rename, disabled and invalid states, constraints, variable table. Each gap is captured with a dedicated scene in `tools/spec/capture.mjs` when its widget is built.

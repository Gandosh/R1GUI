# Phase 4, group g1: Button, IconButton, Checkbox, Switch, Segmented (slices 4.1 and 4.4)

Evidence record. Machine: RTX 3090 (`R1UI_GPU="RTX 3090"`), Vulkan SDK 1.4.363.0. Trees: `build/p4-g1` (dev, release flags) and `build/p4-g1d` (Debug, validation on).

## What was built

| Widget | Files | Notes |
|---|---|---|
| Pressable (shared base) | `button/Pressable.h/.cpp` | press, release-inside click (Router click synthesis), re-arm on return while held, Space/Enter on key-up (no modifiers, no repeat, focus loss / disable cancels), pointer cursor, focus bit only for keyboard focus |
| Button | `button/Button.h/.cpp` | tones ghost, accent, panel, panelAccent, neutral; sizes sm 28, md 32, icon 32, iconSm 28; leading / trailing icon; rows `btn.<tone>`, `btn.size.<size>` |
| IconButton | `iconbutton/IconButton.h/.cpp` | sm 20, md 26, custom box and glyph size; active (accent border and glyph); hover fill only for md (measured); rows `iconbtn`, `iconbtn.sm|md` |
| Checkbox | `checkbox/Checkbox.h/.cpp` | 13 px native-look box, unchecked / checked / mixed, optional 12 px label; row `checkbox.label` |
| Switch | `switch/Switch.h/.cpp` | sm 28x16 and md 36x20 (sizes from tokens), off / on / mixed, 150 ms colour and thumb animation, thumb shadow-sm, focus ring outside the pill |
| Segmented | `segmented/Segmented.h/.cpp` | one widget, roving selection, items with text and/or icon, per-item enabled and tooltip, arrow / Home / End, single-select value API with change callback; rows `segmented.*` |
| Gallery | `button/GalleryButtons.h/.cpp` | `buildGalleryButtons(UiContext&, WidgetId parent)`, about 210 nodes, wrapping rows |

All behaviours: Tab focus, focus ring only for keyboard focus, disabled = 50% opacity and no input, tooltips (base `setTooltip`, per-item for Segmented), accessible names (text, icon name, selected item), 150 ms colour transitions (not for the neutral button, as measured).

## Foundation change (one)

`source/ui-widgets/src/label/Label.cpp`: `Label::measure` now returns `ceil(width)`. Reason: layout rounds box widths to whole pixels, and a label sized to its natural width could land a fraction of a pixel narrower than its text, so `paint()` truncated it with an ellipsis ("ghost icon + t..."). Seen in the gallery captions. Regression test: `tests/ui-widgets/button/button_test.cpp` `testLabelNaturalWidthNeverTruncates` (verified red without the change: 16 failures, green with it). My own widgets use the same ceil in their `measure()` and tolerate 1 px of overflow in paint.

## Tests (all in the worktree)

Fast: `button_test` (Pressable rules, sizes, hostile input, the Label regression), `iconbutton_test`, `checkbox_test`, `switch_test`, `segmented_test`, `gallery_test`. GPU: `*_visual_test` for the five widgets, `gallery_gpu_test` (renders the gallery in both themes to `artifacts/ui-widgets/gallery-buttons-<theme>.png`, asserts no validation message). Hostile cases: rapid input (500 to 1000 click pairs), destroy inside callbacks (pointer and keyboard), disabled while pressed (pointer and key), invalid icon names (`../`, extensions, UTF-8 bytes), NaN and infinite sizes, invalid UTF-8 and 200000 character labels with and without a max width, zero width, 64 and 65 items, no items, zero-size window, scales 1.25 to 3.

## Visual comparison (dev tree; failing pixels / max / mean channel difference)

Profiles: `text` (48 ch, 3%, luminance only), `icons` (32 ch, 2%), `screen` (32 ch, 3%). The checkbox and switch use `screen` because `default` (8 ch, 0.2%) is below the antialiasing noise of rounded shapes; this is stated in their test headers. `tolerance.json` is unchanged.

| Reference (dark and light both run) | dark | light |
|---|---|---|
| share-button idle / hover | 1.02% / 81 / 6.8 and 1.17% / 81 / 7.3 PASS | 1.71% / 159 / 7.5 and 1.53% / 145 / 7.7 PASS |
| text-button idle / hover | 1.49% / 92 / 3.4 and 1.39% / 90 / 3.5 PASS | 0.32% / 82 / 2.1 and 0.12% / 74 / 2.5 PASS |
| text-button-add idle / hover | **3.02%** / 171 / 4.4 FAIL (110 px vs limit 110.4... 111 failing) and 2.99% PASS | 2.96% / 208 / 4.0 and 2.55% PASS |
| button-accent idle / hover | 0.04% / 60 / 0.9 and 0.12% PASS | 0.21% / 71 / 1.1 and 0.13% PASS |
| button-accent-disabled idle / hover (hover rendered as disabled idle) | 0.00% / 36 / 0.5 and 0.00% / 37 / 3.1 PASS | 0.02% / 50 / 1.3 and 0.02% / 50 / 6.0 PASS |
| button-accent-disabled-small idle | 0.00% / 26 / 0.9 PASS | 0.00% / 43 / 1.8 PASS |
| segmented-control idle / hover | 1.14% / 134 / 2.0 and 1.14% / 134 / 3.3 PASS | 1.44% / 159 / 2.5 and 1.44% / 159 / 3.8 PASS |
| switch idle / hover / focus | 0.00% / 2 / 0.08, same, 0.00% / 11 / 0.38 PASS | 0.00% / 8 / 0.38, same, 0.00% / 9 / 0.57 PASS |
| switch on / on-hover | 2.14% / 41 / 1.3 PASS | 2.14% / 45 / 1.5 PASS |
| checkbox idle / hover | 0.00% / 16 / 0.4 and 0.00% / 24 / 0.6 PASS | 0.00% / 28 / 0.6 and 0.64% / 37 / 0.8 PASS |
| checkbox checked / checked-hover | **3.04%** / 93 / 2.5 and **3.20%** / 94 / 2.5 FAIL | **3.68%** / 109 / 2.9 and **4.00%** / 119 / 3.1 FAIL |
| icon-button-panel idle / hover | 0.00% / 24 / 1.0 and 1.45% / 43 / 1.3 PASS | 1.45% / 37 / 1.6 and 1.45% / 53 / 1.8 PASS |
| section-add-button idle / hover | 0.00% / 23 / 0.07 and 0.28% / 42 / 0.13 PASS | 0.28% / 37 / 0.13 and 0.28% / 53 / 0.18 PASS |
| icon-button-add-stop idle / hover | 0.00% / 5 / 0.13 PASS | 0.00% / 9 / 0.26 PASS |
| dialog-close-button idle / hover | 1.39% / 47 / 1.1 PASS and **3.40%** / 86 / 2.3 FAIL | **3.40%** / 74 / 1.8 and **3.40%** / 105 / 2.8 FAIL |
| icon-button-small and -variable-trigger idle / hover | **4.30%** / 64 / 2.3 and **6.25%** / 124 / 4.5 FAIL | **6.25%** / 99 / 3.6 and **6.25%** / 150 / 5.5 FAIL |

Three visual test executables therefore fail against their profile: `button_visual_test` (one image, 111 failing pixels, 0.016 points over), `checkbox_visual_test` (the checked crops), `iconbutton_visual_test` (small / variable-trigger and the close button). Nothing was loosened; the failing cases are the real numbers above.

### What visibly differs (viewed side by side at 5x to 16x)

- Text in all buttons: the reference is heavier (browser rendering of light text on dark, LCD fringes); ours is thinner by about one stem level, which is where the `text` failures sit (glyph edges, max 80 to 210). Glyph positions agree to 1 px (Share: text ink 39..70 reference, 38..71 ours). The "Add" button has the highest share of ink edges per pixel, so it lands at the limit.
- Share and accent buttons: the share icon and the layout match; the 0.5 px fractional crop origin of the text-button crops (y = 468.5) blurs the reference's top and bottom edge, ours is crisp.
- Small icon buttons: the nested-diamond glyph is the known icon rasteriser gap (guide section 6: ink ratio 1.045, mean difference 6.0 before widget context). The widget geometry, background and hover colours agree. The crop is on a number field: the test renders on `panel-field` / `panel-field-hover` and ignores the 3 px above and 4 px below the field.
- Dialog close: the x strokes are 1 px thicker at the ends in the reference (path multisampling vs our Msaa4), 44 pixels at edges.
- Checkbox: the reference check mark is a polygon with soft corners; ours is two strokes (`Painter::line`, butt caps). Box, fill, border and hover colours match to the level (idle crops 0 failing pixels). The reference box has faint halo pixels around the corners that ours lacks.
- Switch: pill and thumb edges differ by up to 45 levels on 24 edge pixels (the reference rasterises rounded edges more coarsely). Colours, thumb inset (3 px), travel (12 px), ring and shadow agree.
- Segmented: the reference "File" text is heavier and about 1 px lower; fills, radius and geometry agree. The crop shows one item of a 2 item control: the test uses a 240 px control at margin -2.

Viewed artifacts: share / text-button / add / accent / disabled-small (dark), icon-button-small idle and hover, dialog-close hover, switch on and focus, checkbox checked (dark and light), segmented (dark and light), and `gallery-buttons-dark.png` / `-light.png` (all under `build/p4-g1/artifacts/ui-widgets/`).

## Commands run (all from the worktree root)

- `tools\build\msvc_env.cmd cmake --preset dev -B build/p4-g1` (exit 0), `... cmake --build build/p4-g1` (exit 0), `ctest -L fast` in `build/p4-g1`: 44 of 44 passed. `ctest -L gpu`: 31 of 34 passed, the 3 failures listed above.
- `tools\build\msvc_env.cmd cmake --preset debug -B build/p4-g1d`, `... cmake --build build/p4-g1d` (exit 0): `ctest -L fast` 44 of 44 passed; `ctest -L gpu` 31 of 34 passed (same three). `gallery_gpu_test` asserts zero validation messages and passed in the Debug tree.

## Not implemented (and why)

- Backdrop blur of the panel tones (24 px blur): the Painter has no blur; the translucent fill is drawn (Approximate).
- Pressed look: the style sheet has no separate pressed colours in the catalogue; pressed shows the hover look (documented in Button.h).
- Pointer capture while pressed: not used. The Router synthesises the click only when the button is released over the widget, and `Pressable` re-arms the pressed look when the pointer returns, which gives the same visible behaviour without capture.
- Disabled-and-hovered look of the accent button (reference keeps the 90% fill while disabled): disabled widgets get no pointer, so the hover crop is compared with the disabled idle render (difference is the 10% fill change at 50% opacity, mean 3.1 and 6.0).
- Segmented hover on the selected item shows nothing extra (matches the reference); no hover transition for items beyond 32 slots is needed (cap 64 items uses 128 slots).
- Toolbar buttons (32 px, accent active, radius 8) and menu checkbox items: not in this group's list; the toolbar crops are not matched.
- Focus rings of Button, IconButton and Segmented follow the 1 px `panel-focus` rule of the catalogue (no reference crop exists); Checkbox focus draws the ring 1 px outside the box.

## Unverified

- Keyboard and pointer behaviour was verified through synthetic input in unit tests only; nothing was operated in a real window.
- Animation timing (150 ms) is exercised for settling only (switch test), not compared with the reference's timing.
- The check mark and thumb positions at fractional display scales (1.25, 1.5) are only checked for not throwing, not against references.
- Hover of the segmented control's non-selected items and the md icon-button hover fill have references only through icon-button-panel / section-add (both pass).

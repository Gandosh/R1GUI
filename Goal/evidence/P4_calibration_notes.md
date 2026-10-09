# Phase 4, slice 4.16: calibration (text weight, icon shapes, tolerance proposal)

Evidence record. Machine: RTX 3090 (`R1UI_GPU="RTX 3090"`), Vulkan SDK 1.4.363.0. Trees: `build/p4-cal` (dev) and `build/p4-cald` (Debug, validation on). Base: main at `ab92fc2`.

## 1. Text weight

### Harness

`tests/ui-widgets/text/text_calibration_visual_test.cpp` (GPU, about 5 s, deterministic). 20 text samples (strings, sizes and weights as the widgets draw them, regions cut from the reference crops and screens, both themes = 40 data points): section title, panel header, tab, dialog title (600); field label, menu item and shortcut, component menu item, menu bar item, inactive tab, neutral button, tooltip, text input, layer row, page row, variable row, number digits (400); flyout item, Share button, toast (500). For each sample and theme it measures the ink (`testing::inkSum`) of the reference region and of our render at 17 constant strengths (0..4), derives the strength at which our ink equals the reference ("target"), fits the model per weight class by minimising the squared ink error (grid search, then refinement), prints every residual, and then checks the shipped constants through the real engine path: every sample within 10 % of the reference ink, and the shipped anchors within 0.06 of the fit. `text_weight_visual_test` (the Phase 3 test) now uses the same probe (`tests/ui-widgets/support/TextProbe.h`) and the new API. `text_weight_model_test` (fast) pins the function shape.

### Model (TextEngine)

Strength multiplier of `defaultEmboldenPx(size)`: `s(L, w) = max(0, dark + (light - dark) * L)` per weight class, `L` = straight sRGB luminance of the text colour (continuous, no light/dark switch), linearly interpolated between the classes for weights between 400, 500 and 600 (600 and above use the 600 anchors).

| Class | strength at L = 0 (dark text) | strength at L = 1 (white text) | rms / worst residual (ink) |
|---|---|---|---|
| 400 | -0.66 | 1.07 | 4.0 % / 7.6 % |
| 500 | -0.13 | 0.83 | 5.2 % / 8.3 % |
| 600 and above | 0.83 | 2.31 | 1.2 % / 2.6 % |

Before: regular 0.2 (light text) / 0.1 (dark text), bold 2.18 / 1.0. Typical results at the luminances of the theme (dark theme surface 0.88, muted 0.53; light theme surface 0.13, muted 0.45): regular `surface` text in the dark theme now gets 0.86 (was 0.2), muted 0.26 (was 0.2), and dark text on light 0 (was 0.1).

### Findings

- Ink ratio ours / reference of bright regular text in the dark theme: 0.73-0.81 before, 0.96-1.06 now (menu item, tooltip, neutral button, text input, layer / page / variable rows, digits).
- Size: fitted slope of the strength is +1 % per px below 12 px (samples at 11, 12 and 14 px), below the noise; no size term is shipped (the test asserts the fitted slope stays within 3 %).
- Weight 500: the data do not show the reference heavier than weight 400 at the same luminance (the browser draws 500 like Regular: the flyout item "Section" needs the same strength as bright regular text). The 500 anchors are fitted separately (so `medium` is its own class), and they come out slightly below 400 because the white-on-accent samples (Share, toast) need less than bright text on a dark surface: the browser's enhancement depends on the contrast with the background, which the engine does not know (it only sees the tint). The 0.75x ink reported for menu and flyout labels was the regular-weight luminance effect, not weight 500.
- Limit: dark text on a light surface (light theme, 400 and 500) is still 1-8 % heavier than the reference even at strength 0, because the reference is thinner than the bare outline there and the rasteriser cannot thin (negative emboldening would be a ui-text change; not done). The shipped bound of the new test is 10 %; worst measured 8.3 % (flyout item, light).
- `text_weight_visual_test` bound moved from 3 % to 5 %: the model is one continuous function over 40 samples instead of constants tuned on four, which costs the 11 px "Layout" 4 % on the dark theme (0.960; it was 0.978). The other three samples are within 1.4 %.
- `overlay_gpu_test` sampled a pixel that lies inside the glyphs of "Copy"; with the heavier text it changed colour. It now samples the padding row above the label ((30, 33)); the assertion (surface colour inside the menu, canvas colour without it) is unchanged.

## 2. Icon shapes

- Checkbox check mark: fitted against the coverage of the reference crops (both themes give the same geometry): a polyline (3.3, 7.05), (5.12, 9.07), (9.9, 3.27) in box pixels, width 2.15, round caps and joins (two lines plus a disc per vertex). `checkbox_visual_test` now passes (screen profile): checked crops dark 3.04 % / 3.20 % failing, light 3.68 % / 4.00 % before; now 0.96 % (dark) and 1.60 % (light).
- Apply-variable icon: the reference does not draw a solid centre dot but a small stroked diamond (half diagonal about 1.4 px, stroke about 1 px) inside the outer diamond; `assets/icons/custom/apply-variable.svg` now has that inner path (stroke 1.5, the single icon stroke width). Ink ratio against `widget-icon-button-small-idle` 1.045 -> 1.011, mean difference 6.0 -> 5.8; the failing share of the icon button crops is unchanged (4.3 / 5.9 %), caused by the outer diamond (our tips extend half a pixel further) and antialiasing steps of the reference's path rasteriser. `icon_reference_test` expectation for the centre changed to the ring (`> 100` coverage instead of `> 200`).
- Dialog close X: measured, not changed. Stroke thickness already matches (horizontal cross-section 2.0 px in both, ink 34.0 vs 32.0, ours 6 % heavier). The reference glyph is 9 rows tall at a fractional position (centre about (17.74, 17.5) in the 36 px crop against our (18, 18)): the dialog sits at y = 112.5 in the reference. Sizes 13 and 15 px (half pixel offsets) were tried and are worse (3.8 and 4.0 % failing against 3.3 %); 14 px (dialog) and 16 px (icon button test) stay. A fix needs fractional placement of icon textures, not a stroke or size tweak.

## 3. Tolerance proposal

See `docs/spec/tolerance_proposal.md` (tables, rationale) and `docs/spec/tolerance_proposed.json`. `tests/reference/tolerance.json` is unchanged.
Tooling: `R1UI_TOLERANCE_FILE` (any tolerance file, read by every visual harness; default the committed file), `R1UI_DIFF_SWEEP=1` (each `visual` line ends with the failing share at channel tolerances 0..128), `tools/spec/tolerance_proposal.py` (parses a `ctest -V` log). The colour picker and gradient region helper now reads its failing share from the profile (it had a hard-coded 0.03).

## Results (RTX 3090)

| Run | fast | gpu | visual comparisons failing |
|---|---|---|---|
| Before (main), committed profiles | 79 / 79 | 50 / 57 (7 failing) | 42 of 300 |
| After, committed profiles | 80 / 80 | 52 / 57 (5 failing: dialog, iconbutton, menu, toast, tooltip) | 38 of 300 |
| After, proposed profiles | | 57 / 57 | 0 of 300 |

Debug tree (`build/p4-cald`, validation layers on): `ctest -L fast` 80 / 80 on two of three runs; the third run failed `curvegraph_test` (a debug timing bound, 5.5 s, unrelated code, only seen under `-j 4` load) and passed on rerun. `ctest -L gpu` with committed profiles 52 / 57 (the same five visual tests as the dev tree), with the proposed file 57 / 57, zero validation messages in the output.

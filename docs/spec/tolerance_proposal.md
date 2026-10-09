# Tolerance profile proposal (Phase 4 calibration)

Status: PROPOSAL. `tests/reference/tolerance.json` is not edited; the owner approves tolerance changes. The proposed file is `docs/spec/tolerance_proposed.json`.

## How to run the suite against it

```
set R1UI_TOLERANCE_FILE=<repo>\docs\spec\tolerance_proposed.json
set R1UI_DIFF_SWEEP=1          (optional: every `visual` line ends with the failing share at channel tolerances 0..128)
ctest -L gpu -j 4 -V
python tools/spec/tolerance_proposal.py <log> [--exclusions file] [--write-json file]
```

## Result

| Run (dev tree, RTX 3090, 300 visual comparisons) | passing | failing |
|---|---|---|
| Committed profiles, before the calibration changes | 258 | 42 |
| Committed profiles, after text weight and icon fixes | 262 | 38 |
| Proposed profiles, after the fixes | 300 | 0 (`ctest -L gpu`: 57 / 57) |

## Proposed profiles

| Profile | current | proposed | comparisons | why |
|---|---|---|---|---|
| default | 8 ch / 0.2 % | 8 ch / 0.5 % | 4 (scrollbars) | channel tolerance already sits at the knee (0.01 % above 8); share rounded up to the next 0.5 % |
| flat | 2 ch / 0 % | unchanged | none | no visual test uses it |
| text | 48 ch / 3 % | 72 ch / 6 % | 214 | luminance compare; failures are 1 px offsets of glyph runs and LCD vs grayscale edges, see below |
| icons | 32 ch / 2 % | 24 ch / 6 % | 40 | Msaa4 differs from the reference's path multisampling by two sample steps on edge pixels (max 99..150); 5.9 % is the small apply-variable button, 3.4 % the dialog close X |
| screen | 32 ch / 3 % | 16 ch / 4 % | 36 | shapes with rounded edges (switch 2.1 %, dialog X 3.3 %, gradient editor region 2.4 %); a lower channel tolerance is possible because edge pixels of rounded shapes differ by 8 to 16 levels only |

Method: for every comparison the harness counts, at each channel tolerance t (steps of 8), the pixels whose largest channel difference exceeds t. The frontier of a profile is the largest such share over its comparisons; the proposal is the first t after which raising the tolerance by two more steps saves no more than one share step (0.5 %), with the share rounded up to the next 0.5 %. All 294 distinct comparisons were included: the worst cases by failing share were viewed side by side with the reference (toast error, tooltip toolbar shortcut, tooltip panel icon button, flyout item, menu bar item hover; the other worst ten are the same widgets in the other theme or state) and are visual matches whose differences are a glyph run or icon offset of about one pixel, slightly heavier or lighter strokes, or antialiasing steps; none of the viewed ones shows a wrong colour, missing element or wrong layout, so none is excluded.

## Comparisons that remain visibly different (even though they pass under the proposal)

- Tooltips: our box is 1 px wider and the text starts about 0.7 px further right than the reference ("Flip horizontal": 110 vs 109 px); text edges then differ at 5 to 7 % of the pixels at 48 channels.
- Toast, flyout and menu bar items: text and leading icon sit about 1 px away from the reference position (toast text 1 px low, flyout icon 1 px right).
- Light theme regular text is 1 to 8 % heavier than the reference (rasteriser cannot thin).
- Small icon buttons (apply-variable): outer diamond 0.5 px larger at the tips.
- Dialog close X: reference glyph sits at a fractional position (0.26 px left, 0.5 px up).
Fixing these is widget geometry work (tooltip and toast metrics, fractional icon placement), not tolerance work; the proposal does not hide them: they pass only because the text and icons budgets allow 6 % of the pixels.

## Every comparison against its current profile

| comparison | theme | profile | failing | max | mean | verdict | counted |
|---|---|---|---|---|---|---|---|
| scrollbar-thumb-hover-light | light | default | 0.01% | 20 | 0.10 | PASS | yes |
| scrollbar-idle | dark | default | 0.00% | 2 | 0.00 | PASS | yes |
| scrollbar-thumb-hover | dark | default | 0.00% | 8 | 0.01 | PASS | yes |
| scrollbar-idle-light | light | default | 0.00% | 6 | 0.00 | PASS | yes |
| widget-icon-button-small-hover | dark | icons | 5.86% | 124 | 4.38 | FAIL | yes |
| widget-icon-button-small-idle | light | icons | 5.86% | 99 | 3.51 | FAIL | yes |
| widget-icon-button-small-hover | light | icons | 5.86% | 150 | 5.32 | FAIL | yes |
| widget-icon-button-variable-trigger-hover | dark | icons | 5.86% | 124 | 4.38 | FAIL | yes |
| widget-icon-button-variable-trigger-idle | light | icons | 5.86% | 99 | 3.51 | FAIL | yes |
| widget-icon-button-variable-trigger-hover | light | icons | 5.86% | 150 | 5.32 | FAIL | yes |
| widget-icon-button-small-idle | dark | icons | 4.30% | 64 | 2.27 | FAIL | yes |
| widget-icon-button-variable-trigger-idle | dark | icons | 4.30% | 64 | 2.27 | FAIL | yes |
| widget-dialog-close-button-hover | dark | icons | 3.40% | 86 | 2.25 | FAIL | yes |
| widget-dialog-close-button-idle | light | icons | 3.40% | 74 | 1.77 | FAIL | yes |
| widget-dialog-close-button-hover | light | icons | 3.40% | 105 | 2.82 | FAIL | yes |
| widget-icon-button-panel-hover | dark | icons | 1.45% | 43 | 1.28 | PASS | yes |
| widget-icon-button-panel-idle | light | icons | 1.45% | 37 | 1.59 | PASS | yes |
| widget-icon-button-panel-hover | light | icons | 1.45% | 53 | 1.84 | PASS | yes |
| widget-dialog-close-button-idle | dark | icons | 1.39% | 47 | 1.12 | PASS | yes |
| widget-fill-picker-tab-hover | light | icons | 0.77% | 49 | 2.34 | PASS | yes |
| widget-fill-picker-tab-active | light | icons | 0.77% | 56 | 1.30 | PASS | yes |
| widget-gradient-stop-inactive-idle | dark | icons | 0.59% | 41 | 0.96 | PASS | yes |
| widget-gradient-stop-inactive-selected | dark | icons | 0.59% | 39 | 1.12 | PASS | yes |
| widget-fill-picker-tab-hover | dark | icons | 0.46% | 41 | 1.72 | PASS | yes |
| widget-fill-picker-tab-active | dark | icons | 0.31% | 45 | 1.04 | PASS | yes |
| widget-section-add-button-hover | dark | icons | 0.28% | 42 | 0.12 | PASS | yes |
| widget-section-add-button-idle | light | icons | 0.28% | 37 | 0.12 | PASS | yes |
| widget-section-add-button-hover | light | icons | 0.28% | 53 | 0.18 | PASS | yes |
| widget-fill-picker-tab-idle | light | icons | 0.15% | 35 | 1.99 | PASS | yes |
| widget-gradient-bar-idle | dark | icons | 0.02% | 41 | 0.28 | PASS | yes |
| widget-gradient-bar-dragging | dark | icons | 0.02% | 35 | 0.23 | PASS | yes |
| widget-section-add-button-idle | dark | icons | 0.00% | 23 | 0.07 | PASS | yes |
| widget-gradient-stop-idle | dark | icons | 0.00% | 31 | 1.24 | PASS | yes |
| widget-gradient-bar-idle | light | icons | 0.00% | 10 | 0.20 | PASS | yes |
| widget-gradient-bar-dragging | light | icons | 0.00% | 10 | 0.17 | PASS | yes |
| widget-gradient-stop-idle | light | icons | 0.00% | 9 | 0.56 | PASS | yes |
| widget-gradient-stop-inactive-idle | light | icons | 0.00% | 10 | 0.34 | PASS | yes |
| widget-gradient-stop-inactive-selected | light | icons | 0.00% | 10 | 0.34 | PASS | yes |
| widget-fill-picker-tab-idle | dark | icons | 0.00% | 22 | 1.27 | PASS | yes |
| widget-icon-button-panel-idle | dark | icons | 0.00% | 24 | 0.95 | PASS | yes |
| widget-icon-button-add-stop-idle | dark | icons | 0.00% | 5 | 0.13 | PASS | yes |
| widget-icon-button-add-stop-hover | dark | icons | 0.00% | 5 | 0.13 | PASS | yes |
| widget-icon-button-add-stop-idle | light | icons | 0.00% | 9 | 0.26 | PASS | yes |
| widget-icon-button-add-stop-hover | light | icons | 0.00% | 9 | 0.26 | PASS | yes |
| widget-dialog-close-button-hover | dark | screen | 3.32% | 171 | 2.32 | FAIL | yes |
| widget-dialog-close-button-idle | light | screen | 3.32% | 141 | 1.74 | FAIL | yes |
| widget-dialog-close-button-hover | light | screen | 3.32% | 208 | 2.83 | FAIL | yes |
| screen-gradient-editor-stop-selected | light | screen | 2.43% | 222 | 0.26 | PASS | yes |
| screen-gradient-editor | light | screen | 2.42% | 222 | 0.26 | PASS | yes |
| screen-gradient-editor-stop-selected | dark | screen | 2.27% | 213 | 0.25 | PASS | yes |
| widget-switch-on | dark | screen | 2.14% | 41 | 1.29 | PASS | yes |
| widget-switch-on-hover | dark | screen | 2.14% | 41 | 1.29 | PASS | yes |
| widget-switch-on | light | screen | 2.14% | 45 | 1.49 | PASS | yes |
| widget-switch-on-hover | light | screen | 2.14% | 45 | 1.49 | PASS | yes |
| screen-gradient-editor | dark | screen | 2.13% | 213 | 0.23 | PASS | yes |
| widget-checkbox-clip-content-checked | light | screen | 1.60% | 51 | 1.32 | PASS | yes |
| widget-checkbox-clip-content-checked-hover | light | screen | 1.60% | 56 | 1.44 | PASS | yes |
| screen-color-picker-open | light | screen | 1.51% | 222 | 0.12 | PASS | yes |
| widget-dialog-close-button-idle | dark | screen | 1.39% | 94 | 1.16 | PASS | yes |
| screen-color-picker-open | dark | screen | 1.33% | 194 | 0.09 | PASS | yes |
| widget-checkbox-clip-content-checked | dark | screen | 0.96% | 45 | 1.19 | PASS | yes |
| widget-checkbox-clip-content-checked-hover | dark | screen | 0.96% | 45 | 1.21 | PASS | yes |
| screen-select-open (list region) | light | screen | 0.74% | 44 | 0.56 | PASS | yes |
| widget-checkbox-clip-content-hover | light | screen | 0.64% | 37 | 0.80 | PASS | yes |
| widget-dialog-content-idle | light | screen | 0.37% | 144 | 0.44 | PASS | yes |
| widget-dialog-content-idle | dark | screen | 0.10% | 170 | 0.18 | PASS | yes |
| widget-popover-grid-settings | light | screen | 0.03% | 39 | 0.05 | PASS | yes |
| screen-select-open (list region) | dark | screen | 0.00% | 22 | 0.27 | PASS | yes |
| widget-switch-idle | dark | screen | 0.00% | 2 | 0.08 | PASS | yes |
| widget-switch-hover | dark | screen | 0.00% | 2 | 0.08 | PASS | yes |
| widget-switch-focus | dark | screen | 0.00% | 11 | 0.38 | PASS | yes |
| widget-switch-idle | light | screen | 0.00% | 8 | 0.38 | PASS | yes |
| widget-switch-hover | light | screen | 0.00% | 8 | 0.38 | PASS | yes |
| widget-switch-focus | light | screen | 0.00% | 9 | 0.57 | PASS | yes |
| widget-popover-grid-settings | dark | screen | 0.00% | 7 | 0.02 | PASS | yes |
| widget-checkbox-clip-content-idle | dark | screen | 0.00% | 16 | 0.41 | PASS | yes |
| widget-checkbox-clip-content-hover | dark | screen | 0.00% | 24 | 0.59 | PASS | yes |
| widget-checkbox-clip-content-idle | light | screen | 0.00% | 28 | 0.62 | PASS | yes |
| widget-dialog-overlay-idle | dark | screen | 0.00% | 0 | 0.00 | PASS | yes |
| widget-dialog-overlay-idle | light | screen | 0.00% | 0 | 0.00 | PASS | yes |
| widget-toast-error | light | text | 7.94% | 205 | 9.24 | FAIL | yes |
| widget-tooltip-toolbar-shortcut | light | text | 7.78% | 177 | 11.29 | FAIL | yes |
| widget-toast-error | dark | text | 7.75% | 205 | 9.09 | FAIL | yes |
| widget-tooltip-panel-icon-button | dark | text | 6.86% | 134 | 9.00 | FAIL | yes |
| widget-flyout-item-idle | dark | text | 6.62% | 182 | 7.75 | FAIL | yes |
| widget-flyout-item-hover | dark | text | 6.57% | 171 | 7.27 | FAIL | yes |
| widget-menubar-item-hover | dark | text | 6.50% | 171 | 9.80 | FAIL | yes |
| widget-flyout-item-idle | light | text | 6.44% | 222 | 8.22 | FAIL | yes |
| widget-flyout-item-hover | light | text | 6.40% | 208 | 7.70 | FAIL | yes |
| widget-menubar-item-hover | light | text | 5.62% | 205 | 11.23 | FAIL | yes |
| widget-flyout-content-idle | dark | text | 5.60% | 182 | 6.68 | FAIL | yes |
| widget-flyout-content-idle | light | text | 5.45% | 222 | 7.29 | FAIL | yes |
| widget-tooltip-tab-close | dark | text | 5.31% | 173 | 8.19 | FAIL | yes |
| widget-menu-item-submenu-idle | dark | text | 5.16% | 182 | 6.50 | FAIL | yes |
| widget-menubar-item-idle | light | text | 5.02% | 141 | 7.55 | FAIL | yes |
| widget-menu-item-submenu-open | dark | text | 4.97% | 171 | 6.04 | FAIL | yes |
| widget-tooltip-panel-icon-button | light | text | 4.95% | 120 | 7.97 | FAIL | yes |
| widget-menubar-item-idle | dark | text | 4.79% | 94 | 4.92 | FAIL | yes |
| widget-menu-item-submenu-idle | light | text | 4.60% | 222 | 6.42 | FAIL | yes |
| widget-menu-item-submenu-open | light | text | 4.22% | 208 | 5.78 | FAIL | yes |
| widget-menu-item-described-idle | light | text | 3.93% | 141 | 4.75 | FAIL | yes |
| widget-menu-item-described-hover | light | text | 3.68% | 127 | 4.31 | FAIL | yes |
| widget-toast-default | light | text | 3.34% | 154 | 4.23 | FAIL | yes |
| widget-menu-item-described-idle | dark | text | 3.33% | 94 | 3.82 | FAIL | yes |
| widget-text-button-add-idle | light | text | 2.96% | 208 | 3.94 | PASS | yes |
| row-action-hover-light | light | text | 2.93% | 146 | 4.41 | PASS | yes |
| toolbar-bar-hover-rect-light | light | text | 2.91% | 119 | 4.73 | PASS | yes |
| widget-text-button-add-idle | dark | text | 2.85% | 171 | 4.27 | PASS | yes |
| widget-text-button-add-hover | dark | text | 2.85% | 166 | 4.26 | PASS | yes |
| widget-menu-item-described-hover | dark | text | 2.85% | 83 | 3.42 | PASS | yes |
| widget-menu-content-described-idle | light | text | 2.82% | 141 | 3.60 | PASS | yes |
| screen-menubar-view-open | light | text | 2.78% | 222 | 4.06 | PASS | yes |
| screen-menubar-view-open | dark | text | 2.70% | 182 | 3.31 | PASS | yes |
| widget-panel-header-rectangle-idle | light | text | 2.69% | 178 | 5.36 | PASS | yes |
| widget-panel-header-rectangle-hover | light | text | 2.69% | 178 | 5.36 | PASS | yes |
| toolbar-bar-idle-light | light | text | 2.58% | 119 | 4.63 | PASS | yes |
| widget-text-button-add-hover | light | text | 2.58% | 187 | 4.04 | PASS | yes |
| screen-file-menu-submenu-open | dark | text | 2.48% | 195 | 3.20 | PASS | yes |
| screen-file-menu-submenu-open | light | text | 2.40% | 222 | 3.58 | PASS | yes |
| widget-menu-content-described-idle | dark | text | 2.37% | 94 | 2.90 | PASS | yes |
| widget-tooltip-tab-close | light | text | 2.10% | 202 | 6.29 | PASS | yes |
| disclosure-expanded-hover-light | light | text | 2.08% | 156 | 4.19 | PASS | yes |
| widget-toast-default | dark | text | 2.08% | 77 | 3.31 | PASS | yes |
| section-header-appearance | dark | text | 2.06% | 119 | 2.37 | PASS | yes |
| row-action-hover | dark | text | 2.04% | 119 | 3.77 | PASS | yes |
| toolbar-button-idle-light | light | text | 1.96% | 119 | 3.11 | PASS | yes |
| toolbar-button-hover-light | light | text | 1.96% | 119 | 3.11 | PASS | yes |
| disclosure-expanded-hover | dark | text | 1.94% | 128 | 3.53 | PASS | yes |
| disclosure-expanded-idle-light | light | text | 1.94% | 106 | 3.40 | PASS | yes |
| widget-panel-section-title-idle | dark | text | 1.91% | 130 | 2.08 | PASS | yes |
| screen-menubar-edit-open | light | text | 1.80% | 222 | 2.71 | PASS | yes |
| tabbar-new-hover-light | light | text | 1.67% | 105 | 1.31 | PASS | yes |
| widget-color-slider-alpha-idle | light | text | 1.63% | 208 | 2.22 | PASS | yes |
| widget-color-slider-alpha-hover | light | text | 1.63% | 208 | 2.22 | PASS | yes |
| screen-menubar-edit-open | dark | text | 1.62% | 182 | 2.10 | PASS | yes |
| widget-segmented-control-idle | dark | text | 1.62% | 161 | 2.42 | PASS | yes |
| widget-segmented-control-hover | dark | text | 1.62% | 161 | 3.71 | PASS | yes |
| section-header-layout | dark | text | 1.61% | 130 | 1.75 | PASS | yes |
| row-selected-hover-light | light | text | 1.60% | 139 | 1.86 | PASS | yes |
| widget-number-field-width-focus | light | text | 1.59% | 98 | 2.80 | PASS | yes |
| widget-color-slider-alpha-idle | dark | text | 1.51% | 173 | 1.85 | PASS | yes |
| widget-color-slider-alpha-hover | dark | text | 1.51% | 173 | 1.85 | PASS | yes |
| widget-panel-header-rectangle-idle | dark | text | 1.50% | 140 | 2.91 | PASS | yes |
| widget-panel-header-rectangle-hover | dark | text | 1.50% | 140 | 2.91 | PASS | yes |
| toolbar-button-idle | dark | text | 1.45% | 99 | 2.22 | PASS | yes |
| toolbar-button-hover | dark | text | 1.45% | 99 | 2.22 | PASS | yes |
| widget-segmented-control-idle | light | text | 1.41% | 157 | 2.48 | PASS | yes |
| widget-segmented-control-hover | light | text | 1.41% | 157 | 3.77 | PASS | yes |
| tabbar-new-hover | dark | text | 1.37% | 97 | 1.21 | PASS | yes |
| row-action-row-hover | dark | text | 1.28% | 120 | 2.58 | PASS | yes |
| widget-menu-content-idle | dark | text | 1.22% | 183 | 2.94 | PASS | yes |
| widget-share-button-idle | light | text | 1.22% | 159 | 7.19 | PASS | yes |
| widget-menu-item-disabled-page-delete-idle | light | text | 1.16% | 92 | 3.46 | PASS | yes |
| widget-menu-item-disabled-page-delete-hover | light | text | 1.16% | 92 | 3.46 | PASS | yes |
| row-selected-idle-light | light | text | 1.15% | 56 | 1.19 | PASS | yes |
| toolbar-toggle-hover-light | light | text | 1.14% | 52 | 1.53 | PASS | yes |
| widget-share-button-hover | light | text | 1.11% | 145 | 7.45 | PASS | yes |
| tabbar-inactive-hover | dark | text | 1.05% | 71 | 1.89 | PASS | yes |
| widget-number-field-x-idle | dark | text | 1.02% | 120 | 1.17 | PASS | yes |
| widget-number-field-opacity-idle | light | text | 1.00% | 122 | 1.73 | PASS | yes |
| widget-menu-item-idle | dark | text | 1.00% | 171 | 2.08 | PASS | yes |
| widget-menu-content-idle | light | text | 0.97% | 222 | 2.53 | PASS | yes |
| widget-number-field-opacity-hover | light | text | 0.96% | 122 | 1.65 | PASS | yes |
| widget-menu-submenu-content-idle | dark | text | 0.94% | 82 | 2.12 | PASS | yes |
| row-rename | dark | text | 0.91% | 174 | 1.67 | PASS | yes |
| widget-menu-item-hover | dark | text | 0.87% | 171 | 1.99 | PASS | yes |
| widget-number-field-x-hover | dark | text | 0.86% | 114 | 1.12 | PASS | yes |
| tabbar-new-idle | dark | text | 0.83% | 53 | 0.66 | PASS | yes |
| tabbar-new-idle-light | light | text | 0.83% | 65 | 0.81 | PASS | yes |
| widget-tooltip-toolbar-shortcut | dark | text | 0.78% | 66 | 3.73 | PASS | yes |
| widget-text-input-sm-filled-focus | dark | text | 0.78% | 194 | 1.50 | PASS | yes |
| widget-share-button-hover | dark | text | 0.77% | 72 | 7.09 | PASS | yes |
| row-action-row-hover-light | light | text | 0.77% | 146 | 2.28 | PASS | yes |
| widget-color-slider-hue-idle | dark | text | 0.77% | 173 | 0.97 | PASS | yes |
| widget-color-slider-hue-hover | dark | text | 0.77% | 173 | 0.97 | PASS | yes |
| widget-text-input-sm-filled-focus | light | text | 0.75% | 222 | 1.73 | PASS | yes |
| disclosure-expanded-idle | dark | text | 0.74% | 70 | 2.62 | PASS | yes |
| section-header-fill | dark | text | 0.74% | 107 | 0.70 | PASS | yes |
| widget-share-button-idle | dark | text | 0.71% | 81 | 6.59 | PASS | yes |
| section-header-appearance-light | light | text | 0.69% | 85 | 1.57 | PASS | yes |
| widget-text-button-idle | dark | text | 0.69% | 69 | 3.03 | PASS | yes |
| row-rename-light | light | text | 0.68% | 194 | 1.82 | PASS | yes |
| widget-menu-submenu-content-idle | light | text | 0.67% | 155 | 1.97 | PASS | yes |
| tree-selected-focused | dark | text | 0.65% | 99 | 16.82 | PASS | yes |
| widget-text-button-hover | dark | text | 0.61% | 68 | 3.10 | PASS | yes |
| row-hover | dark | text | 0.61% | 120 | 1.57 | PASS | yes |
| widget-number-field-width-focus | dark | text | 0.61% | 89 | 2.06 | PASS | yes |
| widget-panel-section-title-idle | light | text | 0.60% | 72 | 1.29 | PASS | yes |
| tree-collapsed-frame | dark | text | 0.60% | 128 | 1.40 | PASS | yes |
| widget-menu-item-disabled-idle | light | text | 0.60% | 70 | 0.82 | PASS | yes |
| widget-menu-item-disabled-hover | light | text | 0.60% | 70 | 0.82 | PASS | yes |
| widget-number-field-width-bound-idle | light | text | 0.58% | 147 | 5.35 | PASS | yes |
| widget-color-slider-hue-idle | light | text | 0.58% | 205 | 0.98 | PASS | yes |
| widget-color-slider-hue-hover | light | text | 0.58% | 205 | 0.98 | PASS | yes |
| row-hover-light | light | text | 0.56% | 146 | 1.40 | PASS | yes |
| flyout-content | dark | text | 0.53% | 91 | 1.35 | PASS | yes |
| widget-menu-item-in-submenu-hover | dark | text | 0.52% | 69 | 1.45 | PASS | yes |
| section-header-layout-light | light | text | 0.51% | 72 | 1.09 | PASS | yes |
| toolbar-bar-hover-rect | dark | text | 0.48% | 99 | 2.56 | PASS | yes |
| widget-number-field-width-idle | light | text | 0.48% | 104 | 1.11 | PASS | yes |
| row-hidden-light | light | text | 0.47% | 78 | 1.02 | PASS | yes |
| row-selected-unfocused-hover | dark | text | 0.46% | 113 | 1.47 | PASS | yes |
| row-selected-unfocused-hover-light | light | text | 0.46% | 139 | 1.32 | PASS | yes |
| row-selected-focused-hover-light | light | text | 0.46% | 139 | 1.31 | PASS | yes |
| widget-number-field-x-idle | light | text | 0.46% | 88 | 0.84 | PASS | yes |
| row-selected-hover | dark | text | 0.46% | 113 | 1.67 | PASS | yes |
| widget-number-field-x-hover | light | text | 0.44% | 83 | 0.81 | PASS | yes |
| widget-number-field-width-hover | light | text | 0.44% | 98 | 1.05 | PASS | yes |
| tree-collapsed-frame-light | light | text | 0.44% | 156 | 1.21 | PASS | yes |
| tabbar-active-idle | dark | text | 0.42% | 67 | 1.78 | PASS | yes |
| tabbar-active-hover | dark | text | 0.42% | 67 | 1.78 | PASS | yes |
| tabbar-active-idle-light | light | text | 0.42% | 66 | 1.50 | PASS | yes |
| tabbar-active-hover-light | light | text | 0.42% | 66 | 1.50 | PASS | yes |
| widget-select-trigger-idle | dark | text | 0.42% | 78 | 1.99 | PASS | yes |
| tabbar-inactive-hover-light | light | text | 0.37% | 53 | 1.28 | PASS | yes |
| toolbar-bar-idle | dark | text | 0.31% | 99 | 2.38 | PASS | yes |
| row-idle | dark | text | 0.30% | 74 | 1.00 | PASS | yes |
| widget-text-input-filled-focus | dark | text | 0.30% | 194 | 0.66 | PASS | yes |
| widget-number-field-width-idle | dark | text | 0.29% | 64 | 1.11 | PASS | yes |
| widget-text-button-idle | light | text | 0.29% | 77 | 2.01 | PASS | yes |
| flyout-content-light | light | text | 0.28% | 111 | 0.90 | PASS | yes |
| widget-number-field-opacity-idle | dark | text | 0.25% | 83 | 1.10 | PASS | yes |
| widget-number-field-x-mixed-idle | light | text | 0.25% | 88 | 1.06 | PASS | yes |
| tree-idle | dark | text | 0.25% | 74 | 1.11 | PASS | yes |
| tabbar-two-documents | dark | text | 0.23% | 67 | 0.75 | PASS | yes |
| tabbar-two-documents-light | light | text | 0.23% | 66 | 0.74 | PASS | yes |
| widget-number-field-width-hover | dark | text | 0.23% | 60 | 1.05 | PASS | yes |
| widget-number-field-opacity-hover | dark | text | 0.23% | 83 | 1.04 | PASS | yes |
| widget-number-field-x-mixed-hover | light | text | 0.23% | 82 | 1.00 | PASS | yes |
| widget-select-trigger-hover | dark | text | 0.23% | 74 | 1.90 | PASS | yes |
| row-selected-focused-hover | dark | text | 0.20% | 94 | 1.22 | PASS | yes |
| widget-select-trigger-idle | light | text | 0.19% | 51 | 1.24 | PASS | yes |
| widget-text-input-sm-focus | dark | text | 0.18% | 194 | 1.55 | PASS | yes |
| widget-text-input-sm-focus | light | text | 0.18% | 222 | 1.49 | PASS | yes |
| widget-select-trigger-hover | light | text | 0.17% | 49 | 1.19 | PASS | yes |
| widget-button-accent-idle | light | text | 0.17% | 65 | 1.01 | PASS | yes |
| widget-text-input-filled-focus | light | text | 0.16% | 222 | 0.55 | PASS | yes |
| row-hidden | dark | text | 0.15% | 68 | 1.12 | PASS | yes |
| widget-number-field-width-bound-idle | dark | text | 0.12% | 59 | 2.13 | PASS | yes |
| section-header-fill-light | light | text | 0.11% | 83 | 0.44 | PASS | yes |
| widget-menu-item-idle | light | text | 0.10% | 104 | 0.91 | PASS | yes |
| tree-idle-light | light | text | 0.10% | 56 | 0.83 | PASS | yes |
| widget-text-button-hover | light | text | 0.10% | 70 | 2.43 | PASS | yes |
| widget-menu-item-in-submenu-hover | light | text | 0.09% | 62 | 0.73 | PASS | yes |
| widget-menu-item-hover | light | text | 0.09% | 104 | 0.86 | PASS | yes |
| row-selected-unfocused-idle | dark | text | 0.07% | 64 | 0.88 | PASS | yes |
| row-idle-light | light | text | 0.07% | 56 | 0.72 | PASS | yes |
| row-selected-idle | dark | text | 0.07% | 64 | 1.09 | PASS | yes |
| widget-number-field-x-mixed-idle | dark | text | 0.04% | 54 | 0.82 | PASS | yes |
| widget-button-accent-idle | dark | text | 0.04% | 53 | 0.84 | PASS | yes |
| widget-button-accent-hover | dark | text | 0.04% | 57 | 0.89 | PASS | yes |
| widget-button-accent-hover | light | text | 0.04% | 58 | 0.91 | PASS | yes |
| widget-number-field-x-mixed-hover | dark | text | 0.02% | 49 | 0.74 | PASS | yes |
| tree-selected-focused-light | light | text | 0.02% | 56 | 8.43 | PASS | yes |
| widget-button-accent-disabled-idle | light | text | 0.02% | 50 | 1.15 | PASS | yes |
| widget-button-accent-disabled-hover | light | text | 0.02% | 50 | 5.78 | PASS | yes |
| row-selected-focused-idle | dark | text | 0.01% | 49 | 0.68 | PASS | yes |
| widget-text-input-focus | dark | text | 0.01% | 194 | 0.66 | PASS | yes |
| widget-text-input-focus | light | text | 0.01% | 222 | 0.37 | PASS | yes |
| widget-panel-field-label-idle | dark | text | 0.00% | 19 | 0.68 | PASS | yes |
| widget-panel-field-label-idle | light | text | 0.00% | 26 | 1.10 | PASS | yes |
| disclosure-collapsed | dark | text | 0.00% | 23 | 0.21 | PASS | yes |
| row-selected-unfocused-idle-light | light | text | 0.00% | 42 | 0.65 | PASS | yes |
| row-selected-focused-idle-light | light | text | 0.00% | 41 | 0.63 | PASS | yes |
| disclosure-collapsed-light | light | text | 0.00% | 35 | 0.31 | PASS | yes |
| widget-gradient-stop-row-idle | dark | text | 0.00% | 28 | 1.22 | PASS | yes |
| widget-gradient-stop-row-inactive-idle | dark | text | 0.00% | 28 | 1.44 | PASS | yes |
| widget-gradient-stop-row-idle | light | text | 0.00% | 35 | 1.57 | PASS | yes |
| widget-gradient-stop-row-inactive-idle | light | text | 0.00% | 35 | 2.16 | PASS | yes |
| toolbar-toggle-idle | dark | text | 0.00% | 43 | 0.86 | PASS | yes |
| toolbar-toggle-hover | dark | text | 0.00% | 43 | 0.86 | PASS | yes |
| toolbar-toggle-idle-light | light | text | 0.00% | 36 | 1.33 | PASS | yes |
| tabbar-inactive-idle | dark | text | 0.00% | 17 | 0.46 | PASS | yes |
| tabbar-inactive-idle-light | light | text | 0.00% | 27 | 0.65 | PASS | yes |
| widget-text-input-idle | dark | text | 0.00% | 42 | 1.16 | PASS | yes |
| widget-text-input-hover | dark | text | 0.00% | 42 | 1.16 | PASS | yes |
| widget-text-input-sm-idle | dark | text | 0.00% | 46 | 1.01 | PASS | yes |
| widget-text-input-sm-hover | dark | text | 0.00% | 46 | 1.01 | PASS | yes |
| widget-text-input-idle | light | text | 0.00% | 27 | 0.60 | PASS | yes |
| widget-text-input-hover | light | text | 0.00% | 27 | 0.60 | PASS | yes |
| widget-text-input-sm-idle | light | text | 0.00% | 23 | 0.67 | PASS | yes |
| widget-text-input-sm-hover | light | text | 0.00% | 23 | 0.67 | PASS | yes |
| widget-button-accent-disabled-idle | dark | text | 0.00% | 36 | 0.74 | PASS | yes |
| widget-button-accent-disabled-hover | dark | text | 0.00% | 37 | 3.34 | PASS | yes |
| widget-button-accent-disabled-small-idle | dark | text | 0.00% | 42 | 1.28 | PASS | yes |
| widget-button-accent-disabled-small-idle | light | text | 0.00% | 46 | 1.52 | PASS | yes |
| widget-menu-item-disabled-idle | dark | text | 0.00% | 47 | 0.60 | PASS | yes |
| widget-menu-item-disabled-hover | dark | text | 0.00% | 47 | 0.60 | PASS | yes |
| widget-menu-item-component-idle | dark | text | 0.00% | 13 | 0.32 | PASS | yes |
| widget-menu-item-component-hover | dark | text | 0.00% | 11 | 0.28 | PASS | yes |
| widget-menu-item-disabled-page-delete-idle | dark | text | 0.00% | 47 | 1.24 | PASS | yes |
| widget-menu-item-disabled-page-delete-hover | dark | text | 0.00% | 47 | 1.24 | PASS | yes |
| widget-menu-item-component-idle | light | text | 0.00% | 45 | 0.93 | PASS | yes |
| widget-menu-item-component-hover | light | text | 0.00% | 39 | 0.81 | PASS | yes |

## Frontier and proposal per profile

Frontier: for a channel tolerance t, the largest failing share (percent of pixels) over the counted comparisons of the profile.

### default: 4 comparisons, 4 counted, current 8 channels / 0.2%

| t | 0 | 8 | 16 | 24 | 32 | 40 | 48 | 56 | 64 | 72 | 80 | 88 | 96 | 104 | 112 | 120 | 128 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| share | 1.47 | 0.01 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |

Proposed: **8 channels, 0.5%**.

### text: 214 comparisons, 214 counted, current 48 channels / 3.0%

| t | 0 | 8 | 16 | 24 | 32 | 40 | 48 | 56 | 64 | 72 | 80 | 88 | 96 | 104 | 112 | 120 | 128 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| share | 99.52 | 46.02 | 35.11 | 34.43 | 33.74 | 33.28 | 7.94 | 7.00 | 6.52 | 5.67 | 5.56 | 5.02 | 4.74 | 4.67 | 4.63 | 4.56 | 4.41 |

Proposed: **72 channels, 6.0%**.

### icons: 40 comparisons, 40 counted, current 32 channels / 2.0%

| t | 0 | 8 | 16 | 24 | 32 | 40 | 48 | 56 | 64 | 72 | 80 | 88 | 96 | 104 | 112 | 120 | 128 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| share | 26.77 | 11.73 | 9.11 | 5.86 | 5.86 | 5.86 | 5.86 | 4.30 | 4.30 | 4.30 | 4.30 | 4.30 | 4.30 | 0.85 | 0.39 | 0.39 | 0.39 |

Proposed: **24 channels, 6.0%**.

### screen: 36 comparisons, 36 counted, current 32 channels / 3.0%

| t | 0 | 8 | 16 | 24 | 32 | 40 | 48 | 56 | 64 | 72 | 80 | 88 | 96 | 104 | 112 | 120 | 128 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| share | 21.61 | 5.28 | 3.84 | 3.32 | 3.32 | 3.32 | 3.32 | 1.71 | 1.57 | 1.42 | 1.39 | 1.39 | 1.39 | 1.02 | 0.88 | 0.78 | 0.71 |

Proposed: **16 channels, 4.0%**.


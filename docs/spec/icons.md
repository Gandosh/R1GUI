# Icon pipeline (Phase 1, slice 1.4)

## Source
Lucide (ISC; notice in `assets/icons/lucide/LICENSE`). The OpenPencil UI uses 74 Lucide icons by name; all but one are extracted as standalone SVG files by `tools/spec/extract_icons.py` from the built bundle (no download): 156 SVGs in `assets/icons/lucide/` (the full set the bundle ships, 73 of them used), listed in `assets/icons/manifest.json` with `usedBySource` and `usedButNotExtracted` (`rotate-ccw`, used only in a Storybook story, not by the application).
All icons share a 24x24 viewBox, stroke width 2, round caps and joins, `currentColor` stroke, no fill (a few dots use `fill="currentColor"`). The UI draws them at 12, 14, 16 px and 1.2em (about 15.6 px) in places; reference renderings exist for 12, 14, 16 and 24 px.

## Decision
Icons are rasterized at build time into single-channel coverage atlases and tinted by the draw call:
- Cell sizes 16, 24, 32 and 48 px (covers 100% to 300% display scale with at most one downscale step; the nearest larger cell is sampled with linear filtering).
- Rasterizer: our own small tool under `tools/icons/` (Phase 3 work): SVG path subset (M, L, H, V, C, S, Q, T, A, Z and relative forms), element subset (path, circle, rect, line, polyline, polygon, ellipse), stroke with round caps/joins at width 2/24 of the cell, 4x4 supersampling, output 8-bit coverage. No third-party SVG library.
- Runtime: the atlas is an ordinary texture; an icon is a textured quad with a colour; hover/active colour changes cost nothing.
- Why not an icon font or runtime vector tessellation: a font needs a font build step and loses the 24 px stroke geometry; runtime tessellation costs CPU every frame and needs a robust stroker. Atlas is the cheapest at draw time. Revisit tessellation only if arbitrary-size vector icons (for example the sculpting brush cursor art) are needed.

## Oracle
`tests/reference/icons/<size>/<name>.png` (292 images, 73 icons x sizes 12, 14, 16, 24): Chrome's rendering of each SVG, white on black so brightness is coverage. The rasterizer test compares its output to these with `tools/spec/imgdiff.py --profile icons` (provisional tolerance in `tests/reference/tolerance.json`). Regenerate with `node tools/spec/render_icons.mjs assets/icons/lucide tests/reference/icons assets/icons/manifest.json`.

## Not decided here
- User-supplied icons (customizable toolbars, slice 2.6/5.8): the same pipeline is expected to accept any SVG in the supported subset at load time; unsupported elements are rejected with a message, not drawn partially.
- Colour (multi-colour) icons: out of scope for now.

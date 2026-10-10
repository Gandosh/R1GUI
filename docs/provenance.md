# Provenance log

Every behavior spec records where it came from: public documentation, observation of a running
product, a permissively licensed reference, a separate-reader summary of Unreal source (behavior only), or
own design. Implementers cite the spec, never the source.

| Date | Spec / area | Source type | Source detail | Written by |
|---|---|---|---|---|
| 2026-10-09 | Phase 0 skeleton (window, checked casts, Vulkan clear) | own design | standard Win32 and Vulkan API usage from public Microsoft/Khronos documentation | owner |
| 2026-10-09 | Design tokens (colours, spacing, radii, type scale, shadows) | permissive reference (MIT) | OpenPencil `src/app.css` and compiled CSS; values only | owner |
| 2026-10-09 | Widget catalogue and reference screenshots | observation of a running product + permissive reference (MIT) | OpenPencil build and `src/theme/*.ts` variant definitions, measured in Chrome | owner |
| 2026-10-09 | Icon pipeline | own design | Lucide SVG geometry (ISC); standard SVG path semantics from the W3C SVG specification | owner |
| 2026-10-09 | Interaction specs 01 to 12 (focus, docking, floating windows, layouts, resize, customization, commands, drag and drop, properties and undo, popups, curve editor, asset browser) | separate-reader summary of a licensed reference editor (behavior only) | owner cleared the reading in slice 0.1; two independent source comparisons and two fix rounds; specs kept local, see Goal/evidence/P2_close.md | owner |
| 2026-10-09 | Docking model and sandbox (ui-dock, preview mode 3) | own design from the behavior specs | implemented from specs 02, 04, 05, 08 without access to the reference source | owner |
| 2026-10-10 | Brush library (quick letters, popup, favourites, recents, letter assignment, file) | own design from the owner's written description | behaviour decided in docs/dev/brush-library.md section 4; no external product, code or artwork used; the sample pictures are drawn by code | builder |

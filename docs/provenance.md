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

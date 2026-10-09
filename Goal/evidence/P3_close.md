# Phase 3 close record

**Status:** TECHNICAL PASS; owner gate ACCEPTED (owner, 2026-10-09)   **Date:** 2026-10-09

## Slices
| Slice | Status | Evidence |
|---|---|---|
| 3.1 Platform: borderless windows, input, clipboard, DPI | TECHNICAL PASS (snap layouts, shadow, mixed DPI unverified) | [P3_S01](P3_S01.md) |
| 3.2 Vulkan device, swapchain per window, frame loop | TECHNICAL PASS | [P3_S02](P3_S02.md) |
| 3.3 2D batcher | TECHNICAL PASS | [P3_S03](P3_S03.md) |
| 3.4 Fonts, shaping, glyph atlas | TECHNICAL PASS | [P3_S04](P3_S04.md) |
| 3.5 Single-line text editing | TECHNICAL PASS | [P3_S05](P3_S05.md) |
| 3.6 Widget tree | TECHNICAL PASS | [P3_S06](P3_S06.md) |
| 3.7 Flexbox layout | TECHNICAL PASS | [P3_S07](P3_S07.md) |
| 3.8 Event routing, focus, capture | TECHNICAL PASS | [P3_S08](P3_S08.md) |
| 3.9 Invalidation | TECHNICAL PASS | [P3_S09](P3_S09.md) |
| 3.10 Theme tokens and style resolution | TECHNICAL PASS | [P3_S10](P3_S10.md) |
| 3.11 Baseline | TECHNICAL PASS | [P3_S11](P3_S11.md) |
| 3.12 Preview: themed panel | TECHNICAL PASS (owner interaction pending) | [P3_S12](P3_S12.md) |

## Launch the preview
`tools/run/preview.cmd`, then the checklist in [P3_S12](P3_S12.md).

## Verification summary
Fresh MSVC build (/W4 /WX) passes 32 of 32 fast tests in Release and in Debug; GPU tier passes on the RTX 3090 (15 of 15 on the merged tree, 18 of 18 reported after integration); debug runs with the validation layer printed no validation messages. clang-cl and sanitizer runs were not done (clang-cl is not installed here).

## Owner decisions in this phase
| Decision | Date |
|---|---|
| Download and vendor FreeType 2.14.3 and HarfBuzz 14.6.0 (hashes in docs/third-party.md) | 2026-10-09 |
| Own flexbox layout, no Yoga | 2026-10-09 |
| Synthetic embolden from the Regular face for weights 500 and up (matches reference widths) | 2026-10-09 |
| Borderless windows with our own title bar | 2026-10-09 |

## Accepted risks and open items
- The panel does not yet match the reference closely enough for the provisional 3% pixel tolerance (3.8% to 6.1% failing, see P3_S12): text weight (browser gamma and contrast), one icon, stroke weights. Phase 4 widgets are compared one by one and the tolerances are calibrated then.
- No independent code review of the new modules (platform, renderer, text, core, preview); recommended before Phase 4 builds on them. The modules were built separately and met for the first time in slice 3.12; the integration builder fixed the issues it found (cursor lag, a painter header mismatch, the move/size loop freeze).
- Windows 11 snap layouts, drag-to-edge snapping, the window shadow, mixed-DPI multi-monitor moves, device-lost recovery and a second GPU (RTX 4080) are unverified.
- Layout: a deep-nesting cap of 512 levels assumes the 8 MiB executable stack now set in CMake; the preview's SVG rasterizer and text weight boost are interim.
- Interaction specs (docs/spec/interaction) remain local only; publication is still the owner's call.
- Git history note: the ignore rules had been hiding tools/build and tests/reference from the repository until this phase; fixed.

## Build notes
- New tiers: `ctest -L gpu` (needs a GPU; use `R1UI_GPU="RTX 3090"` to pick the card) and `ctest -L desktop` (needs an interactive desktop).
- `r1gui-preview --bench <dir>` rewrites the baseline; `--panel-shot <dir>` renders the panel offscreen for comparison.
- Shaders are compiled with glslc from the Vulkan SDK at build time.

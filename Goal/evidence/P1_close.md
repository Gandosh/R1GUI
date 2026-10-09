# Phase 1 close record

**Status:** TECHNICAL PASS; owner gate ACCEPTED (owner, 2026-10-09)   **Date:** 2026-10-09
Phase 0 owner gate was also still PENDING when Phase 1 started on the owner's instruction to continue.

## Slices
| Slice | Status | Evidence |
|---|---|---|
| 1.1 Design tokens | TECHNICAL PASS | [P1_S01](P1_S01.md) |
| 1.2 Widget catalogue | TECHNICAL PASS (gaps declared) | [P1_S02](P1_S02.md) |
| 1.3 Reference screenshots | TECHNICAL PASS (coverage partial) | [P1_S03](P1_S03.md) |
| 1.4 Icon pipeline | TECHNICAL PASS | [P1_S04](P1_S04.md) |
| 1.5 Fonts and text metrics | TECHNICAL PASS | [P1_S05](P1_S05.md) |
| 1.6 Pixel-comparison method | TECHNICAL PASS (tolerances provisional) | [P1_S06](P1_S06.md) |
| 1.7 Preview viewer | TECHNICAL PASS (owner interaction pending) | [P1_S07](P1_S07.md) |

## Launch the preview
`tools/run/preview.cmd`, then the checklist in [P1_S07](P1_S07.md).

## Verification summary
`tools/run/clean_check.cmd`: fresh MSVC build and 7 of 7 fast tests pass; clang-cl pass skipped (not installed). Debug preview run with the validation layer produced no output.

## Owner decisions in this phase
| Decision | Date |
|---|---|
| Local-only checks, GitHub for storage only (no hosted CI) | 2026-10-09 |
| New root directory `assets/` (theme tokens, fonts, icons) recorded here as a workspace-layout addition; spec documents under `docs/spec/`, reference images under `tests/reference/`, spec tooling under `tools/spec/` | 2026-10-09 (recorded, owner to confirm) |

## Accepted risks and open items
- Tolerances in `tests/reference/tolerance.json` are provisional until Phase 4.
- Seven widget areas have no reference capture (switch, dialog, toast, document tab bar, gradient editor, binding picker, layer-tree interaction states); they are described from source only.
- The reference UI loads only Inter Regular; heavier text is browser-synthesized. Decision on matching it belongs to slice 3.4.
- OpenPencil never defines the `danger`, `primary` and `border-strong` colours its widget code references; the toolkit chooses them in Phase 3.
- Lucide's ISC notice text was written from the published notice; verify before release.
- The Phase 1 renderer path is transfer-only (no alpha rectangles, no AA) until slice 3.3; the new renderer API has had no independent review.
- Reference captures come from Chrome with software GL; text anti-aliasing may differ from a GPU machine.

## Build notes
- Regenerate assets: `node tools/spec/capture.mjs ...`, `python tools/spec/build_tokens.py`, `python tools/spec/font_metrics.py`, `python tools/spec/extract_icons.py`, `node tools/spec/render_icons.mjs ...`, `node tools/spec/text_reference.mjs ...` (commands in each slice file). They need the OpenPencil build in `reference/OpenPencil/dist`, Node 24 and Chrome.
- The audit now allows `tests/reference/` paths in build files; bare `reference/` and `scratch/` stay forbidden.
- First run of the preview converts 18 reference screens to raw images (about 40 s).

# Phase 2 close record

**Status:** TECHNICAL PASS; owner gate ACCEPTED (owner, 2026-10-09)   **Date:** 2026-10-09

## Slices
| Slice | Status | Evidence |
|---|---|---|
| 2.1 Focus, keyboard, shortcut routing | TECHNICAL PASS | [P2_S01](P2_S01.md) |
| 2.2 Docking | TECHNICAL PASS | [P2_S02](P2_S02.md) |
| 2.3 Floating panels, second monitor | TECHNICAL PASS | [P2_S03](P2_S03.md) |
| 2.4 Layouts | TECHNICAL PASS | [P2_S04](P2_S04.md) |
| 2.5 Panel resize, adaptive | TECHNICAL PASS | [P2_S05](P2_S05.md) |
| 2.6 Customizable menus and toolbars | TECHNICAL PASS (reference lacks the target capability; design is decisions D1 to D6) | [P2_S06](P2_S06.md) |
| 2.7 Commands | TECHNICAL PASS | [P2_S07](P2_S07.md) |
| 2.8 Drag and drop, selection | TECHNICAL PASS | [P2_S08](P2_S08.md) |
| 2.9 Property binding, undo | TECHNICAL PASS | [P2_S09](P2_S09.md) |
| 2.10 Tooltips, popups, modal | TECHNICAL PASS | [P2_S10](P2_S10.md) |
| 2.11 Curve editor | TECHNICAL PASS | [P2_S11](P2_S11.md) |
| 2.12 Asset browser, thumbnails | TECHNICAL PASS | [P2_S12](P2_S12.md) |
| 2.13 Reference reads by separate readers | TECHNICAL PASS (publication undecided) | [P2_S13](P2_S13.md) |
| 2.14 Preview: docking sandbox | TECHNICAL PASS (owner interaction pending) | [P2_S14](P2_S14.md) |

## Launch the preview
`tools/run/preview.cmd` opens in the docking sandbox (Tab steps through the three modes; three small squares in the top-right corner show which one is active), then the checklist in [P2_S14](P2_S14.md).

## Owner decisions (2026-10-09): all 24 recommendations ACCEPTED (full table with options: docs/spec/interaction/00-index-and-decisions.md, kept local)
Applied now: D9 (Escape during a tab drag cancels and the tab returns to its original place; specs 00, 01, 02, 03, 08 updated, sandbox changed, a drop on no target still floats). Scheduled for later phases: all others, for example D11 (splitter floor 20 px plus an explicit collapse gesture), D12 (tab overflow with a 60 px minimum width), D13 (safe layout loading and reset), D14 (sequence chords), D16 (variable binding spec), D18 (thumbnail zoom), D24 (single drag threshold). Headline items: D1 to D6 user-created menus, command palette placement, removing and restoring entries, resizing and free-form placement of buttons, renaming and locking, undo of customization (the reference supports none of the create/place/resize capability); D9 Escape during a tab drag (decided: return to origin); D11 splitter floor (recommendation 20 px plus explicit collapse); D12 tab overflow (recommendation minimum width plus scroll arrows and dropdown); D13 safe layout loading and reset; D14 sequence shortcut chords; D16 variable binding in property panels (own spec needed); D18 thumbnail zoom model; D24 single drag threshold.

## Publication decision (open)
The repository is public and the interaction specs are derived from reading a licensed source. They are excluded from git locally. Options: (a) keep them local only; (b) make the repository private and commit them; (c) run a third independent source comparison, then publish. The code (`ui-dock` and the sandbox) was written from the specs only and is committed.

## Accepted risks / open items
- Residual close-paraphrase risk in the specs (see P2_S13).
- The dock model is a sandbox: floating areas are rectangles, not native windows; no named layouts, auto-save, import or export, tab overflow, or DPI scaling (slices in Phase 5).
- Several scenarios are manual (multi-monitor).
- `window.escapePressed()` remains in the platform API though the preview no longer uses it.

## Build notes
- New fast tests: `ui-core.json_writer`, `ui-dock.docking`, `ui-dock.resize`, `ui-dock.persistence`, `spec.interaction_lint`.
- `tools/spec/spec_lint.py` runs on the local specs; in a fresh clone (no specs) it reports 0 files.

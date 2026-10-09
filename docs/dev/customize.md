# Customizable menus, toolbars and free-form panels (ui-commands/customize, ui-widgets/customize)

Developer guide to slices 5.7 (customizable menus), 5.8 (customizable toolbars) and 5.9 (customization persistence). Behaviour specs: `docs/spec/interaction/06-user-customization-menus-toolbars.md` (primary), `07-commands.md`, `08-drag-drop-selection.md`, `10-tooltips-popups-menus-modal.md`, `01-focus-keyboard-shortcuts.md`; owner decisions D1 to D6 (below). It builds on the command registry of `docs/dev/commands.md`.

| Decision | What is built |
|---|---|
| D1 user menus, option A | `addUserMenu`: a new top-level menu that holds any commands, stored per user, shown after the built-in menus (sub-menus of user menus work: `addSubmenu`) |
| D2 placing commands, option C | drag a row of the `CommandPalette` onto a menu, toolbar or free-form panel; `openCommandPicker` (dialog) is the keyboard and accessibility path (Insert in an edit display) |
| D3 removal | hide with an explicit restore list (`restoreList`, the tool strip's Restore menu); the same eye toggle for built-in and user entries; only user-created entries can be removed for good |
| D4 free placement | pixel placement only inside `FreeFormPanel`; toolbars have size steps (small 26, medium 32, large 40 px) and item gaps (0 to 32 px); menus have neither |
| D5 labels, lock, storage | `userLabel` beside the stable id; owner lock (`locked` on a menu, toolbar, panel or entry); per-user file plus a per-workspace override (workspace wins) |
| D6 undo | session revert only: `beginEditSession` / `revertSession` / `commitSession`, wired to edit mode |

## 1. Modules and files

| Part | Folder | Depends on | Owns |
|---|---|---|---|
| model, persistence | `source/ui-commands/{include/r1ui/commands,src}/customize` | `r1ui::core` (JSON) | `Layout.h` (tree types), `Delta.h` + `Effective.cpp` (the pure function), `Customization.h` (editing, session, views), `CustomizationIo.h` (format, stores, import/export) |
| widgets | `source/ui-widgets/{include/r1ui/widgets,src}/customize` | `r1ui::widgets`, the commands widgets | controller, drag hub, edit displays, palette, picker, bound bars, tool strip, gallery |
| tests | `tests/ui-commands/customize`, `tests/ui-widgets/customize` | | see section 7 |

No CMake file is edited (every folder is globbed). Nothing outside these folders changed except this document.

## 2. Layout model (`Layout.h`)

A `LayoutSet` is a `MenuLayout` (menu bar), any number of `ToolbarLayout` and any number of `FreeFormPanelLayout`. Nodes are plain `Node` values with a stable `id` (unique inside the set, `[A-Za-z0-9._:/-]`, at most 128 bytes), a `kind`, optional `commandId`, a default `label`, the user's `userLabel`, `visible` (false = hidden by the user), `locked` (set by the owner only), `user` (created by the user), `missing` and, for free buttons, a `rect`.

```
MenuBar    > Menu > Section > { Command | Separator | Heading | Submenu }      Submenu > Section
Toolbar    > { Command | Separator | Group | Spacer }                          Group   > Command
Panel      > FreeButton                                                        (order = z-order, last on top)
```

A section is the unit of drag inside a menu; adjacent sections are separated by a line when the menu is built and a section heading is a Heading row. Limits: 100 000 nodes per set, depth 12, labels 256 bytes, 256 containers. `validateLayout` reports every structural problem; `effectiveLayout` always returns a legal set.

Hosts that already have flat `CommandMenuEntry` / `CommandToolbarItem` lists build their built-in layouts with `menuNodeFromEntries` and `toolbarFromItems` (`LayoutConvert.h`): a flat list is split at separators into sections and a heading right after a boundary becomes the section heading, so converting there and back gives the same rows.

## 3. Delta and `effectiveLayout` (`Delta.h`)

The user's changes are a `Delta` over the built-in layouts, keyed by stable ids: `edits` (per node: hidden, label, rectangle), `moves` (node to a `Placement{parent, anchor, Side::End|Start|Before|After}`), `added` (user nodes, each with its parent and placement, flat and in creation order), `userToolbars`, `userPanels`, `toolbarEdits` (size step, gap), `panelEdits` (snap, grid) and a `serial` counter that only grows (ids are never reused).

`effectiveLayout(builtin, delta, options)` is pure and total: containers, then added nodes in order, then moves in order, then per-node edits, then toolbar and panel settings; every step is validated and a step that cannot be applied is skipped and reported (`Report`: `MissingCommand`, `MissingNode`, `MissingParent`, `MissingAnchor`, `IllegalParent`, `Cycle`, `Locked`, `DuplicateId`, `Invalid`, `Limit`). Update behaviour (spec 06 rules 33 to 42): an entry the product added appears at its default position, a removed command's entry is dropped (and reported, or kept with `missing = true` for the edit display), a vanished anchor falls back to the end of the same parent, user menus survive, changes inside something the owner has since locked are ignored. `EffectiveOptions{keepHidden, keepMissing, exists}`: the normal display drops hidden and missing nodes, the edit display keeps both. Anything moved must stay in the same container kind (a menu entry never lands in a toolbar), may not enter its own subtree and may not cross a locked node.

## 4. Editing and session (`Customization.h`)

`Customization(builtin, exists)` holds the built-in set, the user `Delta`, a read-only workspace `Delta` layered on it, cached views (`effective()`, `editView()`), a `version()` counter and listeners. Every operation returns an `EditResult{ok, error, reason, id, rect}`, never throws and changes nothing on failure; it is validated by applying the candidate delta, so the editing rules are exactly the loader's rules. `preview(op)` runs any operation in dry-run mode (same result, nothing stored, nobody notified): the edit displays use it to show an insertion indicator only where a drop would be accepted.

| Area | Operations |
|---|---|
| visibility and order | `setHidden` / `hideEntry` / `showEntry`, `move(id, Placement)`, `renameLabel` (blank restores the default; menu titles stay unique), `restoreList()` (hidden entries with their path) |
| menus | `addUserMenu`, `deleteUserMenu`, `addCommand`, `addSeparator`, `addHeading`, `addSubmenu`, `addSection`, `removeUserEntry` (user nodes only), `resetMenu` (a menu, toolbar or panel; no confirmation), `resetAll` (the widget asks first) |
| toolbars | `setToolbarSizeStep`, `setToolbarGap`, `addUserToolbar`, `deleteUserToolbar`, `addSpacer`, `addGroup` |
| free-form panels | `addUserPanel`, `deleteUserPanel`, `placeButton`, `setButtonRect`, `moveButton`, `resizeButton`, `deleteButton` (user: removed, built-in: hidden), `bringToFront`, `sendToBack`, `setPanelSnap(snap, grid)`, `fitRect` (snap when on, minimum 16 px, clamped inside the panel) |
| session | `beginEditSession`, `revertSession` (restores the delta of the session start, the session stays open), `commitSession` (calls the commit hook; `CustomizationStorage` saves), `sessionChanged` |

`isLocked`, `lockReason(id)` give the sentence for the lock tooltip; `shownLabel`, `pathOf` serve the restore list.

## 5. Persistence (`CustomizationIo.h`)

File `{"format":"r1ui-customization","version":1,...}`, member list in the header comment; `exportDelta` writes it deterministically (golden test), `parseDelta` is strict: whole-file rejection (nothing changes, the file is moved aside as `<name>.corrupt-N`) for over 24 MiB, invalid JSON (this includes invalid UTF-8, duplicate keys and nesting over 8), wrong format, a newer version, over 100 000 entries, or no usable entry in a non-empty file; single entries are skipped or repaired with an issue (bad id, unknown kind or side, duplicate id, rectangle not finite or under 16 px, gap or grid out of range, over-long or control-character text). Unknown members are ignored; unknown command ids are kept and appear as missing commands. Stores: `MemoryTextStore`, `FileTextStore` (write to `<file>.tmp` then rename, size limit, no exceptions). `loadCustomization(model, userStore, workspaceStore)` reads both layers (a failure keeps the live state), `importCustomization(model, text, Replace|Merge)`, `exportCustomization`, and `CustomizationStorage` (load, save, save when `commitSession` runs). The workspace layer is never written. Every change bumps `version()`, so the UI rebuilds (live apply).

## 6. Widgets

```
CustomizeController(ui, services, sync, model)   edit mode, drag hub, customize.* commands, reset-all dialog, messages
DragHub + DragPayload + DragTarget               ghost overlay, begin / move / end / cancel, target under the pointer
CommandPalette, CommandPaletteList               search + categorized list; Enter / double-click -> onChoose; drag a row
openCommandPicker(controller, options)           modal dialog around a CommandPalette (the keyboard path)
CustomizableMenuBar, CustomizableToolbar         the effective layouts through the command binders; edit display in edit mode
MenuEditor, ToolbarEditor (+ ToolbarEditStrip)   the edit displays
FreeFormPanel (+ FreeFormCanvas, FreeFormButton) the free-form panel kind
CustomizeToolStrip                               Customize, New menu, Restore..., Reset menu, Revert changes, Reset all..., message line
RenameField                                      in-place rename (Enter commits, Escape cancels, blank restores)
LayoutConvert.h                                  model <-> CommandMenuEntry / CommandToolbarItem, label overrides
GalleryCustomize.h                               gallery page with its own registry (24 commands), layouts, controller and widgets
```

**Wiring (host):** after the registry, overrides, keymap, router and `CommandUiSync` exist, build the built-in `LayoutSet`, a `Customization`, a `MemoryTextStore`/`FileTextStore` with `CustomizationStorage` (`load()` at start), then `CustomizeController controller(ui, services, sync, model)`. Replace `MenuBar` + `bindCommandMenuBar` by `ui.create<CustomizableMenuBar>(parent, controller)` and each toolbar by `ui.create<CustomizableToolbar>(parent, controller, "<toolbar id>")`; add `FreeFormPanel(controller, "<panel id>")`, `CommandPalette(controller)` and `CustomizeToolStrip(controller)` where wanted. The controller registers `customize.toggle` (a Toggle named "Customize", checked while editing), `customize.revert`, `customize.resetAll`, `customize.newMenu`; they are ordinary commands (menu, chord, palette). Leaving edit mode commits the session and the storage writes the user file. The controller must outlive every widget made from it and be destroyed before the `Customization`, the registry and the `UiContext`.

**Non-edit mode is the command binders' output.** `CustomizableMenuBar` and `CustomizableToolbar` feed `buildCommandMenu` / `bindCommandToolbar` with the effective layout (sections flattened with separators and headings, spacers split the toolbar into runs bound one after the other), so a customization-free layout is pixel-identical to the command-built widgets (`bars_visual_test`). The size step and gap are applied only when they differ from the defaults. A user label of a command entry is patched into the built row (id prefixed `customize:`, so the per-frame refresh of open menus does not reset it; such a row refreshes its shortcut and enabled state at the next open).

**Edit mode interactions**

| Where | Mouse | Keyboard (display focused) |
|---|---|---|
| menu editor | press a handle (6 dots) and drag: entries, whole sections (their handle) and menu titles; eye toggles visibility; double-click a label renames; right-click: Hide/Show, Rename, Add command / separator / heading / sub-menu / section, Remove or Delete menu (user nodes), Reset this menu; a press on a title selects that menu; dropping an entry on a title moves it to the end of that menu; the New menu button | Up/Down/Home/End cursor, Left/Right switch menu, Space hide/show, F2 or Enter rename, Delete (user: remove, built-in: hide), Alt+Up/Down move (across the section edge), Insert command picker |
| toolbar editor | cells with an eye badge; drag to reorder with a vertical indicator; Size and Gap controls; right-click: Hide/Show, Add command / separator / spacer, Remove (user), Reset this toolbar | arrows, Space, Delete, Alt+arrow, Insert |
| free-form panel | click, Ctrl or Shift toggle, marquee; drag moves the selection (threshold 5 px); eight 8 px handles on a single selection; right-click: Delete, front/back, six alignments, Add command | arrows 1 px, Shift+arrows 10 px (on a grid: one cell, Shift 10 px rounded up to cells), Delete, Ctrl+A, Escape, Insert; Snap switch (default off, 8 px) |
| palette | click selects, drag a row to a target, double-click or Enter chooses | Up/Down/PageUp/PageDown/Home/End, type to search |

Escape during any drag cancels it and removes the indicator and the ghost; a drop outside every target, or where `preview` refuses, does nothing. A locked menu, toolbar, panel or entry shows a lock glyph instead of the handle and the eye, refuses every edit (drag, eye, rename, keyboard, drops) and its tooltip says why; the reason is also in `controller.lastRefusal()` (the tool strip's message line).

## 7. Tests and evidence

Fast tier (`ctest -L fast`): `ui-commands.customize_effective_test` (identity, hide, rename, every placement, locked and illegal moves, missing commands and anchors, the v1 to v2 update simulation, containers and settings, limits, 400 fuzzed deltas with the property checks: legal output, unique ids, hide-then-show identity, order of untouched nodes preserved), `customize_edit_test` (every operation and refusal, session, listeners, workspace layer, preview), `customize_io_test` (golden JSON, round trip, whole-file rejection, entry repairs, mutated and random garbage, stores, aside files, atomic writes on a real directory, import/export, live apply); `ui-widgets.customize.*`: `menu_editor_test`, `toolbar_editor_test`, `free_form_panel_test`, `palette_test` (with the picker), `controller_test`, `bars_test`, `hostile_test`, `gallery_test`. GPU tier (`ctest -L gpu`): `gallery_gpu_test` (six scenes in both themes under the artifact directory), `bars_visual_test` (pixel equality of the bound toolbar, menu bar and open menu with the command binders). Edit mode has no reference: its structure is covered by golden structural expectations (row kinds, indentation, handle | label | eye order, lock glyphs, eight handle rectangles, indicator placement and geometry, palette rows).

## 8. Not implemented, by design or yet

- Sub-menus of user menus are created with `addSubmenu` (a section inside) but there is no UI to build deeper nesting by drag; a user entry cannot be dragged out of the menu bar into a toolbar (the model refuses cross-container moves).
- Separators, spacers and groups are added from the context menus (and `addGroup` in the model), not by dragging a pseudo-command from the palette; groups have no edit UI for their entries.
- Rename in place exists for menus (titles, headings, sub-menus, command entries) only; toolbar buttons and free-form buttons keep the command's label (a `userLabel` stored in a file does apply to free-form buttons and menu rows).
- Only the user layer is edited; the workspace layer is loaded and layered, not authored (no UI to save it).
- Import and export have a model and file API but no dialog: the host calls `importCustomization` / `exportCustomization` (spec 07 style confirmation is the host's).
- A command entry renamed by the user does not refresh its shortcut text or enabled state while its menu stays open (see section 6).
- The toolbar edit display has no overflow handling; a menu of more than 2000 entries is cut by the menu widget's own limit in the bound menu bar, the edit display shows all.
- No auto-scroll while dragging near the edge of a scrolled container (spec 08 rule 25 asks for none in lists).
- Multi-selection resize in the free-form panel (handles only for a single selection); no keyboard marquee.

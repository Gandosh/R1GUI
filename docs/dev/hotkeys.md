# Action list and hotkey editor (ui-widgets/actions, ui-widgets/hotkeys, ui-commands/keyboard)

Developer guide to slices 5.13 (ActionList) and 5.14 (HotkeyEditor). Behaviour specs: `docs/spec/interaction/07-commands.md` (primary), `01-focus-keyboard-shortcuts.md`. It builds on the command model of `docs/dev/commands.md` and the drag hub of `docs/dev/customize.md`; neither is changed.

| Piece | Folder | Depends on | Owns |
|---|---|---|---|
| headless keyboard model | `source/ui-commands/{include/r1ui/commands,src}/keyboard` | `r1ui::commands` only | `KeyboardLayout` (the ANSI caps as data), `collectKeyUsage` (which commands sit on which key for a modifier layer, category and context), `assignKeepingBoth` (the "keep both" resolution) |
| action list | `source/ui-widgets/{include/r1ui/widgets,src}/actions` | commands, `DragHub` | `ActionInfo` and the search rule (`ActionModel.h`), `ActionList` / `ActionListView` (`ActionList.h`) |
| hotkey editor | `source/ui-widgets/{include/r1ui/widgets,src}/hotkeys` | action list, commands | `HotkeyEditor`, `KeyboardView`, `CommandDetailView`, `ChordRecorder`, `buildGalleryHotkeys`, `buildGalleryActions` |
| tests | `tests/ui-commands/keyboard`, `tests/ui-widgets/actions`, `tests/ui-widgets/hotkeys` | | section 6 |

No CMake file is edited (every folder is globbed). The only edit outside these folders is this document.

## 1. ActionList

```cpp
ActionListOptions options;            // columnHeaders, shortcutWidth, showDescriptions, showIcons, showSearch, ...
ActionList& list = ui.create<ActionList>(parent, options);
list.bindRegistry(&registry, &keymap);   // or list.setActions(std::vector<ActionInfo>)
list.setDragHub(&hub);                   // optional: makes rows draggable
list.view().setOnSelect([](const ActionInfo&) {});
list.view().setOnActivate([](const ActionInfo&) {});   // Enter or double click
```

`ActionInfo{id, label, description, category, icon, shortcut, enabled}`. Every text is made well-formed UTF-8 (invalid bytes become U+FFFD, control characters become spaces) and cut at its limit (`kMaxAction*Bytes`; description 4096); at most `kMaxActions` (100 000) actions are taken; an empty category is "General". `actionsFromRegistry(registry, keymap, options)` builds the vector (hidden commands skipped unless asked, shortcut text of both slots from the live keymap, optional sort by label).

**Row** = icon and label, the DESCRIPTION in grey, the shortcut text. Description and label are elided to their column (the full description is the row's tooltip). Columns: label 42 percent of the room up to 300 px, description the rest, shortcut a fixed 140 px column; below 200 px of room the description is dropped. Category headers (sorted case-insensitively, `Category (n)`) collapse by click, Left/Right, Enter or Space; the collapse state survives refreshes and search.

**Search** (`setFilter`, the field above the list): space separated terms, ALL must occur ASCII case-insensitively in label, id, description or category (`matchesAction`). Matches are highlighted in the label and description. While a search is active groups show expanded. A search selects its first hit unless the selected action still matches. At most 16 terms and 256 bytes are used.

**Keyboard**: Up/Down/PageUp/PageDown/Home/End, Enter activates, typing a character in the list moves it to the search box (the character starts the text), Ctrl+F focuses the search box, Up/Down/Enter in the search box drive the list.

**Drag payload** (documented contract for menu creators and other drop targets): pressing an action row and moving past the drag threshold calls `DragHub::begin` with the toolkit's own `DragPayload`:

| Field | Value |
|---|---|
| `kind` | `DragPayload::Kind::Command` |
| `commandId` | `ActionInfo::id` (the command id) |
| `text` | `ActionInfo::label` (the ghost's text) |
| `nodeId` | empty |

That is exactly what a `CommandPalette` row starts, so every `DragTarget` that accepts a palette row (menu, toolbar and free-form editors) accepts an action row. `makeActionDragPayload(action)` builds it for hosts that begin a drag themselves. Escape or losing the pointer capture cancels (no drop); a drop on nothing does nothing; a header row cannot be dragged; with no hub set rows are not draggable. The list ends a running drag when it is destroyed.

**Cost**: only the visible rows are measured and painted (prefix-sum row tops, binary search), the search runs on pre-folded text. Measured with 20 000 actions in the dev build: load 36 ms, one-hit search 1.6 ms, wide search 1.7 ms, clear 0.2 ms, worst paint 4 ms; the widget count does not depend on the action count (tests assert it). Registry changes are coalesced into the next layout pass (`flush()` applies them at once), so registering thousands of commands after the list exists costs one refresh.

**Descriptions**: `commandsWithoutDescription(registry)` returns the ids with an empty or blank description. Hosts (and their tests) can assert it is empty so every command explains itself next to its name.

## 2. HotkeyEditor

```cpp
HotkeyEditor& editor = ui.create<HotkeyEditor>(parent, services /* CommandServices */, HotkeyEditorOptions{});
editor.setOnImport([] { /* host reads the file, calls importOverrides */ });
editor.setOnExport([] { /* host calls exportOverrides */ });
editor.setOnSetsChanged([](const std::string& name, const std::string& json) { /* persist */ });
```

Give it room: it lays out in two columns from 860 logical px (list left, tabs right; the keyboard needs about 500 px to be readable), below that the columns stack. Recommended panel size 1100 x 640 or more.

Layout: the **Hotkey Set** line (set drop-down, Save as..., Import, Export, Reset all); left: "Edit hotkeys for" category drop-down and a context drop-down, the search field, the action list (columns Action | Description | Hotkey, grouped by category); right: tabs **Keyboard** and **Runtime Command Editor**, a Primary/Alternate slot selector, and a one-line message under everything.

**Keyboard tab**: the ANSI keyboard (Esc, F1-F12, number row, QWERTY rows with Tab, Caps, Enter, both Shift, Ctrl, Meta and Alt, Space, Menu, navigation block, arrows; no keypad because the toolkit's `Key` has no keypad codes) drawn from `ansiKeyboardLayout()`. Keys with a hotkey for the shown layer use the accent fill and white text (the toolkit's `accent` token; in the shipped theme it is blue, not teal), unassigned keys are dim, caps without a `Key` code (punctuation, Caps Lock, Menu) are drawn at reduced opacity and ignore clicks. The key of the selected action gets a text-coloured ring. A legend (Assigned Key / Unassigned Key) sits under the board.

- **Layer**: exactly the chords whose modifiers equal the layer. The layer is the OR of the on-screen toggles (the Ctrl/Shift/Alt/Meta buttons, or clicking a modifier cap) and the physical modifiers (key events that reach the editor or the board; `setPhysicalModifiers` for hosts that track them). Selecting an action switches the toggles to the modifiers of its first chord so its key is visible.
- **Filters**: the category drop-down narrows both the list and the keyboard; the context drop-down shows the commands of that context and its ancestors.
- **Hover** a key: tooltip with what is bound to it (`Ctrl+C: Copy`, one line per binding, `[context]` for panel contexts, `unassigned`, or why the cap cannot hold a shortcut).
- **Click a key with an action selected**: assigns (key + layer modifiers) to the chosen slot through `assignChord`. Without a selection: selects the first action bound to the key, or says the key is free.

**Runtime Command Editor tab**: read-only sheet (id, label, description wrapped, category, context, kind, default and current chords, enabled, conflicts of the current chords) plus the slot selector, a `ChordRecorder` and the buttons **Assign** (focus the recorder: modifiers show live, a real key completes, Escape cancels, Shift/Ctrl/Alt+Escape records Escape, focus loss cancels), **Clear** (unbinds the slot) and **Reset to default** (`resetCommand`).

**Conflicts**: an assignment another command holds opens a dialog (modal, Escape = Cancel) naming the combination and the other command:

| Choice | Effect | Offered |
|---|---|---|
| Replace | `assignChord(..., overrideConflicts = true)`: the other command loses the chord wherever it holds it | always |
| Keep both | `assignKeepingBoth`: sets the chord and leaves the other binding; the command of the more specific context wins when its panel has the focus | only when no clash is in the same context (parent or child contexts only) |
| Cancel | nothing changes | always |

Nothing changes until a choice is made (`pending()`, `resolveConflict(...)` for the API and tests). A second assignment while one is pending replaces the pending one.

**Hotkey sets**: a set is a named snapshot of the overrides in the existing keybinding JSON (`exportOverrides`). The editor keeps them in memory (at most `kMaxHotkeySets` = 64, names trimmed and sanitised to 64 bytes). `saveSet(name)` / Save as... stores the current bindings and makes the set active; choosing a name in the drop-down (or `loadSet`) replaces the bindings (`importOverrides`, live); a set without overrides restores the defaults; a rejected file keeps the previous state and says why. The host persists sets: `setOnSetsChanged(name, json)` after each save, `addSet(name, json)` to restore them at start (the content is validated when loaded). Import and Export buttons only call the host's hooks (like the earlier `KeybindingEditor`); Reset all asks for confirmation.

What stays as it was: the `KeybindingEditor` widget and its tests are untouched; the hotkey editor uses the same registry, overrides, keymap, conflicts and file format, so both editors can be used side by side on one `CommandServices`. The search box of the hotkey editor matches label, id, description, category and the hotkey text (`ActionListOptions::searchShortcuts`).

## 3. Headless keyboard model (`r1ui/commands/keyboard/*`)

- `KeyboardLayout`: `KeyCap{id, label, key, modifier role, x, y, w, h}` in key units (main block 15 wide, whole board 18.5 x 6.5). Tests assert unique ids, that every `isRealKey` code appears on exactly one cap, no overlaps, finite positive geometry, and that modifier caps and unnameable caps are not bindable. `capAt` hit-tests in key units.
- `collectKeyUsage(registry, keymap, {category, context}, modifiers)`: bindings in force (a default another command displaced is not listed), first chord of a sequence decides the key (`startsSequence`), registration order inside a key.
- `assignKeepingBoth` / `canKeepBoth`: see the table above.

## 4. Gallery

`buildGalleryHotkeys(ui, parent)` (the editor over about 50 sample commands in ten categories, every one with a description, a `layers` panel context whose Delete coexists with the global Delete, and two unbound actions to try Replace) and `buildGalleryActions(ui, parent)` (an action list plus a drop zone that lists what was dropped, proving the payload). Both own their registry and hub; the integrator registers the pages.

## 5. Not implemented, by design or yet

- No numeric keypad, punctuation or Caps Lock bindings: the toolkit's `Key` enum has no codes for them (the caps are drawn but cannot be clicked to assign).
- No delete or rename of hotkey sets; sets are in memory (host persists).
- Hotkey editor assignment is one chord per click (sequences of two chords can be assigned through `assign()` and are listed, but the keyboard and recorder record a single chord).
- Key release triggers (`:up`) are not recorded; the editor shows them in the Hotkey column but the sheet does not describe them.
- The keyboard does not show which keys are taken in other modifier layers at the same time (switch the layer to see them).
- No auto-scroll while dragging near the edge of the action list.
- No accessibility tree for the drawn keys (the board has one accessible name).

## 6. Tests and evidence

Fast tier (`ctest -L fast`): `ui-commands.keyboard_layout_test`, `key_usage_test`, `keep_both_test`; `ui-widgets.actions.action_model_test`, `action_list_test`, `action_hostile_test` (20 000 actions, caps, invalid UTF-8, 10 000-character descriptions, duplicate ids, zero-sized lists); `ui-widgets.hotkeys.keyboard_view_test`, `hotkey_editor_test`, `hotkey_hostile_test` (empty registry, 8 000 commands, hostile text, a command vanishing during a pending conflict, corrupt sets). GPU tier (`ctest -L gpu`, RTX 4080): `action_list_visual_test` and `hotkey_gpu_test` render the pages in both themes under the artifact directory (`action-list-*.png`, `hotkey-editor-*.png`) and require zero validation messages in Debug trees. The visuals have no reference image; their structure is covered by the headless tests.

# Commands, shortcuts and the keybinding editor (ui-commands, ui-widgets/commands)

Developer guide to slices 5.5 (command registry) and 5.6 (shortcut editor and conflict resolution). Behaviour specs: `docs/spec/interaction/07-commands.md` (primary), `01-focus-keyboard-shortcuts.md`, decisions D14 (sequence chords), D15 (Escape cancels a capture, reset and import are live, shown shortcut text is live) and D20/D21.

## 1. Modules

| Module | Folder | Depends on | What it owns |
|---|---|---|---|
| `r1ui::commands` (`ui-commands`) | `source/ui-commands` | `r1ui::core` only | the headless model: commands, contexts, chords, keymap, conflicts, router, overrides and their file |
| `ui-widgets` commands folder | `source/ui-widgets/{include/r1ui/widgets,src}/commands` | `r1ui::commands` and the widget library | menus, toolbars, key handler, `ChordBox`, `KeybindingEditor`, gallery page |

Tests: `tests/ui-commands` (fast, headless, one executable per `*_test.cpp`) and `tests/ui-widgets/commands` (`*_test` fast, `*_visual_test` and `*_gpu_test` gpu).

## 2. Headless model (namespace `r1ui::commands`)

```
CommandRegistry --- commands (CommandDef) and contexts (parent chain), version counter, listeners
KeybindingOverrides --- per user (command, slot) -> chord | unbound, stamps, batches
Keymap(registry, overrides) --- the bindings in force per context, rebuilt lazily when either version moves
CommandRouter(registry, keymap, clock) --- key events and menu/toolbar clicks -> guarded command execution
assignChord / findConflicts (Conflicts.h) --- conflict report and the "Override" resolution
exportOverrides / importOverrides / loadOverrides / saveOverrides (OverrideIo.h) --- versioned JSON, stores
```

### Command (`CommandDef`)

`id` (1..128 of `[A-Za-z0-9._:/-]`, stable), `label` (required, cut at 256 bytes), `description`, `tooltip` (overrides the description as tooltip), `icon`, `category` (empty = "General"), `context` (default `global`), `kind` (`Action`, `Toggle`, `Radio` with `radioGroup`, `Momentary`), `defaultChords[2]` (`ChordSequence`: primary, alternate), `repeatable`, `hiddenFromEditor`, predicates `enabled`, `checked`, `visible` (cheap, must not throw; missing = enabled, unchecked, visible) and `execute(const ExecuteArgs&) -> ExecuteResult` (`Handled`, `NotHandled`, `Refused(reason)`). A toggle flips its own state inside `execute`. A `Momentary` command receives `Press` on key-down and `Release` on key-up (a menu or toolbar click gives both back to back).

`CommandRegistry::add` refuses (and changes nothing): bad or duplicate id, empty or blank label, bad icon name, unknown context, invalid default chord, radio without a group, more than `kMaxCommands`. Text is sanitised (invalid UTF-8 becomes U+FFFD, control characters become spaces). `remove`, `find`, `commands()` (registration order), `categories()`, `inCategory`, `inRadioGroup`, `version()`, `touch()`, `subscribe/unsubscribe`. Built-in contexts: `global`, `window` (child of global), `text` (child of window, text-entry); `addContext(name, parent, textEntry, description)` adds more.

### Chords (`Chord.h`)

`KeyChord{key, modifiers, onKeyUp}` and `ChordSequence` (one chord or two, `count == 0` is unbound). Text form: `Ctrl+Shift+Z`, sequence `Ctrl+K, Ctrl+C`, release trigger `Ctrl+K:up`; modifier order Ctrl, Cmd, Alt, Shift. `parseChord`, `parseSequence` (case-insensitive, aliases, 128-byte and ASCII limit), `formatChord`, `formatSequence(upperCase)`, `relate()` (Equal, A prefix of B, B prefix of A). Keys are the toolkit `Key` enum; modifier-only presses are `Key::Unknown` and never form a chord.

### Resolution (`Keymap`)

Priority: overrides (newest stamp first), then defaults in registration order, primary before alternate. A candidate is accepted unless an accepted binding of the same context is equal to it (another command), is a prefix of it, or starts with it (D14). The same command holding a chord in both slots is one binding. `effective(id, slot)`, `displayText(id)` (first valid slot), `lookup(context, chord)` (`Exact` / `Prefix` / `None`), `continuations`, `forEachBinding`.

### Conflicts (`Conflicts.h`)

`findConflicts(registry, keymap, id, proposed)` lists every colliding `(command, slot)`: own context first, then ancestors nearest first, then descendants (siblings never conflict, the same command's other slot never conflicts). `assignChord(overrides, keymap, registry, id, slot, chord|nullopt, overrideConflicts)` refuses with the list unless `overrideConflicts`; with it, every listed binding is unbound as an override, the chord is set, and the other slot is pinned to its value in force (unbound when the chord moved). One batch, one registry notification.

### Router (`CommandRouter`)

`handleKey(KeyInput{key, modifiers, repeat, up}, contexts)` where `contexts` are the leaf contexts of the focus path, innermost first (each expanded through its parents, `global` always last). Result `RouteResult{consumed, commandId, result, pendingStarted, sequenceAborted, swallowedByText, reentrant}`.

- In each context the exact binding is tried; a disabled command, a refusal, `NotHandled`, or a repeat of a non-repeatable command lets the next context try (spec 01 scenario 7).
- A text-entry context in the stack makes plain printable keys (letters, digits, Space, with or without Shift) `swallowedByText`; Ctrl, Alt and Meta chords pass.
- Sequences: the first chord of a sequence is consumed and reported through `setPendingObserver(PendingState{active, text, continuations, commandLabels})`; the next key completes it, an unmatched key aborts it (consumed), Escape cancels, modifier-only presses are ignored; the timeout is 1.5 s (`setSequenceTimeoutMs`, clamped 100..60000) on the `Clock` passed to the router, checked by `tick()` and by the next key.
- Key release: `up = true` runs release-trigger chords and the `Release` phase of held momentary commands; `releaseMomentary()` ends all of them.
- Guards: no re-entrancy of the same command, nesting limit 4, key events arriving from inside a command are ignored, exceptions become `Refused`, `setSuppressed(true)` (drag, blocking operation) makes all keys unused.
- `execute(id, source, phase)` is the path of menus, toolbars and the API (checks existence and the enabled predicate).

Clocks: `SteadyClock`, `ManualClock` (tests), `UiClock` (widgets: the `UiContext` time).

### Overrides and their file

`KeybindingOverrides::set/clear/resetCommand/resetAll/replaceAll/find/entries`, `Batch` for one notification, every change calls `registry.touch()` (live). Overrides of unregistered commands are kept dormant and not exported. File (`OverrideIo.h`): `{"format":"r1ui-keybindings","version":1,"overrides":[{"context":"global","command":"edit.undo","slot":0,"chord":"Ctrl+Z"},{"command":"edit.redo","slot":1,"chord":null}]}`; `null` = deliberately unbound. Import validates everything first: whole-file rejection (not JSON, depth over 8, over 1 MiB, wrong format or version, over 20 000 entries) changes nothing; single entries are skipped with an `ImportIssue` (unknown command is counted separately, bad slot, malformed chord, wrong context); a file with no usable entry changes nothing (spec 07 rule 59). `ImportMode::Replace` swaps the table in one step, `Merge` sets entry by entry. Stores: `MemoryKeybindingStore`, `FileKeybindingStore` (temp file and rename, size limit, no exceptions).

## 3. Widget side (namespace `r1ui::widgets`)

`CommandServices{registry, overrides, keymap, router}` bundles the four objects; the host owns them and keeps them alive longer than every widget built from them.

| Piece | Header | Role |
|---|---|---|
| `CommandUiSync` | `CommandUiSync.h` | subscribes to the registry and re-runs attached refreshers and the open-menu refresh; the host calls `refresh()` once per frame (cheap) so predicate changes show; registry changes refresh immediately |
| Menus | `CommandMenus.h` | `buildCommandMenu(services, entries)` (`CommandMenuEntry::command/separator/heading/submenu`), `bindCommandMenuBar(bar, services, titles)` (menus rebuilt right before they open), `openCommandContextMenu`, `refreshOpenCommandMenus` (live text, enabled and checked state in menus that are open) |
| Toolbar | `CommandToolbar.h` | `bindCommandToolbar(ui, toolbar, sync, items)` with `CommandToolbarItem::command/separator/group`; returns a binding handle (`refresh`, `button(id)`); tooltip is `<tooltip or label> (<chord>)` and follows rebinding at once |
| Keys | `CommandKeys.h` | `CommandKeyHandler`, the `GlobalKeyHandler` for `UiContext::setGlobalKeyHandler`; contexts from the focus path through `setContextResolver` (default: `text` for text-taking widgets, else `window`); arms a `UiContext` timer while a sequence is pending; `onKeyUp` for hosts that forward releases |
| Editor | `KeybindingEditor.h`, `ChordBox.h`, `KeybindingFilter.h` | the shortcut table: search, context selector, two-step option, two chord boxes per command, remove, reset command, reset all (confirmation dialog), import and export through `setOnImport/setOnExport`, conflict popup with Override and Cancel |
| Gallery | `GalleryCommands.h` | `buildGalleryCommands(ui, parent)`: 20 sample commands, a menu bar, a toolbar, a context menu, a status line and the editor over the same data |

Mapping of kinds: Action and Momentary become action rows or buttons, Toggle a check row or toggle button, Radio a radio row or a tool button (the checked answer picks the active tool). Invisible commands are skipped (menus collapse stray separators, toolbar buttons take no space). Icons must exist in the icon set.

### Changes made to the existing menu widgets (needed for live menus)

- `MenuPanel::refreshItem(index, spec)` and `MenuItemWidget::refresh(spec)`: update label, shortcut, tooltip, enabled and checked of an open row (a row that becomes disabled loses its highlight).
- `MenuBar::setBeforeOpen(hook)`: lets the owner refresh a title's spec right before its menu opens.

### Capture protocol of the editor (spec 07 rules 42 to 54, D14, D15)

Click (or Enter, Space on a focused box) starts a capture; one box edits at a time. While editing every key belongs to the editor: held modifiers show live, a real key completes the chord, plain Escape cancels (Shift, Ctrl, Alt or Meta with Escape binds it), typed characters and releases are ignored. No conflict: committed at once through `assignChord` (both slots saved as overrides). Conflict: the box stays in edit mode and a popup under it names the combination and the other command; Override unbinds the other command wherever it holds the combination (also in ancestor and descendant contexts) and binds the chord; Cancel, Escape, a click outside or losing the window ends editing without a change; typing another chord re-checks. Losing focus without a conflict ends editing and keeps the old chord. Two-step option: a capture takes two presses (Enter after the first keeps one chord).

## 4. Tests and evidence

Run the fast tier with `ctest -L fast` (all ui-commands and `ui-widgets.commands.*` tests) and the GPU tier with `ctest -L gpu` (`R1UI_GPU=RTX 4080`). The visual tests:

- `command_menu_visual_test`: the hand-written reference menus are turned into commands, rebuilt from the registry, reconciled field by field (nine rows whose reference text is not a chord are patched back and printed) and compared with the same reference crops as `menu_visual_test`; the File menu goes through `bindCommandMenuBar`.
- `command_toolbar_visual_test`: the reference bar built with `bindCommandToolbar`, compared with the same screen regions as `toolbar_visual_test`, and required to be pixel-identical to the hand-built bar.
- `gallery_gpu_test`: renders the gallery page and four editor states (idle, capture, conflict popup, filtered) in both themes under the artifact directory; there is no reference for the editor, so the structure is covered by the golden structural tests in `keybinding_editor_test` and `keybinding_flow_test`.

## 5. Not implemented, by design or yet

- Hidden commands from matching (spec 07 rule 18) are not a flag: `hiddenFromEditor` hides from the editor only; commands stay matchable.
- No toolbar overflow, flyout entries of command toolbars do not refresh their shortcut text or enabled state, and a toolbar is not rebuilt when a module registers commands after it was built.
- Menus are not customised by the user (spec 06): the builders take a fixed layout; the customization slice consumes `CommandMenuEntry` layouts.
- Release-trigger chords are import/API only (the editor captures presses); the router never sees releases from the widget layer unless the host forwards them to `CommandKeyHandler::onKeyUp`.
- The editor uses compact widths by default (name 240, box 168) instead of the spec's 500 and 200; both are options.

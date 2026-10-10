# Custom menus, pie menu and custom workspaces (ui-commands/custommenu, ui-commands/workspace, ui-widgets/pie, ui-widgets/custommenu)

Developer guide to slices 5.15 (custom menus: pie menu and dockable menu), 5.16 (the `.r1mn` file) and 5.18 (custom workspace `.r1ws`). Owner requirements of 2026-10-10 are in the headers of each file; behaviour specs: `docs/spec/interaction/06-user-customization-menus-toolbars.md`, `07-commands.md`, `04-layouts.md`. It builds on `docs/dev/commands.md`, `docs/dev/customize.md` and `docs/dev/docking.md`.

## 1. What exists

| Part | Folder | Depends on | Owns |
|---|---|---|---|
| model, files | `source/ui-commands/{include/r1ui/commands,src}/custommenu` | `r1ui::core` (JSON) and the customize `TextStore` | `CustomMenu.h` (types, limits, validators), `CustomMenuSet.h` (operations, preview, listeners), `CustomMenuIo.h` (`.r1mn`, set store, `CustomMenuStorage`), `CustomMenusMenu.h` (the "Custom Menus" menu as data, command ids), `TextFile.h` (bounded read, atomic write) |
| workspace | `.../workspace` | custommenu (`cleanName`, `TextFile`) | `Workspace.h` (`.r1ws` container), `WorkspaceFolder.h` (the workspaces folder) |
| pie widgets | `source/ui-widgets/{include/r1ui/widgets,src}/pie` | `r1ui::widgets`, commands | `PieGesture.h` (geometry and the gesture state machine, pure), `PieMenu.h` (draws a pie), `PieTrigger.h` (the right-button hold on a widget) |
| dockable menu, glue | `.../custommenu` | `r1ui::widgets`, commands | `CustomMenuPanel.h` (panel, button, dock factory), `CustomMenuCommands.h` (registers the commands behind the menu), `CustomMenusMenu.h` (entries, title, layout node), `GalleryCustomMenus.h` |
| tests | `tests/ui-commands/{custommenu,workspace}`, `tests/ui-widgets/{pie,custommenu}` | | section 8 |

No CMake file was edited (every folder is globbed). The creator window ("Create Custom Menu" with the action list on the right) is a later slice; the model API below is shaped for it.

## 2. The model (`namespace r1ui::commands::custommenu`)

```
CustomMenu{id, serial, kind(Pie|Panel), name, slotCount, entries[], panel{columns, buttonSize, showLabels, width, height}}
MenuEntry{commandId, label, icon}             commandId "" = an empty pie slot; label/icon "" = the command's own
CustomMenuSet                                 the live collection: operations, preview, version, listeners
```

Limits (`CustomMenu.h`): 2000 menus, 256 entries per dockable menu, 100 000 commands in all, names and labels 128 bytes, ids 128 bytes, pie slot counts 4/6/8, panel columns 1..12, button size 24..96 px, panel size 0 (dock decides) or 120..4000 px. Text is cleaned at every boundary (invalid UTF-8 replaced, control characters including NUL become spaces, ends trimmed, cut on a sequence boundary). Names are unique inside the set, ASCII case-insensitive.

**Ids.** `menu.<serial>`, assigned at creation, never reused inside the set (the counter is persisted). A host derives the dock panel id from the serial (for example `base + serial`).

**Operations** (all return `MenuEditResult{ok, error, reason, id, index}`, never throw, change nothing on refusal; `reason` is a sentence for a message line):

| Area | Calls |
|---|---|
| menus | `createMenu(kind, name)`, `renameMenu`, `deleteMenu`, `adopt(menu, CollisionPolicy::Rename|Replace)` (a menu read from a file: fresh id, `Name (2)` or replace keeping the id), `replaceAll` (a store load; validates everything first), `uniqueName(base)` |
| entries | `setSlot(id, index, command, label, icon)`, `clearSlot`, `addEntry(id, command, index|npos, label, icon)`, `moveEntry(id, from, to)`, `removeEntry`, `setEntryAppearance(id, index, label, icon)` |
| pie | `setPieSlotCount(id, 4|6|8)` (refused with `WouldLoseEntries` while a vanishing slot holds a command) |
| panel | `setPanelColumns`, `setPanelButtonSize`, `setPanelShowLabels`, `setPanelSize(w, h)` |
| dry run | `preview(op)` runs any operation above (a lambda over a `CustomMenuSet&`) with the real answer and no effect, no notification (one copy of the set) |

Kind differences: on a pie `setSlot`/`clearSlot` address a slot and `addEntry` fills the first empty slot (`LimitReached` when none), `moveEntry` swaps two slots, `removeEntry` empties a slot; on a panel the entries are a list (`clearSlot` = `removeEntry`, `moveEntry` moves). Unknown command ids are accepted and kept (shown as missing); malformed ones are refused (`InvalidEntry`). `isMissing(entry, exists)` answers "not registered".

`version()` increments on every change; `subscribe(listener)` runs synchronously after a change (a listener may unsubscribe itself; it must not edit the set).

## 3. Files

### `.r1mn` (one menu; `CustomMenuIo.h`)

Version 1, deterministic text (golden test), written atomically (`<file>.tmp` then rename):

```
{ "format":"r1ui-custom-menu", "version":1, "kind":"pie", "name":"Tools", "slotCount":8,
  "slots":[ {"command":"tool.move"}, null, {"command":"tool.rotate","label":"Rotate","icon":"rotate-cw"}, ... ] }
{ "format":"r1ui-custom-menu", "version":1, "kind":"panel", "name":"Quick", "columns":2, "buttonSize":36,
  "showLabels":true, "panelSize":[240,180], "entries":[ {"command":"tool.move","label":"Move","icon":"move"}, ... ] }
```

API: `exportMenuFile`, `parseMenuFile(text)` (result with `ok`, `error`, `menu`, `issues`), `saveMenuFile(menu, path, error)`, `loadMenuFile(path)`, `withMenuExtension(path)`, `importMenuFile(set, path, policy)` / `importMenuText`. **Whole-file rejection** (nothing changes, an error sentence): over 1 MiB, not JSON (invalid UTF-8, duplicate keys and nesting over 8 included), wrong format, no/invalid/newer version, kind not pie/panel, no usable name, pie whose slots do not match `slotCount` (4/6/8), panel without an `entries` list or over 256 entries. **Repairs with an issue**: text cleaned or cut, a malformed command id (empty slot or dropped entry), a bad icon override (dropped), panel settings out of range (defaults). Unknown members are ignored. Unknown command ids are **kept**. Name collisions are the caller's choice (`CollisionPolicy`), reported in `AdoptResult{renamed, replaced, name}`.

### The user's menus (set store)

`{"format":"r1ui-custom-menus","version":1,"serial":N,"menus":[{ "id":"menu.1", ...the members above... }]}`. `exportSet`, `parseSet` (a menu that fails validation or repeats an id or name is skipped with an issue; none usable in a non-empty list rejects the file), `loadMenus(set, TextStore&)` (a rejected file changes nothing and is moved aside as `.corrupt-N` by the existing `FileTextStore`), `saveMenus`, and `CustomMenuStorage(set, store)` which saves after every change (`setAutoSave(false)` to batch, `lastError()`). Use `customize::FileTextStore(<app data>/custom-menus.json)`.

## 4. The pie menu (`ui-widgets/pie`)

**Gesture rules (`PieGesture.h`, pure and tested with an injected clock).** Distances from the press point. Dead zone 24 px: no slot inside. Outside, the slot is the one whose sector (360/slotCount wide, centred on the slot direction) contains the pointer direction, at any distance, so a flick need not reach the slot. Slot 0 points up, clockwise. Empty, disabled and missing slots are never highlighted and never run. The pie is drawn once the button has been held 150 ms; a flick released earlier still selects by direction without ever drawing. Release: over a selectable slot -> execute; over an unselectable one -> cancel; inside the dead zone -> a click (held under 180 ms and the pointer never left the zone) falls back to the host's context menu, anything else (held, or went out and came back) cancels. Escape, capture loss and window deactivation cancel. Non-finite samples are dropped.

**`PieTrigger` (the widget glue).**

```cpp
PieTrigger& area = ui.create<PieTrigger>(parent, services, [&](double x, double y) -> std::optional<CustomMenu> { return currentPie(x, y); });
area.setOnFallback([&](double x, double y) { openContextMenu(x, y); });   // quick right click
area.setOnExecuted([&](const std::string& commandId, const ExecuteResult& r) { ... });  // optional
area.setOnCancelled([&] { ... });                                        // optional
// create the area's content as children of `area`
```

The trigger is a flex container (100 % x 100 %) that listens in the capture phase: a right press inside it asks the provider for a pie at that point; `nullopt` (or a menu that is not a pie, or a throwing provider) leaves the press alone. On a pie the trigger takes the press (children never see it), captures the pointer, and stops the Click the router would send for it, so the host opens its context menu only from `onFallback`. The pie is drawn in the overlay layer (never clipped by the area; the overlay keeps it inside the window, selection still goes by the direction from the press point). The release runs `router.execute(commandId, ExecuteSource::Menu)` after the gesture state is gone (a command may destroy the trigger, open a menu or throw; the result is reported through `onExecuted`). Escape: handled when focus is inside the trigger and by the overlay while the pie is drawn; a host whose focus is elsewhere calls `cancelGesture()` from its key handler. `setGestureConfig` changes the delay, click time and dead zone. Nothing is left behind on any exit (timer, overlay, capture are tested after every path).

`pieSlotViews(services, menu)` gives the `PieSlotView`s (label, icon, filled, selectable, checked, missing) a pie shows right now; `PieMenu` is also usable on its own (gallery, previews).

## 5. The dockable menu (`ui-widgets/custommenu`)

`CustomMenuPanel(services, sync, set, menuId)` shows a menu of kind Panel: a scrolling grid (`columns` equal columns of `buttonSize` px high buttons; a short last row keeps the columns). A button shows icon and label side by side, icon above label when 56 px or higher, or only the icon (labels off). It rebuilds at once when the set changes (any entry or setting edit), and re-reads every command (enabled, checked, label, icon, tooltip with the chord) on `CommandUiSync::refresh()`. A **missing command stays**, dimmed, with the id as label and the tooltip "Command not available: id". A command that throws or whose predicates throw never breaks the panel. A deleted menu leaves a notice; a pie id leaves a pie notice. The panel unsubscribes in `onDetached`; the set, sync hub and services must outlive the `UiContext`.

Dock registration: `PanelDescriptor d = describeCustomMenuPanel(menu, panelId, makeCustomMenuPanelFactory(services, sync, set, menu.id)); panels.add(d);` (title = the menu's name, default float size = the menu's panel size). `PanelRegistry` has no remove: register each menu once, rename with `setTitle`, and let a deleted menu's panel show its notice (or close it from the host).

## 6. "Custom Menus" main menu and its commands

`describeCustomMenusMenu(set)` (headless) is the shape: *Dockable menus* (heading) with one command per panel menu (opens or focuses it), *Pie menus* with one submenu per pie (Edit..., Save As..., Delete), an *Edit Dockable Menu* submenu with the same three per panel menu, then *Create Custom Menu...* and *Load Custom Menu...*. `customMenusMenuEntries(set)` converts it to `CommandMenuEntry`; `customMenusMenuTitle(set)` is the `CommandMenuTitle` for `bindCommandMenuBar`; `customMenusMenuNode(set)` is the layout node for the customizable menu bar (put it last in the built-in `LayoutSet` and call `Customization::setBuiltin` when `set.version()` changes; entry ids are `menu.custom.<commandId>`, stable).

`CustomMenuCommands(registry, set, hooks)` registers the commands (category "Custom Menus"; `custommenu.open.<id>` carries the menu's name; edit/save/delete are hidden from the keybinding editor) and keeps them in step with the set (rename relabels, delete unregisters; the binder never touches a command it did not register; the destructor removes its own). **Hooks** are the host's part: `openPanel(id)`, `edit(id)`, `save(id)` (ask for a path, `saveMenuFile`), `remove(id)` (confirm, then `set.deleteMenu`; default deletes at once), `create()`, `load()` (ask for a path, `importMenuFile`). A missing hook makes its command refuse with a reason; a throwing hook is contained.

## 7. Custom workspace (`ui-commands/workspace`)

`Workspace{name, layout, menus, customization, keybindings}` where each part is optional JSON text supplied and applied by the host (the dock layout text, `exportSet`, `exportCustomization`, the overrides export). File `.r1ws` (version 1): `{"format":"r1ui-workspace","version":1,"name":...,"layout":{...},"menus":{...},"customization":{...},"keybindings":{...}}`; the parts are embedded as JSON (re-serialised compactly, member order kept, numbers in shortest round-trip form, integers above 2^53 not preserved), so a save/load/save cycle is byte-stable. Validation is strict and shallow: whole file up to 32 MiB, each part a JSON object up to 16 MiB and 128 levels, 1 000 000 values, no duplicate keys, valid UTF-8, a usable name, no newer version; a part that is not an object rejects the file. The container never interprets or applies a part. API: `exportWorkspace`, `parseWorkspace`, `saveWorkspaceFile(ws, path, error)` (atomic), `loadWorkspaceFile(path)`, `withWorkspaceExtension`.

`WorkspaceFolder(<app data>/workspaces)`: `save(ws, SaveMode::NewOnly|Overwrite)` (key from the name: ASCII letters, digits, space and `_-.()`, 1..64, no leading/trailing space or dot, no `..`, no Windows device name; `NewOnly` appends ` (2)` instead of overwriting), `list()` (sorted by name; a damaged file is listed with `valid = false` and the reason so it can be removed; at most 1000), `load(key)`, `exists`, `remove(key, error)`. Keys are re-validated on every call, so nothing leaves the folder.

## 8. Tests and evidence

Fast tier: `ui-commands.custom_menu_set_test` (operations, refusals, hostile names, preview, listeners, adopt, 1000 and 2000 menus, 20 random operation sequences), `custom_menu_io_test` (golden `.r1mn`, round trips, rejections, repairs, 4000 mutated and 1000 random inputs, import policies, atomic writes on a real directory including a failing write, the set store with 1000 menus, damaged store moved aside), `custom_menus_menu_test`, `workspace_test`, `workspace_folder_test`; `ui-widgets.pie.pie_gesture_test` (all directions, thresholds, hostile input), `pie_trigger_test` (every gesture path with synthetic pointer input on an injected clock), `ui-widgets.custommenu.custom_menu_panel_test`, `custom_menu_commands_test`, `gallery_test`. GPU tier: `pie_gpu_test`, `custom_menu_panel_gpu_test`, `gallery_gpu_test` (PNGs under the artifact directory, both themes; pixel checks for the accent highlight, dim and empty slots, hover and checked states).

## 9. Not implemented, by design or yet

- The creator window and any dialogs (save, load, confirm delete); the hooks are where they plug in.
- The drop of the owner-rejected "Customize" edit-mode button from the Editor screen (integrator); the customize library is untouched.
- The pie is drawn from the press point with direction selection; at a window edge the overlay shifts it inward (it can then sit off-centre of the pointer). Nested pies, sub-menus inside a pie and per-slot colours do not exist.
- No undo for edits of custom menus (the creator window can use `preview`; session revert can be added over `replaceAll`).
- `PanelRegistry` cannot remove a panel; a deleted dockable menu's open panel shows a notice.
- Keys: a pie is opened with the right button only; no keyboard or touch trigger.
- Workspace parts are not interpreted: applying a layout, delta or overrides is the host's job and uses the owners' own strict loaders.

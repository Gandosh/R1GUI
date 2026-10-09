# Property panels (ui-props and the generated panel)

Developer guide to slices 5.10 and 5.11. `ui-props` is a headless module (links only `r1ui::core`) that declares properties for plain C++ types, models a selection of many objects, edits them with undo, and decides what a panel shows. `ui-widgets/props` turns that model into widgets. The behaviour is specified in `docs/spec/interaction/09-property-binding-undo.md` and decisions D16 (variable binding) and D23 (defaults: categories open, search ignores case).

```
source/ui-props/include/r1ui/props/    Value, Expression, Descriptor, PropertySet, Binding, UndoStack,
                                       ChangeNotifier, PropertyContext, PanelState
source/ui-props/src/                   implementation (ContextEdit.cpp: the one place objects are mutated)
source/ui-widgets/include/r1ui/widgets/props/   PropertyPanel, PropertyRow, PropControls, GalleryProps
tests/ui-props/                        headless tests (fast), support/Samples.h = sample host types
tests/ui-widgets/props/                widget tests (fast), gallery and visual tests (gpu)
```

## 1. Declaring properties

No reflection: declare next to the type, with member pointers or getter/setter pairs.

```cpp
PropertySet<Material> set("Material");
set.category("Appearance");
set.add("color", "Color", &Material::color);
set.add("roughness", "Roughness", &Material::roughness).slider(0.0, 1.0).precision(2).tooltip("Surface roughness");
set.add("shading", "Shading", &Material::shading).value("flat", Shading::Flat, "Flat").value("smooth", Shading::Smooth, "Smooth");
set.category("Texture");
set.add("useTexture", "Use texture", &Material::useTexture).asSwitch();
set.add("texture", "Texture", &Material::texture).enableWhenTrue("useTexture");
set.add("intensity", "Intensity", &Light::intensity, &Light::setIntensity).range(0, 1e6).softRange(0, 1000).unit("lm");
set.defaultsFrom(Material{});            // the values the reset arrow restores
```

- Member types: `bool`, any integer type (checked on write), `float`/`double`, `std::string`, `enum`/`enum class` (named with `value()`), `props::Color` (RGBA float), `Vec2`, `Vec3`. Anything else fails to compile. `slider()` makes a double a Range property.
- Metadata: hard range, soft range, step, unit (display suffix) and `units` (typed suffixes with factors to the stored unit), precision, tooltip, `readOnly`, `hidden`, `advanced`, `multiline`, `password`, `asSwitch`, `notResettable`, `maxLength`, default value, `enableWhen`/`visibleWhen` against another property of the same object (`CompareOp`) or `enableIf<&fn>()`/`visibleIf<&fn>()` (a function pointer or captureless lambda over the object).
- Accessors are erased to two function pointers plus up to 48 bytes of trivially copyable state (`Accessor`); there is no `std::function` and no allocation on the per-object path. Capturing callables are rejected at compile time.
- Declaration mistakes (empty/duplicate/control-character names, bad ranges, a default of the wrong type, invalid UTF-8, too many properties) never throw: the declaration is skipped and listed in `set.errors()` (at most 64 entries). A name collision keeps the first property.
- Order: categories by explicit `category(name, order)` then first appearance; in a category ungrouped properties first, then groups by first appearance, then `order()`, then declaration order.

## 2. The selection model: PropertyContext

`PropertyContext` holds `Target`s (`targetOf(object, set)`): objects of any types, possibly different. Rows are the visible properties that every selected set declares with the same name and kind, in the first object's order. All reads and edits go through it.

| Need | Call |
|---|---|
| Set the selection (rows rebuilt, `Selection` notice) | `setSelection(span<Target>)` |
| Value, mixed flags (whole and per axis/channel), differs-from-default, enabled, visible, read-only, binding | `state(row)` |
| Typed value | `setValue`, `setComponent`, `setText` (units, `Mixed + 5`, enum names, hex colours), `setComputed` |
| Reset | `reset(row)`, `resetCategory(category, group)` |
| Drag gestures | `beginInteraction(row)` ... `endInteraction()` / `cancelInteraction()` |
| Copy and paste as text | `copyValue`, `pasteValue`, `copyGroup`, `pasteGroup` |
| Binding (D16) | `bind`, `unbind`, `refreshBindings`, `BindingProvider`, `BindingState` |
| Live panel | `notifier()`, `notifyExternalChange()` |

Edit semantics (all in `ContextEdit.cpp`): every object is validated first (type, finiteness, enum membership, UTF-8, length; the hard range clamps and is reported as a warning); then the value is written to all objects; a host setter that returns false or throws reverts the objects already written and records nothing; the undo record is made last. An edit that changes nothing records nothing. Results are `EditReport`s (summary code, counts, up to 8 issues); nothing throws. Editing a bound property detaches the binding in the same step.

Edit conditions: a property is enabled only if the condition holds on every selected object and is visible if it holds on any. A condition naming a property the object lacks holds (a typo never hides a property).

Copy/paste text: one value is `formatValue`; a group is `r1props 1` then `name<TAB>escaped value` lines. Passwords are never copied. Pasted text is bounded (16 MiB, 200k lines), a malformed line only affects itself, and unknown names are reported.

## 3. Undo: UndoStack

`UndoStack` records value and binding changes in transactions (`begin`/`commit`/`cancel`; nested begins join). Changes to the same (object, property) collapse to first-old/last-new; entries that end where they started are dropped, so a scrub that returns to its start leaves no step and a no-op edit does not truncate redo. Labels are built from property labels with prefixes the host can replace (`ContextStrings`: "Set ", "Reset ", "Paste ", "Bind ", "Unbind ", "Rebind ").

- Grouping (`EditOptions::mergeable`, or `endInteraction(true)` for a one-notch gesture): consecutive mergeable steps over the same keys within `groupWindowMs` (default 400, tunable; spec 09 names no window) on the injected `Clock` merge into one step; if the merge ends where it began the step disappears. Typed commits, resets and pastes are never mergeable. The widget layer leaves it off by default (`PropertyRowOptions::mergeSteps`) because a NumberField cannot say whether a one-shot change came from typing or from the wheel; when on, a gesture with no interactive (drag) value ends mergeable, so typed commits inside the window group too.
- Budget: bytes per step are estimated; after each commit the oldest steps are dropped until the total fits (256 MiB default, `setBudgetBytes`). A step larger than the whole budget is dropped too.
- `undo()`/`redo()` are all-or-nothing: if a setter fails or throws the partial application is reverted and the step stays. Listeners (`addListener`) see `Committed/Merged/Undone/Redone/Cleared/Evicted/Cancelled/Forgotten`; mutating from a listener is refused.
- Text first: `setTextHook(TextUndoHook*)`; `undoRouted()`/`redoRouted()` ask the hook (a focused text field) before the document history (rule 83). The widget layer decides when a hook is installed.
- Lifetime: steps hold raw object pointers and `PropertySet` pointers. Call `PropertyContext::forgetObject(obj)` (stack and binding store) before destroying an object; keep sets alive as long as the stack. `PropertyContext` turns undo/redo into `Values`/`Structure` notices so views refresh (rule 7).

## 4. Bindings (D16)

A host implements `BindingProvider` (`resolve(source)`, `variables()`); the context keeps the source text per (object, property) in a `BindingStore`. States: Unbound, Bound (all selected bound to one source that resolves to a value of the right kind), Broken (cannot resolve or wrong kind), Mixed. `bind` writes the resolved value in the same step; `refreshBindings()` re-resolves and stores derived values without history steps. The widget shows a bound number as the variable pill (component colour), a broken one in the invalid look with a tooltip "Missing variable: name". The variable button (numeric rows when a provider exists) opens a flyout of variables and Unbind.

## 5. The panel widget

`PropertyPanel(context, panelState, options)` = search box + advanced toggle + `ScrollArea` of `PropertySection`s (collapsible, reset-category action) holding `PropertyRowView`s. Geometry follows the reference: 258 px panel, 12 px side padding, 234 px rows, caption line (11 px muted label + a reserved 16 px reset arrow), 26 px controls; a two-axis row has two 114 px fields.

- Controls by kind: Int/Double `NumberField`; Range `PropSlider` + `NumberField`; Bool `Checkbox` (or `Switch`); Enum `Select`; String `TextInput`; Color `ColorChip` (opens a `ColorPicker` popover) + hex `TextInput` + opacity `NumberField`; Vec2/Vec3 `NumberField`s labelled X, Y, Z.
- Mixed: widgets' kMixed state ("Mixed" muted), per axis for vectors; a Select shows the placeholder "Mixed"; a slider rests mid-track.
- Gestures: scrub, slider drag, colour picker gesture = `beginInteraction` .. `endInteraction` (one step); Escape cancels and restores. Typed text, selects, checkboxes, resets edit once. Right click on a row opens copy/paste/reset/copy-name.
- Refresh: the panel listens to the context's `ChangeNotifier`. Any number of notices before the next tick cost one flush (`setTimer(0)`; nothing stays armed when idle). `Selection` forces a rebuild; other notices recompute the model and rebuild only if the visible rows changed, else refresh row widgets in place. While an interaction is open a structural change is deferred until it ends, so the scrubbed field is never destroyed under the pointer.
- Search is live (no delay) and case-insensitive over label, stored name, category, group and tooltip, every word must match; matching categories open, advanced rows show, and the previous collapse state returns when the search is cleared. `PanelState` (collapse, search, advanced, modified-only) belongs to the host so it can be saved.
- `PropsUiClock` adapts `UiContext::now()` to `props::Clock`.

Not implemented: pinning (give each pinned panel its own context), favourites, per-category advanced dropdown (one global toggle), the inline enable-condition checkbox (rule 65), shift-click copy/paste, category/group copy/paste menus (headless API exists), multi-line text and password masking while typing (a single-line `TextInput`), array and struct rows, the value-column divider, Escape clearing the search box.

## 6. Tests and the gallery

`ctest -L fast`: `ui-props.*` (value, propertyset, context, undo, panelstate, hostile) and `ui-widgets.props.*` (row, panel, golden, gallery, section_action). `ctest -L gpu`: `ui-widgets.props.props_visual_test` (X/mixed number field, Select trigger, generated category header against reference crops) and `gallery_gpu_test` (renders the gallery page for each type and count in both themes to `<artifacts>/ui-widgets/props-gallery-*.png`). `buildGalleryProps(ui, parent)` is the gallery entry: Transform/Material/Light samples, one or two objects (mixed values), undo/redo buttons, a "change from outside" button, a bound and a broken variable. The preview's Gallery mode can mount it like any other page.

Golden strings in `props_golden_test.cpp` come from `PropertyPanel::describe()`; a mismatch prints the actual text so a deliberate change can be reviewed and pasted.

## 7. Defects found on the way

`SectionHeader::onClick` toggled a collapsible section even when the click had been used by a header action button (the section collapsed whenever its action ran). It now ignores a click a child handled; regression test `tests/ui-widgets/props/section_action_test.cpp`.

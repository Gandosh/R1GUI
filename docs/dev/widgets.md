# Writing widgets (ui-widgets)

Developer guide to the Phase 4 widget foundation. Read it with `source/ui-widgets/include/r1ui/widgets/label/Label.h` and `source/ui-widgets/src/label/Label.cpp` open: Label is the complete template widget; copy it.

## 1. Layout of the module and adding a widget

```
source/ui-widgets/include/r1ui/widgets/<widget>/Foo.h     public header
source/ui-widgets/src/<widget>/Foo.cpp                    globbed by CMake (CONFIGURE_DEPENDS)
tests/ui-widgets/<widget>/foo_test.cpp                    fast tier, one executable + one CTest test per file
tests/ui-widgets/<widget>/foo_gpu_test.cpp                GPU tier (offscreen, no window)
tests/ui-widgets/<widget>/foo_visual_test.cpp             GPU tier, compares with a reference crop
```

A widget is added by adding files under its own folder; no CMake file, shared header, style table or test list is edited. CTest names are `ui-widgets.<folder>.<file>`; `_test` files carry the label `fast`, `_gpu_test` and `_visual_test` files `gpu`. Shared test helpers are `tests/ui-widgets/support/TestSupport.h` (expectations, `TestUi`: a headless window with real tokens and fonts) and `VisualSupport.h` (the visual one-liner). `R1UI_ASSETS_DIR`, `R1UI_REFERENCE_DIR` and `R1UI_ARTIFACT_DIR` are defined for every test.

Targets: `r1ui::widgets` (no GPU; textures go through `TextureFactory`, so the whole runtime paints into a CPU `Painter` and is unit tested without Vulkan) and `r1ui::widgets_gpu` (Vulkan texture factory, `renderFrame`, the visual harness; only when the Vulkan SDK exists).

## 2. What a widget is

A widget is a node of `core::tree::WidgetTree` (layout style, flags, rectangles) plus one `WidgetObject` owned by the `UiContext` and reachable from the node. Create with `ui.create<T>(parent, args...)`; destroy with `ui.destroy(id)` (objects are freed once no event dispatch is on the stack, so a handler may destroy its own widget). The constructor must not touch the tree; configure `style()`, flags and children in `onAttached()`.

```cpp
class Foo : public WidgetObject {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows();   // optional: your style rows
  const char* typeName() const override { return "Foo"; }
  void onAttached() override;                                   // style, flags, children
  core::layout::MeasureResult measure(const core::layout::MeasureInput&) override;  // when style().hasMeasure
  void paint(PaintContext&) override;                           // before children
  void paintOver(PaintContext&) override;                       // after children (focus ring, scrollbars)
  void onClick(Event&) override;                                // and the other on* hooks
};
```

Hooks: `onPointerEnter/Leave/Down/Up/Move/Wheel`, `onClick`, `onDoubleClick`, `onDragStart`, `onCaptureLost`, `onKeyDown/Up`, `onTextInput`, `onFocusIn/Out`, `onLayout` (opt in with `setWantsLayoutCallback(true)`), `onStateChanged`, `cursor()`, `tooltipText()` (or `setTooltip`), `accessibleName()` (or `setAccessibleName`), `paintOpacity()`.

### Contracts

- **Units.** Everything in a widget is logical pixels. `PaintContext` converts: `ctx.rect()` (logical, absolute), `ctx.box()` (physical), `ctx.px(v)`, `ctx.scale()`. The Painter has no hidden scaling.
- **Paint.** `paint()` only draws (inside `ctx.box()`, or deliberately outside for shadows); it never mutates the tree or requests layout. Children are painted after `paint()` and before `paintOver()`. Siblings paint in ascending `layer`, absolute ones above in-flow ones (the order the Router hits in). `paintOpacity()` fades the whole subtree.
- **Measure.** Set `style().hasMeasure = true` for a leaf whose content size comes from the widget. `measure()` is a pure function of the widget's state, never mutates the tree, and measures at the display scale (`ui().scale()`) so it equals what is drawn. Content-size changes call `requestLayout()`; appearance changes call `requestPaint()` (state flag changes do it for you).
- **Events.** The base `onEvent` first updates the state flags, then calls the hook. Mark `e.markHandled()` when used; `e.stopPropagation()` hides it from ancestors. Router rules (capture, bubbling, focus on press, Tab navigation, drag threshold) are in `core/events/Router.h`. To capture the pointer call `ui().router().capturePointer(id())` while a button is held.
- **Lifetime.** Keep `WidgetId`s, never raw `Widget*`/`WidgetObject*` across frames; re-check `ui().alive(id)` after calling code that may destroy.
- **Tree limits and hostile input.** `create` throws `std::length_error` at the node limit; validate input at your boundaries and test it (NaN, huge, invalid UTF-8, empty).

### States and the style sheet

`WidgetObject::state()` holds flags `kHover, kPressed, kFocused, kFocusVisible, kDisabled, kSelected, kMixed, kBound, kInvalid`. `styleState()` maps them to `theme::State` bits for `StyleSheet::resolve`:

| Flag | theme::State | Set by |
|---|---|---|
| kHover | kHover | pointer enter / leave (also on ancestors of the hovered widget) |
| kPressed | kActive | left press until release / leave / capture loss |
| kFocused | kFocus | focus in / out |
| kFocusVisible | (none) | focus arrived by keyboard; draw `ctx.focusRing()` when set |
| kDisabled | kDisabled | `setEnabled(false)` (also removes the subtree from hit testing and focus) |
| kSelected, kMixed, kBound, kInvalid | kSelected, kMixed, kBound, kInvalid | `setSelected/Mixed/Bound/Invalid` |

Override `styleState()` to change the mapping (a button that shows focus only when `focusVisible()`). When disabled, hover and active bits are ignored by the style sheet.

Rows: `ctx.style("key")` resolves the key with the widget's state; `ctx.resolve("key", bits)` with explicit bits. Never hard-code a colour: use a row or `ctx.color("token")`. A widget type adds rows by providing `static styleRows()`; `ui.create<T>` registers a table once (keyed by the address of its first row) and a bad row rejects the whole table with a message naming it. Rows reference tokens (`color:NAME`, `space:`, `radius:`, `fontSize:`, `metric:widget.key`, `number:`; grammar in `theme/StyleSheet.h`). Builtin rows from Phase 4: `label.body|muted|caption|heading|title|danger`, `focus.ring`, plus the overlay rows. New colours `danger`, `primary`, `border-strong` exist in both themes (generated by `tools/spec/build_tokens.py`).

### Animation

`ctx.animatedColor(slot, target)` / `animatedValue(slot, target)` return the value to draw now, moving towards `target` over 150 ms with the token easing (0.4, 0, 0.2, 1) and requesting frames while moving. Slots are small per-widget integers. Transitions are instant when animations are disabled or the host has not declared a running frame loop (`ui.setFrameLoopRunning(true)`), which is what tests and offscreen renders get.

### Text and icons

`ctx.drawText(text, style.text, box, TextOptions{padLeft, padRight, align, ellipsis, color})` draws one line vertically centred like CSS (browser-style rounded ascent/descent), shortens with U+2026 on a grapheme boundary and clips when even the ellipsis does not fit. `ui.text().measure(...)` / `fit(...)` for measuring. `ctx.drawIcon("name", logicalSize, box, tint)` draws a Lucide (or `assets/icons/custom`) icon from the coverage atlas, tinted. Both services are shared by all windows (`Services`).

## 3. UiContext, windows and frames

One `UiContext` per OS window; contexts share a `Services` (theme, style sheet, text engine, icon cache) and the RenderDevice. Host loop per iteration: `ui.setTime(ms)`, feed `ui.handlePlatformEvent(e)`, `ui.tick()`, `ui.setViewport(w, h, scale)` on resize / DPI change, then if `ui.needsFrame()`: `renderFrame(ui, target, clear)` (gpu library; it runs `frame()`, `paint()`, `finishPaint()`, endFrame and repaints once when an atlas overflowed). Block on events for at most `ui.msUntilTick()`. `ui.cursor()` is the cursor wanted (map `Cursor` to the OS shape). The target is cleared and repainted completely each frame; `frame()` returns damage rectangles for hosts that can use them. Direct logical-pixel input (`pointerMove`, `pointerDown`, `keyDown`, ...) exists for tests.

## 4. Overlay layer and tooltips

`ui.overlays()` (OverlayManager) draws popups above the tree, never clipped by ancestors. `open(OverlayOptions)` returns `{id, host}`; add the popup content as children of `host` (an `OverlayHost` surface: `OverlaySurface::Popover|Menu|Tooltip|Dialog|None` selects the style rows and shadow). After the next layout the manager measures the host, applies `matchAnchorWidth` and the 80% height cap, positions it with `placePopup` (below / above / right / left / center / manual, flipping at the window edge, pushed inside the window) and shows it.

| Option | Meaning |
|---|---|
| `anchor`, `placement`, `gap`, `flip` | geometry (logical window coordinates) |
| `modal`, `scrim`, `trapFocus` | blocker behind the popup swallows input; Tab stays inside; scrim dims the window |
| `dismissOnOutsidePress`, `outsidePressPassesThrough`, `anchorWidget` | outside press closes it and is also delivered; a press in the anchor widget only closes (toggle) |
| `dismissOnEscape`, `escapeFirst`, `closeAllOnEscape` | Escape closes the top overlay; menus take it before the focused widget and close the whole stack |
| `dismissOnWindowDeactivate` | menus close when the window loses activation |
| `restoreFocus`, `focusOnOpen` | focus returns to where it was (only if focus is gone or was inside the popup, so a command that moved focus wins); optionally focus the first focusable widget on open |
| `interactive`, `fadeInMs` | tooltips are hit-transparent and fade in |
| `onClosed(reason)` | `Programmatic, OutsidePress, Escape, WindowDeactivated, Replaced` |

`close(id)`, `closeAll()`, `setAnchor`, `isOpen`, `stack()`, `topmost()`, `indexContaining(widget)`. Stacking is opening order; the Router hits overlays first because the layer widget has the highest `layer`. Closing may be called from inside the popup's own handlers.

Tooltips (`ui.tooltips()`): any widget with `setTooltip(text)` (nearest ancestor wins) shows it after 50 ms rest + 150 ms, 100 ms fade, placed 12 px right / 8 px below the pointer, following it; never while a button is held, the window is inactive or a modal is open; closed by a press, wheel, leaving the window or the source disappearing. Known gaps: disabled widgets supply no tooltip (the Router does not hit them), the 30 px slide-in of spec 10 rule 3 is not drawn.

## 5. Visual tests

```cpp
// tests/ui-widgets/label/label_visual_test.cpp
const r1test::visual::Build build = [](UiContext& ui, WidgetId parent) {
  Label& l = ui.create<Label>(parent, "Layout", LabelRole::Heading);
  l.style().width = layout::Length::px(234);
  return l.id();                                     // the widget the state applies to
};
R1_EXPECT_MATCHES(build, (r1test::visual::VisualSpec{.reference = "widget-panel-section-title-idle",
                                                     .theme = ThemeId::Dark, .state = VisualState::Idle,
                                                     .profile = "text", .luminance = true}));
```

The harness renders the widget offscreen at the size of the reference crop (6 px padding, top-left aligned, background token `panel`), applies the state (`Hover`: pointer at the centre; `Pressed`; `Focus`: keyboard focus; `Disabled`), compares with `tests/reference/openpencil/<theme>/<reference>.png` through the in-process port of `tools/spec/imgdiff.py` and the profile from `tests/reference/tolerance.json`, prints `failing pixels / max / mean`, and writes `<R1UI_ARTIFACT_DIR>/<reference>-<theme>.png` (plus `.diff.png` with failing pixels in red). `ignore` masks areas of the crop that are not the widget; `luminance` compares brightness only (the reference draws text with LCD subpixel antialiasing, we use grayscale); `scale` renders at another display scale. `renderWidget(build, RenderSpec, paths)` gives the raw image; `inkSum` measures text weight. Icons in visual tests use `AntiAlias::Msaa4` (matches the reference's path rasteriser best).

Calibration numbers live in section 6; the test files that produce them print them.

## 6. Calibrations recorded by the foundation slice (2026-10-09, RTX 3090)

### Text weight

The reference loads Inter Regular only; semibold is a synthetic bold, and its renderer makes light text on dark heavier and dark text on light lighter. `TextEngine` thickens the Regular outlines by `defaultEmboldenPx(size) x strength`; strengths depend on the polarity of the text colour (luminance >= 0.5 = light text):

| Polarity | bold (weight >= 600) | regular (< 600) |
|---|---|---|
| light text (dark theme) | 2.18 | 0.2 |
| dark text (light theme) | 1.0 | 0.1 |

Measured with `text_weight_visual_test` (ink = sum of per-pixel coverage; ratio = ours / reference crop; the previous preview constant 2.0 gave 0.94..0.98 on dark semibold and 1.4 on light):

| Sample | dark theme | light theme |
|---|---|---|
| "Layout" 11 px 600 (section title) | 0.978 | 0.995 |
| "Rectangle" 12 px 600 (panel header) | 1.029 | 0.992 |
| "Design" 12 px 600 (tab) | 1.026 | 0.993 |
| "Blend mode" 11 px 400 (field label) | 1.008 | 0.993 |

All within 3% (the test asserts it). Weights 500 draw like 400: only weights from 600 are synthesised (a 500 sample, the Share button, is not calibrated yet). Advances do not change with weight.

### Icons

`icon_reference_test` compares the rasteriser with Chrome's renders of the 73 reference icons (max / mean absolute channel difference, 0..255). Chrome draws circles and rectangles with analytic coverage and paths with 4-sample multisampling, so no single mode matches exactly; `Msaa4` is the closest:

| Size | Smooth 4x4 (UI default): mean, worst icon | Msaa4: mean, max | outside the provisional `icons` profile |
|---|---|---|---|
| 12 px | 6.96, 20.7 (component) | 4.70, 16.3 | 59 smooth / 60 msaa4 of 73 |
| 14 px | 6.45, 13.1 | 4.48, 11.3 | 61 / 66 |
| 16 px | 5.18, 12.4 | 3.57, 9.9 (blend) | 48 / 54 |
| 24 px | 3.78, 8.5 | 2.88, 7.6 | 54 / 59 |

Max channel difference is 111..128 (two multisample steps on one edge pixel) in every row. The provisional `icons` profile (32 channels, 2% of pixels) is therefore exceeded by most icons at every size: it needs recalibration (a profile of about 128 channels and 8% of pixels would hold); the test asserts the measured means instead (Msaa4 mean of means <= 5.0, worst icon <= 17.5) and does not change the profile. The nested-diamond "apply variable" icon (`assets/icons/custom/apply-variable.svg`, stroke 1.5 plus a centre dot) has an ink ratio of 1.045 against `widget-icon-button-small-idle` and a mean pixel difference of 6.0 (the plain Lucide diamond: 8.5).

### Text placement

The browser rounds ascent and descent to whole pixels and floors the half-leading; `TextEngine::baselineInBox` does the same, which moved 11 px text in an 11 px line up by one pixel to match the reference crops.
## 7. Runtime additions to know when writing widgets (slice 4.17)

- **Frames driven by the widget.** `WidgetObject::wantsContinuousFrames()` (default false): a widget that keeps asking the Invalidator for frames itself (the blinking caret of a focused `TextInput`, an editing `NumberField`) returns true so the animation service does not cancel that request when one of its 150 ms colour transitions ends. The host then renders at the display rate while such a widget has focus and not otherwise (idle with nothing focused is 0 frames).
- **Typed text.** `WidgetObject::wantsTextInput()` (default false): true for a widget that consumes typed characters while focused (text fields, type-ahead lists, an open rename field). The `UiContext` keeps unmodified letters, digits and space away from the application's global shortcut handler while such a widget has focus, so typing "t" never switches the theme.
- **Tabular digits.** `TextOptions::tabular` (and the `tabular` argument of `TextEngine::measure / fit / draw`) selects the OpenType `tnum` figures, as number fields draw them (equal advances); the flag is part of the shaped-run and fit cache keys.
- **Timers.** `UiContext::setTimer(delayMs, callback)` returns an id (0 at the limit `kMaxTimers`) and `cancelTimer(id)` removes it; callbacks run from `tick()` on the context clock (`setTime`), with the timer already removed, so they may set or cancel timers (a timer set by a callback fires on the next tick). Capture `WidgetId`s, never pointers. The shell waits for `msUntilTick()`, so a pending timer costs nothing while idle; nothing may keep a timer armed on a settled screen.
- **Overlay shadow and border.** `OverlayOptions::shadow` names a shadow token that replaces the surface's default (context menus use `overlay`). The `OverlayHost` border is part of its padding (padding row + border width), so content placed in a host needs no 1 px margin of its own; `openPopover` adds `PopoverOptions::padding` on top.
- **OverlayWatch.** Popovers and dialogs put a zero-size `OverlayWatch` in their host. It re-anchors and closes with the owner after every layout pass and polls every 120 ms on the context clock (a hidden anchor needs no layout). It exists only while its overlay is open, so the poll timer is the only scheduled work of an open popover and there is none when nothing is open.
- **Focus from outside a dispatch.** Application code moves focus with `UiContext::focusWidget(id, reason)` and `clearFocus()`, which run inside a dispatch frame; calling `router().focus()` directly can free a widget whose blur handler destroys it while the router still uses it.
- **Pressable.** `Pressable` (button/Pressable.h) is the base of Button, IconButton, Checkbox, Switch and the preview's colour swatch: press, release-inside click, re-arm on return while held, Space / Enter on key-up, pointer cursor, focus ring only for keyboard focus. A subclass implements `activate()`.
- **ActionButton and FlyoutList.** `ActionButton` (section/ActionButton.h) is the private 26 px icon button of `PropertySection` / `PanelHeader` actions (rows `section.action`); `FlyoutList` (toolbar/FlyoutList.h) is the popup list shared by the toolbar's tool-group flyouts and the tab bar's all-tabs list (`openFlyout`).

## 8. Visual test conventions (beyond section 5)

- Whole-widget crops use `r1test::visual::VisualSpec`; crops cut out of a full reference screen use `RegionSpec` (tests/ui-widgets/g3support/RegionCompare.h) with the region `x, y, w, h` in the reference, an optional `clip` (rounded rectangle outside which pixels are ignored: the page behind a floating surface) and the same `profile`.
- `luminance = true` compares brightness only, for every comparison that contains text (the reference has LCD subpixel colour fringes, we draw grayscale). Profiles come from tests/reference/tolerance.json; a profile is never edited to make a test pass.
- `ignore` lists `{x, y, w, h}` masks inside the crop for pixels that are not the widget (a tooltip overlapping the bar, the half-pixel edge columns of a bar whose reference sits at x .5, text rows of a list whose bright regular text is not calibrated). Each mask carries a comment saying why.
- A comparison whose limit is exceeded by a known, documented difference stays red and is listed in the evidence notes; it is not masked away.

## 9. Gallery convention

Every widget group provides one entry function `void buildGalleryXxx(UiContext& ui, WidgetId parent)` (buttons, fields, containers, overlays, editors) that builds one labelled instance of every widget in every state it can show statically (a caption above each, wrapping rows), plus live triggers for what only exists while open (menus, dialogs, toasts). The function builds into `parent`, creates only layout boxes, Labels and the group's widgets, takes all colours from rows and registers nothing global. It is mounted by the preview's Gallery mode (one page per function inside a ScrollArea), by the group's `gallery_test` (fast: it builds and lays out) and by its `gallery_gpu_test` (renders both themes under the artifact directory). A new widget adds its instances to its group's function in the same change that adds the widget.
## 10. Lifetime, fault and frame rules added by the phase 4 review fixes

- **`onDetached` runs once and may destroy.** `UiContext::destroy` calls `onDetached` children first, exactly once per object, even when a hook destroys other widgets of the same subtree (or the subtree's root). The context's destructor does the same for every widget still in the tree, the latest created first (slice 5.12: a native window's context dies with the window while the application's models, registries and notifiers live on, and a widget that unsubscribed from them only in `onDetached` left a dangling callback). Everything a hook talks to must therefore outlive the context (the dock's backend and panel registry, a property context, a command registry). A hook releases only its own state: it must not move focus or open popups, and it does not destroy widgets it does not own (a widget that owns a child may destroy it). `ThumbnailGrid` therefore only forgets its rename box in `onDetached`; the box goes with the subtree.
- **Bookkeeping goes with the widget.** Layout-callback registrations (`setWantsLayoutCallback`) and animation tweens of destroyed widgets are dropped by `destroy`. A widget whose animation slot set changes while it lives (a tab bar closing a tab) calls `ui().releaseAnimation(id(), slot)`; `TabBar` gives each tab a dense slot pair instead of one derived from the tab id.
- **No exception reaches the host from input.** Every input entry point of `UiContext` (`handlePlatformEvent`, `pointer*`, `wheel`, `key*`, `textInput`, `focusWidget`, `clearFocus`), every timer callback and the tooltip tick runs behind a fault boundary: the exception message goes to `UiHost::reportFault` (stderr when unset), `inputFaults()` counts it and the pointer interaction is released. Paint and layout exceptions still propagate to the caller of `frame()` / `paint()` (the shell shows them). Do not rely on the boundary: validate at your own boundaries.
- **`tick()` means "something to draw".** It returns true when a tooltip appeared or went away, or a timer ran and left the context needing a frame. The overlay watch polls every 120 ms but changes nothing, so a menu, popover or dialog left open costs no frames.
- **Modality isolates the keyboard.** While a modal overlay is open the application's global key handler gets nothing except what the overlays decline (Escape goes to the overlays first). A dialog that wants shortcuts sets `OverlayOptions::allowGlobalShortcuts`.
- **Overlays are transactional.** `OverlayManager::open` destroys the blocker and the host it created when a later step throws; an overlay whose host the owner destroyed directly is closed (`Programmatic`, focus restored, `onClosed` called) at the next input or frame hook. A flyout (`openFlyout`) takes `FlyoutOpenOptions::owner` and closes with it.
- **Fades end.** A paint-time animation request (`OverlayHost::paintOpacity`) is cancelled by the same code when the fade is over; an open tooltip is idle after 100 ms.
- **Interaction brackets always close.** A control that told the host "begin" (`ColorPicker` square and sliders, `NumberField` scrub) ends the bracket from `onDetached` if it is destroyed first; the host never keeps an undo transaction open.
- **Paint is read-only.** `TreeView` (slow-click rename, edge auto-scroll) runs its time steps from a context timer; `ThumbnailGrid` notifies a selection change that a finished filter caused from a zero-delay timer, not from paint. Do the same for any new widget: arm a timer, never call the application or move focus from `paint()`.
- **Key repeat.** Activation keys (Enter, Space) act on the first press only (`Event::repeat`); repeats of navigation keys still move.
- **Callbacks are called through a copy** (`const auto callback = onX_; callback(...)`), so a handler may replace its own callback.
- **Icons never throw from paint.** `IconCache::draw` (and `PaintContext::drawIcon`) remembers a (name, size) that failed to load (`failedLoads()`), draws the optional fallback icon or a hollow square, and leaves the Painter's clip and opacity stacks balanced. `IconCache::prepare` still throws, for start-up validation.
- **Text run cache.** `TextEngine::measure` looks runs up without allocating; the cache is cleared at 4096 entries or 4 MiB of text. Code that probes many throw-away substrings (wrapping by bisection) uses `measureTransient`, which does not cache.

## 11. Multi-window rules

The next phase makes floating panels native windows. The runtime was prepared for it; these are the rules and their limits.

- **One `UiContext` per OS window, one `Services` per process (per UI thread).** Contexts share the theme, style sheet, text engine, icon cache and the `RenderDevice`. A theme switch or a newly registered widget style is seen by every context (each notices the revision at its next `frame()` and repaints). `Services::resolve` and `PaintContext::resolve / style` return the `ResolvedStyle` by value, so a style held across `ui.create<NewType>()` (which registers rows) or a theme switch stays valid. Nothing in `Services` may be used from another thread.
- **The glyph atlas has one consumer per window.** `TextEngine` owns the CPU atlas and one GPU texture; each window holds an `AtlasConsumer` (a `UiContext` owns one by default, a shell that also draws text outside the context passes its own with `ui.setAtlasConsumer`). A consumer accumulates the atlas region changed since its own last upload, so a window that finishes its frame first does not consume the dirty region of the others, and a window created later uploads the whole atlas. `consumeAtlasOverflow(consumer)` / `UiContext::consumeRepaint()` report that the window must paint again when the atlas was cleared during its frame, or when another window's frame overlapped its own (the glyph pins belong to the engine, not to a window). Frames of different windows should still run one after the other (`beginFrame` .. `endFrame`); an overlap is detected and repainted, not prevented. The icon atlas works the same way through `IconCache::resetEpoch()`.
- **Popups live in their own window's overlay layer.** `OverlayManager` places menus, selects, popovers, tooltips and dialogs inside the viewport of the context that owns them (`windowMargin` bounds), so a popup cannot extend outside its window: a menu opened from a small floating tool window is clipped to that window. This is the honest limitation of the current design. The recommended design for native popups is a later item: a popup that does not fit asks its window for a borderless, non-activating child window (one more `UiContext` sharing the `Services`, positioned in screen coordinates, closed with the same `OverlayManager` rules and reported to the owner through `onClosed`); until then, floating tool windows must be created large enough for their menus, or open them as dialogs inside the main window.
- **A widget subtree cannot move between contexts.** The tree, router and animation state are per context and `WidgetObject::bind` is one-shot. A panel that moves to another window is rebuilt there from its model. Panel content factories (ui-dock) must therefore support: state kept outside the widgets (a serialisable model: scroll positions, selection, expansion, field values, the active tab), a factory that builds the panel into a given parent of any context from that model (`create(UiContext&, WidgetId parent, const PanelState&)`), a way to read the state back before the old panel is destroyed (`capture()`), and no raw pointers or `WidgetId`s kept by the application across a move (ids are per context). Moving within one context is `ui.reparent` and keeps the widgets (the router is synchronised afterwards).
- **Per-window state to remember when adding a service.** Anything that remembers "since the last frame" (dirty regions, overflow flags, damage) belongs to the window (consumer, epoch), never to a flag on a shared service that the first window to finish consumes.

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

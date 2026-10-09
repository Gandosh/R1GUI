# P4 group g4: menu, popover, tooltip, dialog, toast

Worktree worktree branch (fast-forwarded to main 05cb725 first: the worktree had been cut from an older commit without the widget foundation). Machine: RTX 3090, `R1UI_GPU="RTX 3090"`.

## What was built

| Widget | Files (source/ui-widgets/{include/r1ui/widgets,src}/...) | Notes |
|---|---|---|
| Menu | menu/MenuModel, MenuItems, MenuPanel, MenuController, MenuBar, GalleryOverlays | rows (icon, label, 2nd line, shortcut, chevron or arrow glyph, check, radio, component tone, disabled), separators, headings (empty heading hidden), submenus with spec 10 timings, keyboard (Up/Down no wrap, Home/End, Enter/Space, Right/Left, type-ahead, Tab closes, Escape closes the whole stack in one press as spec 10 rule 37 and 47 say), wheel scrolling and scroll bar, command callback, menu bar (click, 0 s hover switch, toggle, keyboard), chord text helper (spec 07) |
| Popover | popover/Popover, OverlayWatch | anchored, follows its anchor, closes when anchor/owner dies or is hidden, focus in/out, padding and width options |
| Tooltip | tooltip/TooltipContent (+RichTooltips) | measured 26 px box for every tooltip, shortcut after the title, wrapped description, 410 ms timing |
| Dialog | dialog/Dialog, DialogParts | modal, scrim, Escape = cancel, Enter = default action, focus trap, close button, owner watch, stacking, one result callback |
| Toast | toast/Toast, ToastParts | default/warning/error tones, top centre 8 px, stacking, lifetimes 3/5/10 s, hover pause, fade, copy and close buttons, limit |

Gallery: `buildGalleryOverlays(UiContext&, WidgetId)` in menu/GalleryOverlays.h (static previews of every state plus live triggers). `gallery_gpu_test` writes `gallery-overlays-{dark,light}.png`.

## Foundation changes (reconcile at merge)

1. `UiContext::setTimer/cancelTimer` (+ `UiContextTimers.cpp`, `tick`/`msUntilTick` integrate them). Needed for submenu delays, toast lifetimes, popup watch. Test: `runtime/timers_test`.
2. `TooltipManager::setContentFactory` (3 lines): lets the tooltip folder supply the measured box. Default timing is unchanged (50+150 ms); `RichTooltips::install(ui)` sets 50+360 = 410 ms. Merge owner may prefer to change the default.
3. `OverlayManager::pressOutside` and `escape` skipped nothing for tooltips: a visible tooltip (non-interactive overlay above a menu) stopped both the outside-press dismissal and Escape. They now look past non-interactive overlays. Regression test `tooltip/tooltip_test::testTooltipDoesNotShieldMenu` fails without the fix (verified by reverting the file) and passes with it.
4. `OverlayOptions::shadow` + `OverlayHost` shadow override (context menus use the `overlay` shadow, measured).

Not changed but found: the tooltip foundation label was 2 px too short (the 1 px border has no layout effect); the tooltip folder content keeps a 1 px margin. Same rule applies to every overlay surface (menu panel, dialog content, popover padding).

## Visual results (profile `text`, luminance compare; numbers from `ctest -L gpu`, dev tree)

See the table appended in the final report; raw lines are printed by each `*_visual_test`. Summary: surfaces, shadows, borders, radii, row geometry, colours, scrim, positions match (many crops 0 to 1% failing). Every remaining failing crop is dominated by one cause:

- **Text weight of bright regular text on dark.** Reference regular `surface` text is heavier than ours: ink ratio ours/reference 0.77 on "Paste to replace" (dark), 1.105 in the light theme. Probe at strengths 0.1..1.5 (dark, regular strength): ratio 0.72, 0.77 (current 0.2), 0.84, 0.90, 0.97 (0.8), 1.04 (1.0). A regular strength near 0.9 for bright light text would match; the current 0.2 was calibrated on muted grey text. Not changed here (shared TextEngine, owner-calibrated); recommend a luminance-dependent regular strength.
- 1 px offsets of icons in the flyout (icon 1 px right, label 1 px left) and the close icon of the dialog.
- Error toast: reference text is about 3 px narrower and its copy/close icons sit on a 22 px pitch (matched after the change to 16 px slots).

## Not implemented / unverified

- Toast slide of 4 px (fade only), swipe to dismiss, keyboard focus on toast buttons, tooltips over toast buttons.
- Tooltip rule 3 slide, rule 4 (disabled widgets supply no tooltip: Router gap), rule 17 exclusion rectangle for menu rows with an open submenu.
- Menu: no animation of the 0.15 s menu transition; item `tooltip` shows via the normal tooltip manager only.
- Dialog: no scrolling body (clips), no nested drag. Search input, collection tab and table rows of the variables dialog are not dialog widgets.
- Popover has no arrow by design; only the surface of widget-popover-grid-settings is compared (content ignored).
- Menu bar overflow (items cut off at the window edge) not handled.
- Right-click opening is application code (`openContextMenu`).
- OverlayWatch polls every 120 ms while a popover, dialog or anchored menu is open (a hidden anchor needs no layout).

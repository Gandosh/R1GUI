// Owns: gap capture scenes for floating UI: context and menubar menus (item states, submenu), tooltips,
//   toasts, the toolbar flyout, the grid settings popover (switch), the share popover (default-tone text
//   input, disabled buttons) and the assets search input.
// Why: these are the widgets the first pass could not reach; each scene opens its surface, crops the parts
//   and closes it again so the next scene starts from the same screen.
// Callers: tools/spec/capture_gaps.mjs (once per theme), after layers.mjs (inner frame selected).
const POPPER = '[...document.querySelectorAll("[data-reka-popper-content-wrapper]")].map((w) => w.firstElementChild)'
// The tooltip is a plain positioned element (not a popper wrapper): small bordered panel with a shadow.
const TOOLTIP = { css: 'div[class*="shadow-lg"][class*="zoom-in-95"][class*="px-2"][class*="py-1"]' }
const MENU_ITEM = (text) => ({ css: '[role=menuitem],[role=menuitemcheckbox]', textStarts: text })
const ROW = (index) => ({ css: '[data-test-id=layers-item]', index })

export async function overlays(d, theme) {
  const { widget, sleep, clickOn, hoverOn, click, move, must, center, key, away, type, screen, unreachable, c, step, closeAll: escape } = d

  await step(`${theme}/context-menu`, async () => {
    // Context menu on the selected inner rectangle: item states, submenu, toast.
    await d.expandTree(); await clickOn(ROW(4)); await sleep(300)
    await click(410, 335, { button: 'right' }); await sleep(600)
    await screen(theme, 'context-menu-selection', 'canvas context menu for a selected rectangle (disabled, component-tone and submenu items)')
    await widget(theme, 'menu-content', { js: POPPER }, ['idle'], 'context menu content')
    await widget(theme, 'menu-item', MENU_ITEM('Bring forward'), ['idle', 'hover'], 'menu item with shortcut')
    await widget(theme, 'menu-item-disabled', MENU_ITEM('Group selection'), ['idle', 'hover'], 'disabled menu item (no hover change)')
    await widget(theme, 'menu-item-component', MENU_ITEM('Create component'), ['idle', 'hover'], 'component-tone menu item')
    await widget(theme, 'menu-item-submenu', MENU_ITEM('Copy/Paste as'), ['idle', { state: 'open', enter: () => hoverOn(MENU_ITEM('Copy/Paste as')), wait: 900 }], 'submenu trigger, closed then open', { keep: true })
    await screen(theme, 'context-submenu-open', 'context menu with the Copy/Paste as submenu open')
    const sub = { js: `${POPPER}.slice(1)`, index: 0 }
    await widget(theme, 'menu-submenu-content', sub, [{ state: 'idle', enter: async () => {} }], 'submenu content', { keep: true })
    await hoverOn(MENU_ITEM('Copy as SVG')); await sleep(250)
    await widget(theme, 'menu-item-in-submenu', MENU_ITEM('Copy as SVG'), [{ state: 'hover', enter: async () => {} }], 'submenu item hover', { keep: true })
  })

  // Toast: copying the node id raises the default tone toast; a rejected promise raises the error tone.
  await step(`${theme}/toast`, async () => {
    await clickOn(MENU_ITEM('Copy node ID')); await away(); await sleep(400)
    await screen(theme, 'toast-default', 'default tone toast at the top centre after Copy node ID')
    await widget(theme, 'toast', { css: '[data-test-id=toast-item]' }, [{ state: 'default', enter: away }], 'default tone toast')
    await sleep(4200)
    await c.evaluate(`setTimeout(() => { Promise.reject(new Error('Failed to open file: unexpected token')) }, 0)`)
    await sleep(600)
    await screen(theme, 'toast-error', 'error tone toast raised by an unhandled rejection')
    await widget(theme, 'toast', { css: '[data-test-id=toast-item]' }, [{ state: 'error', enter: away }], 'error tone toast')
    await sleep(10800)
    unreachable.push({ item: 'toast warning tone', reason: 'raised only when a pasted clipboard image cannot be resolved in the web build; no reachable UI path in a headless session' })
  })

  // Tooltips with and without a shortcut.
  await step(`${theme}/tooltips`, async () => {
    const tip = TOOLTIP
    // Open delay: poll for the tooltip right after the pointer lands on the toolbar pen button.
    await away(); await sleep(500)
    const pen = await must({ css: 'button', minY: 840, minX: 670, maxX: 690, maxWidth: 40 })
    await move(...center(pen.rect))
    const delay = await c.evaluate(`new Promise((res) => { const t0 = performance.now(); const poll = () => document.querySelector('div[class*="shadow-lg"][class*="zoom-in-95"][class*="px-2"][class*="py-1"]') ? res(Math.round(performance.now() - t0)) : performance.now() - t0 > 3000 ? res(null) : setTimeout(poll, 10); poll() })`)
    await sleep(900)
    await widget(theme, 'tooltip', tip, [{ state: 'toolbar-shortcut', enter: async () => {} }], 'tooltip of a toolbar button with shortcut text (Pen (P))', { keep: true })
    await screen(theme, 'tooltip-toolbar-shortcut', 'tooltip over the toolbar pen button')
    d.measurements[`${theme}/tooltip`].openDelayMs = delay
    await away(); await sleep(400)
    await hoverOn({ css: 'button[aria-label="Flip horizontal"]' }); await sleep(1300)
    await widget(theme, 'tooltip', tip, [{ state: 'panel-icon-button', enter: async () => {} }], 'tooltip of a panel icon button', { keep: true })
    await away(); await sleep(400)
  })

  // Toolbar flyout (chevron beside the frame tool).
  await step(`${theme}/flyout`, async () => {
    await click(...center((await must({ css: 'button', minY: 840, minX: 610, maxX: 625, maxWidth: 14 })).rect)); await sleep(600)
    await screen(theme, 'toolbar-flyout-open', 'frame tool flyout open above the toolbar')
    await widget(theme, 'flyout-content', { js: POPPER }, ['idle'], 'toolbar flyout content')
    await widget(theme, 'flyout-item', { css: '[role=menuitem]', textStarts: 'Section' }, ['idle', 'hover'])
    await escape()
  })

  // Menubar menu: Edit (disabled items) and View (checkbox items).
  await step(`${theme}/menubar`, async () => {
    await clickOn({ css: '[role=menuitem]', text: 'Edit', maxY: 60 }); await sleep(500)
    await screen(theme, 'menubar-edit-open', 'Edit menu open')
    const editDisabled = { css: '[role=menuitem][data-disabled]', index: 0 }
    if (await d.locate(editDisabled)) await widget(theme, 'menu-item-disabled-menubar', editDisabled, ['idle', 'hover'], 'disabled item in a menubar menu')
    await escape()
    await clickOn({ css: '[role=menuitem]', text: 'View', maxY: 60 }); await sleep(500)
    await screen(theme, 'menubar-view-open', 'View menu open')
    const checkItem = { css: '[role=menuitemcheckbox]', index: 0 }
    if (await d.locate(checkItem)) await widget(theme, 'menu-item-checkbox', checkItem, ['idle', 'hover'], 'checkbox menu item')
    await escape()
    await clickOn({ css: '[role=menuitem]', text: 'File', maxY: 60 }); await sleep(500)
    await hoverOn({ css: '[role=menuitem]', textStarts: 'Export selection' }); await sleep(900)
    await screen(theme, 'file-menu-submenu-open', 'File menu with the Export selection submenu open')
    await escape(); await escape()
  })

  // Page row context menu: Delete is disabled while only one page exists.
  await step(`${theme}/page-menu`, async () => {
    await click(...center((await must({ css: 'button', text: 'Page 1', maxX: 20 })).rect), { button: 'right' }); await sleep(500)
    await widget(theme, 'menu-item-disabled-page-delete', { css: '[data-test-id=pages-context-delete]' }, ['idle', 'hover'], 'Delete page while only one page exists')
    await escape(); await away()
  })

  // Grid settings popover: two switches and two native number inputs.
  await step(`${theme}/grid-popover`, async () => {
    await clickOn({ css: 'button[aria-label="Grid settings"]' }); await sleep(700)
    await screen(theme, 'grid-settings-popover', 'canvas grid popover with two switches')
    await widget(theme, 'popover', { js: POPPER }, [{ state: 'grid-settings', enter: away }], 'popover content')
    const sw = { css: 'button[aria-label="Show canvas grid"]', parts: { thumb: 'span' } }
    await widget(theme, 'grid-size-input', { css: 'input[type=number]', index: 0 }, [{ state: 'focus', enter: async () => {} }], 'native number input in the popover, autofocused')
    await widget(theme, 'grid-size-input', { css: 'input[type=number]', index: 1 }, ['idle', 'hover'], 'second input, not focused')
    await widget(theme, 'switch', sw, ['idle', 'hover'], 'switch off (sm 28x16)')
    await key('Tab'); await key('Tab'); await sleep(300)
    if ((await c.evaluate('document.activeElement.getAttribute("aria-label")')) === 'Show canvas grid') await widget(theme, 'switch', sw, [{ state: 'focus', enter: async () => {} }])
    else unreachable.push({ item: `${theme}/switch focus ring`, reason: 'keyboard focus did not land on the switch after two Tab presses' })
    await clickOn(sw); await c.evaluate('document.activeElement.blur()'); await sleep(400)
    await widget(theme, 'switch', sw, [{ state: 'on', enter: away }, { state: 'on-hover', enter: () => hoverOn(sw) }])
    await clickOn(sw); await sleep(300)
    await escape(); await away()
  })

  // Share popover: default-tone text input, disabled accent buttons.
  await step(`${theme}/share-popover`, async () => {
    await clickOn({ css: 'button', text: 'Share' }); await sleep(800)
    await screen(theme, 'share-popover', 'share popover: name input focused, share and join buttons disabled')
    const name = { css: 'input[placeholder="Enter your name"]' }, room = { css: 'input[placeholder="Paste room link or ID"]' }
    await widget(theme, 'text-input', name, [{ state: 'focus', enter: async () => {} }], 'default tone md input, autofocused, placeholder shown')
    await widget(theme, 'text-input', room, ['idle', 'hover'], 'default tone md input with placeholder')
    await widget(theme, 'button-accent-disabled', { css: 'button[disabled]', textStarts: 'Share this file' }, ['idle', 'hover'], 'accent button disabled')
    await widget(theme, 'button-accent-disabled-small', { css: 'button[disabled]', text: 'Join' }, ['idle'], 'small accent button disabled')
    await clickOn(name); await type('Alex'); await sleep(300)
    await widget(theme, 'text-input', name, [{ state: 'filled-focus', enter: async () => {} }], 'typed value')
    await widget(theme, 'button-accent', { css: 'button', textStarts: 'Share this file' }, ['idle', 'hover'], 'accent button enabled after a name was typed')
    for (let i = 0; i < 4; i++) await key('Backspace')
    await escape(); await away()
  })

  // Assets tab search: default-tone sm input.
  await step(`${theme}/assets-search`, async () => {
    await clickOn({ css: 'button', text: 'Assets', maxX: 200, minY: 60, maxY: 90 }); await sleep(500)
    await screen(theme, 'assets-panel', 'assets tab with the search input and no local components')
    const search = { css: '[data-test-id=assets-search]' }
    await widget(theme, 'text-input-sm', search, ['idle', 'hover'], 'default tone sm search input')
    await clickOn(search); await sleep(250)
    await widget(theme, 'text-input-sm', search, [{ state: 'focus', enter: async () => {} }])
    await type('btn'); await sleep(250)
    await widget(theme, 'text-input-sm', search, [{ state: 'filled-focus', enter: async () => {} }])
    for (let i = 0; i < 3; i++) await key('Backspace')
    await clickOn({ css: 'button', text: 'File', maxX: 200, minY: 60, maxY: 90 }); await sleep(400)
    await away()
  })
}

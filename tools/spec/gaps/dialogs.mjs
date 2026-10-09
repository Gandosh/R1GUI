// Owns: gap capture scenes for the variables dialog (overlay, content, table, add menu), the variable
//   binding picker, bound and mixed number fields, the fill picker (tabs, gradient editor, colour sliders).
// Why: dialogs and pickers need a prepared document (variables, a two-rectangle selection, a gradient fill);
//   the scenes create that state through the UI so the references are real app output.
// Callers: tools/spec/capture_gaps.mjs (once per theme), after overlays.mjs. Leaves nothing selected-sensitive
//   behind: the last scene closes every popover.
const POPPER = '[...document.querySelectorAll("[data-reka-popper-content-wrapper]")].map((w) => w.firstElementChild)'
const ROW = (index) => ({ css: '[data-test-id=layers-item]', index })
const DIALOG = '[data-test-id=variables-dialog]'

export async function dialogs(d, theme) {
  const { widget, sleep, clickOn, hoverOn, click, move, must, center, key, away, type, screen, unreachable, c, locate, step, closeAll } = d

  await step(`${theme}/variables-dialog`, async () => {
    // Variables dialog is opened from the empty selection panel.
    await click(900, 650); await sleep(400)
    await clickOn({ css: 'button[aria-label="Open variables"]' }); await sleep(800)
    await screen(theme, 'dialog-variables-empty', 'variables dialog without collections: dim overlay, centred content')
    await widget(theme, 'dialog-overlay', { css: '[class*="bg-black/50"]' }, ['idle'], 'dialog overlay (black at 50%)')
    await widget(theme, 'dialog-content', { css: DIALOG, parts: { title: 'h2[class*="font-semibold"]', header: 'div[class*="border-b"]' } }, ['idle'], 'dialog content, empty state')
    const closeBtn = { js: `[...document.querySelectorAll('${DIALOG} button')].filter((b) => b.querySelector('svg') && !b.textContent.trim())`, index: 0 }
    await widget(theme, 'dialog-close-button', closeBtn, ['idle', 'hover'], 'dialog close icon button')
    const create = { css: '[data-test-id=variables-create-collection]' }
    await widget(theme, 'text-button', create, ['idle', 'hover'], 'neutral text button (Create collection)')
    await clickOn(create); await sleep(700)
    await screen(theme, 'dialog-variables-table-empty', 'variables dialog with an empty collection table')
    await widget(theme, 'dialog-search-input', { css: '[data-test-id=variables-search-input]' }, ['idle', 'hover'], 'search field in the dialog header')
    await widget(theme, 'dialog-collection-tab', { css: '[data-test-id=variables-collection-tab]' }, ['idle', 'hover'], 'collection tab')
    const add = { css: '[data-test-id=variables-add-variable]' }
    await widget(theme, 'text-button-add', add, ['idle', 'hover'], 'Add button with chevron in the dialog footer')
    await clickOn(add); await sleep(600)
    await screen(theme, 'dialog-variables-add-menu', 'add variable menu: items with a title and a description line')
    await widget(theme, 'menu-content-described', { js: POPPER }, ['idle'], 'menu with two-line items')
    await widget(theme, 'menu-item-described', { css: '[role=menuitem]', textStarts: 'Number' }, ['idle', 'hover'], 'menu item with title and description')
    await clickOn({ css: '[role=menuitem]', textStarts: 'Number' }); await sleep(500)
    await clickOn(add); await sleep(500)
    await clickOn({ css: '[role=menuitem]', textStarts: 'Color' }); await sleep(600)
    await screen(theme, 'dialog-variables-table', 'variables dialog with a number and a colour variable')
    await widget(theme, 'variable-row', { css: '[data-test-id=variable-row]' }, ['idle', 'hover'], 'variable table row')
    await closeAll()
    if (await locate(closeBtn)) await clickOn(closeBtn)
    await sleep(500)
  })

  // Bound and mixed number fields, binding picker.
  await step(`${theme}/binding`, async () => {
    await d.expandTree(); await clickOn(ROW(1)); await sleep(300)
    await click(...center((await must(ROW(2))).rect), { shift: true }); await sleep(500)
    const fx = { css: '[role=spinbutton][data-property=x]' }
    await widget(theme, 'number-field-x-mixed', fx, ['idle', 'hover'], 'two rectangles selected with different X: value shows Mixed')
    await clickOn(ROW(4)); await sleep(500)
    const apply = { css: 'button[aria-label="Apply variable"]', minX: 1180, index: 0 }
    await widget(theme, 'icon-button-variable-trigger', apply, ['idle', 'hover'], 'apply variable trigger (20x20) in a number field')
    await clickOn(apply); await sleep(700)
    await screen(theme, 'binding-picker-open', 'binding picker: search row, variable item, create footer')
    await widget(theme, 'binding-picker', { js: '[...document.querySelectorAll("[data-slot=search]")].map((e) => e.closest("[data-slot=content]"))' }, ['idle'], 'binding picker popover')
    await widget(theme, 'binding-picker-search', { css: '[data-slot=search]' }, [{ state: 'focus', enter: async () => {} }], 'search row, autofocused')
    await widget(theme, 'binding-picker-item', { css: '[role=option]', index: 0 }, [{ state: 'idle', enter: async () => { await move(2, 450) } }, 'hover'])
    await widget(theme, 'binding-picker-action', { css: '[data-slot=footer] [data-slot=action]', index: 0 }, ['idle', 'hover'], 'footer action row')
    await clickOn({ css: '[role=option]', index: 0 }); await sleep(700)
    await away()
    const pill = { css: '[data-slot=pill]', minX: 1180, index: 0, parts: { label: 'span' } }
    await widget(theme, 'binding-pill', pill, ['idle', 'hover'], 'bound variable pill replacing the value (component colour)')
    await widget(theme, 'number-field-width-bound', { css: '[role=spinbutton][data-property=width],[data-property=width]', index: 0 }, ['idle'], 'width field bound to a variable')
    await screen(theme, 'number-field-bound', 'width bound to a variable')
  })

  // Fill picker: solid, gradient, image tabs and colour sliders.
  await step(`${theme}/fill-picker`, async () => {
    const swatch = { css: 'button[aria-label="Fill"]', minX: 1180 }
    await clickOn(swatch); await sleep(800)
    await screen(theme, 'fill-picker-solid', 'fill picker, solid tab')
    const tab = (n) => ({ css: `[data-test-id=fill-picker-tab-${n}]` })
    await widget(theme, 'fill-picker-tab', tab('solid'), [{ state: 'active', enter: away }], 'solid tab active')
    await widget(theme, 'fill-picker-tab', tab('gradient'), ['idle', 'hover'], 'inactive tab (idle, hover)')
    await widget(theme, 'color-slider-hue', { css: '[data-test-id=color-slider-hue]', parts: { label: 'span', track: '[data-slot=slider]', thumb: '[class*="border-white"][class*="rounded-full"]' } }, ['idle', 'hover'], 'hue slider (12 px track, 14 px thumb)')
    await widget(theme, 'color-slider-alpha', { css: '[data-test-id=color-slider-alpha]', parts: { label: 'span', track: '[data-slot=slider]', thumb: '[class*="border-white"][class*="rounded-full"]' } }, ['idle', 'hover'], 'alpha slider over a checkerboard')
    await clickOn(tab('gradient')); await sleep(800)
    await screen(theme, 'gradient-editor', 'fill picker, gradient tab: type select, bar with two stops, stop list')
    await widget(theme, 'fill-picker-tab', tab('gradient'), [{ state: 'active', enter: away }])
    const bar = { css: '[data-test-id=fill-picker-gradient-bar]' }
    await widget(theme, 'gradient-bar', bar, ['idle'], 'gradient bar with stop handles')
    const stops = (i) => ({ js: `document.querySelector('[data-test-id=fill-picker-gradient-bar]').querySelectorAll('[class*="cursor-grab"]')`, index: i })
    await widget(theme, 'gradient-stop', stops(0), ['idle', 'hover'], 'stop handle (14 px, active)')
    await widget(theme, 'gradient-stop-inactive', stops(1), ['idle', 'hover'], 'second stop handle, inactive')
    const rows = { js: `[...document.querySelectorAll('[class*="py-0.5"][class*="gap-1"][class*="flex"]')].filter((e) => e.querySelector('input') && e.querySelector('[role=spinbutton]'))` }
    await widget(theme, 'gradient-stop-row', { ...rows, index: 0 }, ['idle', 'hover'], 'stop list row, active (hover tint)')
    await widget(theme, 'gradient-stop-row-inactive', { ...rows, index: 1 }, ['idle', 'hover'])
    await widget(theme, 'icon-button-add-stop', { css: '[data-test-id=fill-picker-add-stop]' }, ['idle', 'hover'], 'add stop (16 px icon button)')
    // Select the second stop from the bar, then drag it without releasing to show the grabbing state.
    const s1 = await must(stops(1))
    const [bx, by] = center(s1.rect)
    await click(bx, by); await sleep(400)
    await screen(theme, 'gradient-editor-stop-selected', 'second stop selected: handle white, list row tinted')
    await widget(theme, 'gradient-stop-inactive', stops(1), [{ state: 'selected', enter: away }])
    await move(bx, by); await d.mouse('mousePressed', bx, by)
    for (let i = 1; i <= 5; i++) await d.mouse('mouseMoved', bx - i * 8, by, { buttons: 1 })
    await sleep(300)
    await widget(theme, 'gradient-bar', bar, [{ state: 'dragging', enter: async () => {} }], 'second stop dragged left, button still down (grabbing)', { keep: true })
    await d.mouse('mouseReleased', bx - 40, by); await sleep(300)
    await clickOn(tab('image')); await sleep(700)
    await screen(theme, 'fill-picker-image', 'fill picker, image tab')
    await closeAll(); await away()
    unreachable.push({ item: 'default-tone text input invalid / mixed / bound states', reason: 'AppInput exposes state variants but no screen in the app passes anything except idle; capturing them would need a modified build' })
  })
}

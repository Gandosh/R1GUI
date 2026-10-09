// Owns: gap capture scenes for the layer tree, the constraints diagram, the layout alignment grid and the checkbox.
// Why: these widgets only exist once the document has nested frames; the scenes work on the document that
//   capture_gaps.mjs builds (outer frame with two rectangles and an inner frame holding one rectangle).
// Callers: tools/spec/capture_gaps.mjs (once per theme). Row indexes: 0 outer frame, 1 and 2 rectangles,
//   3 inner frame, 4 inner rectangle. Ends with the inner frame selected and auto layout added.
const ROWS = '[...document.querySelectorAll("[data-test-id=layers-item]")]'
const ROW = (index) => ({ css: '[data-test-id=layers-item]', index })
const TREE = { css: '[data-test-id=layers-item]', union: true }
const inRow = (row, slot, index = 0) => ({ js: `${ROWS}[${row}].querySelectorAll("[data-slot=${slot}]")`, index })

export async function layers(d, theme) {
  const { widget, sleep, clickOn, hoverOn, click, must, center, key, away, step } = d

  // A selection made on the canvas leaves keyboard focus outside the tree: selected row without focus.
  await step(`${theme}/rows`, async () => {
    await away()
    await widget(theme, 'layer-tree', TREE, ['idle'], 'layer tree with nested frames, inner rectangle selected from the canvas')
    await widget(theme, 'layer-row-selected-unfocused', ROW(4), ['idle', 'hover'])
    await widget(theme, 'layer-row', ROW(1), ['idle', 'hover'], 'unselected row; hover shows the row actions')
    await widget(theme, 'layer-row-action', inRow(1, 'action', 1), [{ state: 'row-hover', enter: () => hoverOn(ROW(1)) }, 'hover'], 'eye action of a hovered row')
  })

  // Disclosure chevron of the inner frame: expanded, hover, collapsed.
  await step(`${theme}/disclosure`, async () => {
    await widget(theme, 'layer-disclosure', inRow(3, 'disclosure'), ['idle', 'hover'], 'inner frame disclosure, expanded')
    await clickOn(inRow(3, 'disclosure')); await sleep(350)
    await widget(theme, 'layer-disclosure', inRow(3, 'disclosure'), [{ state: 'collapsed', enter: away }])
    await widget(theme, 'layer-tree', TREE, ['collapsed-frame'])
    await clickOn(inRow(3, 'disclosure')); await sleep(350)
  })

  // Hidden node: the eye action toggles visibility; the row drops to 50% opacity.
  await step(`${theme}/hidden`, async () => {
    await hoverOn(ROW(1))
    await clickOn(inRow(1, 'action', 1)); await sleep(350)
    await widget(theme, 'layer-row-hidden', ROW(1), ['idle'], 'row of a hidden layer')
    await hoverOn(ROW(1))
    await clickOn(inRow(1, 'action', 1)); await sleep(350)
    await away()
  })

  // Inline rename: double click on the label.
  await step(`${theme}/rename`, async () => {
    const label = await must(inRow(1, 'label'))
    await click(...center(label.rect), { count: 2 }); await sleep(450)
    await widget(theme, 'layer-row-rename', { css: 'input.border-accent.bg-input', up: 1, parts: { input: 'input', icon: 'svg' } }, [{ state: 'focus', enter: async () => {} }], 'inline rename input in a layer row')
    await key('Escape'); await sleep(300)
    // In this build a plain click leaves the tree's focus flag off; leaving the inline rename input sets it
    // (its focusin runs, the removed input never reports a focusout), so the selected row now shows the focused fill.
    await widget(theme, 'layer-row-selected-focused', ROW(1), ['idle', 'hover'], 'selected row while the tree reports focus (after the rename input closed)')
    await widget(theme, 'layer-tree', TREE, ['selected-focused'])
  })

  // Constraints diagram for the rectangle inside the inner frame.
  await step(`${theme}/constraints`, async () => {
    await clickOn(ROW(4)); await sleep(400)
    const diagram = '[...document.querySelectorAll("[class*=\\"w-[72px]\\"]")]'
    const pins = (active) => ({ js: `${diagram}.flatMap((g) => [...g.querySelectorAll("button")]).filter((b) => b.className.includes("border-accent") === ${active})`, index: 0 })
    await widget(theme, 'constraints-diagram', { js: diagram }, ['idle'], 'constraints diagram with left and top pins active')
    await widget(theme, 'constraints-pin-active', pins(true), ['idle', 'hover'])
    await widget(theme, 'constraints-pin', pins(false), ['idle', 'hover'], 'an inactive constraint pin')
    await d.screen(theme, 'constraints-selected', 'rectangle inside a frame selected: constraints section visible')
  })

  // Auto layout on the inner frame: alignment grid and the clip-content checkbox.
  await step(`${theme}/auto-layout`, async () => {
    await clickOn(ROW(3)); await sleep(400)
    await clickOn({ css: 'button[aria-label="Add auto layout"]' }); await sleep(700)
    await away()
    await d.screen(theme, 'auto-layout-section', 'inner frame with auto layout: direction, spacing, clip content, alignment grid')
    const grid = '[...document.querySelectorAll("[class*=\\"grid-cols-3\\"]")].filter((g) => g.className.includes("w-fit"))'
    const cells = (active) => ({ js: `${grid}.flatMap((g) => [...g.children]).filter((b) => b.className.includes("border-accent") === ${active})`, index: 0 })
    await widget(theme, 'layout-alignment-grid', { js: grid }, ['idle'])
    await widget(theme, 'layout-alignment-cell-active', cells(true), ['idle'])
    await widget(theme, 'layout-alignment-cell', cells(false), ['idle', 'hover'])
    const check = { css: 'input[type=checkbox]' }
    await widget(theme, 'checkbox-clip-content', check, ['idle', 'hover'], 'clip content checkbox, unchecked')
    await clickOn(check); await sleep(300)
    await widget(theme, 'checkbox-clip-content', check, [{ state: 'checked-hover', enter: () => hoverOn(check) }, { state: 'checked', enter: away }])
    await clickOn(check); await away()
  })
}

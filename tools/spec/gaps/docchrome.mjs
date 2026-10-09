// Owns: gap capture scenes for the document tab bar and the thin scrollbar.
// Why: the tab bar only exists with two documents, and it shifts the whole window down by 36 px, so these
//   scenes run last; the scrollbar needs an overflowing panel and a browser that does not hide scrollbars.
// Callers: tools/spec/capture_gaps.mjs (once per theme), after dialogs.mjs. The document with the nested
//   frames is the first tab; the new tab is blank.
const ROW = (index) => ({ css: '[data-test-id=layers-item]', index })
const TAB = (index) => ({ css: '[data-test-id=tabbar-tab]', index })
const CLOSE = (index) => ({ css: '[data-test-id=tabbar-close]', index })

export async function docChrome(d, theme) {
  const { widget, sleep, clickOn, hoverOn, move, away, screen, c, measurements, unreachable, step } = d

  await step(`${theme}/tab-bar`, async () => {
    await clickOn({ css: '[role=menuitem]', text: 'File', maxY: 60 }); await sleep(500)
    await clickOn({ css: '[role=menuitem]', textStarts: 'New' }); await sleep(1500)
    await screen(theme, 'tab-bar-two-documents', 'tab bar with an inactive and an active document tab and the new-tab button')
    await widget(theme, 'tab-bar', { ...TAB(0), up: 2 }, ['idle'], 'tab bar root (36 px, bottom border)')
    await widget(theme, 'tab-bar-tab-inactive', TAB(0), ['idle', 'hover'], 'inactive tab; hover reveals the close button')
    await widget(theme, 'tab-bar-tab-active', TAB(1), ['idle', 'hover'], 'active tab with its close button visible')
    await widget(theme, 'tab-bar-close', CLOSE(1), ['idle', 'hover'], 'close button of the active tab')
    await widget(theme, 'tab-bar-new', { css: '[data-test-id=tabbar-new]' }, ['idle', 'hover'], 'new tab button (36x36)')
    await hoverOn(CLOSE(1)); await sleep(1300)
    await widget(theme, 'tooltip', { css: 'div[class*="shadow-lg"][class*="zoom-in-95"][class*="px-2"][class*="py-1"]' }, [{ state: 'tab-close', enter: async () => {} }], 'tooltip of the tab close button', { keep: true })
    await away(); await sleep(400)
  })

  // Back to the first document: with auto layout selected the design panel overflows and shows the scrollbar.
  await step(`${theme}/scrollbar`, async () => {
    await clickOn(TAB(0)); await sleep(1000)
    await d.expandTree()
    await clickOn(ROW(3)); await sleep(600)
    const panel = { js: '[...document.querySelectorAll(".scrollbar-thin")].filter((e) => e.scrollHeight > e.clientHeight + 2 && e.getBoundingClientRect().x > 1100)', index: 0 }
    const found = await d.locate(panel)
    if (!found) {
      unreachable.push({ item: `${theme}/scrollbar`, reason: 'no panel overflowed at 900 px height' })
      return
    }
    const edge = (f) => [f.rect.x + f.rect.width - 3, f.rect.y + 40]
    await widget(theme, 'scrollbar', panel, ['idle', { state: 'thumb-hover', enter: async (f) => { await move(...edge(f)) } }], 'thin scrollbar of the design panel (native, not hidden)')
    const pseudo = await c.evaluate(`(() => {
      const e = [...document.querySelectorAll('.scrollbar-thin')].find((x) => x.scrollHeight > x.clientHeight + 2 && x.getBoundingClientRect().x > 1100)
      const r = {}
      for (const part of ['::-webkit-scrollbar', '::-webkit-scrollbar-track', '::-webkit-scrollbar-thumb']) {
        const cs = getComputedStyle(e, part)
        r[part] = { width: cs.width, height: cs.height, backgroundColor: cs.backgroundColor, borderRadius: cs.borderRadius }
      }
      const cs = getComputedStyle(e)
      r.element = { scrollbarWidth: cs.scrollbarWidth, scrollbarColor: cs.scrollbarColor, overflowY: cs.overflowY, clientWidth: e.clientWidth, offsetWidth: e.offsetWidth }
      return r
    })()`)
    measurements[`${theme}/scrollbar`].pseudo = pseudo
    await screen(theme, 'scrollbar-design-panel', 'design panel scrolled content with the thin scrollbar')
    await away()
  })
}

// Owns: reproducible capture of OpenPencil reference screenshots and computed-style measurements.
// Why: Phase 1 needs pixel references and exact widget metrics from the real app build, produced by
//   a script rather than by hand so they can be regenerated (tokens, widget states, full screens).
// Usage: node tools/spec/capture.mjs <openpencil-dist-dir> <out-dir> <measurements.json>
// Callers: developers at phase boundaries. Not shipped. Reference material only (never a build input).
import { mkdir, writeFile } from 'node:fs/promises'
import { join } from 'node:path'
import { serveDir, launchChrome, connect } from './lib/chrome.mjs'

const [dist, outDir, measurePath] = process.argv.slice(2)
if (!dist || !outDir || !measurePath) throw new Error('usage: capture.mjs <dist> <outDir> <measurements.json>')
const SOURCE_COMMIT = 'dd3161e44' // OpenPencil checkout used for the reference build
const VIEW = { width: 1440, height: 900 }

const server = await serveDir(dist, 4181)
const chrome = await launchChrome({ port: 9334, ...VIEW })
const c = await connect(9334)
const manifest = []
const measurements = {}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

async function shot(theme, name, clip, extra = {}) {
  const dir = join(outDir, theme)
  await mkdir(dir, { recursive: true })
  const params = { format: 'png' }
  if (clip) params.clip = { x: clip.x, y: clip.y, width: clip.width, height: clip.height, scale: 1 }
  const r = await c.send('Page.captureScreenshot', params)
  await writeFile(join(dir, `${name}.png`), Buffer.from(r.data, 'base64'))
  manifest.push({ theme, name, file: `${theme}/${name}.png`, viewport: VIEW, clip: clip ?? null, ...extra })
}

const mouse = (type, x, y, o = {}) => c.send('Input.dispatchMouseEvent', { type, x, y, button: 'left', clickCount: 1, ...o })
const move = (x, y) => mouse('mouseMoved', x, y, { button: 'none', clickCount: 0 })
async function click(x, y, button = 'left') { await move(x, y); await mouse('mousePressed', x, y, { button }); await mouse('mouseReleased', x, y, { button }) }
async function drag(x1, y1, x2, y2) {
  await move(x1, y1); await mouse('mousePressed', x1, y1)
  for (let i = 1; i <= 8; i++) await mouse('mouseMoved', x1 + ((x2 - x1) * i) / 8, y1 + ((y2 - y1) * i) / 8, { buttons: 1 })
  await mouse('mouseReleased', x2, y2)
}
async function escape() {
  await c.send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Escape', code: 'Escape', windowsVirtualKeyCode: 27 })
  await c.send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Escape', code: 'Escape', windowsVirtualKeyCode: 27 })
}

// Resolves a widget spec to a rect in the page. spec: { css, text?, minX?, maxX?, minY?, maxY?, index? }
const FIND = `(spec) => {
  let els = [...document.querySelectorAll(spec.css)].filter((e) => { const r = e.getBoundingClientRect(); return r.width > 0 && r.height > 0 })
  if (spec.text) els = els.filter((e) => (e.textContent || '').trim() === spec.text)
  if (spec.minX != null) els = els.filter((e) => e.getBoundingClientRect().x >= spec.minX)
  if (spec.maxX != null) els = els.filter((e) => e.getBoundingClientRect().x <= spec.maxX)
  if (spec.minY != null) els = els.filter((e) => e.getBoundingClientRect().y >= spec.minY)
  if (spec.maxY != null) els = els.filter((e) => e.getBoundingClientRect().y <= spec.maxY)
  if (spec.maxHeight != null) els = els.filter((e) => e.getBoundingClientRect().height <= spec.maxHeight)
  const e = els[spec.index ?? 0]
  if (!e) return null
  const r = e.getBoundingClientRect()
  const cs = getComputedStyle(e)
  const pick = ['fontFamily','fontSize','fontWeight','lineHeight','letterSpacing','color','backgroundColor','borderTopWidth','borderTopColor','borderTopLeftRadius','paddingTop','paddingRight','paddingBottom','paddingLeft','gap','boxShadow','opacity','textTransform','cursor']
  const style = {}; for (const k of pick) style[k] = cs[k]
  return { rect: { x: r.x, y: r.y, width: r.width, height: r.height }, style }
}`
async function locate(spec) { return c.evaluate(`(${FIND})(${JSON.stringify(spec)})`) }

const WIDGETS = [
  { name: 'menubar-item', spec: { css: '[role=menuitem]', text: 'File' } },
  { name: 'segmented-control', spec: { css: 'button', text: 'File', maxX: 200, minY: 60, maxY: 90 }, grow: 'parent' },
  { name: 'page-row', spec: { css: 'button', text: 'Page 1', maxX: 20 } },
  { name: 'tab-design', spec: { css: '[role=tab]', text: 'Design' } },
  { name: 'tab-code', spec: { css: '[role=tab]', text: 'Code' } },
  { name: 'share-button', spec: { css: 'button', text: 'Share' } },
  { name: 'toolbar-button', spec: { css: 'button', minY: 840, maxX: 560, minX: 540 } },
  { name: 'toolbar-button-active-theme', spec: { css: 'button[aria-label="Use dark theme"]' } },
  { name: 'icon-button-panel', spec: { css: 'button[aria-label="Flip horizontal"]' } },
  { name: 'icon-button-small', spec: { css: 'button[aria-label="Apply variable"]' } },
  { name: 'number-field-x', spec: { css: '[role=spinbutton][data-property=x]' } },
  { name: 'number-field-width', spec: { css: '[role=spinbutton][data-property=width]' } },
  { name: 'number-field-opacity', spec: { css: '[role=spinbutton][data-property=opacity]' } },
  { name: 'select-trigger', spec: { css: 'button[aria-label="Blend mode"]' } },
  { name: 'paint-field', spec: { css: '[data-slot=paint-field]' } },
  { name: 'panel-header-rectangle', spec: { css: '[data-slot=root]', minX: 1180, minY: 80, maxY: 90 } },
  { name: 'panel-section-title', spec: { css: '[data-slot=title]', text: 'Layout' } },
  { name: 'panel-field-label', spec: { css: '[data-slot=label]', text: 'Blend mode' } },
  { name: 'layer-row-selected', spec: { css: 'div,span,button', text: 'Rectangle', maxX: 60, minY: 370, maxHeight: 30 } },
  { name: 'section-add-button', spec: { css: 'button[aria-label="Add fill"]' } }
]

async function captureWidgets(theme) {
  for (const w of WIDGETS) {
    const found = await locate(w.spec)
    if (!found) { measurements[`${theme}/${w.name}`] = { missing: true }; continue }
    measurements[`${theme}/${w.name}`] = { rect: found.rect, style: found.style, states: [] }
    const pad = 6
    const clip = { x: Math.max(0, found.rect.x - pad), y: Math.max(0, found.rect.y - pad), width: found.rect.width + pad * 2, height: found.rect.height + pad * 2 }
    const cx = found.rect.x + found.rect.width / 2, cy = found.rect.y + found.rect.height / 2
    await move(2, 450); await sleep(150)
    await shot(theme, `widget-${w.name}-idle`, clip, { widget: w.name, state: 'idle' })
    await move(cx, cy); await sleep(250)
    await shot(theme, `widget-${w.name}-hover`, clip, { widget: w.name, state: 'hover' })
    const hovered = await locate(w.spec)
    measurements[`${theme}/${w.name}`].states.push({ state: 'hover', style: hovered?.style })
    await move(2, 450); await sleep(150)
  }
}

async function scenes(theme) {
  await move(2, 450); await sleep(200)
  await shot(theme, 'screen-rectangle-selected', null, { scene: 'rectangle selected, design panel' })
  // File menu
  const file = await locate({ css: '[role=menuitem]', text: 'File' })
  await click(file.rect.x + 10, file.rect.y + 10); await sleep(500)
  await shot(theme, 'screen-file-menu-open', null, { scene: 'File menu open' })
  await escape(); await sleep(300)
  // Select dropdown (blend mode)
  const bm = await locate({ css: 'button[aria-label="Blend mode"]' })
  await click(bm.rect.x + 20, bm.rect.y + 10); await sleep(500)
  await shot(theme, 'screen-select-open', null, { scene: 'Blend mode select open' })
  await escape(); await sleep(300)
  // Fill color picker popover
  const fill = await locate({ css: 'button[aria-label="Fill"]' })
  await click(fill.rect.x + 10, fill.rect.y + 10); await sleep(700)
  await shot(theme, 'screen-fill-popover-open', null, { scene: 'selection toolbar fill popover open' })
  // Swatch inside the fill popover (fixed position in this deterministic layout).
  await click(449, 531); await sleep(900)
  await shot(theme, 'screen-color-picker-open', null, { scene: 'color picker open from the fill popover swatch' })
  await escape(); await sleep(300)
  await escape(); await sleep(300)
  // Canvas context menu
  await click(550, 310, 'right'); await sleep(500)
  await shot(theme, 'screen-context-menu', null, { scene: 'canvas context menu' })
  await escape(); await sleep(300)
  // Tooltip on toolbar
  const tb = await locate({ css: 'button', minY: 840, maxX: 650, minX: 625 })
  await move(tb.rect.x + 16, tb.rect.y + 16); await sleep(1200)
  await shot(theme, 'screen-toolbar-tooltip', null, { scene: 'toolbar tooltip' })
  await move(2, 450); await sleep(200)
  // Number field focused (editing)
  const nf = await locate({ css: '[role=spinbutton][data-property=width]' })
  await click(nf.rect.x + 50, nf.rect.y + 12); await sleep(300)
  await shot(theme, 'screen-number-field-editing', null, { scene: 'width number field in edit mode' })
  const pad = 6
  await shot(theme, 'widget-number-field-width-focus', { x: nf.rect.x - pad, y: nf.rect.y - pad, width: nf.rect.width + 2 * pad, height: nf.rect.height + 2 * pad }, { widget: 'number-field-width', state: 'focus' })
  await escape(); await sleep(200)
}

try {
  await c.send('Page.enable')
  await c.send('Emulation.setDeviceMetricsOverride', { width: VIEW.width, height: VIEW.height, deviceScaleFactor: 1, mobile: false })
  await c.send('Page.navigate', { url: 'http://127.0.0.1:4181/' })
  await sleep(4500)
  await shot('dark', 'screen-empty', null, { scene: 'empty document' })
  // Draw a rectangle so the design panel and layers show real content.
  await click(648, 863); await sleep(200)
  await drag(400, 200, 700, 420); await sleep(900)
  await scenes('dark')
  await captureWidgets('dark')
  // Light theme
  const light = await locate({ css: 'button[aria-label="Use light theme"]' })
  await click(light.rect.x + 16, light.rect.y + 16); await sleep(800)
  await move(2, 450); await sleep(200)
  await shot('light', 'screen-empty-canvas-selected', null, { scene: 'light theme, rectangle selected' })
  await scenes('light')
  await captureWidgets('light')
  await writeFile(join(outDir, 'manifest.json'), JSON.stringify({ source: 'OpenPencil', commit: SOURCE_COMMIT, viewport: VIEW, captured: new Date().toISOString().slice(0, 10), images: manifest }, null, 2))
  await writeFile(measurePath, JSON.stringify({ source: 'OpenPencil', commit: SOURCE_COMMIT, viewport: VIEW, widgets: measurements }, null, 2))
  console.log(`captured ${manifest.length} images, ${Object.keys(measurements).length} widget measurements`)
} finally {
  c.close(); chrome.kill(); server.close()
}

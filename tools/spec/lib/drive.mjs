// Owns: the input, locate, screenshot and measurement helpers used by the gap capture scenes.
// Why: the first capture pass (tools/spec/capture.mjs) inlines these helpers; the gap pass has many
//   scenes in several files, so they live here once. Behaviour matches capture.mjs (1440x900 viewport,
//   6 px crop padding, `widget-<name>-<state>` / `screen-<name>` naming) so both image sets compare directly.
// Callers: tools/spec/capture_gaps.mjs and tools/spec/gaps/*.mjs. Reference material only, never shipped.
// Outputs: PNG files under <outDir>/<theme>/, plus manifest and measurement records handed back to the caller.
import { mkdir, writeFile } from 'node:fs/promises'
import { join } from 'node:path'

const KEY_CODES = { Tab: 9, Enter: 13, Escape: 27, Backspace: 8, Delete: 46 }

// Resolves a spec to the first visible match and reads its rect and computed style.
// spec: { css | js, text?, textStarts?, minX?, maxX?, minY?, maxY?, maxHeight?, index?, up?, union?, parts? }
//   css: querySelectorAll selector; js: expression returning an array of elements (for relationships css cannot say)
//   up: climb N ancestors after selecting; union: bounding box of every match; parts: { name: css } measured inside the match
const FIND = `(spec) => {
  let els = spec.js ? [...eval(spec.js)] : [...document.querySelectorAll(spec.css)]
  els = els.filter((e) => { const r = e.getBoundingClientRect(); return r.width > 0 && r.height > 0 })
  if (spec.text) els = els.filter((e) => (e.textContent || '').trim() === spec.text)
  if (spec.textStarts) els = els.filter((e) => (e.textContent || '').trim().startsWith(spec.textStarts))
  const num = (k, f) => { if (spec[k] != null) els = els.filter((e) => f(e.getBoundingClientRect(), spec[k])) }
  num('minX', (r, v) => r.x >= v); num('maxX', (r, v) => r.x <= v); num('minY', (r, v) => r.y >= v); num('maxY', (r, v) => r.y <= v)
  num('maxHeight', (r, v) => r.height <= v); num('maxWidth', (r, v) => r.width <= v)
  if (!els.length) return null
  const pick = ['fontFamily','fontSize','fontWeight','lineHeight','letterSpacing','color','backgroundColor','borderTopWidth','borderTopColor','borderRightColor','borderBottomColor','borderLeftColor','borderTopLeftRadius','paddingTop','paddingRight','paddingBottom','paddingLeft','gap','boxShadow','opacity','textTransform','cursor','outlineStyle','outlineColor','outlineWidth','transitionProperty','transitionDuration','transitionTimingFunction','transform','filter','backdropFilter','zIndex']
  const read = (e) => {
    const r = e.getBoundingClientRect(), cs = getComputedStyle(e), style = {}
    for (const k of pick) style[k] = cs[k]
    return { rect: { x: r.x, y: r.y, width: r.width, height: r.height }, style }
  }
  let e = els[spec.index ?? 0]
  if (!e) return null
  for (let i = 0; i < (spec.up ?? 0); i++) e = e.parentElement
  let out = read(e)
  if (spec.union) {
    const rs = els.map((x) => x.getBoundingClientRect())
    const x0 = Math.min(...rs.map((r) => r.x)), y0 = Math.min(...rs.map((r) => r.y))
    const x1 = Math.max(...rs.map((r) => r.right)), y1 = Math.max(...rs.map((r) => r.bottom))
    out.rect = { x: x0, y: y0, width: x1 - x0, height: y1 - y0 }
  }
  if (spec.parts) {
    out.parts = {}
    for (const [name, sel] of Object.entries(spec.parts)) { const p = e.querySelector(sel); if (p) out.parts[name] = read(p) }
  }
  return out
}`

export function createDriver({ c, outDir, viewport, manifest, measurements, unreachable, failures }) {
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
  const mouse = (type, x, y, o = {}) => c.send('Input.dispatchMouseEvent', { type, x, y, button: 'left', clickCount: 1, ...o })
  const move = (x, y) => mouse('mouseMoved', x, y, { button: 'none', clickCount: 0 })
  const away = async () => { await move(2, 450); await sleep(200) }
  const mods = (m = {}) => (m.alt ? 1 : 0) | (m.ctrl ? 2 : 0) | (m.shift ? 8 : 0)

  async function click(x, y, { button = 'left', count = 1, ...m } = {}) {
    await move(x, y)
    for (let i = 1; i <= count; i++) {
      await mouse('mousePressed', x, y, { button, clickCount: i, modifiers: mods(m) })
      await mouse('mouseReleased', x, y, { button, clickCount: i, modifiers: mods(m) })
    }
  }
  async function drag(x1, y1, x2, y2) {
    await move(x1, y1); await mouse('mousePressed', x1, y1)
    for (let i = 1; i <= 8; i++) await mouse('mouseMoved', x1 + ((x2 - x1) * i) / 8, y1 + ((y2 - y1) * i) / 8, { buttons: 1 })
    await mouse('mouseReleased', x2, y2)
  }
  async function key(k, m = {}) {
    const p = { key: k, code: k, windowsVirtualKeyCode: KEY_CODES[k] ?? k.toUpperCase().charCodeAt(0), modifiers: mods(m) }
    await c.send('Input.dispatchKeyEvent', { type: 'rawKeyDown', ...p })
    await c.send('Input.dispatchKeyEvent', { type: 'keyUp', ...p })
  }
  const type = (text) => c.send('Input.insertText', { text })

  const locate = (spec) => c.evaluate(`(${FIND})(${JSON.stringify(spec)})`)
  async function must(spec) {
    const f = await locate(spec)
    if (!f) throw new Error(`not found: ${JSON.stringify(spec)}`)
    return f
  }
  const center = (r) => [r.x + r.width / 2, r.y + r.height / 2]
  async function clickOn(spec, opts) { const f = await must(spec); await click(...center(f.rect), opts); return f }
  async function hoverOn(spec) { const f = await must(spec); await move(...center(f.rect)); return f }

  async function shot(theme, name, clip, extra = {}) {
    const dir = join(outDir, theme)
    await mkdir(dir, { recursive: true })
    const params = { format: 'png' }
    if (clip) params.clip = { ...clip, scale: 1 }
    const r = await c.send('Page.captureScreenshot', params)
    await writeFile(join(dir, `${name}.png`), Buffer.from(r.data, 'base64'))
    manifest.push({ theme, name, file: `${theme}/${name}.png`, viewport, clip: clip ?? null, set: 'gaps', ...extra })
  }
  const screen = (theme, name, scene) => shot(theme, `screen-${name}`, null, { scene })

  // Crops the element (6 px padding, clamped to the viewport) once per state and records its measurements.
  // states: 'idle' | 'hover' | { state, enter?(found), exit?(), wait? }; the first state also fills the top-level record.
  // A name captured several times (different scenes) keeps one record with all its states.
  // opts.keep leaves the pointer where it is afterwards (needed while a drag is in progress).
  async function widget(theme, name, spec, states = ['idle', 'hover'], scene, opts = {}) {
    const rec = (measurements[`${theme}/${name}`] ??= { states: [] })
    for (const raw of states) {
      const st = typeof raw === 'string' ? { state: raw } : raw
      let found = await locate(spec)
      if (!found) { rec.missing = true; unreachable.push({ item: `${theme}/${name} (${st.state})`, reason: 'element not found when capturing' }); return }
      if (st.state === 'idle' && !st.enter) await away()
      else if (st.state === 'hover' && !st.enter) await move(...center(found.rect))
      else if (st.enter) await st.enter(found)
      await sleep(st.wait ?? 280)
      found = (await locate(spec)) ?? found
      const pad = 6, r = found.rect
      const x0 = Math.max(0, r.x - pad), y0 = Math.max(0, r.y - pad)
      const x1 = Math.min(viewport.width, r.x + r.width + pad), y1 = Math.min(viewport.height, r.y + r.height + pad)
      await shot(theme, `widget-${name}-${st.state}`, { x: x0, y: y0, width: x1 - x0, height: y1 - y0 }, { widget: name, state: st.state, ...(scene ? { scene } : {}) })
      if (rec.rect === undefined) { rec.rect = found.rect; rec.style = found.style; if (found.parts) rec.parts = found.parts }
      rec.states.push({ state: st.state, rect: found.rect, style: found.style, ...(found.parts ? { parts: found.parts } : {}) })
      if (st.exit) await st.exit()
    }
    if (!opts.keep) await away()
  }

  // Closes menus, popovers and dialogs with Escape (up to four layers) and does nothing when none is open,
  // so the selection survives.
  async function closeAll() {
    for (let i = 0; i < 4; i++) {
      if (!(await c.evaluate(`!!document.querySelector('[role=menu],[data-reka-popper-content-wrapper],[role=dialog]')`))) break
      await key('Escape'); await sleep(250)
    }
  }
  // Expands every collapsed layer tree node so row indexes mean the same thing in every scene.
  async function expandTree() {
    for (let i = 0; i < 4; i++) {
      const f = await locate({ css: '[data-test-id=layers-item] [data-slot=disclosure]:not([data-expanded=true])' })
      if (!f) return
      await click(...center(f.rect)); await sleep(300)
    }
  }
  // Runs one scene section; a failure is recorded and the surfaces it left open are closed, so later
  // sections still run.
  async function step(label, fn) {
    try { await fn() } catch (e) { failures.push(`${label}: ${e.message}`); console.error(`step ${label} failed: ${e.message}`); await closeAll() }
  }

  return { c, closeAll, expandTree, step, measurements, sleep, mouse, move, away, click, drag, key, type, locate, must, center, clickOn, hoverOn, shot, screen, widget, unreachable }
}

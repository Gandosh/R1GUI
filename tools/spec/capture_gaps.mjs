// Owns: reproducible capture of the OpenPencil widget states the first pass (capture.mjs) could not reach:
//   switch, dialog, toast, default-tone text input, tab bar, gradient editor, binding picker, layer tree states,
//   menu item states, tooltips, flyout, colour sliders, constraints, alignment grid, checkbox, scrollbar.
// Why: Phase 4 compares widgets pixel-wise, so every state needs a reference crop and computed-style numbers.
// Usage: node tools/spec/capture_gaps.mjs <openpencil-dist-dir> <out-dir> <measurements_gaps.json>
//   Adds `set: "gaps"` entries to <out-dir>/manifest.json (replacing earlier gap entries, leaving the first
//   pass entries alone) and writes the measurements file. Scenes live in tools/spec/gaps/*.mjs.
// Callers: developers at phase boundaries. Reference material only, never a build input.
import { readFile, writeFile } from 'node:fs/promises'
import { join } from 'node:path'
import { serveDir, launchChrome, connect } from './lib/chrome.mjs'
import { createDriver } from './lib/drive.mjs'
import { layers } from './gaps/layers.mjs'
import { overlays } from './gaps/overlays.mjs'
import { dialogs } from './gaps/dialogs.mjs'
import { docChrome } from './gaps/docchrome.mjs'

const [dist, outDir, measurePath] = process.argv.slice(2)
if (!dist || !outDir || !measurePath) throw new Error('usage: capture_gaps.mjs <dist> <outDir> <measurements_gaps.json>')
const SOURCE_COMMIT = 'dd3161e44' // OpenPencil checkout used for the reference build
const VIEW = { width: 1440, height: 900 }

const server = await serveDir(dist, 4182)
// Scrollbars stay visible here (the first pass hides them) so the scrollbar widget can be captured.
const chrome = await launchChrome({ port: 9335, ...VIEW, hideScrollbars: false })
const c = await connect(9335)
// Items checked by hand that no scene can reach (scenes append whatever else fails to resolve at run time).
const unreachable = [
  { item: 'layer tree drag states (dragged row at 30% opacity, 2 px drop line, child drop outline)', reason: 'row drag is native HTML5 drag and drop; headless Chrome starts the drag (drag interception reports a payload) but the app never enters its dragging state when the protocol replays enter/over events, so no indicator is drawn. Values stay source-derived (theme/layer-tree.ts).' },
  { item: 'switch mixed state and md size', reason: 'mixed is only used for boolean component properties of an instance (needs a component with a boolean property); no screen uses the md size.' },
  { item: 'disabled icon buttons and number fields', reason: 'no screen of the default build disables them; disabled appears only on menu items, accent buttons in the share popover and the page delete item (all captured).' },
  { item: 'keyboard focus rings of tabs, tab bar items and toolbar buttons', reason: 'only the switch focus ring was driven through the keyboard (Tab).' },
  { item: 'constraints scale badge, variable table resize handle, status text, collaboration avatars, HSL/HEX colour modes, eyedropper', reason: 'not needed by the first Phase 4 widgets.' }
]
const manifest = [], measurements = {}, failures = []
const d = createDriver({ c, outDir, viewport: VIEW, manifest, measurements, unreachable, failures })

// Draws an outer frame with two rectangles and an inner frame holding one rectangle (toolbar buttons are at
// fixed positions in the 1440x900 layout; the first pass relies on the same positions).
async function buildDocument() {
  const tool = (x) => d.click(x, 863)
  await tool(602); await d.drag(330, 150, 850, 520); await d.sleep(500)
  await tool(648); await d.drag(360, 180, 480, 260); await d.sleep(400)
  await tool(648); await d.drag(520, 180, 640, 260); await d.sleep(400)
  await tool(602); await d.drag(360, 300, 700, 480); await d.sleep(400)
  await tool(648); await d.drag(380, 330, 480, 400); await d.sleep(600)
}

async function runTheme(theme) {
  await c.send('Page.navigate', { url: 'http://127.0.0.1:4182/' })
  await d.sleep(4500)
  await d.clickOn({ css: `button[aria-label="Use ${theme} theme"]` }); await d.sleep(800)
  await buildDocument()
  await d.screen(theme, 'nested-frames-document', 'document with an outer frame, two rectangles and an inner frame holding one rectangle')
  for (const [name, scene] of [['layers', layers], ['overlays', overlays], ['dialogs', dialogs], ['docChrome', docChrome]]) {
    await d.step(`${theme}/${name}`, () => scene(d, theme))
  }
}

try {
  await c.send('Page.enable')
  await c.send('Emulation.setDeviceMetricsOverride', { width: VIEW.width, height: VIEW.height, deviceScaleFactor: 1, mobile: false })
  await runTheme('dark')
  await runTheme('light')
  const manifestPath = join(outDir, 'manifest.json')
  const base = JSON.parse(await readFile(manifestPath, 'utf8'))
  base.images = [...base.images.filter((e) => e.set !== 'gaps'), ...manifest]
  await writeFile(manifestPath, JSON.stringify(base, null, 2))
  const seen = new Set()
  const notReached = unreachable.filter((u) => { const k = u.item.replace(/^(dark|light)\//, ''); if (seen.has(k)) return false; seen.add(k); return true })
  await writeFile(measurePath, JSON.stringify({ source: 'OpenPencil', commit: SOURCE_COMMIT, viewport: VIEW, widgets: measurements, notCaptured: notReached }, null, 2))
  console.log(`captured ${manifest.length} gap images, ${Object.keys(measurements).length} widget measurements, ${notReached.length} not captured, ${failures.length} scene failures`)
  process.exitCode = failures.length ? 1 : 0
} finally {
  c.close(); chrome.kill(); server.close()
}

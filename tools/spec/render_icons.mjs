// Owns: browser renderings of the extracted Lucide icons, used as the pixel oracle for the icon rasterizer.
// Why: the toolkit rasterizes icons itself (Phase 3); Chrome's rendering of the same SVG at the sizes the UI
//   uses (12, 14, 16, 24 px) is the reference the "icons" comparison profile checks against.
// Output: white icon on black, one PNG per icon and size, so brightness equals coverage.
// Usage: node tools/spec/render_icons.mjs <icons-dir> <out-dir> <manifest.json>
import { readdir, readFile, writeFile, mkdir } from 'node:fs/promises'
import { join } from 'node:path'
import { launchChrome, connect } from './lib/chrome.mjs'

const [iconsDir, outDir, manifestPath] = process.argv.slice(2)
if (!iconsDir || !outDir || !manifestPath) throw new Error('usage: render_icons.mjs <iconsDir> <outDir> <manifest.json>')
const SIZES = [12, 14, 16, 24]
const manifest = JSON.parse(await readFile(manifestPath, 'utf8'))
const used = manifest.usedBySource.filter((n) => manifest.icons.includes(n))
const CELL = 40
const COLS = 12

const chrome = await launchChrome({ port: 9336, width: CELL * COLS, height: 900 })
const c = await connect(9336)
try {
  await c.send('Page.enable')
  const svgs = {}
  for (const n of used) svgs[n] = (await readFile(join(iconsDir, `${n}.svg`), 'utf8')).replace('currentColor', '#fff').replace('currentColor', '#fff')
  const written = []
  for (const size of SIZES) {
    const cells = used.map((n, i) => `<div style="position:absolute;left:${(i % COLS) * CELL}px;top:${Math.floor(i / COLS) * CELL}px;width:${CELL}px;height:${CELL}px"><img width="${size}" height="${size}" style="position:absolute;left:${Math.floor((CELL - size) / 2)}px;top:${Math.floor((CELL - size) / 2)}px" src="data:image/svg+xml;base64,${Buffer.from(svgs[n]).toString('base64')}"></div>`).join('')
    const rows = Math.ceil(used.length / COLS)
    await c.send('Emulation.setDeviceMetricsOverride', { width: CELL * COLS, height: CELL * rows, deviceScaleFactor: 1, mobile: false })
    await c.send('Page.navigate', { url: 'data:text/html;base64,' + Buffer.from(`<body style="margin:0;background:#000">${cells}</body>`).toString('base64') })
    await new Promise((r) => setTimeout(r, 700))
    await mkdir(join(outDir, String(size)), { recursive: true })
    for (let i = 0; i < used.length; i++) {
      const clip = { x: (i % COLS) * CELL + Math.floor((CELL - size) / 2), y: Math.floor(i / COLS) * CELL + Math.floor((CELL - size) / 2), width: size, height: size, scale: 1 }
      const r = await c.send('Page.captureScreenshot', { format: 'png', clip })
      await writeFile(join(outDir, String(size), `${used[i]}.png`), Buffer.from(r.data, 'base64'))
      written.push(`${size}/${used[i]}.png`)
    }
  }
  await writeFile(join(outDir, 'manifest.json'), JSON.stringify({ source: 'Chrome rendering of assets/icons/lucide/*.svg, white on black', sizes: SIZES, icons: used, count: written.length }, null, 1) + '\n')
  console.log(`rendered ${written.length} icon images (${used.length} icons x ${SIZES.length} sizes)`)
} finally { c.close(); chrome.kill() }

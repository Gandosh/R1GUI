// Owns: browser-measured text widths used as the text-layout oracle (slices 3.4, 3.5).
// Why: the toolkit's FreeType + HarfBuzz output must match the widths the OpenPencil UI gets in a browser
//   for the same strings, sizes and weights; this records those widths from the real build.
// Usage: node tools/spec/text_reference.mjs <openpencil-dist-dir> <out.json>
import { writeFile } from 'node:fs/promises'
import { serveDir, launchChrome, connect } from './lib/chrome.mjs'

const [dist, out] = process.argv.slice(2)
if (!dist || !out) throw new Error('usage: text_reference.mjs <dist> <out.json>')
const STRINGS = ['Rectangle', 'Pass through', 'Blend mode', 'Corner smoothing', 'Position', 'Layout', 'Appearance',
  'Untitled', 'Page 1', 'D4D4D4', '100', 'File', 'Edit', 'Arrange', 'Share', 'Design', 'Variables', 'No local variables',
  'The quick brown fox jumps over the lazy dog', 'AVATAR To Wave', '0123456789', 'WWWWWWWWWW', 'iiiiiiiiii', 'fi fl ffi']
const SIZES = [10, 11, 12, 13, 14, 16]
const WEIGHTS = [400, 500, 600, 700]
const server = await serveDir(dist, 4182)
const chrome = await launchChrome({ port: 9335 })
const c = await connect(9335)
try {
  await c.send('Page.enable')
  await c.send('Page.navigate', { url: 'http://127.0.0.1:4182/' })
  await new Promise((r) => setTimeout(r, 4000))
  const result = await c.evaluate(`(async () => {
    const weights = ${JSON.stringify(WEIGHTS)}
    await Promise.all(weights.map((w) => document.fonts.load(w + ' 12px Inter')))
    const ready = weights.every((w) => document.fonts.check(w + ' 12px Inter'))
    const ctx = document.createElement('canvas').getContext('2d')
    const rows = []
    for (const w of weights) for (const size of ${JSON.stringify(SIZES)}) {
      ctx.font = w + ' ' + size + 'px Inter'
      for (const s of ${JSON.stringify(STRINGS)}) rows.push({ weight: w, size, text: s, width: ctx.measureText(s).width })
    }
    return { ready, rows }
  })()`)
  if (!result.ready) throw new Error('Inter did not load in the page; widths would be a fallback font')
  await writeFile(out, JSON.stringify({ source: 'OpenPencil build, Chrome canvas measureText, Inter 4.001, kerning and ligatures as the browser applies by default. FINDING: the DOM UI loads only the Inter Regular face (document.fonts lists three Inter 400 faces), so weights 500-700 are synthesized by the browser and share the Regular advance widths; whether canvas (design surface) text uses the real Inter TTFs from public/ was not verified', count: result.rows.length, rows: result.rows }, null, 1) + '\n')
  console.log(`wrote ${result.rows.length} text measurements`)
} finally { c.close(); chrome.kill(); server.close() }

// Owns: one-off page probe. Usage: node tools/spec/probe.mjs <dist-dir> "<js expression>" [shot.png]
// Starts a static server + headless Chrome, loads the app, evaluates the expression, optional screenshot.
import { serveDir, launchChrome, connect } from './lib/chrome.mjs'
import { writeFile } from 'node:fs/promises'
const [dist, expr, shot] = process.argv.slice(2)
const server = await serveDir(dist, 4180)
const chrome = await launchChrome()
try {
  const c = await connect()
  await c.send('Page.enable'); await c.send('Page.navigate', { url: 'http://127.0.0.1:4180/' })
  await new Promise((r) => setTimeout(r, 4000))
  console.log(JSON.stringify(await c.evaluate(expr), null, 1))
  if (shot) { const r = await c.send('Page.captureScreenshot', { format: 'png' }); await writeFile(shot, Buffer.from(r.data, 'base64')) }
  c.close()
} finally { chrome.kill(); server.close() }

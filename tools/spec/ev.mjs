// Owns: client for the inspection session. Usage:
//   node tools/spec/ev.mjs eval "<js>"
//   node tools/spec/ev.mjs shot out.png [x y w h]
//   node tools/spec/ev.mjs drag x1 y1 x2 y2
//   node tools/spec/ev.mjs click x y [right]   |   node tools/spec/ev.mjs move x y   |   node tools/spec/ev.mjs key <key> [text]
import { connect } from './lib/chrome.mjs'
import { writeFile } from 'node:fs/promises'
const [cmd, ...a] = process.argv.slice(2)
const c = await connect()
const mouse = (type, x, y, extra = {}) => c.send('Input.dispatchMouseEvent', { type, x: +x, y: +y, button: 'left', clickCount: 1, ...extra })
if (cmd === 'eval') console.log(JSON.stringify(await c.evaluate(a[0]), null, 1))
if (cmd === 'shot') {
  const clip = a.length >= 5 ? { x: +a[1], y: +a[2], width: +a[3], height: +a[4], scale: 1 } : undefined
  const r = await c.send('Page.captureScreenshot', { format: 'png', clip })
  await writeFile(a[0], Buffer.from(r.data, 'base64'))
}
if (cmd === 'move') await mouse('mouseMoved', a[0], a[1], { button: 'none', clickCount: 0 })
if (cmd === 'click') {
  const button = a[2] === 'right' ? 'right' : 'left'
  await mouse('mouseMoved', a[0], a[1], { button: 'none', clickCount: 0 })
  await mouse('mousePressed', a[0], a[1], { button }); await mouse('mouseReleased', a[0], a[1], { button })
}
if (cmd === 'drag') {
  const [x1, y1, x2, y2] = a.map(Number)
  await mouse('mouseMoved', x1, y1, { button: 'none', clickCount: 0 }); await mouse('mousePressed', x1, y1)
  for (let i = 1; i <= 8; i++) await mouse('mouseMoved', x1 + ((x2 - x1) * i) / 8, y1 + ((y2 - y1) * i) / 8, { buttons: 1 })
  await mouse('mouseReleased', x2, y2)
}
if (cmd === 'key') {
  await c.send('Input.dispatchKeyEvent', { type: 'keyDown', key: a[0], text: a[1] ?? (a[0].length === 1 ? a[0] : undefined) })
  await c.send('Input.dispatchKeyEvent', { type: 'keyUp', key: a[0] })
}
c.close()

// Owns: a long-lived inspection session (static server + headless Chrome with the app loaded).
// Usage: node tools/spec/session.mjs <dist-dir>   then use tools/spec/ev.mjs to evaluate / click / shoot.
import { serveDir, launchChrome, connect } from './lib/chrome.mjs'
const [dist] = process.argv.slice(2)
await serveDir(dist, 4180)
await launchChrome()
const c = await connect()
await c.send('Page.enable')
await c.send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false })
await c.send('Page.navigate', { url: 'http://127.0.0.1:4180/' })
console.log('session ready')
setInterval(() => {}, 1 << 30)

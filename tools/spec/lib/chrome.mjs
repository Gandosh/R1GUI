// Owns: launching headless Chrome with a DevTools connection and a minimal static file server.
// Why: reference screenshots and measurements are taken from the real OpenPencil build with no
//   third-party npm packages (Node's built-in WebSocket and http only).
// Callers: tools/spec/capture.mjs and ad-hoc probes. Never part of the shipped toolkit.
import { spawn } from 'node:child_process'
import { createServer } from 'node:http'
import { readFile, stat, mkdtemp } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join, extname, normalize } from 'node:path'

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.wasm': 'application/wasm',
  '.json': 'application/json', '.png': 'image/png', '.ttf': 'font/ttf', '.svg': 'image/svg+xml', '.ico': 'image/x-icon' }

export function serveDir(root, port) {
  const server = createServer(async (req, res) => {
    try {
      let rel = normalize(decodeURIComponent(new URL(req.url, 'http://x').pathname))
      while (rel.startsWith('/') || rel.startsWith(String.fromCharCode(92))) rel = rel.slice(1)
      if (rel.includes('..')) { res.writeHead(403).end(); return }
      let file = join(root, rel)
      if ((await stat(file).catch(() => null))?.isDirectory()) file = join(file, 'index.html')
      let data = await readFile(file).catch(() => null)
      if (!data) { file = join(root, 'index.html'); data = await readFile(file) } // SPA fallback
      res.writeHead(200, { 'content-type': MIME[extname(file)] ?? 'application/octet-stream' }).end(data)
    } catch { res.writeHead(500).end() }
  })
  return new Promise((resolve) => server.listen(port, '127.0.0.1', () => resolve(server)))
}

export async function launchChrome({ port = 9333, width = 1440, height = 900, chromePath } = {}) {
  const exe = chromePath ?? 'C:/Program Files/Google/Chrome/Application/chrome.exe'
  const dir = await mkdtemp(join(tmpdir(), 'r1gui-chrome-'))
  const proc = spawn(exe, [`--headless=new`, `--remote-debugging-port=${port}`, `--user-data-dir=${dir}`,
    `--window-size=${width},${height}`, '--force-device-scale-factor=1', '--use-angle=swiftshader',
    '--enable-unsafe-swiftshader', '--hide-scrollbars', '--no-first-run', '--disable-extensions',
    '--force-color-profile=srgb', 'about:blank'], { stdio: 'ignore' })
  for (let i = 0; i < 100; i++) {
    try { const r = await fetch(`http://127.0.0.1:${port}/json/version`); if (r.ok) break } catch {}
    await new Promise((r) => setTimeout(r, 100))
  }
  return proc
}

export async function connect(port = 9333) {
  const targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json()
  const page = targets.find((t) => t.type === 'page')
  const ws = new WebSocket(page.webSocketDebuggerUrl)
  await new Promise((r, j) => { ws.onopen = r; ws.onerror = j })
  let id = 0; const pending = new Map()
  ws.onmessage = (m) => { const d = JSON.parse(m.data); if (d.id && pending.has(d.id)) { const [ok, bad] = pending.get(d.id); pending.delete(d.id); d.error ? bad(new Error(d.error.message)) : ok(d.result) } }
  const send = (method, params = {}) => new Promise((ok, bad) => { const i = ++id; pending.set(i, [ok, bad]); ws.send(JSON.stringify({ id: i, method, params })) })
  const evaluate = async (expression) => {
    const r = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true })
    if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description ?? 'eval failed')
    return r.result.value
  }
  return { send, evaluate, close: () => ws.close() }
}

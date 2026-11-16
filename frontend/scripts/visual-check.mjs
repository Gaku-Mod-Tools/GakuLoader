// Headless visual verification of the built manager UI using the system Edge
// (WebView2's browser). Serves dist/ locally, drives the mock-backed app, and
// captures each tab + theme so rendering can be inspected. Not a game test.
import { chromium } from 'playwright'
import http from 'node:http'
import { readFile, mkdir, readdir } from 'node:fs/promises'
import path from 'node:path'

const dist = path.resolve(import.meta.dirname, '../dist')
const out = path.resolve(import.meta.dirname, '../visual-check')
await mkdir(out, { recursive: true })

const mime = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.svg': 'image/svg+xml' }
const server = http.createServer(async (req, res) => {
  let p = (req.url ?? '/').split('?')[0]
  if (p === '/') p = '/index.html'
  try {
    const data = await readFile(path.join(dist, p))
    res.writeHead(200, { 'Content-Type': mime[path.extname(p)] ?? 'application/octet-stream' })
    res.end(data)
  } catch {
    res.writeHead(404)
    res.end('not found')
  }
})
await new Promise((resolve) => server.listen(4173, resolve))

const browser = await chromium.launch({ channel: 'msedge', headless: true })
const page = await browser.newPage({ viewport: { width: 1120, height: 820 } })
const errors = []
page.on('pageerror', (e) => errors.push(`pageerror: ${e.message}`))
page.on('console', (m) => {
  if (m.type() === 'error') errors.push(`console: ${m.text()}`)
})

const shot = (name) => page.screenshot({ path: path.join(out, `${name}.png`) })
const selectTheme = async (name) => {
  await page.click('#theme')
  await page.getByRole('option', { name, exact: true }).click()
  await page.waitForTimeout(120)
}

// The production entry is now the Motion panel; this script visually checks the
// isolated Loader Manager prototype instead.
await page.goto('http://localhost:4173/?prototype=1')
await page.getByText('Plugin List').first().waitFor()
await page.waitForTimeout(200)
await shot('01-plugin-list-dark')

await page.getByRole('tab', { name: 'Appearance' }).click()
await page.getByText('Theme').waitFor()
await shot('02-appearance-dark')
await selectTheme('Sapphire')
await shot('03-appearance-sapphire')
await selectTheme('Paper & Ink')
await shot('04-appearance-paper')
await selectTheme('Apple Light')
await shot('05-appearance-apple-light')
await selectTheme('Apple Dark')
await shot('06-appearance-apple-dark')
await selectTheme('Dark')

await page.getByRole('tab', { name: 'Hotkeys' }).click()
await page.getByText('Show motion GUI').waitFor()
await shot('07-hotkeys')
await page.getByRole('button', { name: 'Edit' }).first().click()
await page.getByText('Rebind hotkey').waitFor()
await shot('08-hotkey-editor')
await page.getByRole('button', { name: 'Cancel' }).click()

await page.getByRole('tab', { name: 'Settings' }).click()
await page.getByText('Enabled plugins').waitFor()
await shot('09-settings')

// Plugin selection placeholder (native ImGui hosts the real panel in the
// GakuLoader control window's plugin content area, not in the game window).
await page.getByRole('button', { name: 'GakuMotion' }).first().click()
await page.waitForTimeout(150)
await shot('10-plugin-selected')

console.log('CAPTURED', (await readdir(out)).filter((f) => f.endsWith('.png')).length, 'screenshots')
console.log('RUNTIME_ERRORS', errors.length ? errors : 'none')

await browser.close()
server.close()

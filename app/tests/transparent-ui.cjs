'use strict';
// Internal QA only. Colored desktop backdrops stay under work/qa.
const { _electron: electron } = require('playwright');
const { execFileSync } = require('node:child_process');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const output = path.resolve(root, '..', 'qa');
fs.mkdirSync(output, { recursive: true });
const data = fs.mkdtempSync(path.join(output, 'transparent-ui-data-'));
const postersOnly = process.argv.includes('--posters-only');
const env = { ...process.env }; delete env.ELECTRON_RUN_AS_NODE;
const bootstrap = path.join(data, 'bootstrap.cjs');
// Keep fixtures at the provider boundary so the real host's periodic snapshots
// retain them. Renderer-only fixtures expire when the host broadcasts signed-out
// state and would leave a native-region probe comparing obsolete DOM positions.
fs.writeFileSync(bootstrap, `
'use strict';
global.__shapeFixtures = {};
const empty = id => ({id,status:'signed_out',account:null,plan:null,windows:[],updatedAt:null,error:null});
function provider(id) {
  return class FakeProvider {
    constructor({onUpdate}) { this.onUpdate=onUpdate; this.state=empty(id); global.__shapeFixtures[id]=this; }
    getState() { return structuredClone(this.state); }
    publish() { this.onUpdate(this.getState()); return this.getState(); }
    set(value) { this.state={...empty(id),...value,id}; return this.publish(); }
    async start() { return this.publish(); }
    async refresh() { return this.publish(); }
    async login() { return this.publish(); }
    async logout() { this.state=empty(id); return this.publish(); }
    async openSetup() { return this.getState(); }
    stop() {}
  };
}
require(${JSON.stringify(path.join(root, 'backend/codex-provider.cjs'))}).CodexProvider=provider('codex');
require(${JSON.stringify(path.join(root, 'backend/claude-client.cjs'))}).ClaudeClient=provider('claude');
require(${JSON.stringify(path.join(root, 'main.cjs'))});
`);

function nativeRegion(hwnd, layoutPoints = []) {
  const points = [
    { name: 'emptyCorner', x: 10, y: 10 }, { name: 'character', x: 130, y: 110 },
    { name: 'underCharacter', x: 130, y: 232 }, { name: 'formerGauge', x: 210, y: 225 },
    ...layoutPoints.map(({ name, x, y }) => ({ name, x, y })),
  ];
  if (points.some(point => !/^[a-z][a-z0-9]*$/i.test(point.name) || !Number.isFinite(point.x) || !Number.isFinite(point.y))) throw new Error('Invalid region probe coordinates');
  const script = `
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class TigaRegionProbe {
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
 [DllImport("user32.dll")] public static extern int GetWindowRgn(IntPtr window, IntPtr region);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out Rect rect);
 [DllImport("gdi32.dll")] public static extern IntPtr CreateRectRgn(int left,int top,int right,int bottom);
 [DllImport("gdi32.dll")] public static extern bool PtInRegion(IntPtr region,int x,int y);
 [DllImport("gdi32.dll")] public static extern bool DeleteObject(IntPtr handle);
}

'@
$window=[IntPtr]::new([long]::Parse('${hwnd}'))
$region=[TigaRegionProbe]::CreateRectRgn(0,0,0,0)
try {
 $kind=[TigaRegionProbe]::GetWindowRgn($window,$region)
 $client=New-Object TigaRegionProbe+Rect
 [void][TigaRegionProbe]::GetClientRect($window,[ref]$client)
 $points=ConvertFrom-Json '${JSON.stringify(points)}'
 $result=@{kind=$kind;width=$client.Right;height=$client.Bottom}
 foreach($point in $points) { $result[$point.name]=[TigaRegionProbe]::PtInRegion($region,[int][math]::Round($point.x*$client.Right/420),[int][math]::Round($point.y*$client.Bottom/246)) }
 $result | ConvertTo-Json -Compress
} finally { [void][TigaRegionProbe]::DeleteObject($region) }
`;
  return JSON.parse(execFileSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', script],
    { windowsHide: true, encoding: 'utf8', timeout: 15000 }).trim());
}

async function readLayout(page) {
  return page.evaluate(() => {
    const bounds = element => {
      const rect = element.getBoundingClientRect();
      return { x: rect.x, y: rect.y, width: rect.width, height: rect.height };
    };
    const center = (name, element) => {
      const rect = bounds(element);
      return { name, x: rect.x + rect.width / 2, y: rect.y + rect.height / 2 };
    };
    const visible = element => { const rect = element.getBoundingClientRect(); return rect.width > 0 && rect.height > 0; };
    const headings = [...document.querySelectorAll('.provider h3')].filter(visible);
    const controls = [...document.querySelectorAll('.widget-actions button')].filter(visible);
    const groups = [...document.querySelectorAll('.quota-ring-group')].filter(visible);
    const points = [
      ...headings.map((element, index) => center(`account${index}`, element)),
      ...controls.map((element, index) => center(`control${index}`, element)),
      ...groups.map((element, index) => center(`accountRing${index}`, element)),
      center('settings', document.querySelector('#settings-open')),
      center('status', document.querySelector('#sync-status')),
    ];
    return { points, accounts: bounds(document.querySelector('.widget-accounts')),
      footer: bounds(document.querySelector('.widget-footer')),
      toolbar: bounds(document.querySelector('.widget-actions')), ringBounds: groups.map(bounds),
      visibleAccountCount: headings.length,
      veinCount: document.querySelectorAll('.quota-veins,.quota-bar-veins,.vein-body,.vein-light').length };
  });
}

async function waitForNativeLayout(page, hwnd, extra = []) {
  const layout = await readLayout(page);
  const points = [...layout.points, ...extra];
  const expected = { emptyCorner: false, character: true, underCharacter: false, formerGauge: false,
    ...Object.fromEntries(points.map(point => [point.name, point.expected ?? true])) };
  const deadline = Date.now() + 15000;
  let region;
  do {
    region = nativeRegion(hwnd, points);
    if (Object.entries(expected).every(([name, value]) => region[name] === value)) break;
    await page.waitForTimeout(100);
  } while (Date.now() < deadline);
  assert(region.kind > 1, `A native nonempty region must be applied: ${JSON.stringify(region)}`);
  for (const [name, value] of Object.entries(expected)) {
    assert.equal(region[name], value, `Native region ${name}: ${JSON.stringify({ region, layout })}`);
  }
  return { region, layout };
}

async function alphaStats(page, scene, video = false) {
  return page.evaluate(async ({ scene, video }) => {
    let source;
    if (video) source = document.querySelector('.scene-video.is-visible');
    else { source = new Image(); source.src = `assets/characters/${scene}.png`; await source.decode(); }
    const width = video ? source.videoWidth : source.naturalWidth;
    const height = video ? source.videoHeight : source.naturalHeight;
    const canvas = document.createElement('canvas'); canvas.width = width; canvas.height = height;
    const context = canvas.getContext('2d', { willReadFrequently: true }); context.drawImage(source, 0, 0);
    const pixels = context.getImageData(0, 0, width, height).data;
    let transparent = 0, visible = 0;
    for (let offset = 3; offset < pixels.length; offset += 4) {
      if (pixels[offset] === 0) transparent++;
      if (pixels[offset] > 200) visible++;
    }
    const at = (x, y) => pixels[(y * width + x) * 4 + 3];
    return { width, height, transparent: transparent / (width * height),
      visible: visible / (width * height), corner: at(0, 0),
      subject: at(Math.floor(width / 2), Math.floor(height * .65)) };
  }, { scene, video });
}

(async () => {
  let app;
  const checked = { posters: [], videos: [], native: null, visualBackdrops: ['light', 'dark'] };
  try {
    app = await electron.launch({ executablePath: require('electron'),
      args: [bootstrap, '--test-data-dir', path.join(data, 'profile')], env, cwd: root });
    const page = await app.firstWindow();
    await page.getByRole('button', { name: '使用 Claude Code 账号登录', exact: true }).waitFor();
    assert.deepEqual(await page.evaluate(() => [innerWidth, innerHeight]), [420, 246]);
    const backgrounds = await page.locator('.floating-widget,.scene-stage,.scene-video,.widget-info,.widget-accounts,.widget-footer')
      .evaluateAll(elements => elements.map(element => ({ background: getComputedStyle(element).backgroundColor,
        border: getComputedStyle(element).borderWidth })));
    for (const style of backgrounds) assert.deepEqual(style, { background: 'rgba(0, 0, 0, 0)', border: '0px' });
    const stage = await page.locator('.scene-stage').boundingBox();
    assert.deepEqual(stage, { x: 0, y: 8, width: 260, height: 195 });
    for (const scene of ['full', 'low', 'stone']) {
      const stats = await alphaStats(page, scene);
      assert.equal(stats.width / stats.height, 4 / 3);
      assert.equal(stats.corner, 0);
      assert(stats.transparent > .25 && stats.visible > .08, `${scene}: ${JSON.stringify(stats)}`);
      assert(stats.subject > 0);
      checked.posters.push({ scene, transparent: stats.transparent });
    }
    const shapes = await page.evaluate(async () => {
      const { installCharacterShape } = await import('../ui/shape.js');
      const samples = [];
      const apply = await installCharacterShape({ updateShape: rects => samples.push(rects.length) });
      for (const scenes of [['full'], ['low'], ['stone'], ['full', 'low'], ['low', 'stone']]) await apply(scenes);
      return samples;
    });
    assert(shapes.every(count => count > 10 && count <= 1000), `Native rect counts: ${shapes}`);
    checked.shapeRectangles = shapes;
    if (postersOnly) {
      await page.locator('video').evaluateAll(videos => videos.forEach(video => {
        video.removeAttribute('src'); video.querySelectorAll('source').forEach(source => source.remove()); video.load();
      }));
    } else {
      for (const scene of ['full', 'low', 'stone']) {
        assert(fs.statSync(path.join(root, 'ui', 'assets', 'characters', `${scene}.webm`)).size > 0);
        await page.evaluate(scene => window.monitor.windowAction(`preview-${scene}`), scene);
        await page.waitForFunction(scene => document.querySelector('#scene-stage').dataset.state === scene, scene);
        await page.waitForFunction(() => document.querySelector('.scene-video.is-visible').readyState >= 2);
        await page.waitForTimeout(1900);
        const stats = await alphaStats(page, scene, true);
        assert.equal(stats.width / stats.height, 4 / 3);
        assert.equal(stats.corner, 0);
        assert(stats.transparent > .2 && stats.visible > .08, `${scene}: ${JSON.stringify(stats)}`);
        checked.videos.push({ scene, transparent: stats.transparent });
      }
      await page.evaluate(() => window.monitor.windowAction('preview-auto'));
      await page.waitForFunction(() => document.querySelector('#scene-stage').dataset.state === 'full');
      await page.waitForTimeout(2000);
    }
    await page.mouse.move(-20, -20); await page.waitForTimeout(180);
    const image = await page.screenshot({ path: path.join(output, 'character-transparent-alpha.png'), omitBackground: true, scale: 'css' });
    const alpha = await app.evaluate(({ nativeImage }, base64) => {
      const bitmap = nativeImage.createFromBuffer(Buffer.from(base64, 'base64')).getBitmap();
      return bitmap[3];
    }, image.toString('base64'));
    assert.equal(alpha, 0, 'Actual transparent capture must preserve alpha at the empty corner');
    for (const [name, color] of [['light', '#f2f3f4'], ['dark', '#162333']]) {
      await page.evaluate(color => document.body.style.background = color, color);
      await page.screenshot({ path: path.join(output, `character-QA-${name}-desktop.png`), scale: 'css' });
    }
    await page.evaluate(() => document.body.style.background = 'transparent');
    const hwnd = await app.evaluate(({ BrowserWindow }) =>
      BrowserWindow.getAllWindows()[0].getNativeWindowHandle().readBigUInt64LE().toString());
    const initial = await waitForNativeLayout(page, hwnd);
    checked.native = { bars: initial.region };
    for (const quotaDisplay of ['rings', 'bars']) {
      await page.evaluate(quotaDisplay => window.monitor.updateSettings({ quotaDisplay }), quotaDisplay);
      await page.waitForFunction(quotaDisplay => document.body.dataset.quotaDisplay === quotaDisplay, quotaDisplay);
      const updated = await waitForNativeLayout(page, hwnd);
      assert.equal(await page.locator('#energy-gauge').count(), 0);
      checked.native[quotaDisplay === 'rings' ? 'rings' : 'restoredBars'] = updated.region;
      await page.screenshot({ path: path.join(output, `transparent-${quotaDisplay}-signed-out.png`), omitBackground: true, scale: 'css' });
    }
    // Only the provider boundary is faked; periodic host snapshots, settings IPC,
    // native energy classification and Windows shape updates remain real.
    const fixture = await page.evaluate(() => window.monitor.getSnapshot());
    const hostNow = await app.evaluate(() => Date.now());
    const updatedAt = new Date(hostNow - 1000).toISOString();
    const resetsAt = new Date(Date.now() + 3600000).toISOString();
    for (const [id, weekly, short] of [['codex', .01, 80], ['claude', 30, 70]]) {
      fixture.providers[id] = { ...fixture.providers[id], status: 'connected', account: `fixture-${id}@example.invalid`, updatedAt,
        windows: [{ id: 'weekly', label: '总周', windowMinutes: 10080, remainingPercent: weekly, resetsAt },
          { id: 'five_hour', label: '5 小时', windowMinutes: 300, remainingPercent: short, resetsAt }] };
    }
    await app.evaluate((_electron, providers) => {
      for (const [id, state] of Object.entries(providers)) global.__shapeFixtures[id].set(state);
    }, fixture.providers);
    await page.evaluate(() => window.monitor.refresh());
    const pushFixture = async (quotaDisplay, claudeVisible) => {
      await page.evaluate(({ quotaDisplay, claudeVisible }) => window.monitor.updateSettings({
        quotaDisplay, visible: { codex: true, claude: claudeVisible },
      }), { quotaDisplay, claudeVisible });
      await page.waitForFunction(({ quotaDisplay, claudeVisible }) => {
        const count = claudeVisible ? 2 : 1;
        return document.body.dataset.quotaDisplay === quotaDisplay && document.querySelectorAll('.provider').length === count
          && document.querySelectorAll('.quota-ring-group').length === (quotaDisplay === 'rings' ? count : 0)
          && document.querySelectorAll('.quota-window').length === count * 2;
      }, { quotaDisplay, claudeVisible });
    };
    checked.collapsedLayouts = {};
    const layoutEvidence = {};
    for (const quotaDisplay of ['bars', 'rings']) {
      await pushFixture(quotaDisplay, true);
      const both = await waitForNativeLayout(page, hwnd);
      assert.equal(both.layout.veinCount, 0, 'Low quotas must not retain vein decoration.');
      const oldSettings = both.layout.points.find(point => point.name === 'settings');
      if (quotaDisplay === 'rings') {
        assert.equal(both.layout.ringBounds.length, 2);
        await page.screenshot({ path: path.join(output, 'transparent-account-rings-fixture.png'), omitBackground: true, scale: 'css' });
      }
      await pushFixture(quotaDisplay, false);
      const single = await waitForNativeLayout(page, hwnd, [{ ...oldSettings, name: 'oldSettingsPosition', expected: false }]);
      assert.equal(single.layout.visibleAccountCount, 1);
      assert(single.layout.footer.y < both.layout.footer.y, `${quotaDisplay}: the toolbar must rise when an account is hidden`);
      assert(oldSettings.y > single.layout.footer.y + single.layout.footer.height,
        `${quotaDisplay}: the old toolbar probe must be in newly empty space`);
      assert.equal(single.layout.accounts.y, both.layout.accounts.y, 'The remaining account stays at the top without an empty slot');
      assert.deepEqual(await page.locator('.scene-stage').boundingBox(), stage, 'Collapsing information must not move the character');
      assert.equal(single.layout.ringBounds.length, quotaDisplay === 'rings' ? 1 : 0);
      assert.equal(single.layout.veinCount, 0, 'Hidden-account changes cannot restore vein decoration.');
      layoutEvidence[quotaDisplay] = { both, single };
      checked.collapsedLayouts[quotaDisplay] = { bothFooter: both.layout.footer, singleFooter: single.layout.footer,
        ringGroups: single.layout.ringBounds.length, decorationPaths: single.layout.veinCount,
        currentNativeProbes: single.layout.points.length, oldSettingsPositionHit: single.region.oldSettingsPosition };
      await page.screenshot({ path: path.join(output, `transparent-single-${quotaDisplay}-fixture.png`), omitBackground: true, scale: 'css' });
    }
    fs.writeFileSync(path.join(output, 'transparent-layout-regions.json'), JSON.stringify(layoutEvidence, null, 2));
    await app.evaluate(() => {
      for (const provider of Object.values(global.__shapeFixtures)) provider.set({ status: 'signed_out' });
    });
    await page.evaluate(() => window.monitor.updateSettings({ quotaDisplay: 'bars', visible: { codex: true, claude: true } }));
    await page.getByRole('button', { name: '使用 ChatGPT 账号登录', exact: true }).waitFor();
    const snapshot = await page.evaluate(() => window.monitor.getSnapshot());
    assert.equal(snapshot.providers.codex.status, 'signed_out');
    assert.equal(snapshot.providers.claude.status, 'signed_out');
    assert.deepEqual(snapshot.providers.codex.windows, []);
    assert.deepEqual(snapshot.providers.claude.windows, []);
    console.log(JSON.stringify({ ok: true, postersOnly, checked, output: 'work/qa only; no output screenshots or real login' }));
  } finally { if (app) await app.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });

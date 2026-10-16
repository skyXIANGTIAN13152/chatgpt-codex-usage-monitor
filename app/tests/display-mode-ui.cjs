'use strict';
// Real host, IPC, settings and native classification; only account providers are
// replaced. Legacy startup preferences are intentionally present in the fixture.
const { _electron } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const qa = path.resolve(root, '..', 'qa');
fs.mkdirSync(qa, { recursive: true });
const folder = fs.mkdtempSync(path.join(qa, 'display-mode-ui-'));
const data = path.join(folder, 'profile');
fs.mkdirSync(data);
fs.writeFileSync(path.join(data, 'settings.json'), JSON.stringify({ startupAnimation: true,
  followSystemMotion: false, reducedMotion: false, paused: false, energyTarget: 'codex' }));
const bootstrap = path.join(folder, 'bootstrap.cjs');
fs.writeFileSync(bootstrap, `
const { app } = require('electron');
global.__displayQA = { providers: {}, requests: [], classifications: [] };
app.on('web-contents-created', (_event, contents) => {
 contents.session.webRequest.onBeforeRequest((details, callback) => {
  global.__displayQA.requests.push(details.url); callback({});
 });
});
const empty = id => ({ id, status: 'signed_out', account: null, plan: null, windows: [], updatedAt: null, error: null });
function provider(id) { return class {
 constructor({ onUpdate }) { this.onUpdate = onUpdate; this.state = empty(id); global.__displayQA.providers[id] = this; }
 getState() { return structuredClone(this.state); }
 publish() { this.onUpdate(this.getState()); return this.getState(); }
 set(value) { this.state = { ...empty(id), ...value, id }; return this.publish(); }
 async start() { return this.publish(); } async refresh() { return this.publish(); }
 async login() { return this.publish(); } async logout() { this.state = empty(id); return this.publish(); }
 async openSetup() { return this.getState(); } stop() {}
}; }
require(${JSON.stringify(path.join(root, 'backend/codex-provider.cjs'))}).CodexProvider = provider('codex');
require(${JSON.stringify(path.join(root, 'backend/claude-client.cjs'))}).ClaudeClient = provider('claude');
const { CoreBridge } = require(${JSON.stringify(path.join(root, 'backend/core-bridge.cjs'))});
const classify = CoreBridge.prototype.classifyEnergy;
CoreBridge.prototype.classifyEnergy = function(...args) {
 global.__displayQA.classifications.push(args); return classify.apply(this, args);
};
require(${JSON.stringify(path.join(root, 'main.cjs'))});
`);
const env = { ...process.env }; delete env.ELECTRON_RUN_AS_NODE;

(async () => {
  let app, page, settings;
  const errors = [], checks = [];
  try {
    const launch = async () => {
      app = await _electron.launch({ executablePath: require('electron'),
        args: [bootstrap, '--test-data-dir', data], cwd: root, env });
      page = await app.firstWindow();
      page.on('pageerror', error => errors.push(error.message));
      await page.waitForFunction(() => Boolean(window.monitor));
      await page.evaluate(() => {
        window.__displayOverlaps = [];
        const visible = element => element && !element.hidden && getComputedStyle(element).display !== 'none'
          && getComputedStyle(element).visibility !== 'hidden' && element.getClientRects().length > 0;
        function sample() {
          const bars = [...document.querySelectorAll('.widget-accounts .quota-track')].some(visible);
          const rings = [...document.querySelectorAll('.widget-accounts .quota-ring')].some(visible);
          if (bars && rings) window.__displayOverlaps.push(performance.now());
          requestAnimationFrame(sample);
        }
        requestAnimationFrame(sample);
      });
    };
    const snapshot = () => page.evaluate(() => window.monitor.getSnapshot());
    const waitSnapshot = async expected => {
      await page.waitForFunction(async expected => {
        const state = await window.monitor.getSnapshot();
        return Object.entries(expected).every(([key, value]) => JSON.stringify(
          key.split('.').reduce((current, part) => current?.[part], state)) === JSON.stringify(value));
      }, expected, { timeout: 10000 });
      return snapshot();
    };
    const publish = async (fixtures = [['codex', 20, 80], ['claude', 80, 40]]) => {
      await app.evaluate((_electron, fixtures) => {
        const now = Date.now();
        for (const [id, short, weekly] of fixtures) {
          global.__displayQA.providers[id].set({ status: 'connected', account: `fixture-${id}@example.invalid`,
            plan: 'fixture', updatedAt: new Date(now - 1000).toISOString(), error: null,
            windows: [short === null ? null : { id: 'five_hour', label: '5 小时', remainingPercent: short, windowMinutes: 300,
              resetsAt: new Date(now + 7200000).toISOString() },
            { id: id === 'codex' ? 'weekly' : 'seven_day', label: '总周', remainingPercent: weekly,
              windowMinutes: 10080, resetsAt: new Date(now + 345600000).toISOString() }]
              .filter(Boolean).map(window => ({ ...window, usedPercent: 100 - window.remainingPercent })) });
        }
      }, fixtures);
      await page.evaluate(() => window.monitor.refresh());
    };
    const openSettings = async () => {
      const opening = app.waitForEvent('window');
      await page.getByRole('button', { name: '打开设置', exact: true }).click();
      settings = await opening;
      settings.on('pageerror', error => errors.push(error.message));
      await settings.locator('#quota-display').waitFor();
      assert.equal(await settings.locator('#startup-animation-enabled,#startup-note').count(), 0);
      assert.equal(await settings.getByText('启动动画', { exact: true }).count(), 0);
    };
    const assertMode = async mode => {
      await waitSnapshot({ 'settings.quotaDisplay': mode });
      if (mode === 'bars') {
        await page.waitForFunction(() => document.querySelectorAll('.widget-accounts .quota-track').length === 4);
        assert.equal(await page.locator('.widget-accounts .quota-track:visible').count(), 4);
        assert.equal(await page.locator('.widget-accounts .quota-ring').count(), 0);
      } else {
        await page.waitForFunction(() => document.querySelectorAll('.widget-accounts .quota-ring').length === 4);
        assert.equal(await page.locator('.widget-accounts .quota-ring:visible').count(), 4);
        assert.equal(await page.locator('.widget-accounts .quota-ring-group[role=group]').count(), 2);
        assert.equal(await page.locator('.widget-accounts .quota-track').count(), 0);
      }
      assert.equal(await page.locator('#energy-gauge,.energy-gauge').count(), 0);
      assert.equal(await page.locator('.widget-accounts .quota-window').count(), 4);
      assert.deepEqual(await page.evaluate(() => window.__displayOverlaps), []);
    };
    const assertAccountDetails = async () => {
      const details = await page.locator('.widget-accounts .provider').evaluateAll(providers => providers.map(provider => ({
        id: provider.dataset.provider, status: provider.querySelector('.provider-status').textContent,
        gradient: getComputedStyle(provider.querySelector('h3')).backgroundImage,
        windows: [...provider.querySelectorAll('.quota-window')].map(window => ({ period: window.dataset.period,
          label: window.querySelector('.quota-label').textContent, number: window.querySelector('.quota-number').textContent,
          title: window.title })),
      })));
      assert.deepEqual(details.map(provider => provider.id), ['codex', 'claude']);
      for (const provider of details) {
        assert.match(provider.status, /刷新/);
        assert.match(provider.gradient, /gradient\(/);
        const values = provider.id === 'codex' ? { weekly: 80, short: 20 } : { weekly: 40, short: 80 };
        assert.deepEqual(provider.windows.map(window => window.period), ['weekly', 'short']);
        for (const window of provider.windows) {
          assert.equal(window.label, window.period === 'weekly' ? '总周' : '5小时');
          assert.equal(window.number, `${values[window.period]}%`);
          assert(window.title.includes(`${values[window.period]}% 剩余`));
          assert.match(window.title, /后重置/); assert.match(window.title, /刷新/);
          assert.match(window.title, /\d{4}/, 'Absolute reset date remains available in the tooltip.');
        }
      }
      assert.notEqual(details[0].gradient, details[1].gradient, 'Provider names must retain distinct actual gradients.');
      assert.match(await page.locator('#sync-status').getAttribute('title'), /刷新/);
      return details;
    };
    const assertRingValues = async (values = { codex: { weekly: 80, short: 20 }, claude: { weekly: 40, short: 80 } }) => {
      const references = [];
      for (const [id, periods] of Object.entries(values)) for (const [period, expected] of Object.entries(periods)) {
        const meter = page.locator(`.widget-accounts [data-provider="${id}"] .quota-ring[data-period="${period}"]`);
        assert.equal(await meter.getAttribute('role'), 'meter');
        assert.equal(await meter.getAttribute('aria-valuenow'), String(expected));
        const arc = await meter.locator('.gauge-fill').evaluate(circle =>
          parseFloat(circle.style.strokeDasharray || circle.getAttribute('stroke-dasharray')));
        assert.equal(arc, expected, `${id}/${period} always shows its own remaining quota.`);
        const gradient = await meter.evaluate(ring => ({ id: ring.querySelector('linearGradient')?.id,
          stroke: ring.querySelector('.gauge-fill').getAttribute('stroke') || ring.querySelector('.gauge-fill').style.stroke }));
        assert(gradient.id && gradient.stroke.includes(`#${gradient.id}`), 'Each circle references its own palette.');
        references.push(gradient.id);
      }
      assert.equal(new Set(references).size, references.length, 'Account-period gradients must not collide.');
      for (const [id, periods] of Object.entries(values)) {
        const period = Object.hasOwn(periods, 'short') ? 'short' : 'weekly';
        const center = page.locator(`.widget-accounts [data-provider="${id}"] .quota-ring-value`);
        assert.equal(await center.getAttribute('data-period'), period);
        assert.equal(await center.textContent(), `${periods[period]}%`);
      }
    };
    const assertRingGeometry = async () => {
      assert.deepEqual(await page.evaluate(() => [innerWidth, innerHeight]), [420, 246]);
      const boxes = await page.locator('.widget-accounts .quota-ring-group').evaluateAll(rings => {
        const rect = element => { const { x, y, width, height } = element.getBoundingClientRect(); return { x, y, width, height }; };
        const footer = rect(document.querySelector('.widget-footer'));
        return rings.map(ring => {
          const number = ring.querySelector('.quota-ring-value');
          const range = document.createRange(); range.selectNodeContents(number);
          const { x, y, width, height } = range.getBoundingClientRect();
          return { ring: rect(ring), provider: rect(ring.closest('.provider')), footer,
            legend: rect(ring.closest('.provider').querySelector('.quota-ring-legend')),
            number: { x, y, width, height }, viewBox: ring.querySelector('svg').getAttribute('viewBox'),
            periods: [...ring.querySelectorAll('.quota-ring')].map(group => ({ period: group.dataset.period,
              r: Number(group.querySelector('.gauge-fill').getAttribute('r')),
              cx: Number(group.querySelector('.gauge-fill').getAttribute('cx')),
              cy: Number(group.querySelector('.gauge-fill').getAttribute('cy')) })) };
        });
      });
      assert.equal(boxes.length, 2);
      for (const { ring, provider, footer, legend, number, viewBox, periods } of boxes) {
        assert.equal(viewBox, '0 0 40 40');
        assert(Math.abs(ring.width - 52) <= 1 && Math.abs(ring.height - 52) <= 1);
        assert(ring.x >= provider.x - .5 && ring.y >= provider.y - .5);
        assert(ring.x + ring.width <= provider.x + provider.width + .5);
        assert(ring.y + ring.height <= provider.y + provider.height + .5);
        assert(ring.y + ring.height <= footer.y, 'Account rings must not overlap the footer.');
        assert(legend.x >= ring.x + ring.width, 'Period labels and values sit alongside their account ring.');
        assert.deepEqual(periods.map(period => [period.period, period.r]), periods.length === 2
          ? [['weekly', 16.8], ['short', 11.8]] : [['weekly', 16.8]]);
        assert(periods.every(period => period.cx === 20 && period.cy === 20), 'Periods share the same center.');
        assert(Math.abs(number.x + number.width / 2 - ring.x - ring.width / 2) <= 2);
        assert(Math.abs(number.y + number.height / 2 - ring.y - ring.height / 2) <= 2.5,
          `Percentage must be centered in its own ring: ${JSON.stringify({ ring, number })}`);
      }
    };
    const assertRemovedStartup = async () => {
      assert.equal(await page.locator('#startup-animation,.startup-animation,#energy-target,.widget-target').count(), 0);
      assert.equal(await page.locator('body').evaluate(body => body.classList.contains('startup-playing')), false);
      assert.doesNotMatch(await page.locator('body').innerText(), /跟随\s*(?:Codex|Claude|聚合)/);
      const requests = await app.evaluate(() => global.__displayQA.requests);
      assert.deepEqual(requests.filter(url => /(?:^|\/)startup(?:[/.\-]|$)/i.test(url)), []);
    };
    const checkAndClose = async () => {
      assert.deepEqual(await page.evaluate(() => window.__displayOverlaps), []);
      await assertRemovedStartup();
      await app.close(); app = null;
    };

    await launch();
    await publish();
    await waitSnapshot({ 'energyTarget.remainingPercent': 20, 'energy.scene': 'low' });
    await assertMode('bars');
    await assertAccountDetails();
    await assertRemovedStartup();
    await openSettings();
    assert.equal(await settings.locator('#quota-display').inputValue(), 'bars');
    assert.deepEqual(await settings.locator('#quota-display option').evaluateAll(options => options.map(option => option.value)), ['bars', 'rings']);
    checks.push('bars-default-with-four-account-periods', 'distinct-provider-gradients', 'legacy-startup-and-follow-label-removed');

    await settings.locator('#quota-display').selectOption('rings');
    await assertMode('rings');
    await assertAccountDetails();
    await assertRingValues();
    await assertRingGeometry();
    await waitSnapshot({ 'settings.energyTarget': 'codex', 'energyTarget.remainingPercent': 20, 'energy.scene': 'low' });
    checks.push('real-settings-select-rings-without-bars', 'all-period-values-reset-tooltips-and-refresh-retained');
    await settings.locator('#energy-target').selectOption('aggregate');
    const aggregate = await waitSnapshot({ 'energyTarget.remainingPercent': 50, 'energy.scene': 'full' });
    assert.deepEqual(aggregate.energyTarget.windows.map(window => [window.id, window.remainingPercent]), [['short', 50], ['weekly', 60]]);
    await assertRingValues();
    await assertAccountDetails();
    await settings.locator('#quota-display').selectOption('bars');
    await assertMode('bars');
    await assertAccountDetails();
    await waitSnapshot({ 'settings.energyTarget': 'aggregate', 'energyTarget.remainingPercent': 50, 'energy.scene': 'full' });
    checks.push('aggregate-period-math-and-native-state-independent-of-display-mode', 'aggregate-never-replaces-account-ring-values',
      'two-concentric-account-groups-center-five-hour-value-and-fit-above-footer', 'real-settings-switch-back-to-bars');
    await settings.locator('#quota-display').selectOption('rings');
    await assertMode('rings');
    await publish([['codex', 80, 10], ['claude', 60, 20]]);
    const priority = await waitSnapshot({ 'energyTarget.remainingPercent': 70, 'energy.scene': 'full' });
    assert.deepEqual(priority.energyTarget.windows.map(window => [window.id, window.remainingPercent]), [['short', 70], ['weekly', 15]]);
    await assertRingValues({ codex: { weekly: 10, short: 80 }, claude: { weekly: 20, short: 60 } });
    await publish([['codex', null, 10], ['claude', null, 20]]);
    await waitSnapshot({ 'energyTarget.remainingPercent': 15, 'energy.scene': 'low' });
    await assertRingValues({ codex: { weekly: 10 }, claude: { weekly: 20 } });
    await assertRingGeometry();
    checks.push('five-hour-controls-character-even-with-lower-weekly', 'weekly-fallback-only-when-five-hour-absent');
    await publish();
    await assertMode('rings');
    await app.evaluate(({ app }) => app.emit('second-instance', {}, ['TigaEnergyMonitor.exe', '--trigger=codex'], process.cwd()));
    await page.waitForTimeout(200);
    await assertRemovedStartup();
    assert.equal(JSON.parse(fs.readFileSync(path.join(data, 'settings.json'), 'utf8')).quotaDisplay, 'rings');
    assert((await app.evaluate(() => global.__displayQA.classifications)).some(args => args[0] === 50));
    await checkAndClose();

    await launch();
    assert.equal((await snapshot()).settings.quotaDisplay, 'rings');
    await publish();
    await assertMode('rings');
    await assertRingValues();
    await assertRingGeometry();
    await openSettings();
    assert.equal(await settings.locator('#quota-display').inputValue(), 'rings');
    await settings.locator('#quota-display').selectOption('bars');
    await assertMode('bars');
    await checkAndClose();
    await launch();
    assert.equal((await snapshot()).settings.quotaDisplay, 'bars');
    await publish();
    await assertMode('bars');
    await assertAccountDetails();
    checks.push('rings-and-bars-both-persist-across-restarts', 'no-frame-shows-bars-and-rings-together', 'no-startup-media-requests');
    await assertRemovedStartup();
    assert.deepEqual(errors, []);
    console.log(JSON.stringify({ ok: true, checks, realProviders: 0, nativeCore: 'actual monitor-core.exe', output: 'work/qa only' }));
  } catch (error) {
    if (page && !page.isClosed()) {
      await page.screenshot({ path: path.join(folder, 'failure.png'), omitBackground: true }).catch(() => {});
      console.error(`Display mode UI diagnostic: ${folder}`);
    }
    throw error;
  } finally { if (app) await app.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });

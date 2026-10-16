'use strict';
// Test fixtures are injected only by Playwright's main-process connection.
// The product exposes no fixture or fake-account API.
const { _electron: electron } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const qa = path.resolve(root, '..', 'qa');
fs.mkdirSync(qa, { recursive: true });
const data = fs.mkdtempSync(path.join(qa, 'fixture-data-'));
const env = { ...process.env };
delete env.ELECTRON_RUN_AS_NODE;
(async () => {
  const application = await electron.launch({ executablePath: require('electron'), args: [root, '--test-data-dir', data, '--no-providers'], env, cwd: root });
  try {
    const page = await application.firstWindow();
    await page.waitForFunction(() => Boolean(window.monitor));
    await page.getByRole('button', { name: '使用 ChatGPT 账号登录', exact: true }).waitFor();
    const baseline = await page.evaluate(() => window.monitor.getSnapshot());
    const push = async (state) => {
      await application.evaluate(({ BrowserWindow }, value) => BrowserWindow.getAllWindows()[0].webContents.send('monitor:snapshot', value), state);
      await page.waitForTimeout(80);
    };
    for (const quotaDisplay of ['bars', 'rings']) {
      const state = structuredClone(baseline);
      state.settings.quotaDisplay = quotaDisplay;
      const codex = state.providers.codex;
      codex.status = 'connected';
      codex.account = '<img src=x onerror=alert(1)>@test.invalid';
      codex.updatedAt = new Date(Date.now() - 1000).toISOString();
      codex.windows = [];
      await push(state);
      assert.equal(await page.locator('[data-provider=codex] button[data-action=refresh]').count(), 1);
      assert.equal(await page.locator('[data-provider=codex] button[data-action=login]').count(), 0);
      codex.windows = [{ id: 'five_hour', label: '5 小时', usedPercent: 100, remainingPercent: 0, resetsAt: new Date(Date.now() + 3600000).toISOString(), windowMinutes: 300 }];
      state.energy = { scene: 'stone' };
      await push(state);
      const number = page.locator('[data-provider=codex] .quota-window .quota-number');
      const track = page.locator('[data-provider=codex] .quota-track');
      const ring = page.locator('[data-provider=codex] .quota-ring');
      const center = page.locator('[data-provider=codex] .quota-ring-value');
      const meter = quotaDisplay === 'bars' ? track : ring;
      assert.equal(await number.isVisible(), true, `${quotaDisplay}: account values remain visible`);
      assert.match(await number.innerText(), /^0/);
      assert.equal(await track.count(), quotaDisplay === 'bars' ? 1 : 0);
      assert.equal(await ring.count(), quotaDisplay === 'rings' ? 1 : 0);
      assert.equal(await page.locator('#energy-gauge,.energy-gauge').count(), 0);
      assert.equal(await meter.isVisible(), true);
      assert.equal(await meter.getAttribute('aria-valuenow'), '0');
      assert.equal(await meter.getAttribute('role'), 'meter');
      const assertArc = async value => {
        if (quotaDisplay !== 'rings') return;
        assert.equal(await ring.locator('.gauge-fill').evaluate(circle =>
          parseFloat(circle.style.strokeDasharray || circle.getAttribute('stroke-dasharray'))), value);
        assert.equal(await page.locator('[data-provider=codex] .quota-ring-group[role=group]').count(), 1);
        assert.equal(await center.getAttribute('data-period'), 'short');
      };
      await assertArc(0);
      if (quotaDisplay === 'rings') assert.equal(await center.textContent(), '0%');
      assert.equal(await page.locator('[data-provider=codex] .provider-title img').count(), 0);
      assert.match(await page.locator('[data-provider=codex] .provider-title').getAttribute('title'), /^<img/);
      codex.windows[0].remainingPercent = 0.2;
      await push(state);
      assert.match(await number.innerText(), /^<1/);
      await assertArc(.2);
      for (const remaining of [50, 100]) {
        codex.windows[0].remainingPercent = remaining;
        await push(state);
        assert.equal(await meter.getAttribute('aria-valuenow'), String(remaining));
        assert.match(await number.innerText(), new RegExp(`^${remaining}`));
        await assertArc(remaining);
        if (quotaDisplay === 'rings') assert.equal(await center.textContent(), `${remaining}%`);
      }
      codex.windows[0].remainingPercent = 0;
      codex.status = 'connecting';
      await push(state);
      assert.match(await number.innerText(), /^0/);
      codex.updatedAt = new Date(Date.now() - 181000).toISOString();
      await push(state);
      assert.match(await number.innerText(), /待同步/);
      assert.equal(await meter.getAttribute('aria-valuenow'), null);
      await assertArc(0);
      if (quotaDisplay === 'rings') assert.equal(await center.textContent(), '—');
      codex.updatedAt = new Date(Date.now() - 1000).toISOString();
      codex.windows[0].resetsAt = new Date(Date.now() - 1000).toISOString();
      await push(state);
      assert.match(await number.innerText(), /待同步/);
      assert.equal(await meter.getAttribute('aria-valuenow'), null);
      await assertArc(0);
      if (quotaDisplay === 'rings') assert.equal(await center.textContent(), '—');
      state.providers.claude = { ...state.providers.claude, status: 'quota_pending', needsSetup: true, account: 'test-only@example.invalid' };
      await push(state);
      await page.getByRole('button', { name: '完成官方首次设置', exact: true }).waitFor();
      assert.equal(await page.locator('[data-provider=claude] .quota-number').count(), 0);
    }
    console.log(JSON.stringify({ ok: true, modes: ['bars', 'rings'], checks: ['connected-without-quota-refresh', 'real-zero-preserved', 'tiny-positive-is-not-zero', '50-and-100-reference-values', 'account-html-escaped', 'fresh-cache-during-refresh', 'stale-is-unknown', 'elapsed-reset-is-unknown', 'official-setup-action', 'pending-no-fake-percent', 'account-local-ring-aria-and-arcs-without-detached-gauge'] }));
  } finally { await application.close(); }
})().catch((error) => { console.error(error); process.exitCode = 1; });

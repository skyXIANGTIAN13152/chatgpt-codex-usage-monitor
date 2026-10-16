'use strict';
// Test-only provider boundaries. The real main process, IPC, settings store,
// target aggregation and native C++ classification remain in the execution path.
const { _electron: electron } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const qa = path.resolve(root, '..', 'qa');
fs.mkdirSync(qa, { recursive: true });
const folder = fs.mkdtempSync(path.join(qa, 'energy-target-test-'));
const data = path.join(folder, 'profile');
const bootstrap = path.join(folder, 'bootstrap.cjs');
const env = { ...process.env }; delete env.ELECTRON_RUN_AS_NODE;
fs.writeFileSync(bootstrap, `
'use strict';
global.__targetFixtures = {};
global.__nativeClassificationInputs = [];
const empty = id => ({id,status:'signed_out',account:null,plan:null,windows:[],updatedAt:null,error:null});
function provider(id) {
  return class FakeProvider {
    constructor({onUpdate}) { this.onUpdate=onUpdate; this.state=empty(id); global.__targetFixtures[id]=this; }
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
require(${JSON.stringify(path.join(root, 'backend', 'codex-provider.cjs'))}).CodexProvider=provider('codex');
require(${JSON.stringify(path.join(root, 'backend', 'claude-client.cjs'))}).ClaudeClient=provider('claude');
const {CoreBridge}=require(${JSON.stringify(path.join(root, 'backend', 'core-bridge.cjs'))});
const classify=CoreBridge.prototype.classifyEnergy;
CoreBridge.prototype.classifyEnergy=function(...args){global.__nativeClassificationInputs.push(args);return classify.apply(this,args);};
require(${JSON.stringify(path.join(root, 'main.cjs'))});
`);

function fixture(id, short, weekly) {
  const future = new Date(Date.now() + 3600000).toISOString();
  const windows = [{ id: 'five_hour', label: '5 小时', remainingPercent: short, windowMinutes: 300 }];
  if (weekly !== null) windows.push({ id: id === 'codex' ? 'weekly' : 'seven_day',
    label: '每周', remainingPercent: weekly, windowMinutes: 10080 });
  return { id, status: 'connected', account: `fixture-${id}@example.invalid`, plan: 'fixture',
    windows: windows.map(window => ({ ...window, usedPercent: 100 - window.remainingPercent, resetsAt: future })),
    updatedAt: new Date().toISOString(), error: null };
}

async function launch() {
  const app = await electron.launch({ executablePath: require('electron'),
    args: [bootstrap, '--test-data-dir', data], env, cwd: root });
  const page = await app.firstWindow();
  await page.waitForFunction(() => Boolean(window.monitor));
  await page.evaluate(() => window.monitor.getSnapshot());
  return { app, page };
}

async function publish(app, page, states) {
  await app.evaluate((_electron, states) => {
    for (const [id, value] of Object.entries(states)) global.__targetFixtures[id].set(value);
  }, states);
  return page.evaluate(() => window.monitor.refresh());
}

async function waitSnapshot(page, expected) {
  await page.waitForFunction(async expected => {
    const snapshot = await window.monitor.getSnapshot();
    return Object.entries(expected).every(([key, value]) => {
      const actual = key.split('.').reduce((current, part) => current?.[part], snapshot);
      return JSON.stringify(actual) === JSON.stringify(value);
    });
  }, expected, { timeout: 10000 });
  return page.evaluate(() => window.monitor.getSnapshot());
}

async function expectAccountRings(page, values) {
  const expected = Object.entries(values).flatMap(([id, periods]) =>
    Object.entries(periods).map(([period, value]) => ({ id, period, value })));
  await page.waitForFunction(expected => document.querySelectorAll('.provider .quota-ring').length === expected.length
    && expected.every(({ id, period, value }) => {
      const ring = document.querySelector(`[data-provider="${id}"] .quota-ring[data-period="${period}"]`);
      return ring && ring.getAttribute('aria-valuenow') === (value === null ? null : String(value));
    }), expected);
  assert.equal(await page.locator('#energy-gauge').count(), 0, 'There is no separate target ring under the character');
  assert.equal(await page.locator('.quota-ring-group').count(), Object.keys(values).length);
  for (const { id, period, value } of expected) {
    const provider = page.locator(`[data-provider="${id}"]`);
    const ring = provider.locator(`.quota-ring[data-period="${period}"]`);
    assert.equal(await ring.isVisible(), true);
    assert.equal(await ring.getAttribute('role'), 'meter');
    const text = (await provider.locator(`.quota-ring-legend .quota-window[data-period="${period}"] .quota-number`).textContent()).replace(/\s/g, '');
    if (value === null) assert.match(text, /—/);
    else assert.equal(text, `${value}%`);
  }
  for (const [id, periods] of Object.entries(values)) {
    const provider = page.locator(`[data-provider="${id}"]`);
    const controlling = Object.hasOwn(periods, 'short') ? 'short' : 'weekly';
    const center = provider.locator('.quota-ring-value');
    assert.equal(await provider.locator('.quota-ring-group').getAttribute('role'), 'group');
    assert.equal(await center.getAttribute('data-period'), controlling);
    const text = (await center.textContent()).replace(/\s/g, '');
    if (periods[controlling] === null) assert.match(text, /—/);
    else assert.equal(text, `${periods[controlling]}%`);
  }
}

(async () => {
  let application;
  const checks = [];
  try {
    let launched = await launch(); application = launched.app;
    const page = launched.page;
    assert.equal(await page.locator('#energy-target').count(), 0, 'Target selection belongs in Settings, not above the floating accounts');
    await publish(application, page, { codex: fixture('codex', 10, 90), claude: fixture('claude', 90, 10) });
    let snapshot = await waitSnapshot(page, { 'settings.energyTarget': 'codex', 'energyTarget.remainingPercent': 10, 'energy.scene': 'low' });
    checks.push('default-codex-real-native-classification');
    const opening = application.waitForEvent('window');
    await page.evaluate(() => window.monitor.windowAction('settings'));
    const settings = await opening;
    await settings.locator('#energy-target').waitFor();
    assert.equal(await settings.locator('#energy-target').inputValue(), 'codex');
    await settings.locator('#energy-target').selectOption('aggregate');
    snapshot = await waitSnapshot(page, { 'settings.energyTarget': 'aggregate', 'energyTarget.remainingPercent': 50, 'energy.scene': 'full' });
    assert.deepEqual(snapshot.energyTarget.windows.map(window => window.remainingPercent), [50, 50]);
    await settings.waitForFunction(() => document.querySelector('#target-summary').textContent.includes('50%'));
    checks.push('equal-weight-per-period-50-with-five-hour-priority');
    assert.equal(await settings.locator('#quota-display').inputValue(), 'bars');
    await settings.locator('#quota-display').selectOption('rings');
    await waitSnapshot(page, { 'settings.quotaDisplay': 'rings', 'energyTarget.remainingPercent': 50, 'energy.scene': 'full' });
    const ownQuota = { codex: { weekly: 90, short: 10 }, claude: { weekly: 10, short: 90 } };
    await expectAccountRings(page, ownQuota);
    checks.push('display-mode-changes-presentation-without-changing-target-calculation');
    for (const target of ['codex', 'claude', 'aggregate']) {
      await settings.locator('#energy-target').selectOption(target);
      await waitSnapshot(page, { 'settings.energyTarget': target,
        'energyTarget.remainingPercent': target === 'aggregate' ? 50 : target === 'claude' ? 90 : 10,
        'energy.scene': target === 'codex' ? 'low' : 'full' });
      await expectAccountRings(page, ownQuota);
    }
    checks.push('account-rings-retain-own-period-values-across-selected-and-aggregate-targets');
    await settings.locator('#show-codex').uncheck();
    await settings.locator('#show-claude').uncheck();
    await waitSnapshot(page, { 'settings.visible.codex': false, 'settings.visible.claude': false, 'energyTarget.remainingPercent': 50, 'energy.scene': 'full' });
    await page.waitForFunction(() => document.querySelectorAll('.quota-ring').length === 0);
    checks.push('target-independent-of-account-visibility');
    await settings.locator('#aggregate-codex').uncheck();
    await waitSnapshot(page, { 'settings.aggregateMembers': ['claude'], 'energyTarget.remainingPercent': 90, 'energy.scene': 'full' });
    assert.equal(await settings.locator('#aggregate-claude').isDisabled(), true);
    assert.equal(await settings.locator('#aggregate-claude').isChecked(), true);
    checks.push('last-member-cannot-be-deselected');
    // Both changes are dispatched in one renderer turn before IPC can reply.
    await settings.evaluate(() => {
      const codex = document.querySelector('#aggregate-codex'); codex.checked = true; codex.dispatchEvent(new Event('change', { bubbles: true }));
      const claude = document.querySelector('#aggregate-claude'); claude.checked = false; claude.dispatchEvent(new Event('change', { bubbles: true }));
    });
    await waitSnapshot(page, { 'settings.aggregateMembers': ['codex'], 'energyTarget.remainingPercent': 10 });
    await settings.waitForFunction(() => document.querySelector('#aggregate-codex').checked && !document.querySelector('#aggregate-claude').checked && !document.querySelector('#target-save-status').textContent.includes('正在保存'));
    checks.push('same-turn-member-edits-keep-final-selection');
    await settings.locator('#aggregate-claude').check();
    await waitSnapshot(page, { 'settings.aggregateMembers': ['codex', 'claude'], 'energyTarget.remainingPercent': 50 });
    await settings.locator('#show-codex').check();
    await settings.locator('#show-claude').check();
    await expectAccountRings(page, ownQuota);
    const expired = fixture('claude', 90, 10); expired.updatedAt = new Date(Date.now() - 181000).toISOString();
    await publish(application, page, { claude: expired });
    snapshot = await waitSnapshot(page, { 'energyTarget.status': 'incomplete', 'energyTarget.remainingPercent': null, 'energy.scene': 'unknown' });
    assert(snapshot.energyTarget.missingIds.includes('claude'));
    assert(snapshot.energyTarget.windows.every(window => window.remainingPercent === null));
    await settings.waitForFunction(() => document.querySelector('#target-summary').textContent === '待同步' && document.querySelector('#target-explanation').textContent.includes('Claude Code'));
    await expectAccountRings(page, { codex: { weekly: 90, short: 10 }, claude: { weekly: null, short: null } });
    checks.push('expired-member-is-unknown-never-fabricated-quota');
    await publish(application, page, { claude: fixture('claude', 90, null) });
    snapshot = await waitSnapshot(page, { 'energyTarget.status': 'ready', 'energyTarget.remainingPercent': 50,
      'energyTarget.controllingPeriod': 'short', 'energy.scene': 'full' });
    assert.deepEqual(snapshot.energyTarget.missingIds, []);
    assert.equal(snapshot.energyTarget.windows.find(window => window.id === 'short').remainingPercent, 50);
    assert.equal(snapshot.energyTarget.windows.find(window => window.id === 'weekly').remainingPercent, null);
    await settings.waitForFunction(() => document.querySelector('#target-periods').textContent.includes('50%') && document.querySelector('#target-periods').textContent.includes('待同步'));
    await expectAccountRings(page, { codex: { weekly: 90, short: 10 }, claude: { short: 90 } });
    checks.push('complete-five-hour-average-controls-despite-missing-weekly-reference');
    const missingShort = fixture('claude', 90, 10);
    missingShort.windows = missingShort.windows.filter(window => window.windowMinutes !== 300);
    await publish(application, page, { claude: missingShort });
    snapshot = await waitSnapshot(page, { 'energyTarget.status': 'incomplete', 'energyTarget.remainingPercent': null,
      'energyTarget.controllingPeriod': 'short', 'energy.scene': 'unknown' });
    assert(snapshot.energyTarget.missingIds.includes('claude'));
    await expectAccountRings(page, { codex: { weekly: 90, short: 10 }, claude: { weekly: 10 } });
    checks.push('missing-declared-five-hour-member-never-falls-back-to-weekly');
    const codexWeeklyOnly = fixture('codex', 10, 90);
    codexWeeklyOnly.windows = codexWeeklyOnly.windows.filter(window => window.windowMinutes !== 300);
    await publish(application, page, { codex: codexWeeklyOnly });
    await waitSnapshot(page, { 'energyTarget.status': 'ready', 'energyTarget.remainingPercent': 50,
      'energyTarget.controllingPeriod': 'weekly', 'energy.scene': 'full' });
    await expectAccountRings(page, { codex: { weekly: 90 }, claude: { weekly: 10 } });
    checks.push('weekly-only-accounts-fall-back-to-real-weekly-average');
    const unsupported = fixture('claude', 90, 10);
    unsupported.windows.push({ id: 'future_window', label: '其他周期', remainingPercent: 33, usedPercent: 67,
      windowMinutes: 60, resetsAt: new Date(Date.now() + 3600000).toISOString() });
    await publish(application, page, { codex: fixture('codex', 10, 90), claude: unsupported });
    await waitSnapshot(page, { 'energyTarget.status': 'ready', 'energyTarget.remainingPercent': 50,
      'energyTarget.controllingPeriod': 'short', 'energy.scene': 'full' });
    await expectAccountRings(page, ownQuota);
    checks.push('unrelated-extra-period-does-not-block-complete-five-hour-average');
    const customOnly = Object.fromEntries(['codex', 'claude'].map((id, index) => {
      const provider = fixture(id, 10, 90);
      provider.windows = [{ id: 'future_window', label: '其他周期', remainingPercent: 30 + index * 20,
        windowMinutes: 60, resetsAt: new Date(Date.now() + 3600000).toISOString() }];
      return [id, provider];
    }));
    await publish(application, page, customOnly);
    await waitSnapshot(page, { 'energyTarget.status': 'incomplete', 'energyTarget.remainingPercent': null,
      'energyTarget.controllingPeriod': null, 'energy.scene': 'unknown' });
    await settings.waitForFunction(() => document.querySelector('#target-summary').textContent === '待同步');
    checks.push('no-comparable-period-never-fabricates-an-aggregate');
    await publish(application, page, { codex: fixture('codex', 0, 90), claude: fixture('claude', 90, 10) });
    await settings.locator('#energy-target').selectOption('codex');
    await waitSnapshot(page, { 'settings.energyTarget': 'codex', 'energyTarget.remainingPercent': 0, 'energy.scene': 'stone' });
    await settings.waitForFunction(() => document.querySelector('#target-summary').textContent.includes('0%'));
    await expectAccountRings(page, { codex: { weekly: 90, short: 0 }, claude: { weekly: 10, short: 90 } });
    assert.equal(await settings.locator('#aggregate-members').isHidden(), true);
    checks.push('real-zero-single-target-stone');
    await publish(application, page, { codex: fixture('codex', 80, 0) });
    await waitSnapshot(page, { 'energyTarget.remainingPercent': 80, 'energyTarget.controllingPeriod': 'short', 'energy.scene': 'full' });
    await expectAccountRings(page, { codex: { weekly: 0, short: 80 }, claude: { weekly: 10, short: 90 } });
    checks.push('zero-weekly-reference-does-not-stone-a-healthy-five-hour-target');
    await settings.locator('#energy-target').selectOption('aggregate');
    await settings.locator('#aggregate-codex').uncheck();
    await waitSnapshot(page, { 'settings.energyTarget': 'aggregate', 'settings.aggregateMembers': ['claude'] });
    const nativeInputs = await application.evaluate(() => global.__nativeClassificationInputs);
    assert(nativeInputs.some(args => args[0] === 50)); assert(nativeInputs.some(args => args[0] === 0));
    await settings.locator('#show-codex').uncheck();
    await settings.locator('#show-claude').uncheck();
    await waitSnapshot(page, { 'settings.visible.codex': false, 'settings.visible.claude': false });
    await application.close(); application = null;
    launched = await launch(); application = launched.app;
    snapshot = await launched.page.evaluate(() => window.monitor.getSnapshot());
    assert.equal(snapshot.settings.energyTarget, 'aggregate');
    assert.deepEqual(snapshot.settings.aggregateMembers, ['claude']);
    assert.deepEqual(snapshot.settings.visible, { codex: false, claude: false });
    assert.equal(snapshot.settings.quotaDisplay, 'rings');
    assert.equal(snapshot.energyTarget.remainingPercent, null);
    await launched.page.locator('#no-providers').waitFor({ state: 'visible' });
    assert.equal(await launched.page.locator('#energy-gauge,.quota-ring').count(), 0);
    checks.push('target-members-display-mode-and-independent-visibility-survive-restart');
    console.log(JSON.stringify({ ok: true, checks, authentication: 'fake provider boundary only; no real login', nativeCore: 'actual monitor-core.exe', output: 'work/qa only' }));
  } finally { if (application) await application.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });

'use strict';
// Real isolated Electron host/IPC/native classifier. Only account I/O and
// Date.now are fixtures; the real five-second renderer tick updates countdowns.
const { _electron } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const qa = path.resolve(root, '..', 'qa');
fs.mkdirSync(qa, { recursive: true });
const folder = fs.mkdtempSync(path.join(qa, 'reset-countdown-ui-'));
const env = { ...process.env }; delete env.ELECTRON_RUN_AS_NODE;
const checks = [], layouts = [], errors = [];
const probe = process.argv.includes('--red-probe');

function prepare(locale) {
  const profile = path.join(folder, locale);
  fs.mkdirSync(profile);
  fs.writeFileSync(path.join(profile, 'settings.json'), JSON.stringify({
    floatingScale: 100, alwaysOnTop: false, followSystemMotion: false,
    reducedMotion: true, quotaDisplay: 'bars', energyTarget: 'codex', refreshSeconds: 300,
  }));
  const bootstrap = path.join(profile, 'bootstrap.cjs');
  fs.writeFileSync(bootstrap, `
require(${JSON.stringify(path.join(root, 'package.json'))}).appLocale=${JSON.stringify(locale)};
const test=global.__resetCountdown={now:Date.now(),providers:{},refreshCalls:[]};
Date.now=()=>test.now;
const empty=id=>({id,status:'signed_out',account:null,plan:null,windows:[],updatedAt:null,error:null});
test.quota=(id,{single=false,shortOffset=9000000,weeklyOffset=525600000}={})=>({
  id,status:'connected',account:id+'-fixture@example.invalid',plan:'Test subscription',error:null,
  updatedAt:new Date(test.now).toISOString(),windows:[
    ...(!single?[{id:'weekly',label:'Overall weekly',windowMinutes:10080,remainingPercent:84,
      resetsAt:weeklyOffset===null?null:new Date(test.now+weeklyOffset).toISOString()}]:[]),
    {id:'five_hour',label:'5-hour usage',windowMinutes:300,remainingPercent:75,
      resetsAt:shortOffset===null?null:new Date(test.now+shortOffset).toISOString()}]});
function provider(id){return class{
  constructor({onUpdate}){this.onUpdate=onUpdate;this.state=empty(id);test.providers[id]=this;}
  getState(){return structuredClone(this.state);}
  publish(state=this.state){this.state=state;this.onUpdate(this.getState());return this.getState();}
  async start(){return this.publish(test.quota(id));}
  async refresh(){test.refreshCalls.push({id,time:test.now});return this.publish();}
  async login(){throw new Error('Countdown regression must never authenticate');}
  stop(){}
};}
require(${JSON.stringify(path.join(root, 'backend/codex-provider.cjs'))}).CodexProvider=provider('codex');
require(${JSON.stringify(path.join(root, 'backend/claude-client.cjs'))}).ClaudeClient=provider('claude');
require(${JSON.stringify(path.join(root, 'main.cjs'))});
`);
  return { profile, bootstrap };
}

async function waitSnapshot(page, expected) {
  await page.waitForFunction(async expected => {
    const snapshot = await window.monitor.getSnapshot();
    return Object.entries(expected).every(([key, value]) => JSON.stringify(
      key.split('.').reduce((item, part) => item?.[part], snapshot)) === JSON.stringify(value));
  }, expected, { timeout: 10000 });
}

async function setClock(app, page, advance = 0) {
  const now = await app.evaluate((_electron, advance) => global.__resetCountdown.now += advance, advance);
  await page.evaluate(now => { window.__resetNow = now; Date.now = () => window.__resetNow; }, now);
}

async function publish(app, options = {}) {
  await app.evaluate((_electron, options) => {
    const test = global.__resetCountdown;
    for (const [id, provider] of Object.entries(test.providers)) provider.publish(test.quota(id, options));
  }, options);
}

async function expectReset(page, selector, text) {
  await page.waitForFunction(({ selector, text }) => {
    const element = document.querySelector(selector);
    if (!element) return false;
    const rect = element.getBoundingClientRect();
    const style = getComputedStyle(element);
    return rect.width > 0 && rect.height > 0 && style.display !== 'none' && style.visibility !== 'hidden'
      && element.textContent.replace(/\s+/g, '') === text.replace(/\s+/g, '');
  }, { selector, text }, { timeout: probe ? 2000 : 8000 });
}

async function inspectLayout(page, name, expectedCount) {
  await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
  const result = await page.evaluate(() => {
    const rect = element => {
      const bounds = element.getBoundingClientRect();
      return { x: bounds.x, y: bounds.y, right: bounds.right, bottom: bounds.bottom,
        width: bounds.width, height: bounds.height, text: element.innerText,
        scrollWidth: element.scrollWidth, clientWidth: element.clientWidth };
    };
    return { viewport: [innerWidth, innerHeight], footer: rect(document.querySelector('.widget-footer')),
      accounts: rect(document.querySelector('.widget-accounts')),
      elements: [...document.querySelectorAll('.provider, .quota-reset, .quota-ring-group, .widget-actions, .widget-status')]
        .map(element => ({ selector: element.className, ...rect(element) })),
      resets: [...document.querySelectorAll('.quota-window')].map(row => ({
        reset: row.querySelector('.quota-reset') ? rect(row.querySelector('.quota-reset')) : null,
        line: rect(row.querySelector('.quota-line')),
        track: row.querySelector('.quota-bar') ? rect(row.querySelector('.quota-bar')) : null,
      })) };
  });
  layouts.push({ name, ...result });
  assert.equal(result.resets.length, expectedCount, `${name}: all selected periods are visible`);
  for (const item of result.elements) {
    if (!item.width || !item.height) continue;
    assert(item.x >= -1 && item.y >= -1 && item.right <= 421 && item.bottom <= 247,
      `${name}: ${item.selector} must fit the 420x246 logical widget: ${JSON.stringify(item)}`);
  }
  for (const row of result.resets) {
    assert(row.reset?.height > 0, `${name}: each period needs a visible countdown`);
    assert(row.reset.scrollWidth <= row.reset.clientWidth + 1, `${name}: countdown text must not clip`);
    assert(row.reset.y >= row.line.bottom - 1, `${name}: countdown must not overlap quota values`);
    if (row.track) assert(row.reset.y >= row.track.bottom - 1, `${name}: countdown must not overlap bar`);
    assert(row.reset.bottom <= result.footer.y, `${name}: countdown must not overlap toolbar`);
  }
  assert(result.accounts.bottom <= result.footer.y, `${name}: toolbar follows visible accounts`);
  return result;
}

async function runLocale(locale) {
  const { profile, bootstrap } = prepare(locale);
  let app, page;
  try {
    app = await _electron.launch({ executablePath: require('electron'),
      args: [bootstrap, '--test-data-dir', profile], cwd: root, env });
    page = await app.firstWindow();
    page.on('pageerror', error => errors.push(error.message));
    await waitSnapshot(page, { 'energyTarget.remainingPercent': 75, 'energy.scene': 'full' });
    await setClock(app, page);
    const short = '[data-provider="codex"] .quota-window[data-period="short"] .quota-reset';
    const weekly = '[data-provider="codex"] .quota-window[data-period="weekly"] .quota-reset';
    const words = locale === 'en'
      ? { short: 'Reset 2h 30m', next: 'Reset 2h 29m', weekly: 'Reset 6d 2h', pending: 'Reset pending', unknown: 'Reset unknown' }
      : { short: '重置 2小时30分', next: '重置 2小时29分', weekly: '重置 6天2小时', pending: '重置待同步', unknown: '重置时间未知' };
    await expectReset(page, short, words.short);
    await expectReset(page, weekly, words.weekly);
    if (probe) return;
    const update = patch => page.evaluate(patch => window.monitor.updateSettings(patch), patch);
    for (const mode of ['bars', 'rings']) {
      await update({ quotaDisplay: mode, visible: { codex: true, claude: true } });
      await publish(app);
      await waitSnapshot(page, { 'settings.quotaDisplay': mode, 'settings.visible.claude': true });
      await expectReset(page, short, words.short);
      let doubleFooter;
      for (const scale of [80, 100, 125, 150]) {
        await update({ floatingScale: scale });
        await waitSnapshot(page, { 'settings.floatingScale': scale });
        const layout = await inspectLayout(page, `${locale}-${mode}-dual-${scale}`, 4);
        if (scale === 100) {
          doubleFooter = layout.footer.y;
          await page.screenshot({ path: path.join(folder, `${locale}-${mode}-dual-fixture.png`), omitBackground: true, scale: 'css' });
        }
      }
      await update({ visible: { codex: true, claude: false } });
      await publish(app, { single: true });
      await page.waitForFunction(() => document.querySelectorAll('.provider').length === 1 && document.querySelectorAll('.quota-window').length === 1);
      for (const scale of [80, 100, 125, 150]) {
        await update({ floatingScale: scale });
        await waitSnapshot(page, { 'settings.floatingScale': scale });
        const layout = await inspectLayout(page, `${locale}-${mode}-single-${scale}`, 1);
        assert(layout.footer.y < doubleFooter - 20, 'Hiding an account must move the toolbar upwards');
        if (scale === 100) await page.screenshot({ path: path.join(folder, `${locale}-${mode}-single-fixture.png`), omitBackground: true, scale: 'css' });
      }
      checks.push(`${locale}-${mode}-dual-and-single-countdowns-fit-all-four-scales`);
    }
    await update({ floatingScale: 100, quotaDisplay: 'bars', paused: true });
    await publish(app, { single: true });
    await expectReset(page, short, words.short);
    const before = await app.evaluate(() => global.__resetCountdown.refreshCalls.length);
    await setClock(app, page, 60000);
    // No snapshot push or settings mutation here: the actual renderer interval
    // must change the text even with animation paused and without provider I/O.
    await expectReset(page, short, words.next);
    assert.equal(await app.evaluate(() => global.__resetCountdown.refreshCalls.length), before);
    checks.push(`${locale}-real-render-tick-advances-countdown-without-network-while-paused`);

    for (const mode of ['bars', 'rings']) {
      await update({ quotaDisplay: mode });
      await publish(app, { single: true, shortOffset: 60000 });
      await waitSnapshot(page, { 'energyTarget.remainingPercent': 75, 'energy.scene': 'full' });
      await setClock(app, page, 60000);
      await expectReset(page, short, words.pending);
      await waitSnapshot(page, { 'energyTarget.remainingPercent': null, 'energy.scene': 'unknown' });
      const meter = page.locator(mode === 'rings' ? '[data-provider="codex"] .quota-ring[data-period="short"]'
        : '[data-provider="codex"] .quota-window[data-period="short"] .quota-track');
      assert.equal(await meter.getAttribute('aria-valuenow'), null, 'Reset must never fabricate a fresh 100% allowance');
      const actual = await page.evaluate(() => window.monitor.getSnapshot());
      assert.equal(actual.providers.codex.windows[0].remainingPercent, 75, 'Raw account data remains the last real reading');
      await publish(app, { single: true, shortOffset: null });
      await expectReset(page, short, words.unknown);
      await inspectLayout(page, `${locale}-${mode}-unknown-reset`, 1);
      checks.push(`${locale}-${mode}-elapsed-reset-pending-with-no-fake-percentage-and-unknown-deadline-label`);
    }
  } catch (error) {
    if (page) {
      await page.screenshot({ path: path.join(folder, `${locale}-failure.png`), omitBackground: true, scale: 'css' }).catch(() => {});
      const html = await page.content().catch(() => '');
      fs.writeFileSync(path.join(folder, `${locale}-failure.html`), html);
    }
    throw error;
  } finally { if (app) await app.close(); }
}

(async () => {
  for (const locale of probe ? ['zh-CN'] : ['zh-CN', 'en']) await runLocale(locale);
  assert.deepEqual(errors, []);
  fs.writeFileSync(path.join(folder, 'result.json'), JSON.stringify({ checks, layouts, errors,
    provider: 'Isolated test accounts only; no real authentication or startup changes',
    clock: 'Frozen Date.now with real renderer intervals; minute jumps are simulated, not online waiting' }, null, 2));
  console.log(JSON.stringify({ ok: true, checks, layoutCases: layouts.length, evidence: folder }));
})().catch(error => {
  fs.writeFileSync(path.join(folder, 'failure.json'), JSON.stringify({ error: error.stack, checks, layouts, errors }, null, 2));
  console.error('Countdown regression evidence:', folder); console.error(error); process.exitCode = 1;
});

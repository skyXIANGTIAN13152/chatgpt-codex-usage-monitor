'use strict';
// Real Electron host, native classifier, IPC and settings store. Only provider
// boundaries are replaced; this test never starts real login or writes startup
// registration. All artifacts and profiles stay under work/qa/english-*.
const { _electron } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const qa = path.resolve(root, '..', 'qa');
fs.mkdirSync(qa, { recursive: true });
const folder = fs.mkdtempSync(path.join(qa, 'english-ui-'));
const profile = path.join(folder, 'profile');
fs.mkdirSync(profile);
fs.writeFileSync(path.join(profile, 'settings.json'), JSON.stringify({
  floatingScale: 100, alwaysOnTop: false, followSystemMotion: false,
  reducedMotion: false, paused: false, quotaDisplay: 'bars', energyTarget: 'codex',
}));
const bootstrap = path.join(folder, 'bootstrap.cjs');
fs.writeFileSync(bootstrap, `
'use strict';
require(${JSON.stringify(path.join(root, 'package.json'))}).appLocale='en';
global.__englishQA={providers:{},calls:[]};
const empty=id=>({id,status:'signed_out',account:null,plan:null,windows:[],updatedAt:null,error:null});
function provider(id){return class{
 constructor({onUpdate}){this.onUpdate=onUpdate;this.state=empty(id);global.__englishQA.providers[id]=this;}
 getState(){return structuredClone(this.state);}
 publish(){this.onUpdate(this.getState());return this.getState();}
 set(value){this.state={...empty(id),...value,id};return this.publish();}
 async start(){return this.publish();}
 async refresh(){global.__englishQA.calls.push({id,method:'refresh'});return this.publish();}
 async login(){global.__englishQA.calls.push({id,method:'login'});return this.set({status:'awaiting_login'});}
 async logout(){global.__englishQA.calls.push({id,method:'logout'});return this.set(empty(id));}
 async openSetup(){global.__englishQA.calls.push({id,method:'setup'});return this.getState();}
 stop(){}
};}
require(${JSON.stringify(path.join(root, 'backend/codex-provider.cjs'))}).CodexProvider=provider('codex');
require(${JSON.stringify(path.join(root, 'backend/claude-client.cjs'))}).ClaudeClient=provider('claude');
require(${JSON.stringify(path.join(root, 'main.cjs'))});
`);
const env = { ...process.env };
delete env.ELECTRON_RUN_AS_NODE;
const han = /\p{Script=Han}/u;
const checks = [];
const languageAudits = [];
const layouts = [];
const errors = [];

function account(id, short, weekly, options = {}) {
  const now = Date.now();
  const windows = [];
  if (short !== undefined) windows.push({ id: 'five_hour', label: '5-hour usage', windowMinutes: 300,
    remainingPercent: short, usedPercent: short === null ? null : 100 - short,
    resetsAt: new Date(now + (options.shortResetOffset ?? 3600000)).toISOString() });
  if (weekly !== undefined) windows.push({ id: id === 'codex' ? 'weekly' : 'seven_day', label: 'Overall weekly', windowMinutes: 10080,
    remainingPercent: weekly, usedPercent: weekly === null ? null : 100 - weekly,
    resetsAt: new Date(now + 345600000).toISOString() });
  return { id, status: options.status || 'connected', account: `${id}-english-test@example.invalid`,
    plan: 'Test subscription', windows, updatedAt: new Date(now - (options.age ?? 65000)).toISOString(),
    error: options.error || null, ...options.provider };
}

async function waitSnapshot(page, expected) {
  await page.waitForFunction(async expected => {
    const snapshot = await window.monitor.getSnapshot();
    return Object.entries(expected).every(([key, value]) =>
      JSON.stringify(key.split('.').reduce((item, part) => item?.[part], snapshot)) === JSON.stringify(value));
  }, expected, { timeout: 10000 });
  return page.evaluate(() => window.monitor.getSnapshot());
}

async function publish(app, states) {
  await app.evaluate((_electron, states) => {
    for (const [id, state] of Object.entries(states)) global.__englishQA.providers[id].set(state);
  }, states);
}

async function englishOnly(page, name) {
  const evidence = await page.evaluate(() => {
    const rendered = element => {
      const bounds = element.getBoundingClientRect();
      const style = getComputedStyle(element);
      return bounds.width > 0 && bounds.height > 0 && style.visibility !== 'hidden' && style.display !== 'none';
    };
    const attributes = [...document.querySelectorAll('[title],[aria-label],[aria-description],[placeholder],img[alt]')]
      .filter(rendered).flatMap(element => ['title', 'aria-label', 'aria-description', 'placeholder', 'alt']
        .filter(attribute => element.hasAttribute(attribute))
        .map(attribute => ({ selector: element.id || element.className?.baseVal || element.className || element.tagName,
          attribute, value: element.getAttribute(attribute) })));
    // Closed native selects hide their option rectangles; the option labels
    // remain user-facing choices, so audit them separately as well.
    const options = [...document.querySelectorAll('select')].filter(rendered)
      .flatMap(select => [...select.options].map(option => ({ select: select.id, value: option.value, text: option.textContent })));
    return { lang: document.documentElement.lang, title: document.title,
      text: document.body.innerText, attributes, options };
  });
  assert.match(evidence.lang, /^en(?:-|$)/i, `${name}: document language`);
  const offenders = [];
  if (han.test(evidence.title)) offenders.push({ title: evidence.title });
  if (han.test(evidence.text)) offenders.push({ text: evidence.text });
  offenders.push(...evidence.attributes.filter(item => han.test(item.value)));
  offenders.push(...evidence.options.filter(item => han.test(item.text)));
  languageAudits.push({ name, ...evidence });
  assert.deepEqual(offenders, [], `${name}: visible text/accessibility/tooltip labels must be English`);
}

async function widgetLayout(page, name) {
  await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
  const result = await page.evaluate(() => {
    const selectors = ['.widget-info', '.widget-accounts', '.widget-footer', '.widget-actions',
      '.widget-status', '.provider', '.quota-ring-group', '.login-link', '#no-providers'];
    const bounds = selectors.flatMap(selector => [...document.querySelectorAll(selector)].flatMap(element => {
      const rect = element.getBoundingClientRect();
      if (!rect.width || !rect.height) return [];
      return [{ selector, text: element.innerText || '', x: rect.x, y: rect.y,
        right: rect.right, bottom: rect.bottom, width: rect.width, height: rect.height,
        scrollWidth: element.scrollWidth, clientWidth: element.clientWidth }];
    }));
    return { viewport: [innerWidth, innerHeight], bounds };
  });
  layouts.push({ name, ...result });
  for (const bounds of result.bounds) {
    assert(bounds.x >= -1 && bounds.y >= -1 && bounds.right <= 421 && bounds.bottom <= 247,
      `${name}: ${bounds.selector} must fit logical 420x246: ${JSON.stringify(bounds)}`);
    if (bounds.selector === '.login-link') {
      assert(bounds.scrollWidth <= bounds.clientWidth + 1,
        `${name}: the full English sign-in label must fit: ${JSON.stringify(bounds)}`);
    }
  }
}

async function clickWidget(page, selector) {
  // Exercise the real click listener without depending on physical overlap of
  // the transparent widget and the separate Settings window during automation.
  await page.locator(selector).evaluate(element => element.click());
}

async function verifyReadings(page, mode, expected) {
  const selector = mode === 'rings' ? '.quota-ring' : '.quota-track';
  assert.equal(await page.locator(mode === 'rings' ? '.quota-bar' : '.quota-ring-group').count(), 0);
  for (const [id, periods] of Object.entries(expected)) {
    for (const [period, value] of Object.entries(periods)) {
      const base = `[data-provider="${id}"]`;
      const meter = page.locator(mode === 'rings'
        ? `${base} ${selector}[data-period="${period}"]`
        : `${base} .quota-window[data-period="${period}"] ${selector}`);
      await meter.waitFor();
      await page.waitForFunction(({ id, period, mode, value }) => {
        const element = document.querySelector(mode === 'rings'
          ? `[data-provider="${id}"] .quota-ring[data-period="${period}"]`
          : `[data-provider="${id}"] .quota-window[data-period="${period}"] .quota-track`);
        return element && element.getAttribute('aria-valuenow') === (value === null ? null : String(value));
      }, { id, period, mode, value });
      assert.equal(await meter.getAttribute('aria-valuenow'), value === null ? null : String(value));
      assert.equal(await meter.getAttribute('role'), 'meter');
      assert(!han.test(await meter.getAttribute('aria-label') || ''));
      const row = page.locator(`${base} .quota-window[data-period="${period}"]`);
      assert.match(await row.getAttribute('title'), /reset|remaining|pending|stale|sync/i);
      if (value === null) assert.match(await row.locator('.quota-number').innerText(), /—/);
      else assert.equal((await row.locator('.quota-number').innerText()).replace(/\s/g, ''), `${value}%`);
    }
  }
}

(async () => {
  let app;
  try {
    app = await _electron.launch({ executablePath: require('electron'), args: [bootstrap, '--test-data-dir', profile], env, cwd: root });
    const widget = await app.firstWindow();
    widget.on('pageerror', error => errors.push(error.message));
    await widget.waitForFunction(() => window.monitor?.locale === 'en' && document.documentElement.lang.startsWith('en'));
    await widget.getByRole('button', { name: 'Sign in with ChatGPT', exact: true }).waitFor();
    await widget.getByRole('button', { name: 'Sign in with Claude Code', exact: true }).waitFor();
    await englishOnly(widget, 'signed-out-widget');
    await widgetLayout(widget, 'signed-out-both');
    await widget.screenshot({ path: path.join(folder, 'english-signed-out.png'), omitBackground: true, scale: 'css' });
    for (const id of ['codex', 'claude']) {
      await clickWidget(widget, `[data-id="${id}"][data-action="login"]`);
      await waitSnapshot(widget, { [`providers.${id}.status`]: 'awaiting_login' });
      await widget.locator(`[data-id="${id}"][data-action="cancel"]`).waitFor();
      await englishOnly(widget, `${id}-awaiting-authorization`);
      await widgetLayout(widget, `${id}-awaiting-authorization`);
      await clickWidget(widget, `[data-id="${id}"][data-action="cancel"]`);
      await waitSnapshot(widget, { [`providers.${id}.status`]: 'signed_out' });
    }
    checks.push('two-English-sign-in-flows-and-cancel-at-fake-provider-boundary');

    const opening = app.waitForEvent('window');
    await clickWidget(widget, '#settings-open');
    const settings = await opening;
    settings.on('pageerror', error => errors.push(error.message));
    await settings.waitForFunction(() => window.monitor?.locale === 'en' && document.documentElement.lang.startsWith('en'));
    await settings.locator('#quota-display').waitFor();
    await englishOnly(settings, 'all-static-settings-and-select-options');
    assert.deepEqual(await settings.locator('#energy-target option').evaluateAll(options => options.map(option => option.value)), ['codex', 'claude', 'aggregate']);
    assert.deepEqual(await settings.locator('#quota-display option').evaluateAll(options => options.map(option => option.value)), ['bars', 'rings']);
    const titles = await app.evaluate(({ BrowserWindow }) => BrowserWindow.getAllWindows().map(window => window.getTitle()));
    assert(titles.every(title => title.length > 0 && !han.test(title)), `Native window titles: ${titles}`);
    checks.push('English-window-titles-settings-headings-and-all-option-labels');

    for (const mode of ['bars', 'rings']) {
      await settings.locator('#quota-display').selectOption(mode);
      await waitSnapshot(widget, { 'settings.quotaDisplay': mode });
      for (const [value, scene] of [[80, 'full'], [12, 'low'], [0, 'stone']]) {
        await publish(app, { codex: account('codex', value, 90), claude: account('claude', 76, 82) });
        await waitSnapshot(widget, { 'energyTarget.remainingPercent': value, 'energy.scene': scene });
        await widget.waitForFunction(scene => document.querySelector('#scene-stage').dataset.state === scene, scene);
        await verifyReadings(widget, mode, { codex: { short: value, weekly: 90 }, claude: { short: 76, weekly: 82 } });
        await englishOnly(widget, `${mode}-${scene}`);
        await englishOnly(settings, `${mode}-${scene}-details`);
        await widgetLayout(widget, `${mode}-${scene}`);
        await widget.waitForFunction(scene => {
          const video = document.querySelector('.scene-video.is-visible');
          return video?.currentSrc.endsWith(`/${scene}.webm`) && Number(getComputedStyle(video).opacity) > .99;
        }, scene);
        await widget.screenshot({ path: path.join(folder, `english-${mode}-${scene}.png`), omitBackground: true, scale: 'css' });
      }
    }
    assert.match(await settings.locator('.quota-reset').first().innerText(), /reset/i);
    assert.match(await widget.locator('.provider-status').first().innerText(), /ago|update|refresh|minute|min/i);
    checks.push('bars-rings-full-warning-zero-with-English-reset-refresh-and-accessibility-text');

    await publish(app, { codex: account('codex', 20, 40), claude: account('claude', 80, 80) });
    for (const [mode, expected] of [['codex', 20], ['claude', 80], ['aggregate', 50]]) {
      await settings.locator('#energy-target').selectOption(mode);
      await waitSnapshot(widget, { 'settings.energyTarget': mode, 'energyTarget.remainingPercent': expected });
      await englishOnly(settings, `target-${mode}`);
    }
    await verifyReadings(widget, 'rings', { codex: { short: 20, weekly: 40 }, claude: { short: 80, weekly: 80 } });
    await settings.locator('#aggregate-codex').uncheck();
    await waitSnapshot(widget, { 'settings.aggregateMembers': ['claude'], 'energyTarget.remainingPercent': 80 });
    assert.equal(await settings.locator('#aggregate-claude').isDisabled(), true);
    await englishOnly(settings, 'aggregate-last-member-hint');
    await settings.locator('#aggregate-codex').check();
    await waitSnapshot(widget, { 'energyTarget.remainingPercent': 50 });
    await settings.locator('#show-codex').uncheck();
    await waitSnapshot(widget, { 'settings.visible.codex': false, 'energyTarget.remainingPercent': 50 });
    await widgetLayout(widget, 'only-Claude');
    await settings.locator('#show-claude').uncheck();
    await waitSnapshot(widget, { 'settings.visible.claude': false, 'energyTarget.remainingPercent': 50 });
    await widget.locator('#no-providers').waitFor({ state: 'visible' });
    await englishOnly(widget, 'both-accounts-hidden');
    await widgetLayout(widget, 'both-accounts-hidden');
    await settings.locator('#show-codex').check();
    await widgetLayout(widget, 'only-Codex');
    await settings.locator('#show-claude').check();
    await waitSnapshot(widget, { 'settings.visible.codex': true, 'settings.visible.claude': true });
    await settings.screenshot({ path: path.join(folder, 'english-aggregate-settings.png'), scale: 'css' });
    checks.push('all-targets-real-aggregate-membership-and-independent-visibility');

    await publish(app, { claude: account('claude', 80, 80, { age: 181000 }) });
    await waitSnapshot(widget, { 'energyTarget.status': 'incomplete', 'energyTarget.remainingPercent': null, 'energy.scene': 'unknown' });
    await verifyReadings(widget, 'rings', { codex: { short: 20, weekly: 40 }, claude: { short: null, weekly: null } });
    assert(!/%/.test(await settings.locator('#target-summary').innerText()));
    await englishOnly(widget, 'expired-account');
    await englishOnly(settings, 'expired-account-details');
    await widgetLayout(widget, 'expired-account');
    await publish(app, { claude: account('claude', undefined, 80) });
    await waitSnapshot(widget, { 'energyTarget.status': 'incomplete', 'energyTarget.controllingPeriod': 'short' });
    await englishOnly(settings, 'missing-controlling-period');
    await publish(app, { claude: account('claude', 80, 80, { shortResetOffset: -1000 }) });
    await waitSnapshot(widget, { 'energyTarget.remainingPercent': null, 'energy.scene': 'unknown' });
    await verifyReadings(widget, 'rings', { claude: { short: null, weekly: 80 } });
    await englishOnly(widget, 'elapsed-reset');
    await englishOnly(settings, 'elapsed-reset-details');
    await publish(app, { claude: { status: 'quota_pending', needsSetup: true,
      account: 'claude-english-test@example.invalid', plan: 'Test subscription', windows: [],
      notice: 'Complete the official first-run setup.', error: null } });
    await widget.locator('[data-id="claude"][data-action="setup"]').waitFor();
    await englishOnly(widget, 'official-setup-required');
    await englishOnly(settings, 'official-setup-details');
    await widgetLayout(widget, 'official-setup-required');
    await clickWidget(widget, '[data-id="claude"][data-action="setup"]');
    await publish(app, { claude: { status: 'quota_pending', windows: [], notice: 'Usage is not available yet.' } });
    await widget.locator('[data-id="claude"][data-action="refresh"]').waitFor();
    await englishOnly(widget, 'quota-pending');
    await widgetLayout(widget, 'quota-pending');
    checks.push('missing-stale-reset-expired-and-official-setup-states-no-fabricated-percentages');

    await publish(app, { codex: account('codex', 80, 0, { age: 1000 }), claude: account('claude', 80, 80, { age: 1000 }) });
    await settings.locator('#energy-target').selectOption('codex');
    await waitSnapshot(widget, { 'energyTarget.remainingPercent': 80, 'energy.scene': 'full' });
    await widget.waitForFunction(() => document.querySelector('#scene-stage').dataset.state === 'full');
    await widget.waitForTimeout(1900);
    await clickWidget(widget, '#pause');
    await waitSnapshot(widget, { 'settings.paused': true });
    await widget.waitForFunction(() => [...document.querySelectorAll('.scene-video')].every(video => video.paused));
    const before = await widget.locator('.scene-video.is-visible').evaluate(video => video.currentTime);
    const callsBefore = await app.evaluate(() => global.__englishQA.calls.filter(call => call.method === 'refresh').length);
    await clickWidget(widget, '#refresh');
    await widget.waitForTimeout(200);
    const callsAfter = await app.evaluate(() => global.__englishQA.calls.filter(call => call.method === 'refresh').length);
    assert(callsAfter > callsBefore, 'Refresh continues while the English UI is paused');
    const after = await widget.locator('.scene-video.is-visible').evaluate(video => video.currentTime);
    assert(Math.abs(after - before) < .03, `Paused media moved: ${before} -> ${after}`);
    await englishOnly(widget, 'paused-refresh');
    await clickWidget(widget, '#pause');
    await waitSnapshot(widget, { 'settings.paused': false });
    await widget.waitForFunction(() => !document.querySelector('.scene-video.is-visible').paused);
    await settings.locator('#reduce-motion').check();
    await waitSnapshot(widget, { 'settings.reducedMotion': true });
    await englishOnly(widget, 'reduced-motion-play-label');
    await clickWidget(widget, '#pause');
    await waitSnapshot(widget, { 'settings.reducedMotion': false, 'settings.paused': false });
    checks.push('English-pause-play-reduced-motion-and-refresh-with-real-video');

    for (const threshold of ['10', '20', '30', '40']) {
      await settings.locator('#threshold').selectOption(threshold);
      await waitSnapshot(widget, { 'settings.warningThreshold': Number(threshold) });
    }
    await settings.locator('#threshold').selectOption('20');
    for (const scale of ['80', '100', '125', '150']) {
      await settings.locator('#floating-scale').selectOption(scale);
      await waitSnapshot(widget, { 'settings.floatingScale': Number(scale) });
      await widgetLayout(widget, `scale-${scale}`);
    }
    await settings.locator('#floating-scale').selectOption('100');
    await waitSnapshot(widget, { 'settings.floatingScale': 100 });
    for (const [selector, key] of [['#always-on-top', 'alwaysOnTop'], ['#launch-codex', 'launchWithCodex'], ['#launch-claude', 'launchWithClaude']]) {
      await settings.locator(selector).check();
      await waitSnapshot(widget, { [`settings.${key}`]: true });
      await settings.locator(selector).uncheck();
      await waitSnapshot(widget, { [`settings.${key}`]: false });
    }
    for (const scene of ['full', 'low', 'stone']) {
      await settings.locator(`[data-scene="${scene}"]`).click();
      await waitSnapshot(widget, { previewScene: scene });
      await englishOnly(widget, `preview-${scene}`);
      await englishOnly(settings, `preview-${scene}-settings`);
      await widgetLayout(widget, `preview-${scene}`);
    }
    await settings.locator('#follow-quota').click();
    await waitSnapshot(widget, { previewScene: null });
    await settings.locator('#account-settings').scrollIntoViewIfNeeded();
    await englishOnly(settings, 'complete-account-details');
    await settings.screenshot({ path: path.join(folder, 'english-account-settings.png'), scale: 'css' });
    const nativeBounds = await app.evaluate(({ BrowserWindow }) => {
      const main = BrowserWindow.getAllWindows().find(window => !window.webContents.getURL().includes('settings.html'));
      return main.getContentSize();
    });
    assert.deepEqual(nativeBounds, [420, 246]);
    checks.push('all-scale-threshold-pin-startup-preview-options-within-widget-bounds');

    const calls = await app.evaluate(() => global.__englishQA.calls);
    for (const id of ['codex', 'claude']) assert(calls.some(call => call.id === id && call.method === 'login'));
    assert(calls.some(call => call.id === 'claude' && call.method === 'setup'));
    assert.deepEqual(errors, [], 'No renderer exceptions');
    fs.writeFileSync(path.join(folder, 'english-ui-report.json'), JSON.stringify({
      ok: true, checks, languageAudits, layouts, nativeBounds, rendererErrors: errors,
      authentication: 'Test providers only; no real login or startup registration.',
    }, null, 2));
    console.log(JSON.stringify({ ok: true, checks, screenshots: folder, languageAudits: languageAudits.length }));
  } finally {
    if (app) await app.close();
    if (!fs.existsSync(path.join(folder, 'english-ui-report.json'))) {
      fs.writeFileSync(path.join(folder, 'english-ui-failure-evidence.json'), JSON.stringify({ checks, languageAudits, layouts, errors }, null, 2));
    }
  }
})().catch(error => { console.error(error); process.exitCode = 1; });

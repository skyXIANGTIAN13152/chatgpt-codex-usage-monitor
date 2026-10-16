'use strict';
// Real isolated host and native classifier; fake account providers supply exact
// boundary values. Static-motion preference makes computed palettes immediate.
const { _electron } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const qa = path.resolve(root, '..', 'qa');
fs.mkdirSync(qa, { recursive: true });
const folder = fs.mkdtempSync(path.join(qa, 'quota-colors-ui-'));
const data = path.join(folder, 'profile');
fs.mkdirSync(data);
fs.writeFileSync(path.join(data, 'settings.json'), JSON.stringify({ quotaDisplay: 'bars',
  followSystemMotion: false, reducedMotion: true, energyTarget: 'codex', warningThreshold: 20 }));
const bootstrap = path.join(folder, 'bootstrap.cjs');
fs.writeFileSync(bootstrap, `
global.__quotaColorProviders = {};
const empty = id => ({ id, status: 'signed_out', account: null, plan: null, windows: [], updatedAt: null, error: null });
function provider(id) { return class {
 constructor({ onUpdate }) { this.onUpdate = onUpdate; this.state = empty(id); global.__quotaColorProviders[id] = this; }
 getState() { return structuredClone(this.state); } publish() { this.onUpdate(this.getState()); return this.getState(); }
 set(value) { this.state = { ...empty(id), ...value, id }; return this.publish(); }
 async start() { return this.publish(); } async refresh() { return this.publish(); }
 async login() { return this.publish(); } async logout() { this.state = empty(id); return this.publish(); }
 async openSetup() { return this.getState(); } stop() {}
}; }
require(${JSON.stringify(path.join(root, 'backend/codex-provider.cjs'))}).CodexProvider = provider('codex');
require(${JSON.stringify(path.join(root, 'backend/claude-client.cjs'))}).ClaudeClient = provider('claude');
require(${JSON.stringify(path.join(root, 'main.cjs'))});
`);
const env = { ...process.env }; delete env.ELECTRON_RUN_AS_NODE;
const ids = ['codex', 'claude'];
const both = (short, weekly = 90, extras = {}) => Object.fromEntries(ids.map(id => [id, { short, weekly, ...extras }]));
function rgb(value) {
  const hex = /^#([a-f\d]{3}|[a-f\d]{6})$/i.exec(value.trim());
  if (hex) { const expanded = hex[1].length === 3 ? [...hex[1]].map(c => c + c).join('') : hex[1];
    return [0, 2, 4].map(index => parseInt(expanded.slice(index, index + 2), 16)); }
  const colors = value.match(/[\d.]+/g)?.map(Number);
  assert(colors?.length >= 3, `Unsupported computed palette color: ${value}`);
  return colors.slice(0, 3);
}
const palette = style => ({ start: style.start, end: style.end, gradient: style.gradient,
  blend: style.blend, size: style.size, filter: style.filter });

(async () => {
  let app, page;
  const errors = [], checks = [];
  try {
    app = await _electron.launch({ executablePath: require('electron'),
      args: [bootstrap, '--test-data-dir', data], cwd: root, env });
    page = await app.firstWindow();
    page.on('pageerror', error => errors.push(error.message));
    await page.waitForFunction(() => Boolean(window.monitor));
    const snapshot = () => page.evaluate(() => window.monitor.getSnapshot());
    const update = patch => page.evaluate(patch => window.monitor.updateSettings(patch), patch);
    const publish = async specs => {
      await app.evaluate((_electron, specs) => {
        const now = Date.now();
        for (const [id, spec] of Object.entries(specs)) {
          const windows = spec.pending ? [] : [
            { id: 'five_hour', label: '5 小时', remainingPercent: spec.short, windowMinutes: 300,
              resetsAt: new Date(now + (spec.expiredShort ? -1000 : 7200000)).toISOString() },
            { id: id === 'codex' ? 'weekly' : 'seven_day', label: '总周', remainingPercent: spec.weekly,
              windowMinutes: 10080, resetsAt: new Date(now + 345600000).toISOString() },
          ].map(window => ({ ...window, usedPercent: 100 - window.remainingPercent }));
          global.__quotaColorProviders[id].set({ status: spec.pending ? 'quota_pending' : 'connected',
            account: `fixture-${id}@example.invalid`, plan: 'fixture', windows,
            updatedAt: spec.pending ? null : new Date(now - (spec.age ?? 1000)).toISOString() });
        }
      }, specs);
      await page.evaluate(() => window.monitor.refresh());
      // Let renderer subscription callbacks and styles finish in their own turn.
      await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
    };
    const read = () => page.locator('.widget-accounts .provider').evaluateAll(providers => {
      const style = element => {
        if (!element) return null;
        const computed = getComputedStyle(element);
        return { start: computed.getPropertyValue('--quota-start').trim(), end: computed.getPropertyValue('--quota-end').trim(),
          gradient: computed.backgroundImage, color: computed.color, filter: computed.filter,
          blend: computed.backgroundBlendMode, size: computed.backgroundSize };
      };
      return Object.fromEntries(providers.map(provider => [provider.dataset.provider, {
        state: provider.dataset.quotaLevel, header: style(provider.querySelector('h3')),
        periods: Object.fromEntries([...provider.querySelectorAll('.quota-window')].map(window => [window.dataset.period, {
          state: window.dataset.quotaLevel, text: window.querySelector('.quota-number').textContent,
          title: window.title, number: style(window.querySelector('.quota-number')),
          bar: style(window.querySelector('.quota-fill')),
        }])),
      }]));
    });
    const assertState = (sample, id, header, short, weekly) => {
      assert.equal(sample[id].state, header, `${id} header state`);
      if (short !== null) assert.equal(sample[id].periods.short.state, short, `${id} five-hour state`);
      if (weekly !== null) assert.equal(sample[id].periods.weekly.state, weekly, `${id} weekly state`);
    };
    const numbers = sample => Object.fromEntries(ids.map(id => [id, { state: sample[id].state, header: palette(sample[id].header),
      periods: Object.fromEntries(Object.entries(sample[id].periods).map(([period, item]) => [period, {
        state: item.state, text: item.text, number: palette(item.number),
      }])) }]));
    const bars = sample => Object.fromEntries(ids.map(id => [id, Object.fromEntries(
      Object.entries(sample[id].periods).map(([period, item]) => [period, item.bar ? palette(item.bar) : null]))]));
    const assertNoDust = style => assert.doesNotMatch(style.gradient, /type-dust/);
    const ring = () => page.locator('.widget-accounts .provider').evaluateAll(providers => Object.fromEntries(
      providers.map(provider => {
        const center = provider.querySelector('.quota-ring-value'), style = center && getComputedStyle(center);
        return [provider.dataset.provider, { ...Object.fromEntries(
          [...provider.querySelectorAll('.quota-ring')].map(meter => {
            const fill = meter.querySelector('.gauge-fill');
            return [meter.dataset.period, { level: meter.dataset.quotaLevel,
              stops: [...meter.querySelectorAll('.gauge-start,.gauge-end')].map(stop => getComputedStyle(stop).stopColor),
              arc: parseFloat(fill.style.strokeDasharray || fill.getAttribute('stroke-dasharray')),
              aria: meter.getAttribute('aria-valuenow') }];
          })), center: center && { period: center.dataset.period, level: center.dataset.quotaLevel,
            text: center.textContent, start: style.getPropertyValue('--quota-start').trim(),
            end: style.getPropertyValue('--quota-end').trim() } }];
      })));

    await publish(both(50));
    const full = await read();
    for (const id of ids) {
      assertState(full, id, 'full', 'full', 'full');
      assert.match(full[id].periods.short.number.gradient, /linear-gradient/);
      assertNoDust(full[id].header); assertNoDust(full[id].periods.short.number);
    }
    const blueStart = rgb(full.codex.header.start), blueEnd = rgb(full.codex.header.end);
    assert(blueStart[2] > blueStart[0] && blueEnd[1] > blueEnd[0], 'Codex keeps its blue-to-cyan palette.');
    const white = rgb(full.claude.header.start), yellow = rgb(full.claude.header.end);
    assert(white.every(channel => channel >= 220) && white[0] >= 245 && white[1] >= 245, 'Claude starts near white.');
    assert(yellow[0] >= 245 && yellow[1] >= 220 && yellow[2] <= 96, 'Claude ends in a bright, saturated yellow.');
    checks.push('exactly-50-percent-keeps-normal-provider-palettes', 'claude-white-to-bright-yellow-codex-blue-to-cyan');

    await publish(both(49.9));
    const low = await read();
    for (const id of ids) {
      assertState(low, id, 'low', 'low', 'full');
      assert.equal(low[id].periods.short.text, '50%', 'Rounded text must not decide the color boundary.');
      assert.notDeepEqual(palette(low[id].periods.short.number), palette(full[id].periods.short.number));
      assert.deepEqual(palette(low[id].periods.weekly.number), palette(full[id].periods.weekly.number));
      assert.notEqual(low[id].periods.short.bar.gradient, low[id].periods.weekly.bar.gradient);
      assertNoDust(low[id].periods.short.number);
    }
    const rose = rgb(low.codex.header.start), blood = rgb(low.codex.header.end);
    assert(rose[0] > rose[1] + 15 && rose[2] > rose[1], 'Codex warning starts in rose.');
    assert(blood[0] > blood[1] + 50 && blood[0] > blood[2] + 50, 'Codex warning ends in blood red.');
    const deepYellow = rgb(low.claude.header.start), orangeRed = rgb(low.claude.header.end);
    assert(deepYellow[0] > 150 && deepYellow[1] > 100 && deepYellow[2] < 100, 'Claude warning starts in deep yellow.');
    assert(orangeRed[0] > 200 && orangeRed[1] < 170 && orangeRed[2] < 100, 'Claude warning ends in orange red.');
    checks.push('49-point-9-is-low-even-when-rounded-to-50', 'weekly-and-five-hour-colors-are-independent');

    await publish({ codex: { short: 20, weekly: 85 }, claude: { short: 75, weekly: 30 } });
    await page.waitForFunction(() => document.querySelector('#scene-stage').dataset.state === 'low');
    await page.screenshot({ path: path.join(folder, 'independent-period-colors.png'), omitBackground: true, scale: 'css' });

    await publish({ codex: { short: 0, weekly: 90 }, claude: { short: .01, weekly: 90 } });
    const distinct = await read();
    assertState(distinct, 'codex', 'stone', 'stone', 'full');
    assertState(distinct, 'claude', 'low', 'low', 'full');
    assert.equal(distinct.codex.periods.short.text, '0%');
    assert.equal(distinct.claude.periods.short.text, '<1%');
    assertNoDust(distinct.claude.periods.short.number);
    assert.deepEqual(palette(distinct.claude.periods.short.number), palette(low.claude.periods.short.number));
    await publish(both(.01));
    const tiny = await read();
    for (const id of ids) {
      assertState(tiny, id, 'low', 'low', 'full');
      assert.equal(tiny[id].periods.short.text, '<1%');
      assert.deepEqual(palette(tiny[id].periods.short.number), palette(low[id].periods.short.number));
      assertNoDust(tiny[id].periods.short.number);
    }
    checks.push('zero-and-tiny-positive-do-not-collapse-into-one-state', 'providers-can-have-different-states');
    await publish(both(0));
    const stone = await read();
    for (const id of ids) {
      assertState(stone, id, 'stone', 'stone', 'full');
      assert.match(stone[id].header.gradient, /type-dust/);
      assert.match(stone[id].periods.short.number.gradient, /type-dust/);
      assert.deepEqual(palette(stone[id].periods.weekly.number), palette(full[id].periods.weekly.number));
    }
    assert.deepEqual(palette(stone.codex.header), palette(stone.claude.header));
    assert.deepEqual(palette(stone.codex.periods.short.number), palette(stone.claude.periods.short.number));
    checks.push('both-zero-providers-share-the-same-limestone-and-dust');
    await publish(both(0, 0));
    await page.waitForFunction(() => document.querySelector('#scene-stage').dataset.state === 'stone');
    await page.screenshot({ path: path.join(folder, 'both-providers-limestone.png'), omitBackground: true, scale: 'css' });

    await publish({ codex: { short: 20, weekly: 85 }, claude: { short: 75, weekly: 90 } });
    const independent = numbers(await read());
    const independentBars = bars(await read());
    assertState(await read(), 'codex', 'low', 'low', 'full');
    assertState(await read(), 'claude', 'full', 'full', 'full');
    for (const scene of ['stone', 'low', 'full']) {
      await page.evaluate(scene => window.monitor.windowAction(`preview-${scene}`), scene);
      await page.waitForFunction(scene => document.querySelector('#scene-stage').dataset.state === scene, scene);
      assert.deepEqual(numbers(await read()), independent);
      assert.deepEqual(bars(await read()), independentBars);
    }
    await page.evaluate(() => window.monitor.windowAction('preview-auto'));
    await update({ energyTarget: 'claude' });
    assert.deepEqual(numbers(await read()), independent);
    assert.deepEqual(bars(await read()), independentBars);
    await update({ energyTarget: 'codex', warningThreshold: 10 });
    assert.equal((await snapshot()).energy.scene, 'full');
    assert.deepEqual(numbers(await read()), independent);
    assert.deepEqual(bars(await read()), independentBars);
    await update({ warningThreshold: 40 });
    assert.equal((await snapshot()).energy.scene, 'low');
    assert.deepEqual(numbers(await read()), independent);
    assert.deepEqual(bars(await read()), independentBars);
    checks.push('preview-selected-target-and-role-threshold-never-recolor-account-data');

    await update({ quotaDisplay: 'rings' });
    await page.locator('.widget-accounts .quota-ring').first().waitFor({ state: 'visible' });
    assert.equal(await page.locator('#energy-gauge,.energy-gauge').count(), 0);
    assert.equal(await page.locator('.widget-accounts .quota-track').count(), 0);
    assert.deepEqual(numbers(await read()), independent);
    await page.waitForFunction(() => document.querySelector('#scene-stage').dataset.state === 'low');
    await page.screenshot({ path: path.join(folder, 'account-ring-groups.png'), omitBackground: true, scale: 'css' });
    await publish(both(49.9));
    for (const id of ids) {
      await update({ energyTarget: id });
      const gauge = await ring();
      for (const provider of ids) {
        assert.equal(gauge[provider].short.level, 'low'); assert.equal(gauge[provider].weekly.level, 'full');
        assert.equal(gauge[provider].short.arc, 49.9); assert.equal(gauge[provider].weekly.arc, 90);
        assert.equal(gauge[provider].short.aria, '49.9');
        assert.equal(gauge[provider].short.stops.length, 2);
        assert.equal(gauge[provider].center.period, 'short');
        assert.equal(gauge[provider].center.level, 'low');
      }
    }
    const ringsLow = numbers(await read());
    await update({ quotaDisplay: 'bars' });
    assert.deepEqual(numbers(await read()), ringsLow);
    await update({ quotaDisplay: 'rings' });
    await publish(both(50));
    const boundaryRing = await ring();
    for (const id of ids) { assert.equal(boundaryRing[id].short.level, 'full'); assert.equal(boundaryRing[id].short.arc, 50); }
    await publish(both(100));
    const fullRings = await ring();
    for (const id of ids) { assert.equal(fullRings[id].short.arc, 100); assert.equal(fullRings[id].short.aria, '100'); }
    await publish(both(0));
    const zeroRings = await ring();
    for (const id of ids) { assert.equal(zeroRings[id].short.level, 'stone'); assert.equal(zeroRings[id].short.arc, 0); assert.equal(zeroRings[id].short.aria, '0'); }
    assert.deepEqual(zeroRings.codex.short.stops, zeroRings.claude.short.stops);
    checks.push('bars-and-rings-keep-identical-account-number-palettes', 'rings-use-per-period-50-percent-boundary-and-shared-stone');

    await publish(both(0, 50, { age: 181000 }));
    const stale = await read();
    for (const id of ids) {
      assertState(stale, id, 'unknown', 'unknown', 'unknown');
      assert.deepEqual(palette(stale[id].header), palette(full[id].header));
      for (const item of Object.values(stale[id].periods)) {
        assert.equal(item.text, '—待同步'); assertNoDust(item.number);
        const color = rgb(item.number.start);
        assert(Math.max(...color) - Math.min(...color) < 50, 'Unknown quota uses a neutral palette.');
      }
    }
    const unknownRings = await ring();
    for (const id of ids) for (const period of ['short', 'weekly']) {
      assert.equal(unknownRings[id][period].level, 'unknown');
      assert.equal(unknownRings[id][period].aria, null); assert.equal(unknownRings[id][period].arc, 0);
    }
    for (const id of ids) { assert.equal(unknownRings[id].center.text, '—'); assert.equal(unknownRings[id].center.level, 'unknown'); }
    await publish(both(0, 50, { pending: true }));
    const pending = await read();
    for (const id of ids) {
      assert.equal(pending[id].state, 'unknown');
      assert.deepEqual(pending[id].periods, {});
      assert.deepEqual(palette(pending[id].header), palette(full[id].header));
    }
    checks.push('stale-zero-is-unknown-without-dust', 'missing-quota-keeps-brand-header-without-invented-percentages');
    await publish({ codex: { short: 0, weekly: 80, expiredShort: true }, claude: { short: 50, weekly: 80 } });
    const expired = await read();
    assertState(expired, 'codex', 'unknown', 'unknown', 'full');
    assert.equal(expired.codex.periods.short.text, '—待同步');
    assertNoDust(expired.codex.periods.short.number);
    assert.deepEqual(palette(expired.codex.header), palette(full.codex.header));
    const expiredCenter = (await ring()).codex.center;
    assert.equal(expiredCenter.period, 'short'); assert.equal(expiredCenter.text, '—');
    assert.equal(expiredCenter.level, 'unknown', 'A declared expired five-hour window cannot fall back to a healthy weekly value.');
    checks.push('expired-period-does-not-stone-a-valid-account');
    await publish({ codex: { short: null, weekly: 80 }, claude: { short: 50, weekly: 80 } });
    const invalidCenter = (await ring()).codex.center;
    assert.equal(invalidCenter.period, 'short');
    assert.equal(invalidCenter.text, '—'); assert.equal(invalidCenter.level, 'unknown');
    checks.push('declared-invalid-five-hour-value-does-not-fall-back-to-weekly');

    assert.deepEqual(errors, []);
    fs.writeFileSync(path.join(folder, 'palettes.json'), JSON.stringify({ full, low, stone }, null, 2));
    console.log(JSON.stringify({ ok: true, checks, realProviders: 0, nativeCore: 'actual monitor-core.exe', output: folder,
      screenshots: ['independent-period-colors.png', 'both-providers-limestone.png', 'account-ring-groups.png'] }));
  } catch (error) {
    if (page && !page.isClosed()) {
      await page.screenshot({ path: path.join(folder, 'failure.png'), omitBackground: true }).catch(() => {});
      console.error(`Quota color UI diagnostic: ${folder}`);
    }
    throw error;
  } finally { if (app) await app.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });

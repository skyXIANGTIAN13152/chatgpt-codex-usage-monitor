'use strict';
// Actual host/IPC and native classifier, with isolated fake account providers.
// No account credentials, product test API, packaging, or login requests are used.
const { _electron } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const qa = path.resolve(root, '..', 'qa');
fs.mkdirSync(qa, { recursive: true });
const folder = fs.mkdtempSync(path.join(qa, 'compact-effects-ui-'));
const data = path.join(folder, 'profile');
fs.mkdirSync(data);
fs.writeFileSync(path.join(data, 'settings.json'), JSON.stringify({ quotaDisplay: 'bars',
  followSystemMotion: false, reducedMotion: true, paused: false, energyTarget: 'codex' }));
const bootstrap = path.join(folder, 'bootstrap.cjs');
fs.writeFileSync(bootstrap, `
global.__compactEffectProviders = {};
const empty = id => ({ id, status: 'signed_out', account: null, plan: null, windows: [], updatedAt: null, error: null });
function provider(id) { return class {
 constructor({ onUpdate }) { this.onUpdate = onUpdate; this.state = empty(id); global.__compactEffectProviders[id] = this; }
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
const providerIds = ['codex', 'claude'];
const visibleCases = [{ codex: true, claude: true }, { codex: true, claude: false },
  { codex: false, claude: true }, { codex: false, claude: false }];
const both = (short, weekly = 80, extra = {}) => Object.fromEntries(providerIds.map(id => [id, { short, weekly, ...extra }]));

(async () => {
  let app, page;
  const errors = [], checks = [], geometries = [], screenshots = [];
  try {
    app = await _electron.launch({ executablePath: require('electron'),
      args: [bootstrap, '--test-data-dir', data], cwd: root, env });
    page = await app.firstWindow();
    page.on('pageerror', error => errors.push(error.message));
    await page.waitForFunction(() => Boolean(window.monitor));
    await page.evaluate(() => document.fonts.ready);
    const settle = () => page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
    const update = async patch => { await page.evaluate(patch => window.monitor.updateSettings(patch), patch); await settle(); };
    const publish = async specs => {
      await app.evaluate((_electron, specs) => {
        const now = Date.now();
        for (const id of ['codex', 'claude']) {
          const spec = specs[id] || { signedOut: true };
          if (spec.signedOut) { global.__compactEffectProviders[id].set({ status: 'signed_out' }); continue; }
          const windows = [['short', 300], ['weekly', 10080]].filter(([period]) => Object.hasOwn(spec, period))
            .map(([period, duration]) => ({ id: period === 'short' ? 'five_hour' : 'weekly',
              label: period === 'short' ? '5 小时' : '总周', windowMinutes: duration,
              remainingPercent: spec[period], usedPercent: Number.isFinite(spec[period]) ? 100 - spec[period] : null,
              resetsAt: new Date(now + (period === 'short' ? 7200000 : 345600000)).toISOString() }));
          global.__compactEffectProviders[id].set({ status: 'connected', account: `fixture-${id}@example.invalid`,
            plan: 'fixture', windows, updatedAt: new Date(now - (spec.age ?? 1000)).toISOString() });
        }
      }, specs);
      await page.evaluate(() => window.monitor.refresh()); await settle();
    };
    const videoGeometry = () => page.locator('.scene-video').evaluateAll(videos => videos.map(video => {
      const { x, y, width, height } = video.getBoundingClientRect(), style = getComputedStyle(video);
      return { x, y, width, height, transform: style.transform, translate: style.translate,
        rotate: style.rotate, scale: style.scale, objectFit: style.objectFit };
    }));
    const videoBaseline = await videoGeometry();
    for (const video of videoBaseline) {
      assert.equal(video.transform, 'none'); assert.equal(video.translate, 'none');
      assert.equal(video.rotate, 'none'); assert.equal(video.scale, 'none'); assert.equal(video.objectFit, 'contain');
    }
    const geometry = () => page.evaluate(() => {
      const rect = element => { const { x, y, width, height, bottom, right } = element.getBoundingClientRect();
        return { x, y, width, height, bottom, right }; };
      const providers = [...document.querySelectorAll('.widget-accounts .provider')];
      const placeholder = document.querySelector('#no-providers');
      return { width: innerWidth, height: innerHeight, accounts: rect(document.querySelector('.widget-accounts')),
        providers: providers.map(provider => ({ id: provider.dataset.provider, rect: rect(provider),
          buttons: [...provider.querySelectorAll('button')].map(rect),
          numbers: [...provider.querySelectorAll('.quota-window .quota-number')].map(element => element.textContent),
          meters: provider.querySelectorAll('[role=meter]').length })),
        placeholder: !placeholder.hidden ? rect(placeholder) : null,
        footer: rect(document.querySelector('.widget-footer')),
        buttons: [...document.querySelectorAll('.widget-actions button')].map(rect),
        status: rect(document.querySelector('#sync-status')) };
    });
    const assertLayout = (sample, visible, fixtureName, mode) => {
      assert.deepEqual([sample.width, sample.height], [420, 246]);
      const ids = providerIds.filter(id => visible[id]);
      assert.deepEqual(sample.providers.map(provider => provider.id), ids);
      assert.equal(Boolean(sample.placeholder), ids.length === 0);
      const content = ids.length ? sample.providers.map(provider => provider.rect) : [sample.placeholder];
      const contentBottom = Math.max(...content.map(rect => rect.bottom));
      const gap = sample.footer.y - contentBottom;
      assert(Math.abs(gap - 12) <= .5, `${mode}/${fixtureName}/${ids}: footer gap ${gap} must remain 12 CSS px.`);
      assert(Math.abs(content[0].y - sample.accounts.y) <= 1, 'Remaining content starts at the account anchor without an empty slot.');
      for (let index = 1; index < content.length; index++) {
        assert(content[index].y >= content[index - 1].bottom + 2, 'Account contents must not overlap.');
      }
      assert(sample.footer.bottom <= sample.height && sample.footer.right <= sample.width);
      assert(sample.status.bottom <= sample.footer.bottom + 1);
      for (const button of sample.buttons) {
        assert(button.y >= contentBottom && button.bottom <= sample.footer.bottom + 1);
        assert(button.width >= 18 && button.height >= 18, 'Compact layout preserves usable footer controls.');
      }
      for (let index = 1; index < sample.buttons.length; index++) {
        assert(sample.buttons[index].x >= sample.buttons[index - 1].right, 'Footer controls must not overlap.');
      }
      for (const provider of sample.providers) for (const button of provider.buttons) {
        assert(button.y >= provider.rect.y && button.bottom <= provider.rect.bottom + 1);
        assert(button.bottom <= sample.footer.y, 'Login link must remain above the footer.');
      }
    };

    for (const mode of ['bars', 'rings']) {
      await update({ quotaDisplay: mode });
      for (const [fixtureName, specs] of Object.entries({ connected: both(25, 80),
        signed_out: both(25, 80, { signedOut: true }),
        mixed: { codex: { short: 25, weekly: 80 }, claude: { signedOut: true } } })) {
        await publish(specs);
        let dual;
        for (const visible of visibleCases) {
          await update({ visible });
          const sample = await geometry();
          geometries.push({ mode, fixtureName, visible, ...sample });
          assertLayout(sample, visible, fixtureName, mode);
          if (visible.codex && visible.claude) dual = sample;
          else if (visible.codex || visible.claude) {
            assert(sample.footer.y < dual.footer.y - 15, 'Hiding either account must lift the footer by its removed content.');
            assert(Math.abs(sample.providers[0].rect.y - dual.providers[0].rect.y) <= 1);
            const same = dual.providers.find(provider => provider.id === sample.providers[0].id);
            assert.deepEqual(sample.providers[0].numbers, same.numbers, 'Hiding another account does not change quota values.');
            assert.equal(sample.providers[0].meters, same.meters);
          }
          assert.deepEqual(await videoGeometry(), videoBaseline, 'Content compression must never move or transform Tiga videos.');
        }
      }
    }
    checks.push('24-real-geometry-cases-cover-both-modes-zero-one-two-accounts-and-login-states',
      'either-hidden-account-lifts-footer-without-placeholder-space-or-overlap',
      'login-links-values-and-footer-controls-remain-usable');

    const effects = () => page.locator('.widget-accounts .quota-window').evaluateAll(rows => rows.map(row => {
      const provider = row.closest('.provider'), period = row.dataset.period;
      const ring = provider.querySelector(`.quota-ring[data-period="${period}"]`);
      const surface = ring || row;
      const meter = ring || row.querySelector('.quota-track');
      const fill = surface.querySelector(ring ? '.gauge-fill' : '.quota-fill');
      const attributes = circle => Object.fromEntries(['cx', 'cy', 'r', 'pathLength', 'transform', 'stroke-dasharray']
        .map(name => [name, circle.getAttribute(name)]));
      const style = getComputedStyle(fill);
      return { id: provider.dataset.provider, period, level: row.dataset.quotaLevel,
        text: row.querySelector('.quota-number').textContent, aria: meter.getAttribute('aria-valuenow'),
        value: ring ? parseFloat(fill.getAttribute('stroke-dasharray')) : parseFloat(fill.style.width),
        fill: ring ? attributes(fill) : null, background: style.backgroundImage, shadow: style.boxShadow,
        metals: [...surface.querySelectorAll('.gauge-metal,.gauge-bevel')].map(attributes),
        materialVisibility: [...surface.querySelectorAll('.gauge-metal,.gauge-bevel')].map(material => {
          const computed = getComputedStyle(material), bounds = material.getBoundingClientRect();
          return { display: computed.display, visibility: computed.visibility, opacity: Number(computed.opacity),
            width: bounds.width, height: bounds.height, stroke: computed.stroke,
            animations: material.getAnimations().map(animation => animation.playState) };
        }),
        baseStops: ring ? [...ring.querySelectorAll('.gauge-start,.gauge-end')].map(stop => getComputedStyle(stop).stopColor) : null,
        veinCount: surface.querySelectorAll('.quota-veins,.quota-bar-veins,.vein-body,.vein-light').length };
    }));
    const assertEffects = (samples, specs, mode) => {
      for (const sample of samples) {
        const spec = specs[sample.id], expected = spec[sample.period];
        const known = Number.isFinite(expected) && (spec.age ?? 1000) <= 180000;
        const low = known && expected > 0 && expected < 50;
        assert.equal(sample.aria, known ? String(expected) : null);
        assert.equal(sample.value, known ? expected : 0, 'Metal decoration cannot change progress geometry.');
        assert.equal(sample.text, !known ? '—待同步' : expected > 0 && expected < 1 ? '<1%' : `${Math.round(expected)}%`);
        assert.equal(sample.veinCount, 0, `${mode}/${sample.id}/${sample.period}: vein decoration must be completely removed.`);
        assert.equal(sample.metals.length, mode === 'rings' && low ? 2 : 0);
        if (mode === 'rings') {
          assert.equal(sample.baseStops.length, 2, 'The original account color gradient is preserved.');
          for (const metal of sample.metals) assert.deepEqual(metal, sample.fill, 'Reflection and bevel match the exact quota arc.');
        } else if (low) {
          assert((sample.background.match(/linear-gradient/g) || []).length >= 2, 'Low bars have layered metallic light.');
          assert.match(sample.shadow, /inset/, 'Low bars retain raised light/shadow edges.');
        }
        for (const material of sample.materialVisibility) {
          assert.notEqual(material.display, 'none'); assert.notEqual(material.visibility, 'hidden');
          assert(material.opacity > 0 && material.width > 0 && material.height > 0, 'Static metal remains visible.');
          assert.notEqual(material.stroke, 'none');
          assert.deepEqual(material.animations, [], 'Metal reflections are static and introduce no pulse animation.');
        }
      }
    };
    const scenarios = [
      { codex: { short: 49.9, weekly: 50 }, claude: { short: 0, weekly: null } },
      { codex: { short: .01, weekly: 100 }, claude: { short: 25, weekly: 80 } },
      { codex: { short: 80, weekly: 47 }, claude: { short: 30, weekly: 80 } },
      { codex: { short: 25, weekly: 0, age: 181000 }, claude: { short: 100, weekly: 50 } },
    ];
    for (const mode of ['bars', 'rings']) {
      await update({ quotaDisplay: mode, visible: { codex: true, claude: true } });
      for (const specs of scenarios) { await publish(specs); assertEffects(await effects(), specs, mode); }
      const specs = { codex: { short: 25, weekly: 80 }, claude: { short: 80, weekly: 47 } };
      await publish(specs);
      await update({ reducedMotion: false, followSystemMotion: false, paused: false });
      const before = await effects(); assertEffects(before, specs, mode);
      for (const patch of [{ paused: true }, { paused: false, reducedMotion: true }]) {
        await update(patch);
        const stopped = await effects(); assertEffects(stopped, specs, mode);
        assert.deepEqual(stopped.map(({ id, period, text, aria, value, fill, metals, background, shadow }) => ({ id, period, text, aria, value, fill, metals, background, shadow })),
          before.map(({ id, period, text, aria, value, fill, metals, background, shadow }) => ({ id, period, text, aria, value, fill, metals, background, shadow })));
      }
      assert.deepEqual(await videoGeometry(), videoBaseline);
    }
    assert.equal(await page.locator('.quota-veins,.quota-bar-veins,.vein-body,.vein-light').count(), 0);
    checks.push('low-periods-alone-retain-static-raised-metal',
      'veins-are-absent-for-all-periods-and-display-modes', '49-point-9-and-tiny-positive-preserve-exact-aria-and-arc',
      'paused-and-reduced-motion-retain-identical-static-metal', 'source-video-transform-and-geometry-never-change');

    await page.evaluate(() => window.monitor.windowAction('preview-full'));
    await update({ reducedMotion: true, paused: false });
    for (const mode of ['bars', 'rings']) for (const [name, specs, visible] of [
      ['single-weekly47', { codex: { weekly: 47 } }, { codex: true, claude: false }],
      ['mixed-account-periods', { codex: { short: 25, weekly: 80 }, claude: { short: 80, weekly: 47 } }, { codex: true, claude: true }],
    ]) {
      await publish(specs); await update({ quotaDisplay: mode, visible });
      for (const [background, color] of [['white', '#f6f8fb'], ['dark', '#101a2a']]) {
        await page.evaluate(color => { document.body.style.background = color; }, color);
        const file = `${mode}-${name}-${background}.png`;
        await page.screenshot({ path: path.join(folder, file), scale: 'css' }); screenshots.push(file);
      }
    }
    await page.evaluate(() => { document.body.style.background = 'transparent'; });
    assert.deepEqual(errors, []);
    fs.writeFileSync(path.join(folder, 'geometry.json'), JSON.stringify(geometries, null, 2));
    console.log(JSON.stringify({ ok: true, checks, geometryCases: geometries.length, realProviders: 0,
      nativeCore: 'actual monitor-core.exe', output: folder, screenshots }));
  } catch (error) {
    fs.writeFileSync(path.join(folder, 'geometry.json'), JSON.stringify(geometries, null, 2));
    if (page && !page.isClosed()) {
      await page.screenshot({ path: path.join(folder, 'failure.png'), omitBackground: true }).catch(() => {});
      console.error(`Compact effects diagnostic: ${folder}`);
    }
    throw error;
  } finally { if (app) await app.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });

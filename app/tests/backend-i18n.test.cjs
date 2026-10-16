'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const root = path.resolve(__dirname, '..');
const { ENGLISH_MESSAGES, t, localizedError } = require('../backend/i18n.cjs');

function inVariant(locale, body, environment = {}) {
  // Override the package object only inside a disposable Node process. Neither
  // the source package nor any installed profile is changed by language tests.
  const code = `const assert = require('node:assert/strict');
    const packagePath = require.resolve('./package.json'); require(packagePath);
    require.cache[packagePath].exports = { ...require.cache[packagePath].exports, appLocale: ${JSON.stringify(locale)} };
    (async () => { ${body} })().catch(error => { console.error(error); process.exitCode = 1; });`;
  return execFileSync(process.execPath, ['-e', code], { cwd: root,
    env: { ...process.env, ...environment }, windowsHide: true, encoding: 'utf8', timeout: 10000 });
}

test('every product-owned Chinese backend message has a complete English translation', () => {
  const files = ['main.cjs', ...fs.readdirSync(path.join(root, 'backend'))
    .filter(file => file.endsWith('.cjs') && file !== 'i18n.cjs').map(file => `backend/${file}`)];
  for (const file of files) {
    const source = fs.readFileSync(path.join(root, file), 'utf8');
    for (const match of source.matchAll(/'([^'\r\n]*\p{Script=Han}[^'\r\n]*)'/gu)) {
      assert(Object.hasOwn(ENGLISH_MESSAGES, match[1]), `${file}: untranslated message ${match[1]}`);
    }
  }
  for (const [source, english] of Object.entries(ENGLISH_MESSAGES)) {
    assert(english.trim().length > 0);
    assert.doesNotMatch(english, /\p{Script=Han}/u, source);
    assert.equal(t(source, 'en'), english);
    assert.equal(t(source, 'zh-CN'), source);
  }
});

test('fixed package language ignores OS locale and English fallback handles local OS errors', () => {
  inVariant('en', `
    const { APP_LOCALE, t, localizedError } = require('./backend/i18n.cjs');
    assert.equal(APP_LOCALE, 'en');
    assert.equal(t('迪迦额度显示器'), 'Tiga Energy Monitor');
    assert.equal(localizedError(new Error('本地系统拒绝访问')), 'The action could not be completed. Please try again.');
    assert.equal(localizedError(new Error('登录已过期，请重新登录。')), 'Your sign-in has expired. Please sign in again.');
  `, { LANG: 'zh_CN.UTF-8', LC_ALL: 'zh_CN.UTF-8' });
  for (const value of ['zh-CN', 'unexpected', undefined]) inVariant(value, `
    const { APP_LOCALE, t } = require('./backend/i18n.cjs');
    assert.equal(APP_LOCALE, 'zh-CN'); assert.equal(t('迪迦额度显示器'), '迪迦额度显示器');
  `, { LANG: 'en_US.UTF-8', LC_ALL: 'en_US.UTF-8' });
  assert.equal(localizedError(new Error('Read failed'), undefined, 'en'), 'Read failed');
});

test('English quota labels preserve official parsing, identifiers, values and account fields', () => {
  inVariant('en', String.raw`
    const { ClaudeProvider, parseUsageScreen, statusLineWindows } = require('./backend/claude-provider.cjs');
    const { CoreBridge } = require('./backend/core-bridge.cjs');
    const { selectEnergyTarget } = require('./backend/energy-selection.cjs');
    const screen = 'Current session\n12.5% used\nResets in 2 hr\nCurrent week (all models)\n20% remaining\nCurrent week (Sonnet only)\n33% used';
    const windows = parseUsageScreen(screen);
    assert.deepEqual(windows.map(window => [window.id, window.label, window.remainingPercent, window.windowMinutes]), [
      ['five_hour', '5 hours', 87.5, 300], ['seven_day', 'Weekly', 20, 10080], ['seven_day_sonnet', 'Weekly · Sonnet', 67, 10080],
    ]);
    assert.deepEqual(parseUsageScreen('Context window\n80% used'), []);
    assert.equal(statusLineWindows({ rate_limits: { five_hour: { used_percentage: 22, resets_at: Date.now() / 1000 + 3600 } } })[0].label, '5 hours');
    const provider = new ClaudeProvider({ dataDir: 'unused-test-profile' });
    provider.prepare = () => true;
    provider.run = async () => ({ code: 0, output: JSON.stringify({ loggedIn: true, authMethod: 'claude.ai',
      email: '原始账号@example.invalid', subscriptionType: '原始套餐' }) });
    provider.readUsage = async () => ({ windows });
    const state = await provider.refresh();
    assert.equal(state.status, 'connected');
    assert.equal(state.account, '原始账号@example.invalid'); assert.equal(state.plan, '原始套餐');
    assert.equal(state.notice, 'From the official Claude Code usage screen.');
    const target = selectEnergyTarget({ claude: state }, { energyTarget: 'claude' });
    assert.equal(target.remainingPercent, 87.5);
    assert.deepEqual(target.windows.map(window => window.label), ['5 hours', 'Weekly total']);
    const core = new CoreBridge();
    core.connect = () => ({ request: async () => ({ plan: '原始套餐', windows: [
      { id: 'five_hour', label: '5 小时', remainingPercent: 0, resetsAt: 1800000000, windowMinutes: 300 },
      { id: 'weekly', label: '每周', remainingPercent: 50, resetsAt: null, windowMinutes: 10080 },
      { id: 'other', label: '其他额度', remainingPercent: 100, resetsAt: null, windowMinutes: 60 },
    ] }) });
    const parsed = await core.parseRateLimits({});
    assert.deepEqual(parsed.windows.map(window => window.label), ['5 hours', 'Weekly', 'Other quota']);
    assert.deepEqual(parsed.windows.map(window => window.remainingPercent), [0, 50, 100]);
    assert.equal(parsed.plan, '原始套餐');
  `);
});

test('English provider failures and settings/shape validation never fall back to Chinese', () => {
  inVariant('en', `
    const { ClaudeProvider } = require('./backend/claude-provider.cjs');
    const { ClaudeClient } = require('./backend/claude-client.cjs');
    const { CodexProvider } = require('./backend/codex-provider.cjs');
    const { validatePatch } = require('./backend/settings-store.cjs');
    const { validateShapeRects, widgetDimensions } = require('./backend/window-shape.cjs');
    const claude = new ClaudeProvider({ dataDir: 'unused-test-profile' });
    claude.prepare = () => true;
    claude.run = async () => { throw new Error('未预见的本地系统错误'); };
    const failed = await claude.refresh();
    assert.equal(failed.error, 'Claude Code quota could not be read.');
    assert.doesNotMatch(failed.notice, /[\\u3400-\\u9fff]/u);
    claude.run = async () => ({ code: 0, output: 'invalid JSON' });
    assert.equal((await claude.refresh()).error, 'The official Claude Code app returned an unrecognized sign-in status.');
    const client = new ClaudeClient(); client.request = async () => { throw new Error('本地读取失败'); };
    assert.equal((await client.refresh()).error, 'The Claude Code request could not be completed.');
    const codex = new CodexProvider({ executable: 'unused-codex.exe', dataDir: 'unused-test-profile', openExternal() {} });
    codex.connect = async () => { const error = new Error('private details'); error.code = 'LAUNCH_FAILED'; throw error; };
    assert.equal((await codex.refresh()).error, 'The bundled Codex service could not be started.');
    for (const patch of [null, { unsupported: true }, { visible: { other: true } }, { visible: { codex: 'yes' } },
      { energyTarget: 'other' }, { quotaDisplay: 'both' }, { aggregateMembers: [] }, { floatingScale: 123 },
      { paused: 'true' }, { refreshSeconds: 0 }]) {
      assert.throws(() => validatePatch(patch), error => error instanceof TypeError && !/[\\u3400-\\u9fff]/u.test(error.message));
    }
    assert.throws(() => widgetDimensions(123), /Invalid floating widget size/);
    assert.throws(() => validateShapeRects(null), /Invalid number of interactive regions/);
    assert.throws(() => validateShapeRects([{}]), /must be valid rectangles/);
    assert.throws(() => validateShapeRects([{ x: 400, y: 0, width: 30, height: 10 }]), /extends beyond/);
  `);
});

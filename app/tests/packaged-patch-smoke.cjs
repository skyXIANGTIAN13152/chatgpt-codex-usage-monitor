'use strict';
// Patch-release smoke: launch the real portable executable with providers
// disabled. Quota fixtures replace only outbound test-window snapshots, so the
// production renderer runs unchanged while official account I/O never starts.
const { _electron } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { execFileSync } = require('node:child_process');
const root = path.resolve(__dirname, '..');
assert(process.argv[2], 'Usage: node tests/packaged-patch-smoke.cjs <portable-directory> [base-version]');
const target = path.resolve(process.argv[2]);
const appDirectory = path.join(target, 'resources', 'app');
const metadata = JSON.parse(fs.readFileSync(path.join(appDirectory, 'package.json'), 'utf8'));
const locale = metadata.appLocale || 'zh-CN';
const baseVersion = process.argv[3] || JSON.parse(fs.readFileSync(path.join(root, 'package.json'), 'utf8')).version.split('-')[0];
const expectedVersion = baseVersion + (locale === 'en' ? '-en' : '');
assert(['zh-CN', 'en'].includes(locale));
assert.equal(metadata.version, expectedVersion);
const qa = path.resolve(root, '..', 'qa');
fs.mkdirSync(qa, { recursive: true });
const evidence = fs.mkdtempSync(path.join(qa, `packaged-patch-${locale}-`));
const profile = path.join(evidence, 'profile');
fs.mkdirSync(profile);
fs.writeFileSync(path.join(profile, 'settings.json'), JSON.stringify({
  followSystemMotion: false, reducedMotion: false, paused: false, alwaysOnTop: false,
  launchWithCodex: false, launchWithClaude: false, refreshSeconds: 300,
}));
const env = { ...process.env }; delete env.ELECTRON_RUN_AS_NODE;
const errors = [], checks = [], audits = [];
const han = /\p{Script=Han}/u;

function scanNames(directory, inspect) {
  const findings = [];
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    const full = path.join(directory, entry.name);
    if (entry.isSymbolicLink()) { findings.push(full); continue; }
    if (inspect(entry.name, entry.isDirectory())) findings.push(full);
    if (entry.isDirectory()) findings.push(...scanNames(full, inspect));
  }
  return findings;
}

function auditPackage() {
  // Inspect names only: never open potential credential/user-data contents.
  const privateNames = /^(?:auth\.json|credentials\.json|\.credentials\.json|\.claude\.json|settings\.json|widget-position\.json|cookies|login data|web data|session_index\.jsonl|history\.jsonl|\.env(?:\..*)?)$/i;
  const privateDirectories = /^(?:\.git|\.codex|\.claude|user-data|userdata|profiles?|sessions?|cache|code cache|gpucache|dawncache|local storage|session storage|indexeddb|logs|plugin-cache)$/i;
  const privateFiles = scanNames(target, (name, directory) => directory ? privateDirectories.test(name) : privateNames.test(name));
  assert.deepEqual(privateFiles, [], 'Portable files must not include account credentials, plugin/browser caches or user profiles');
  const files = ['main.cjs', 'backend/refresh-scheduler.cjs', 'backend/energy-selection.cjs', 'ui/shared.js', 'ui/widget-layout.css'];
  const hashes = {};
  for (const relative of files) {
    const digest = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
    hashes[relative] = digest(path.join(appDirectory, relative));
    assert.equal(hashes[relative], digest(path.join(root, relative)), `${relative}: package must contain the reviewed patch source`);
  }
  return hashes;
}

async function auditLanguage(page, name) {
  const values = await page.evaluate(() => {
    const visible = element => {
      const rect = element.getBoundingClientRect(), style = getComputedStyle(element);
      return rect.width > 0 && rect.height > 0 && style.display !== 'none' && style.visibility !== 'hidden';
    };
    const attributes = [...document.querySelectorAll('[title],[aria-label],[placeholder]')].filter(visible)
      .flatMap(element => ['title', 'aria-label', 'placeholder'].map(key => element.getAttribute(key)).filter(Boolean));
    const options = [...document.querySelectorAll('select')].filter(visible).flatMap(select => [...select.options].map(option => option.text));
    return { lang: document.documentElement.lang, title: document.title, text: document.body.innerText, attributes, options };
  });
  audits.push({ name, ...values });
  assert.equal(values.lang, locale);
  if (locale === 'en') {
    assert(!han.test(JSON.stringify(values)), `${name}: visible English text, tooltips and accessibility labels contain no Chinese`);
  }
}

async function installRendererFixtures(application, windowId) {
  await application.evaluate(({ BrowserWindow }, windowId) => {
    const window = BrowserWindow.fromId(windowId);
    const contents = window.webContents;
    const send = contents.send;
    const fixture = global.__packagedPatchFixture = { windowId, contents, send, deliveries: 0, expires: false };
    const provider = id => {
      const now = Date.now();
      return { id, status: 'connected', account: `${id}-fixture@example.invalid`, plan: 'Test subscription',
        updatedAt: new Date(now).toISOString(), error: null, windows: [
          { id: 'weekly', label: 'Overall weekly', windowMinutes: 10080, remainingPercent: 84,
            resetsAt: new Date(now + 525600000).toISOString() },
          { id: 'five_hour', label: '5-hour usage', windowMinutes: 300, remainingPercent: 75,
            resetsAt: new Date(now + (fixture.expires ? -1000 : 9000000)).toISOString() },
        ] };
    };
    contents.send = function(channel, ...args) {
      if (channel === 'monitor:snapshot') {
        fixture.deliveries++;
        args[0] = { ...args[0], providers: { codex: provider('codex'), claude: provider('claude') } };
      }
      return send.call(this, channel, ...args);
    };
  }, windowId);
}

(async () => {
  let application, page, details;
  const hashes = auditPackage();
  try {
    application = await _electron.launch({ executablePath: path.join(target, 'TigaEnergyMonitor.exe'),
      args: ['--test-data-dir', profile, '--no-providers'], cwd: target, env });
    page = await application.firstWindow();
    page.on('pageerror', error => errors.push(error.message));
    await page.waitForFunction(() => Boolean(window.monitor));
    const snapshot = await page.evaluate(() => window.monitor.getSnapshot());
    for (const id of ['codex', 'claude']) assert.equal(snapshot.providers[id].status, 'signed_out');
    assert.equal(snapshot.energy.scene, 'unknown');
    assert.equal(snapshot.version, expectedVersion);
    assert.equal(snapshot.locale, locale);
    assert.equal(snapshot.settings.launchWithCodex, false); assert.equal(snapshot.settings.launchWithClaude, false);
    assert.equal(await page.evaluate(() => window.monitor.locale), locale);
    await page.locator('[data-action="login"][data-id="codex"]').waitFor();
    await page.locator('[data-action="login"][data-id="claude"]').waitFor();
    await auditLanguage(page, 'signed-out-widget');
    details = await application.evaluate(({ app, BrowserWindow }) => ({
      version: app.getVersion(), packaged: app.isPackaged, appPath: app.getAppPath(),
      modules: Object.keys(process.mainModule.require('node:module')._cache),
      widgetId: BrowserWindow.getAllWindows().find(window => window.webContents.getURL().endsWith('/index.html')).id,
      argv: process.argv,
    }));
    assert.equal(details.packaged, true); assert.equal(details.version, expectedVersion);
    assert.equal(path.resolve(details.appPath).toLowerCase(), appDirectory.toLowerCase());
    assert(details.argv.includes('--no-providers'));
    assert(details.modules.some(file => path.resolve(file).toLowerCase() === path.join(appDirectory, 'backend', 'refresh-scheduler.cjs').toLowerCase()),
      'The packaged main process actually loads the new scheduler module');
    const windowsVersion = execFileSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command',
      '$item = Get-Item -LiteralPath $env:TIGA_SMOKE_EXE; $item.VersionInfo | Select-Object FileVersion,ProductVersion | ConvertTo-Json -Compress'],
      { windowsHide: true, encoding: 'utf8', env: { ...env, TIGA_SMOKE_EXE: path.join(target, 'TigaEnergyMonitor.exe') } });
    details.windowsVersion = JSON.parse(windowsVersion);
    for (const key of ['FileVersion', 'ProductVersion']) assert.equal(details.windowsVersion[key], `${baseVersion}.0`);
    checks.push('real-portable-version-locale-and-loaded-patch-module-with-providers-disabled');

    await page.waitForFunction(() => document.querySelector('#video-a')?.readyState >= 2);
    assert.deepEqual(await page.locator('#video-a').evaluate(video => [video.videoWidth, video.videoHeight]), [800, 600]);
    await page.waitForFunction(() => !document.querySelector('#video-a').paused);
    const before = await page.locator('#video-a').evaluate(video => video.currentTime);
    await page.waitForTimeout(650);
    const after = await page.locator('#video-a').evaluate(video => video.currentTime);
    assert(after > before || before > 13, `Packaged animation must advance: ${before} -> ${after}`);
    checks.push('packaged-transparent-animation-ready-and-playing');

    const opening = application.waitForEvent('window');
    await page.evaluate(() => window.monitor.windowAction('settings'));
    const settings = await opening;
    settings.on('pageerror', error => errors.push(error.message));
    await settings.locator('#quota-display').waitFor();
    await auditLanguage(settings, 'signed-out-settings');
    const footer = await settings.locator('.settings-footer').innerText();
    assert(footer.includes(`v${baseVersion}`), `Settings footer must show the patch version: ${footer}`);
    assert(!footer.includes('v2.0.0'), 'Settings must not retain the previous release label');
    checks.push('settings-version-and-complete-visible-edition-language');

    await installRendererFixtures(application, details.widgetId);
    for (const mode of ['bars', 'rings']) {
      await settings.locator('#quota-display').selectOption(mode);
      // Trigger the actual IPC publication even when the select already has the
      // requested mode. Every later host tick passes through the same wrapper.
      await page.evaluate(mode => window.monitor.updateSettings({ quotaDisplay: mode }), mode);
      await page.waitForFunction(mode => document.body.dataset.quotaDisplay === mode && document.querySelectorAll('.quota-reset').length === 4, mode);
      const periods = await page.locator('.quota-reset').evaluateAll(elements => elements.map(element => {
        const rect = element.getBoundingClientRect(), style = getComputedStyle(element);
        return { text: element.innerText, x: rect.x, right: rect.right, bottom: rect.bottom, height: rect.height,
          visible: style.display !== 'none' && style.visibility !== 'hidden', scrollWidth: element.scrollWidth, clientWidth: element.clientWidth };
      }));
      const expected = locale === 'en' ? ['Reset 6d 2h', 'Reset 2h 30m'] : ['重置 6天2小时', '重置 2小时30分'];
      assert.deepEqual(periods.map(item => item.text.replace(/\s+/g, '')), [...expected, ...expected].map(text => text.replace(/\s+/g, '')));
      for (const item of periods) assert(item.visible && item.height > 0 && item.x >= 0 && item.right <= 421 && item.bottom <= 247 && item.scrollWidth <= item.clientWidth + 1,
        `Countdown is visible and unclipped: ${JSON.stringify(item)}`);
      assert.equal(await page.locator(mode === 'rings' ? '.quota-ring-group' : '.quota-track').count(), mode === 'rings' ? 2 : 4);
      assert.equal(await page.locator(mode === 'rings' ? '.quota-track' : '.quota-ring-group').count(), 0);
      await auditLanguage(page, `${mode}-renderer-fixtures`);
      await page.screenshot({ path: path.join(evidence, `${locale}-${mode}-fixture.png`), omitBackground: true, scale: 'css' });
      checks.push(`${mode}-actual-renderer-period-reset-countdowns-and-exclusive-display-mode`);
    }
    // Prove that renderer fixtures never become official host account state.
    const hostSnapshot = await page.evaluate(() => window.monitor.getSnapshot());
    for (const id of ['codex', 'claude']) assert.equal(hostSnapshot.providers[id].status, 'signed_out');
    assert.equal(hostSnapshot.energy.scene, 'unknown');
    assert.deepEqual(errors, []);
    checks.push('fixture-only-rendering-never-authenticates-or-replaces-real-host-state');
  } finally {
    if (application) {
      await application.evaluate(() => {
        const fixture = global.__packagedPatchFixture;
        if (fixture && !fixture.contents.isDestroyed()) fixture.contents.send = fixture.send;
        delete global.__packagedPatchFixture;
      }).catch(() => {});
      await application.close();
    }
  }
  const accountArtifacts = scanNames(profile, (name, directory) => directory
    ? /^(?:codex|claude|\.codex|\.claude|\.git|plugins?|plugin-cache)$/i.test(name)
    : /^(?:auth\.json|credentials\.json|\.credentials\.json|\.claude\.json)$/i.test(name));
  assert.deepEqual(accountArtifacts, [], 'Disabled providers must not create official account profiles, tokens or plugin caches');
  auditPackage(); // The portable directory itself must remain free of user data.
  checks.push('no-official-provider-profile-plugin-git-cache-or-account-artifacts-created');
  fs.writeFileSync(path.join(evidence, 'result.json'), JSON.stringify({ ok: true, target, locale, expectedVersion,
    checks, hashes, details, audits, errors,
    boundary: 'Official providers disabled; fixture snapshots exercise only the actual packaged renderer. Authentication and scheduler runtime are covered by separate source tests.' }, null, 2));
  console.log(JSON.stringify({ ok: true, locale, version: expectedVersion, checks, evidence }));
})().catch(error => {
  fs.writeFileSync(path.join(evidence, 'failure.json'), JSON.stringify({ error: error.stack, checks, audits, errors }, null, 2));
  console.error('Packaged patch evidence:', evidence); console.error(error); process.exitCode = 1;
});

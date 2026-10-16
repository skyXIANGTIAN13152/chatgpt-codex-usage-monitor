'use strict';
const { _electron: electron } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const root = path.resolve(__dirname, '..');
const target = path.resolve(process.argv[2]);
const qa = path.resolve(root, '..', 'qa');
fs.mkdirSync(qa, { recursive: true });
const data = fs.mkdtempSync(path.join(qa, 'packaged-data-'));
fs.writeFileSync(path.join(data, 'settings.json'), JSON.stringify({ startupAnimation: true, followSystemMotion: false, reducedMotion: false, paused: false }));
const env = { ...process.env };
delete env.ELECTRON_RUN_AS_NODE;
(async () => {
  const application = await electron.launch({ executablePath: path.join(target, 'TigaEnergyMonitor.exe'), args: ['--test-data-dir', data], env, cwd: target });
  try {
    const page = await application.firstWindow();
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.waitForFunction(() => Boolean(window.monitor));
    let snapshot, signedOutSince = null, ready = false;
    const deadline = Date.now() + 15000;
    while (Date.now() < deadline) {
      snapshot = await page.evaluate(() => window.monitor.getSnapshot());
      const signedOut = snapshot.providers.codex.status === 'signed_out' && snapshot.providers.claude.status === 'signed_out';
      // The host publishes an initial empty signed-out snapshot before the
      // bundled providers start connecting. Poll resolved IPC results outside
      // waitForFunction, whose predicate treats an async Promise as truthy.
      if (!signedOut) signedOutSince = null;
      else {
        signedOutSince ??= Date.now();
        if (Date.now() - signedOutSince >= 600) { ready = true; break; }
      }
      await page.waitForTimeout(100);
    }
    assert(ready, 'Bundled providers must settle into the signed-out state');
    assert.equal(snapshot.providers.codex.status, 'signed_out');
    assert.equal(snapshot.providers.claude.status, 'signed_out');
    assert.equal(snapshot.energy.scene, 'unknown');
    assert.equal(snapshot.settings.launchWithCodex, false);
    assert.equal(snapshot.settings.launchWithClaude, false);
    const expectedLocale = JSON.parse(fs.readFileSync(path.join(target, 'resources', 'app', 'package.json'), 'utf8')).appLocale || 'zh-CN';
    assert.equal(snapshot.locale, expectedLocale);
    assert.equal(await page.evaluate(() => window.monitor.locale), expectedLocale);
    assert.equal(await page.locator('html').getAttribute('lang'), expectedLocale);
    if (expectedLocale === 'en') {
      assert.doesNotMatch(await page.locator('body').innerText(), /\p{Script=Han}/u);
      await page.getByRole('button', { name: 'Sign in with ChatGPT', exact: true }).waitFor();
    }
    await page.locator('[data-action="login"][data-id="codex"]').waitFor();
    await page.locator('[data-action="login"][data-id="claude"]').waitFor();
    await page.waitForFunction(() => document.querySelector('#video-a').readyState >= 2);
    assert.deepEqual(await page.locator('#video-a').evaluate(video => [video.videoWidth, video.videoHeight]), [800, 600]);
    const first = await page.locator('#video-a').evaluate(video => video.currentTime);
    await page.waitForTimeout(600);
    const second = await page.locator('#video-a').evaluate(video => video.currentTime);
    assert(second > first || first > 13);
    // The legacy preference must be harmless, including after a watcher or
    // manual launch asks the already-running application to show again.
    await application.evaluate(({ app }) => app.emit('second-instance', {},
      ['TigaEnergyMonitor.exe', '--trigger=codex'], process.cwd()));
    await page.waitForTimeout(200);
    assert.equal(await page.locator('#startup-animation,.startup-animation').count(), 0);
    assert.equal(await page.locator('body').evaluate(body => body.classList.contains('startup-playing')), false);
    assert.equal(await page.locator('#energy-target').count(), 0);
    const resources = await page.evaluate(() => [
      ...performance.getEntriesByType('resource').map(entry => entry.name),
      ...[...document.querySelectorAll('[src],[href]')].map(element => element.src || element.href),
    ]);
    assert.deepEqual(resources.filter(url => /(?:^|\/)startup(?:[/.\-]|$)/i.test(url)), []);
    assert.equal(snapshot.settings.quotaDisplay, 'bars');
    assert.deepEqual(errors, []);
    await page.screenshot({ path: path.join(qa, 'packaged-preview.png'), scale: 'css' });
    const app = path.join(target, 'resources', 'app');
    // This one-shot probe owns its process. node-pty can leave a referenced
    // ConPTY worker MessagePort after onExit. Dispose listeners/terminal, then
    // flush the result before exiting; do not kill an already-exited shell.
    const probe = "const {createRequire}=require('module'); const r=createRequire(process.argv[1]); const p=r('node-pty'); const {Terminal}=r('@xterm/headless'); const t=new Terminal({cols:80,rows:24}); const c=p.spawn(process.env.ComSpec||'cmd.exe',['/d','/c','echo TIGA_RUNTIME_OK'],{cols:80,rows:24,env:process.env,useConpty:true}); let s=''; const dataListener=c.onData(d=>s+=d); const exitListener=c.onExit(e=>{dataListener.dispose();exitListener.dispose();t.dispose();const ok=e.exitCode===0&&s.includes('TIGA_RUNTIME_OK');process.stdout.write(ok?'PACKAGED_PTY_OK\\n':'PACKAGED_PTY_FAILED\\n',()=>process.exit(ok?0:1));});";
    const result = execFileSync(path.join(app, 'runtime', 'node.exe'), ['-e', probe, path.join(app, 'backend', 'claude-runtime', 'package.json')], { windowsHide: true, encoding: 'utf8', timeout: 15000 });
    assert.match(result, /PACKAGED_PTY_OK/);
    console.log(JSON.stringify({ ok: true, packaged: true, actualOfficialComponents: 'both-signed-out', quota: 'unknown', nativeClaudeRuntime: 'ConPTY passed', legacyStartupPreference: 'ignored; no UI or media requests', quotaDisplay: 'bars', video: 'playing', login: 'not performed', autostart: 'not enabled' }));
  } finally { await application.close(); }
})().catch((error) => { console.error(error); process.exitCode = 1; });

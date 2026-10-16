'use strict';
const { _electron: electron } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const qa = path.resolve(root, '..', 'qa');
fs.mkdirSync(qa, { recursive: true });
const data = fs.mkdtempSync(path.join(qa, 'motion-control-'));
const env = { ...process.env }; delete env.ELECTRON_RUN_AS_NODE;
(async () => {
  const application = await electron.launch({ executablePath: require('electron'),
    args: [root, '--test-data-dir', data, '--no-providers'], env, cwd: root });
  try {
    const page = await application.firstWindow();
    await page.locator('#pause').waitFor();
    assert.equal(await page.locator('video').count(), 2, 'Playback controls apply directly to the two character layers');
    await page.waitForFunction(() => document.querySelector('#video-a').readyState >= 2);
    await page.emulateMedia({ reducedMotion: 'reduce' });
    await page.waitForFunction(() => document.body.classList.contains('reduced-motion'));
    assert.equal(await page.locator('#pause').isEnabled(), true);
    assert.match(await page.locator('#pause').getAttribute('title'), /播放动画/);
    assert.equal(await page.locator('.scene-video.is-visible').evaluate(video => video.paused), true);
    await page.locator('#pause').click();
    await page.waitForFunction(() => !document.body.classList.contains('reduced-motion') && !document.querySelector('.scene-video.is-visible').paused);
    const preferences = await page.evaluate(async () => (await window.monitor.getSnapshot()).settings);
    assert.equal(preferences.followSystemMotion, false);
    assert.equal(preferences.reducedMotion, false);
    const first = await page.locator('.scene-video.is-visible').evaluate(video => ({ time: video.currentTime, duration: video.duration }));
    assert(Number.isFinite(first.duration) && first.duration > 0);
    await page.waitForTimeout(800);
    const second = await page.locator('.scene-video.is-visible').evaluate(video => video.currentTime);
    const elapsed = (second - first.time + first.duration) % first.duration;
    assert(elapsed > .1 && elapsed < 3, `Character playback must advance, including at its loop boundary: ${JSON.stringify({ first, second, elapsed })}`);
    assert.equal(await page.locator('.scene-video.is-visible').evaluate(video => getComputedStyle(video).transitionDuration), '1.8s');
    await page.locator('#pause').click();
    await page.waitForFunction(() => document.querySelector('.scene-video.is-visible').paused);
    await page.locator('#pause').click();
    await page.waitForFunction(() => !document.querySelector('.scene-video.is-visible').paused);
    await page.evaluate(() => window.monitor.updateSettings({ reducedMotion: true }));
    await page.waitForFunction(() => document.body.classList.contains('reduced-motion'));
    assert.equal(await page.locator('#pause').isEnabled(), true);
    await page.locator('#pause').click();
    await page.waitForFunction(() => !document.querySelector('.scene-video.is-visible').paused);
    await page.screenshot({ path: path.join(qa, 'motion-controls-enabled.png'), omitBackground: true, scale: 'css' });
    console.log(JSON.stringify({ ok: true, checks: ['system-reduced-motion-respected-by-default', 'static-button-remains-enabled', 'manual-play-overrides-system-preference', 'video-time-advances', 'crossfade-restored', 'pause-resume', 'manual-play-overrides-app-static-setting'] }));
  } finally { await application.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });

'use strict';
// Actual Electron host, isolated profile and disabled providers. Windows state
// is read through Win32, not inferred from a saved preference. The only native
// mutations are ordinary Electron operations on this test's own windows.
// Usage: node tests/always-on-top-ui.cjs [portable-directory]
const { _electron } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { execFileSync } = require('node:child_process');
assert.equal(process.platform, 'win32', 'This regression checks Windows native topmost state');
const root = path.resolve(__dirname, '..');
const portableDirectory = process.argv[2] ? path.resolve(process.argv[2]) : null;
const appDirectory = portableDirectory ? path.join(portableDirectory, 'resources', 'app') : root;
const main = path.join(appDirectory, 'main.cjs');
const sourceMain = path.join(root, 'main.cjs');
const digest = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const reviewedMainHash = digest(sourceMain);
assert.equal(digest(main), reviewedMainHash, 'The portable main must match the reviewed source');
const manifest = JSON.parse(fs.readFileSync(path.join(appDirectory, 'package.json'), 'utf8'));
const executablePath = portableDirectory ? path.join(portableDirectory, 'TigaEnergyMonitor.exe') : require('electron');
const runMetadata = { mode: portableDirectory ? 'packaged' : 'source', appDirectory, executablePath,
  mainPath: main, sourceMainPath: sourceMain, expectedVersion: manifest.version,
  expectedLocale: manifest.appLocale || 'zh-CN', sourceMainSha256: reviewedMainHash, testedMainSha256: digest(main) };
const qa = path.resolve(root, '..', 'qa'); fs.mkdirSync(qa, { recursive: true });
const folder = fs.mkdtempSync(path.join(qa, 'always-on-top-ui-'));
const profile = path.join(folder, 'profile'); fs.mkdirSync(profile);
fs.writeFileSync(path.join(profile, 'settings.json'), JSON.stringify({
  alwaysOnTop: true, paused: true, reducedMotion: true, followSystemMotion: false,
}));
const env = { ...process.env }; delete env.ELECTRON_RUN_AS_NODE;
const observations = [], checks = [], errors = [];
const nativeScript = `
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class TigaNativeProbe {
 [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] public static extern IntPtr GetWindowLongPtr(IntPtr h, int index);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern IntPtr GetTopWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint command);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string title);
}
'@
$targets = New-Object 'System.Collections.Generic.List[object]'
foreach ($entry in (ConvertFrom-Json -InputObject $env:TIGA_TOPMOST_HANDLES)) { $targets.Add($entry) }
$bar = [TigaNativeProbe]::FindWindow('Shell_TrayWnd', $null)
if ($bar -ne [IntPtr]::Zero) { $targets.Add([pscustomobject]@{id=0;kind='primary-taskbar';hwnd=$bar.ToInt64()}) }
$order = @{}; $handle = [TigaNativeProbe]::GetTopWindow([IntPtr]::Zero); $index = 0
while ($handle -ne [IntPtr]::Zero -and $index -lt 10000) {
 $order[$handle.ToInt64().ToString()] = $index; $index++; $handle = [TigaNativeProbe]::GetWindow($handle,2)
}
$rows = @($targets | ForEach-Object {
 $handle=[IntPtr][long]$_.hwnd; $style=[TigaNativeProbe]::GetWindowLongPtr($handle,-20).ToInt64()
 [pscustomobject]@{id=$_.id;kind=$_.kind;hwnd=$_.hwnd;electronTopmost=$_.electronTopmost;
  topmost=($style -band 8) -ne 0;exStyle=('0x{0:x}' -f $style);
  visible=[TigaNativeProbe]::IsWindowVisible($handle);minimized=[TigaNativeProbe]::IsIconic($handle);
  foreground=$handle -eq [TigaNativeProbe]::GetForegroundWindow();z=$order[$handle.ToInt64().ToString()];
  owner=[TigaNativeProbe]::GetWindow($handle,4).ToInt64()}
})
ConvertTo-Json -InputObject $rows -Compress
`;
let application, widget, settings, otherId;
async function capture(name) {
  const windows = await application.evaluate(({ BrowserWindow }) => BrowserWindow.getAllWindows().map(window => ({
    id: window.id, kind: window.webContents.getURL().endsWith('/index.html') ? 'widget'
      : window.webContents.getURL().endsWith('/settings.html') ? 'settings' : 'ordinary-test-window',
    hwnd: Number(window.getNativeWindowHandle().readBigUInt64LE()), electronTopmost: window.isAlwaysOnTop(),
  })));
  const native = JSON.parse(execFileSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', nativeScript], {
    windowsHide: true, encoding: 'utf8', env: { ...env, TIGA_TOPMOST_HANDLES: JSON.stringify(windows) }, timeout: 10000,
  }));
  const record = { name, windows: native }; observations.push(record);
  return record;
}
function expectTopmost(record, expected, kinds = ['widget']) {
  for (const kind of kinds) {
    const window = record.windows.find(window => window.kind === kind);
    assert(window, `${record.name}: ${kind} exists`);
    assert.equal(window.electronTopmost, expected, `${record.name}: ${kind} Electron topmost`);
    assert.equal(window.topmost, expected, `${record.name}: ${kind} native WS_EX_TOPMOST; ${JSON.stringify(record)}`);
  }
}
function expectAboveOrdinary(record) {
  const widget = record.windows.find(window => window.kind === 'widget');
  const ordinary = record.windows.find(window => window.kind === 'ordinary-test-window');
  assert.equal(ordinary.topmost, false, 'The covering fixture remains an ordinary non-topmost window');
  assert(widget.z < ordinary.z, `${record.name}: widget must remain above an activated ordinary window`);
}
async function secondInstance() {
  await application.evaluate(({ app }) => app.emit('second-instance', {}, ['fixture', '--trigger=codex'], process.cwd()));
}
async function waitPinned(expected) {
  await widget.waitForFunction(async expected => (await window.monitor.getSnapshot()).settings.alwaysOnTop === expected,
    expected, { timeout: 5000 });
}

(async () => {
  try {
    application = await _electron.launch({ executablePath,
      args: [...(portableDirectory ? [] : [main]), '--test-data-dir', profile, '--no-providers'],
      cwd: portableDirectory || root, env });
    widget = await application.firstWindow(); widget.on('pageerror', error => errors.push(error.message));
    await widget.waitForFunction(() => Boolean(window.monitor));
    const snapshot = await widget.evaluate(() => window.monitor.getSnapshot());
    runMetadata.actual = await application.evaluate(({ app }) => ({
      isPackaged: app.isPackaged, appPath: app.getAppPath(), version: app.getVersion(), electronVersion: process.versions.electron,
    }));
    assert.equal(runMetadata.actual.isPackaged, Boolean(portableDirectory));
    assert.equal(path.resolve(runMetadata.actual.appPath).toLowerCase(), appDirectory.toLowerCase());
    assert.equal(snapshot.version, manifest.version); assert.equal(runMetadata.actual.version, manifest.version);
    assert.equal(snapshot.locale, runMetadata.expectedLocale);
    assert.equal(await widget.evaluate(() => window.monitor.locale), runMetadata.expectedLocale);
    assert.equal(snapshot.settings.alwaysOnTop, true);
    assert.equal(snapshot.providers.codex.status, 'signed_out'); assert.equal(snapshot.providers.claude.status, 'signed_out');
    expectTopmost(await capture('startup-saved-topmost'), true);
    checks.push('saved-topmost-is-an-actual-native-window-flag-at-startup');

    otherId = await application.evaluate(async ({ BrowserWindow }) => {
      const main = BrowserWindow.getAllWindows()[0], bounds = main.getBounds();
      const other = new BrowserWindow({ x: bounds.x - 20, y: bounds.y - 20, width: 480, height: 330,
        frame: false, alwaysOnTop: false, title: 'Isolated topmost regression',
        webPreferences: { sandbox: true, contextIsolation: true } });
      await other.loadURL('data:text/html,<body style="background:%23232d3a;color:white;font:18px sans-serif">Isolated topmost regression</body>');
      other.show(); other.focus(); other.moveTop(); return other.id;
    });
    let record = await capture('ordinary-window-activation'); expectTopmost(record, true); expectAboveOrdinary(record);
    checks.push('native-z-order-stays-above-an-ordinary-test-window');

    // Explorer may naturally demote its taskbar when an ordinary window enters
    // fullscreen. Do not change taskbar styles or preferences. This reproduces
    // the original floating-level failure without touching any user windows.
    await application.evaluate(({ BrowserWindow }, id) => { const other = BrowserWindow.fromId(id);
      other.setFullScreen(true); other.show(); other.focus(); }, otherId);
    try {
      await widget.waitForTimeout(300);
      await widget.evaluate(() => window.monitor.updateSettings({ alwaysOnTop: false }));
      expectTopmost(await capture('fullscreen-pin-off'), false);
      await widget.evaluate(() => window.monitor.updateSettings({ alwaysOnTop: true }));
      record = await capture('fullscreen-pin-on'); expectTopmost(record, true); expectAboveOrdinary(record);
      checks.push('settings-pin-on-remains-native-topmost-with-an-ordinary-fullscreen-window');
    } finally {
      await application.evaluate(({ BrowserWindow }, id) => BrowserWindow.fromId(id)?.setFullScreen(false), otherId);
    }

    await widget.evaluate(() => window.monitor.windowAction('close'));
    await secondInstance(); record = await capture('hidden-widget-second-instance-showInactive');
    expectTopmost(record, true); assert.equal(record.windows.find(window => window.kind === 'widget').visible, true);
    await widget.evaluate(() => window.monitor.windowAction('minimize'));
    await secondInstance(); record = await capture('minimized-widget-second-instance-restore');
    expectTopmost(record, true); assert.equal(record.windows.find(window => window.kind === 'widget').minimized, false);
    checks.push('hide-showInactive-and-minimize-restore-preserve-native-topmost');

    const opening = application.waitForEvent('window');
    await widget.evaluate(() => window.monitor.windowAction('settings')); settings = await opening;
    settings.on('pageerror', error => errors.push(error.message)); await settings.locator('#always-on-top').waitFor();
    record = await capture('settings-open-and-focused'); expectTopmost(record, true, ['widget', 'settings']);
    assert.equal(record.windows.find(window => window.kind === 'settings').owner, record.windows.find(window => window.kind === 'widget').hwnd);
    await settings.locator('#always-on-top').uncheck();
    await waitPinned(false);
    expectTopmost(await capture('settings-checkbox-off-both-windows'), false, ['widget', 'settings']);
    await settings.locator('#always-on-top').check();
    await waitPinned(true);
    expectTopmost(await capture('settings-checkbox-on-both-windows'), true, ['widget', 'settings']);
    await application.evaluate(({ BrowserWindow }, id) => { const ordinary = BrowserWindow.fromId(id);
      ordinary.show(); ordinary.focus(); ordinary.moveTop(); }, otherId);
    record = await capture('ordinary-activation-after-settings'); expectTopmost(record, true, ['widget', 'settings']); expectAboveOrdinary(record);
    checks.push('settings-owner-focus-and-pin-toggle-maintain-both-native-flags');

    await widget.evaluate(() => window.monitor.updateSettings({ floatingScale: 125 }));
    expectTopmost(await capture('scaled-bounds'), true, ['widget', 'settings']);
    await widget.evaluate(() => window.monitor.updateShape([{ x: 0, y: 0, width: 420, height: 246 }]));
    expectTopmost(await capture('native-shape-change'), true, ['widget', 'settings']);
    checks.push('bounds-and-native-shape-updates-preserve-topmost');
    assert.deepEqual(errors, []);
    assert.equal(digest(sourceMain), reviewedMainHash, 'The reviewed source stays unchanged during this run');
    assert.equal(digest(main), reviewedMainHash, 'The tested main stays identical to the reviewed source');
    fs.writeFileSync(path.join(folder, 'result.json'), JSON.stringify({ ok: true, checks, observations, errors,
      metadata: runMetadata, mainSha256: reviewedMainHash }, null, 2));
    console.log(JSON.stringify({ ok: true, checks, evidence: folder, metadata: runMetadata, mainSha256: reviewedMainHash }));
  } catch (error) {
    fs.writeFileSync(path.join(folder, 'failure.json'), JSON.stringify({ error: error.stack, checks, observations, errors,
      metadata: runMetadata, mainSha256: reviewedMainHash }, null, 2));
    console.error('Native topmost evidence:', folder); throw error;
  } finally {
    if (application) {
      if (otherId) await application.evaluate(({ BrowserWindow }, id) => BrowserWindow.fromId(id)?.destroy(), otherId).catch(() => {});
      await application.close();
    }
  }
})().catch(error => { console.error(error); process.exitCode = 1; });

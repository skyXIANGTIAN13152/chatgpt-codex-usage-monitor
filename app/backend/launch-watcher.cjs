'use strict';

const childProcess = require('node:child_process');
const fs = require('node:fs/promises');
const path = require('node:path');

const RUN_KEY = 'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run';
const RUN_VALUE_NAME = 'TigaQuotaLaunchWatcher';

function quoteWindowsArgument(argument) {
  // The value is a CreateProcess command line stored in the registry, never a
  // shell command. Escape quotes and trailing backslashes using Windows rules.
  return '"' + String(argument).replace(/(\\*)"/g, '$1$1\\"').replace(/(\\+)$/, '$1$1') + '"';
}

function buildWatcherPlan({ settings = {}, settingsPath, appExecutable, watcherExecutable, appDirectory }) {
  for (const [name, value] of Object.entries({ settingsPath, appExecutable, watcherExecutable })) {
    if (typeof value !== 'string' || !path.win32.isAbsolute(value) || /[\r\n\0]/.test(value)) {
      throw new TypeError(`${name} must be an absolute Windows path`);
    }
  }
  const directory = appDirectory || path.win32.dirname(appExecutable);
  if (!path.win32.isAbsolute(directory)) throw new TypeError('appDirectory must be an absolute Windows path');
  // Native exclusion is based on the application executable's parent. Keep
  // host configuration consistent with that boundary.
  if (path.win32.normalize(directory).replace(/\\+$/, '').toLowerCase() !==
      path.win32.dirname(path.win32.normalize(appExecutable)).replace(/\\+$/, '').toLowerCase()) {
    throw new TypeError('appDirectory must contain appExecutable directly');
  }
  const enabled = settings.launchWithCodex === true || settings.launchWithClaude === true;
  const args = ['--settings', settingsPath, '--app', appExecutable];
  const command = [watcherExecutable, ...args].map(quoteWindowsArgument).join(' ');
  return { enabled, runKey: RUN_KEY, runValueName: RUN_VALUE_NAME, command,
    executable: watcherExecutable, args, directory,
    actions: enabled ? ['register-current-user-startup', 'start-hidden-watcher'] : ['remove-own-startup-entry'] };
}

function runRegistry(args) {
  return new Promise((resolve, reject) => {
    childProcess.execFile('reg.exe', args, { windowsHide: true, shell: false }, (error, stdout, stderr) => {
      if (error) { error.stderr = stderr; reject(error); }
      else resolve(stdout);
    });
  });
}

async function configureLaunchWatcher(options) {
  const plan = buildWatcherPlan(options);
  if (options.testMode === true) {
    return { ...plan, dryRun: true, registered: false, started: false };
  }
  if (process.platform !== 'win32') throw new Error('Launch watching is available on Windows');
  if (!plan.enabled) {
    try {
      await runRegistry(['QUERY', RUN_KEY, '/v', RUN_VALUE_NAME]);
    } catch (error) {
      if (error.code === 1) return { ...plan, registered: false, started: false };
      throw error;
    }
    await runRegistry(['DELETE', RUN_KEY, '/v', RUN_VALUE_NAME, '/f']);
    // The running helper rereads settings every 2.5 seconds and exits by itself.
    return { ...plan, registered: false, started: false };
  }
  await Promise.all([fs.access(plan.executable), fs.access(options.appExecutable), fs.access(options.settingsPath)]);
  await runRegistry(['ADD', RUN_KEY, '/v', RUN_VALUE_NAME, '/t', 'REG_SZ', '/d', plan.command, '/f']);
  await new Promise((resolve, reject) => {
    const watcher = childProcess.spawn(plan.executable, plan.args, {
      cwd: plan.directory, detached: true, windowsHide: true, stdio: 'ignore', shell: false,
    });
    watcher.once('error', reject);
    watcher.once('spawn', () => { watcher.unref(); resolve(); });
  });
  return { ...plan, registered: true, started: true };
}

module.exports = { configureLaunchWatcher, buildWatcherPlan, quoteWindowsArgument, RUN_VALUE_NAME };

'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const childProcess = require('node:child_process');
const fs = require('node:fs/promises');
const path = require('node:path');
const { promisify } = require('node:util');
const { configureLaunchWatcher, buildWatcherPlan, quoteWindowsArgument } = require('../backend/launch-watcher.cjs');

const base = {
  settingsPath: 'C:\\用户 数据\\Tiga\\settings.json',
  appExecutable: 'D:\\Tiga Monitor\\Tiga.exe',
  watcherExecutable: 'D:\\Tiga Monitor\\runtime\\tiga-launch-watcher.exe',
  appDirectory: 'D:\\Tiga Monitor',
};

test('both opt-in flags default off and strings cannot enable startup', () => {
  assert.equal(buildWatcherPlan(base).enabled, false);
  assert.equal(buildWatcherPlan({ ...base, settings: { launchWithCodex: 'true', launchWithClaude: 1 } }).enabled, false);
  assert.equal(buildWatcherPlan({ ...base, settings: { launchWithCodex: true } }).enabled, true);
  assert.equal(buildWatcherPlan({ ...base, settings: { launchWithClaude: true } }).enabled, true);
});

test('registration quotes Unicode paths while retaining separate executable arguments', () => {
  const plan = buildWatcherPlan({ ...base, settings: { launchWithCodex: true } });
  assert.deepEqual(plan.args, ['--settings', base.settingsPath, '--app', base.appExecutable]);
  assert.equal(plan.command, '"D:\\Tiga Monitor\\runtime\\tiga-launch-watcher.exe" "--settings" "C:\\用户 数据\\Tiga\\settings.json" "--app" "D:\\Tiga Monitor\\Tiga.exe"');
  assert.equal(quoteWindowsArgument('D:\\has spaces\\'), '"D:\\has spaces\\\\"');
  assert.equal(quoteWindowsArgument('name"value'), '"name\\"value"');
});

test('testMode invokes no registry commands and starts no process', async () => {
  const originalExec = childProcess.execFile;
  const originalSpawn = childProcess.spawn;
  let calls = 0;
  childProcess.execFile = childProcess.spawn = () => { calls += 1; throw new Error('Side effect in dry run'); };
  try {
    const enabled = await configureLaunchWatcher({ ...base, settings: { launchWithClaude: true }, testMode: true });
    assert.equal(enabled.enabled, true);
    assert.equal(enabled.dryRun, true);
    assert.equal(enabled.started, false);
    assert.deepEqual(enabled.actions, ['register-current-user-startup', 'start-hidden-watcher']);
    const disabled = await configureLaunchWatcher({ ...base, settings: {}, testMode: true });
    assert.deepEqual(disabled.actions, ['remove-own-startup-entry']);
    assert.equal(disabled.runValueName, enabled.runValueName);
    assert.equal(calls, 0);
  } finally {
    childProcess.execFile = originalExec;
    childProcess.spawn = originalSpawn;
  }
});

test('configuration rejects ambiguous paths before any system changes', async () => {
  await assert.rejects(configureLaunchWatcher({ ...base, settingsPath: 'relative.json', testMode: true }), /absolute Windows path/);
  await assert.rejects(configureLaunchWatcher({ ...base, appDirectory: 'D:\\Unrelated', testMode: true }), /contain appExecutable/);
});

test('native helper exits with both switches off without inspecting or launching apps', { skip: process.platform !== 'win32' }, async () => {
  const executable = path.resolve(__dirname, '../runtime/tiga-launch-watcher.exe');
  const temporaryRoot = path.resolve(__dirname, '../native/launch-watcher/build');
  await fs.mkdir(temporaryRoot, { recursive: true });
  const directory = await fs.mkdtemp(path.join(temporaryRoot, 'isolated-'));
  try {
    const settingsPath = path.join(directory, '设置 with spaces.json');
    await fs.writeFile(settingsPath, JSON.stringify({ launchWithCodex: false, launchWithClaude: false }));
    // The nonexistent application is deliberate: disabled options must exit
    // successfully without attempting to run it or requiring it to exist.
    await promisify(childProcess.execFile)(executable, ['--settings', settingsPath, '--app', path.join(directory, 'never launched.exe')], {
      timeout: 5000, windowsHide: true, shell: false,
    });
  } finally {
    assert.ok(path.resolve(directory).startsWith(temporaryRoot + path.sep));
    await fs.rm(directory, { recursive: true, force: true });
  }
});
